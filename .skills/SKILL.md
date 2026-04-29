# CrossLua Reader Development Guide

Project: Lua-powered e-reader runtime for Xteink X4 (ESP32-C3)
Mission: A thin pure C firmware runtime that enables extensibility through Lua plugins loaded from the SD card.

## AI Agent Identity and Cognitive Rules
* Role: Senior Embedded Systems Engineer (ESP-IDF / bare-metal C specialized).
* Primary Constraint: 380KB RAM is the hard ceiling. The runtime must stay under ~500KB flash. Stability is non-negotiable.
* Evidence-Based Reasoning: Before proposing a change, you MUST cite the specific file path and line numbers that justify the modification.
* Anti-Hallucination: Do not assume the existence of ESP-IDF functions or libraries. If you are unsure of an API's availability for the ESP32-C3 RISC-V target, check the open-x4-sdk or official ESP-IDF docs first.
* No Unfounded Claims: Do not claim performance gains or memory savings without explaining the technical mechanism (e.g., DRAM vs IRAM usage, stack vs heap).
* Resource Justification: You must justify any new heap allocation (malloc, calloc) or explain why a stack/static alternative was rejected.
* Verification: After suggesting a fix, instruct the user on how to verify it (e.g., monitoring heap via Serial or testing a specific plugin).

---

## Language: Pure C (with C++ SDK bridge)

CrossLua's runtime is written in C. The only C++ in the project is the open-x4-sdk hardware drivers, accessed through thin `extern "C"` bridge files.

**Why pure C for the runtime:**
- Lua's API is natively C — no binding wrappers needed
- ESP-IDF is C — direct access without extern/mangling
- Smaller binary — no C++ runtime, RTTI stubs, exception tables, or template bloat
- The Lua interpreter is C — everything links cleanly
- Simpler toolchain, faster compilation

**Why keep the SDK in C++:**
- The open-x4-sdk is proven, community-maintained, and rarely changes
- It's only 1,738 lines of hardware register access — not worth rewriting
- PlatformIO links C and C++ cleanly — the bridge is 5-10 lines per module

**Rules:**
- All runtime source files are `.c` / `.h` — never `.cpp` / `.hpp`
- SDK bridge files are the ONLY `.cpp` allowed — one per SDK module, `extern "C"` wrappers only
- No C++ keywords in runtime code: no `class`, `namespace`, `template`, `new`, `delete`
- Use `stdbool.h` for `bool`
- Use `struct` with function pointers for polymorphism where needed
- Use `static` functions for file-scoped encapsulation (replaces `private`)
- Use prefixed function names for module namespacing: `hal_display_init()`, `font_cache_get()`

