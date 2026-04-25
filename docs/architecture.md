# CrossLua Reader Architecture

## Overview

CrossLua Reader splits the firmware into two layers:

1. **Native C runtime** (~500KB in flash) — hardware access, rendering, Lua interpreter
2. **Lua plugins** (on SD card) — all application logic, UI, readers, network features

The C runtime never changes during normal use. All extensibility happens through Lua scripts on the SD card.

## Directory Structure

```
CrossLuaReader/
├── src/                    # Main entry point
│   └── main.c              # Boot sequence, main loop
├── lib/
│   ├── hal/                # Hardware Abstraction Layer (pure C)
│   │   ├── hal_display.c/h     # E-ink SPI display driver
│   │   ├── hal_gpio.c/h        # Button input, ISR handling
│   │   ├── hal_storage.c/h     # SD card file I/O
│   │   ├── hal_power.c/h       # Battery, sleep, watchdog
│   │   └── hal_system.c/h      # Boot, restart, heap stats
│   ├── renderer/           # Framebuffer rendering (pure C)
│   │   └── renderer.c/h        # Pixel, line, rect, polygon drawing
│   ├── font/               # Font loading and text rendering (pure C)
│   │   ├── font_loader.c/h     # .cfont file parser
│   │   ├── font_cache.c/h      # LRU decompression cache
│   │   └── font_render.c/h     # Glyph rendering, drawText, text measurement
│   ├── bidi/               # Bidirectional text support (pure C)
│   │   ├── bidi_classify.c/h   # Codepoint direction classification
│   │   └── bidi_reorder.c/h    # Visual reordering for RTL/mixed text
│   ├── lua/                # Lua 5.4 interpreter source (vendored, MIT)
│   ├── lua_api/            # C → Lua API bindings
│   │   ├── api_display.c/h     # display.* module
│   │   ├── api_input.c/h       # input.* module
│   │   ├── api_storage.c/h     # storage.* module
│   │   ├── api_wifi.c/h        # wifi.* module
│   │   ├── api_font.c/h        # font.* module
│   │   ├── api_system.c/h      # system.* module
│   │   └── api_register.c/h    # Registers all modules with Lua state
│   ├── plugin/             # Plugin lifecycle management
│   │   └── plugin_manager.c/h  # Discovery, loading, switching, error handling
│   ├── zip/                # ZIP/EPUB archive access (pure C, wraps miniz/uzlib)
│   ├── xml/                # XML/HTML parser (pure C, wraps expat or custom SAX)
│   └── json/               # JSON parser (pure C, cJSON or custom)
├── open-x4-sdk/            # Low-level hardware SDK (symlink)
├── sdcard/                 # Default SD card contents (shipped with firmware)
│   ├── plugins/            # Core Lua plugins
│   └── fonts/              # .cfont font files
├── tools/                  # Development tools
│   └── cfont-convert/      # TTF → .cfont converter (Python)
├── scripts/                # Build and utility scripts
├── docs/                   # Documentation
├── platformio.ini          # Build configuration
├── partitions.csv          # Flash partition layout
├── build_spec.md           # Project specification
└── build_plan.md           # Phased build plan
```

## Runtime Flow

### Boot Sequence

```
1. hal_system_init()        → Clock, watchdog, USB CDC
2. hal_display_init()       → SPI init, e-ink power on
3. hal_gpio_init()          → Button ISRs registered
4. hal_storage_init()       → SD card mount
5. hal_power_init()         → Battery ADC, sleep timer
6. font_loader_init()       → Scan /fonts/, load UI font
7. lua_init()               → Create Lua state, register API modules
8. plugin_manager_init()    → Scan /plugins/, parse manifests
9. plugin_manager_start()   → Load home plugin or restore last state
10. main_loop()             → Dispatch loop() to active plugin
```

### Main Loop

```c
while (1) {
    hal_gpio_poll();                    // Update button states
    plugin_manager_dispatch_loop();     // Call active plugin's loop()
    hal_power_check_sleep();            // Sleep if idle
    vTaskDelay(1);                      // Yield to FreeRTOS
}
```

### Plugin Switching

```c
// When a plugin requests navigation to another plugin:
plugin_manager_switch("epub_reader", "/books/torah.epub");

// Internally:
// 1. Call current plugin's onExit()
// 2. Free Lua state for current plugin
// 3. Create new Lua state
// 4. Load and execute new plugin .lua file
// 5. Call new plugin's onEnter(arg)
```

## Memory Model

### Measured (Phase 1 — HAL + renderer, no Lua/fonts yet)

| Metric | Value | % of Budget |
|--------|-------|-------------|
| Flash used | 376KB | 5.7% of 6.5MB |
| RAM used | 68KB | 20.9% of 327KB |

### Projected (full runtime with Lua + fonts)

| Region | Size | Usage |
|--------|------|-------|
| Flash | ~500KB | C runtime + Lua interpreter |
| DRAM | ~380KB total | |
| — Arduino/ESP-IDF base | ~68KB | Measured Phase 1 baseline |
| — Lua state | ~50KB | Interpreter + script state |
| — Lua stack | ~20KB | Execution stack |
| — Font cache | ~30KB | LRU decompressed glyph groups |
| — Framebuffer | 48KB | Single e-ink buffer (inside SDK) |
| — WiFi (when active) | ~50KB | TCP/IP stack |
| — Available | ~100-150KB | Plugin working memory |
| SD card | 32GB+ | Fonts, plugins, books, cache |

## Font System

Fonts are stored as `.cfont` binary files on the SD card. The font loader reads them on demand, and the LRU cache keeps recently-used glyph groups decompressed in RAM.

See `docs/cfont-format.md` for the binary format specification.

## Plugin API

See `build_spec.md` for the complete Lua API surface (display, input, storage, wifi, font, zip, xml, json, system, i18n modules).

## Relationship to CrossPoint

CrossLua Reader is inspired by and built on hardware knowledge from CrossPoint Reader. Key things carried forward:
- Hardware driver understanding (e-ink timing, button mapping, SD SPI)
- Font rendering approach (2-bit compressed bitmaps, LRU cache)
- BiDi/RTL text handling
- The open-x4-sdk submodule

Key things changed:
- C++ → pure C
- Monolithic → plugin architecture
- Compiled fonts → SD-loadable .cfont files
- All application logic → Lua scripts