---

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection
```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

### Platform-Specific Behaviors
- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use `find`+`xargs`)
- **macOS (Darwin)**: Full bash, Unix paths, `brew` for packages
- **Linux/WSL**: Full bash, Unix paths, native glob support

---

## Platform and Hardware Constraints

### Hardware Specs
* MCU: ESP32-C3 (Single-core RISC-V @ 160MHz)
* RAM: ~380KB usable (VERY LIMITED — primary project constraint)
  * **NO PSRAM**: ESP32-C3 has no PSRAM capability (unlike ESP32-S3)
  * **Single Buffer Mode**: Only ONE 48KB framebuffer (not double-buffered)
* Flash: 16MB (runtime target: ~500KB, leaving ~15MB for OTA + future use)
* Display: 800x480 E-Ink (slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 x 480 / 8)
* Storage: SD Card (books, fonts, plugins, cache — the extensibility layer)

### The Resource Protocol

1. **Stack Safety**: Limit local variables to < 256 bytes. The ESP32-C3 default stack is small. Use malloc for larger buffers, document why.

2. **Heap Discipline**: Every `malloc` must have a matching `free`. Always check for NULL after allocation. Set pointer to NULL after free. Avoid repeated malloc/free in loops — allocate once, reuse.
   ```c
   uint8_t *buf = malloc(size);
   if (!buf) {
       LOG_ERR("MOD", "malloc failed: %d bytes", size);
       return false;
   }
   // use buf
   free(buf);
   buf = NULL;
   ```

3. **Flash Placement**: Large constant data (lookup tables, default strings) MUST be `static const` to stay in Flash via the instruction bus, freeing DRAM.

4. **String Policy**: Use `char[]` stack buffers with `snprintf` for construction. Use `const char*` for read-only strings. Never allocate heap strings in hot paths. For Lua strings, rely on Lua's internal string interning — don't duplicate them into C-side buffers.

5. **No Dynamic Arrays in C Runtime**: The runtime does not use growable arrays. Use fixed-size arrays with bounds checking, or Lua tables (which handle their own memory via the Lua allocator).

6. **SD Write Throttling**: Never write settings/progress on every user interaction. Guard writes with value-change checks. Debounce progress saves to activity exit or every N page turns.

7. **Integer types — pick the right size**: Use `uint8_t`/`uint16_t`/`int32_t` from `<stdint.h>`, not bare `int`. Default `int` is 32-bit on C3 — wasteful for values that fit in 8 or 16 bits, especially in arrays and structs. Be explicit about size.

8. **Struct fields — largest-to-smallest**: Order struct members by size descending. Compiler alignment padding can otherwise waste 30-50% of a struct. Free win, no `__attribute__((packed))` required (and packed brings unaligned-access risk on RISC-V).

9. **No VLAs**: Variable-length arrays go on the stack and you can't bound them at compile time. Use a fixed-size buffer or heap allocation instead.

10. **Don't use `enum` for OR'd flags**: `enum` is `int`-sized (4 bytes). Use `#define` or `static const uint8_t` for bit flags to avoid the 4× size overhead.

11. **Fixed-point over float on hot paths**: ESP32-C3 has no FPU. Every `float`/`double` op is software-emulated and pulls in a large libgcc helper. Use Q16.16 fixed-point or scaled integers for UI math, sensor scaling, animations.

12. **`restrict` on hot-loop pointer params**: When a function loops over two non-overlapping buffers, mark the parameters `restrict` so the compiler can keep values in registers instead of reloading on every access.
   ```c
   void blit(uint8_t *restrict dst, const uint8_t *restrict src, size_t n);
   ```

### RISC-V Alignment
ESP32-C3 faults on unaligned multi-byte loads. Never cast a `uint8_t*` buffer to a wider type and dereference directly. Use `memcpy`:

```c
// WRONG — faults if buf is not 4-byte aligned:
uint32_t val = *(uint32_t*)buf;

// CORRECT:
uint32_t val;
memcpy(&val, buf, sizeof(val));
```

This applies to all binary file parsing (.cfont, cache files, etc.) and any raw buffer-to-struct casting.

### IRAM_ATTR and Flash Cache Safety
All code runs from flash via the instruction cache. During SPI flash operations (OTA write, NVS update) the cache is briefly suspended. Any code that executes during this window — ISRs in particular — must reside in IRAM.

```c
// ISR handler: must be in IRAM
void IRAM_ATTR gpio_isr_handler(void *arg) { ... }

// Data accessed from IRAM code: must be in DRAM
static DRAM_ATTR volatile uint32_t isr_event_flags = 0;
```

**Rules:**
- All ISR handlers: `IRAM_ATTR`
- Data read by `IRAM_ATTR` code: `DRAM_ATTR` (flash-resident `static const` will fault)
- Normal task code does NOT need `IRAM_ATTR`

### ISR vs Task Shared State
`xSemaphoreTake()` (mutex) CANNOT be called from ISR context — it will crash. Use the correct primitive:

| Direction | Correct primitive |
|---|---|
| ISR → task (data) | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()` |
| ISR → task (signal) | `xSemaphoreGiveFromISR()` + `portYIELD_FROM_ISR()` |
| Task → task | `xSemaphoreTake()` / mutex |
| Simple flag (single writer ISR) | `volatile` + `portENTER_CRITICAL_ISR()` |

---

## Project Architecture

### Build System: PlatformIO

**CLI Tool** (`pio` command):
```bash
pio run              # Build firmware
pio run -t upload    # Build and flash to device
pio run -t clean     # Clean build artifacts
pio device monitor   # Serial monitor
```

**Configuration Files:**
* `platformio.ini`: Main build configuration (committed)
* `platformio.local.ini`: Local overrides — serial port, debug flags (gitignored)
* `partitions.csv`: ESP32 flash partition layout

### Build Environment
* **Standard**: C17 (`-std=c17`). No exceptions (C doesn't have them).
* **Compiler**: GCC for RISC-V (riscv32-esp-elf)
* **Logging**: Use `LOG_INF`, `LOG_DBG`, `LOG_ERR` macros. Never raw `printf` to serial.

### Directory Structure
```
CrossLuaReader/
├── src/
│   └── main.c                  # Entry point: boot sequence, main loop
├── lib/
│   ├── hal/                    # Hardware Abstraction Layer
│   │   ├── hal_display.c/h         # Display API (pure C)
│   │   ├── hal_gpio.c/h            # Input API (pure C)
│   │   ├── hal_storage.c/h         # Storage API (pure C)
│   │   ├── hal_power.c/h           # Power API (pure C)
│   │   ├── hal_system.c/h          # System API (pure C)
│   │   ├── bridge_display.cpp       # SDK bridge (only C++ in project)
│   │   ├── bridge_input.cpp         # SDK bridge
│   │   ├── bridge_storage.cpp       # SDK bridge
│   │   └── bridge_battery.cpp       # SDK bridge
│   ├── renderer/               # Framebuffer rendering (pure C)
│   │   └── renderer.c/h            # Pixel, line, rect drawing
│   ├── font/                   # Font system (pure C)
│   │   ├── font_loader.c/h         # .cfont file parser (reads from SD)
│   │   ├── font_cache.c/h          # LRU decompression cache
│   │   └── font_render.c/h         # Glyph rendering, drawText, measurement
│   ├── bidi/                   # Bidirectional text (pure C)
│   │   ├── bidi_classify.c/h       # Codepoint direction classification
│   │   └── bidi_reorder.c/h        # Visual reordering for RTL/mixed text
│   ├── lua/                    # Lua 5.4 interpreter (vendored, MIT license)
│   ├── lua_api/                # C → Lua API bindings
│   │   ├── api_display.c/h         # display.* module
│   │   ├── api_input.c/h           # input.* module
│   │   ├── api_storage.c/h         # storage.* module
│   │   ├── api_wifi.c/h            # wifi.* module
│   │   ├── api_font.c/h            # font.* module
│   │   ├── api_system.c/h          # system.* module
│   │   └── api_register.c/h        # Registers all modules with Lua state
│   ├── plugin/                 # Plugin lifecycle management
│   │   └── plugin_manager.c/h      # Discovery, loading, switching, errors
│   ├── zip/                    # ZIP archive access (wraps miniz/uzlib)
│   ├── xml/                    # XML/HTML parser (wraps expat or custom SAX)
│   └── json/                   # JSON parser (cJSON or custom)
├── open-x4-sdk/                # Low-level hardware SDK (symlink)
├── sdcard/                     # Default SD card contents
│   ├── plugins/                    # Core Lua plugins
│   └── fonts/                      # .cfont font files
├── tools/                      # Development tools
│   └── cfont-convert/              # TTF → .cfont converter (Python)
├── scripts/                    # Build and utility scripts
├── docs/                       # Documentation
├── platformio.ini              # Build configuration
└── partitions.csv              # Flash partition layout
```

### Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL functions, NOT SDK classes directly.

| HAL Module | Wraps SDK (via bridge) | Purpose |
|------------|----------------------|---------|
| `hal_display` | `EInkDisplay` | E-ink display control |
| `hal_gpio` | `InputManager` | Button input handling |
| `hal_storage` | `SDCardManager` | SD card file I/O |
| `hal_power` | `BatteryMonitor` | Battery, sleep, wake |
| `hal_system` | ESP-IDF system | Boot, restart, heap |

**SDK Bridge Pattern** (the ONLY place C++ is allowed):
```cpp
// lib/hal/bridge_display.cpp — thin extern "C" wrapper
#include "EInkDisplay.h"
extern "C" {
#include "hal_display.h"
}

static EInkDisplay display;

extern "C" bool hal_display_init(void) {
    display.begin();
    return true;
}

extern "C" void hal_display_write_buffer(const uint8_t *buf, size_t len) {
    display.writeBuffer(buf, len);
}
```

**HAL Usage** (pure C, what all runtime code calls):
```c
#include "hal_storage.h"

FILE *f = hal_storage_open("/books/torah.epub", "r");
if (!f) {
    LOG_ERR("MOD", "Failed to open file");
    return false;
}
// read from f
hal_storage_close(f);
```

---

## Coding Standards

### Naming Conventions
* Functions: `snake_case` with module prefix — `hal_display_init()`, `font_cache_get()`
* Variables: `snake_case` — `page_count`, `current_offset`
* Constants/Macros: `UPPER_SNAKE_CASE` — `MAX_BUFFER_SIZE`, `FRAMEBUFFER_BYTES`
* Structs: `snake_case` with `_t` suffix — `font_data_t`, `plugin_info_t`
* Files: `snake_case` — `hal_display.c`, `font_loader.h`
* Enum values: `UPPER_SNAKE_CASE` with prefix — `BTN_BACK`, `BTN_CONFIRM`

### Header Guards
Use `#pragma once` for all header files.

### Module Pattern

Every module follows this pattern:

```c
// hal_display.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Public API
bool hal_display_init(void);
void hal_display_clear(uint8_t color);
void hal_display_refresh(void);
int  hal_display_width(void);
int  hal_display_height(void);
```

```c
// hal_display.c
#include "hal_display.h"
#include "logging.h"

// Private state
static uint8_t *framebuffer = NULL;
static int display_width = 0;
static int display_height = 0;

// Private helpers
static void spi_write_command(uint8_t cmd) { ... }

// Public implementation
bool hal_display_init(void) {
    // ...
}
```

### Error Handling

**Pattern Hierarchy:**
1. **LOG_ERR + return false/NULL** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); use_default();`
3. **assert()**: Only for fatal "impossible" states (framebuffer missing at boot)
4. **esp_restart()**: Only for recovery (OTA complete)

**Rules:** No exceptions (C doesn't have them). ALWAYS log before error return. Never silently fail.

### Lua API Binding Pattern

Every Lua-exposed function follows this pattern:

```c
// api_display.c
#include "lua.h"
#include "lauxlib.h"
#include "hal_display.h"
#include "font_render.h"

// display.drawText(fontId, x, y, text)
static int l_display_draw_text(lua_State *L) {
    int font_id = luaL_checkinteger(L, 1);
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    const char *text = luaL_checkstring(L, 4);

    font_render_draw_text(font_id, x, y, text);
    return 0;  // no return values
}

// display.width()
static int l_display_width(lua_State *L) {
    lua_pushinteger(L, hal_display_width());
    return 1;  // one return value
}

// Register all display.* functions
void api_display_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"drawText", l_display_draw_text},
        {"width", l_display_width},
        // ...
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "display");
}
```

**Rules:**
- Always validate arguments with `luaL_check*` functions
- Return value count must match `return N` statement
- Use `lua_pushnil` + `lua_pushstring` for error returns (Lua convention: `nil, "error message"`)
- Never call `malloc` in Lua API functions — let Lua manage its own memory
- Keep binding functions thin — call into HAL/renderer, don't put logic here

---

## Plugin System

### Plugin Lifecycle

Plugins are `.lua` files on the SD card at `/plugins/`. Each defines a `plugin` table:

```lua
plugin = {
    name = "My Plugin",
    id = "my_plugin",
    type = "activity",      -- "activity", "reader", "service"
    menuEntry = "My Tool",  -- shown in home menu (nil = hidden)
}

function plugin.onEnter(arg)  end  -- called when activated
function plugin.loop()        end  -- called every frame
function plugin.onExit()      end  -- called when deactivated
```

### Plugin Manager Rules
- Each plugin runs in its own Lua state (isolation)
- Plugin switching: call `onExit()` on current, destroy state, create new state, load new plugin, call `onEnter()`
- All plugin calls wrapped in `lua_pcall` — errors logged, never crash the runtime
- Watchdog: if `loop()` takes >5 seconds, force-kill and show error screen
- Plugin errors show a friendly message with option to disable the plugin

### Reader Plugins
Reader plugins register file extensions:
```lua
plugin = {
    type = "reader",
    fileExtensions = {"epub", "epub3"},
}
function plugin.canOpen(path) return true end
function plugin.open(path) ... end
function plugin.renderPage(pageNum) ... end
function plugin.getPageCount() return N end
```

---

## Font System

### SD-Loaded Fonts (.cfont format)
Fonts are binary files on the SD card, loaded on demand:
- `/fonts/NotoSans-14-Regular.cfont`
- `/fonts/NotoSansHebrew-14-Regular.cfont`
- `/fonts/Ubuntu-12-Regular.cfont`

The font loader reads `.cfont` files into the same data structures the renderer expects. The LRU cache decompresses glyph groups on demand — only the active page's glyphs are in RAM at any time.

### Font Rendering
The font renderer is pure C and handles:
- Glyph lookup by codepoint (binary search on intervals)
- Bitmap decompression (DEFLATE, group-based LRU cache)
- Text drawing with kerning and ligatures
- Combining mark positioning (diacriticals, Hebrew nikkud)
- BiDi text reordering (RTL/mixed text)
- Text width measurement and advance calculation

### BiDi / RTL Support
Built into the font renderer, not plugins:
- `bidi_classify`: codepoint direction classification (Hebrew, Arabic → RTL)
- `bidi_reorder`: visual reordering of mixed LTR/RTL text
- Grapheme cluster preservation during reversal (combining marks stay attached)
- RTL auto-detection for paragraph alignment

---

## Orientation-Aware Logic
* Never hardcode 800 or 480. Use `hal_display_width()` and `hal_display_height()`.
* Use `hal_display_get_margins()` to stay within physical bezel margins.
* All coordinate transforms handled by the renderer — plugins work in logical coordinates.

---

## Build and Flash

### Build Commands
```bash
pio run                  # Build firmware
pio run -t upload        # Build and flash
pio run -t clean         # Clean build
pio device monitor       # Serial monitor
```

### Debugging

**Heap monitoring:**
```c
LOG_DBG("MEM", "Free: %d bytes", esp_get_free_heap_size());
```

**Stack monitoring:**
```c
LOG_DBG("TASK", "Stack HWM: %d", uxTaskGetStackHighWaterMark(NULL));
```

**Common crashes:**
1. **Out of memory** — check heap before/after operations, verify Lua state size
2. **Stack overflow** — increase task stack, move large buffers to heap
3. **Alignment fault** — use `memcpy` for unaligned reads from binary data
4. **Watchdog timeout** — add `vTaskDelay(1)` in tight loops
5. **Plugin error** — check serial log for Lua pcall error messages

---

## Git Workflow

### Branch Naming
```
feature/<description>       # New features
fix/<description>           # Bug fixes
refactor/<component>        # Code refactoring
docs/<topic>                # Documentation
```

### Commit Messages
```
<type>: <short summary (50 chars max)>

<optional detailed description>
```
Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

### Rules
- **If uncertain, ASK before committing.**
- Never commit build artifacts (`.pio/`, `compile_commands.json`)
- Never commit personal config (`platformio.local.ini`)
- Always `pio run` successfully before committing

---

## Key Differences from CrossPoint

| Aspect | CrossPoint | CrossLua Reader |
|--------|-----------|----------|
| Language | C++20 | Pure C17 |
| Architecture | Monolithic firmware | Thin runtime + Lua plugins |
| Features | Compiled in | Loaded from SD card |
| Fonts | Baked into flash (~4MB) | Loaded from SD (.cfont) |
| Flash usage | ~5.8MB (89%) | ~500KB target (8%) |
| Extensibility | Requires recompilation | Drop .lua file on SD |
| Application logic | C++ Activities | Lua plugin scripts |
| UI strings | Compiled string tables | Lua-side or SD-loaded |

---

Philosophy: CrossLua Reader is a platform, not an application. The runtime provides capabilities; plugins provide features. Keep the runtime minimal, stable, and extensible. Every line of C in the runtime should justify why it can't be Lua instead.
