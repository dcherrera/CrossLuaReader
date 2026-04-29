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
│   │   ├── hal_power.c/h       # Battery, sleep, USB detection
│   │   ├── hal_system.c/h      # Boot, restart, heap stats
│   │   ├── boot_font.c/h       # Boot-time font for crash/sleep screens
│   │   └── sleep_screen.c/h    # Sleep screen modes + wallpaper rendering
│   ├── renderer/           # Framebuffer rendering (pure C)
│   │   ├── renderer.c/h        # Pixel, line, rect, polygon drawing
│   │   └── bmp_decoder.c/h     # Streaming BMP-to-framebuffer decoder
│   ├── font/               # Font loading and text rendering (pure C)
│   │   ├── font_manager.c/h    # Multi-font slot management + fallback chains
│   │   ├── font_loader.c/h     # .cfont file parser
│   │   ├── font_cache.c/h      # LRU decompression cache
│   │   └── font_render.c/h     # Glyph rendering, drawText, fallback-aware text
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
│   │   ├── api_text.c/h        # text.* module (streaming indexing + page layout)
│   │   └── api_register.c/h    # Registers all modules with Lua state
│   ├── plugin/             # Plugin lifecycle management
│   │   └── plugin_manager.c/h  # Discovery, loading, switching, error handling
│   ├── zip/                # ZIP/EPUB archive access (pure C, wraps miniz/uzlib)
│   ├── xml/                # XML/HTML parser (pure C, wraps expat or custom SAX)
│   └── json/               # JSON parser (pure C, cJSON or custom)
├── open-x4-sdk/            # Low-level hardware SDK (symlink)
├── sdcard/                 # Default SD card contents (shipped with firmware)
│   ├── plugins/            # Lua plugins (single .lua files or folders with main.lua)
│   ├── fonts/              # .cfont font files (Latin, Cyrillic, Greek)
│   └── languages/          # Language packs (drop-in folders)
│       ├── en/lang.json        # English (default)
│       └── he/                 # Hebrew pack
│           ├── lang.json       # UI translations + font metadata
│           └── fonts/          # Hebrew .cfont files
├── font_packs/             # Pre-built font packs (copy to SD /fonts/)
├── lang_packs/             # Pre-built language packs (copy to SD /languages/)
├── templates/              # Plugin templates (copy to SD /plugins/)
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
4. hal_storage_init()       → SD card mount (logs and continues on failure)
5. hal_power_init()         → Battery ADC, sleep timer
6. font_cache_init()        → LRU decompression cache
7. boot_font_load()         → Try /fonts/Ubuntu/Ubuntu-12-Regular.cfont from SD;
                               on failure load the firmware-bundled copy from
                               .rodata. Boot font is always available.
8. plugin_manager_init()    → Scan /plugins/, parse manifests (C-side, no Lua)
9. plugin_manager_start()   → Create Lua state, load home plugin from SD;
                               if home isn't on SD, fall back to firmware-
                               bundled rescue home.
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

### Measured

| Phase | Flash | RAM | What |
|-------|-------|-----|------|
| 1 - HAL + renderer | 376KB (5.7%) | 68KB (20.9%) | Hardware, pixels |
| 2 - Font system | 396KB (6.0%) | 71KB (21.7%) | .cfont, loader, cache, renderer, BiDi |
| 3 - Lua interpreter | 552KB (8.4%) | 71KB (21.7%) | Lua 5.4 + all API bindings |
| 4 - Plugin manager | 554KB (8.5%) | 75KB (22.9%) | Discovery, lifecycle, switching |
| 5 - Core UI plugins | 555KB (8.5%) | 75KB (22.9%) | Home, browser, settings + renderer additions |
| 6 - Settings & persistence | 562KB (8.6%) | 75KB (22.9%) | Settings, fonts, progress, sleep, physical button bar, content area |
| 7 - Font fallback & language packs | 564KB (8.6%) | 75KB (22.9%) | Per-slot font fallback, language pack discovery, UI translation |
| 8 - Sleep screen & error recovery | 568KB (8.7%) | 77KB (23.6%) | Boot font, crash screen, BMP wallpapers, SD reload, USB detection |
| Optimizations | 568KB (8.7%) | 75KB (22.8%) | On-demand glyphs, C-side discovery, 3 font slots, cache 48, X4 framebuffer, zero-alloc wallpapers, no coroutine lib |
| 8.5 - Firmware home fallback | 636KB (9.7%) | 77KB (23.5%) | Embedded rescue Lua + embedded boot font (.cfont) in .rodata, plugin-manager fallback, deferred system.reload() |

**Runtime free heap: 89KB available for plugins** (measured with home plugin active).
**Plugin discovery: 12ms** (C-side string parsing, no Lua states created).

### Projected (full runtime with Lua + fonts)

| Region | Size | Usage |
|--------|------|-------|
| Flash | ~554KB | C runtime + Lua interpreter + plugin manager |
| DRAM | ~380KB total | |
| — Arduino/ESP-IDF base | ~68KB | Measured Phase 1 baseline |
| — Lua state | ~80-90KB | Interpreter + libs + script tables |
| — Boot font metadata | ~8KB | Intervals + kerning (on-demand glyphs) |
| — Framebuffer | 48KB | Single e-ink buffer (X4-only, inside SDK) |
| — Font bitmap cache | ~5KB | LRU decompressed glyph groups (3 slots) |
| — WiFi (when active) | ~50KB | TCP/IP stack |
| — Available | **~89KB** | Plugin working memory (measured) |
| SD card | 32GB+ | Fonts, plugins, books, cache |

## Font System

Fonts are stored as `.cfont` binary files on the SD card. The font system uses on-demand glyph loading: only the binary search index (intervals), compression groups, kerning tables, and ligature pairs are loaded into RAM. Glyph metrics (the largest section — 14 bytes × glyph count) stay on SD and are read through a 48-entry LRU cache per font slot. This reduces per-font RAM from ~25-31KB to ~2-8KB. The bitmap cache separately handles glyph image decompression on demand.

### Embedded fonts (firmware-resident)

A font can also be loaded from a flash buffer instead of the SD card. The boot font ships in firmware via `tools/embed_assets.py`, which converts `sdcard/fonts/Ubuntu/Ubuntu-12-Regular.cfont` into `lib/font/embedded_boot_font.h` as a `const unsigned char[]` array. Use `font_manager_load_buffer(data, len)` to register an embedded font; from Lua, `font.boot()` returns the slot id of whichever copy of the boot font is currently loaded.

`font_data_t` carries an optional `embedded_data` / `embedded_size` pair. When non-NULL, every on-demand read in `font_cache.c` and `font_render.c` (glyph metrics, compressed bitmap groups, uncompressed bitmaps, aligned-offset width-height probes) does a `memcpy` from `embedded_data + offset` instead of opening an SD file. The cache infrastructure is otherwise unchanged — bitmap groups still decompress into the same LRU slots. A `cfont_src_t` abstraction inside `font_loader.c` lets `parse_cfont_body()` populate the metadata identically from either source.

Cost: the embedded `.cfont` lives in `.rodata` (flash), zero DRAM until parsed. Metadata parsed on load is ~1-2 KB of heap, identical to the SD-loaded path. Bitmap decompression still allocates per-group cache slots only when glyphs are actually drawn.

### Font Fallback

Each font slot can have a fallback font. When a glyph is missing from the primary font, the renderer tries the fallback before falling back to U+FFFD. This enables seamless mixed-script text (e.g., Latin + Hebrew in one `drawText` call).

The fallback chain is: primary exact → fallback exact → primary U+FFFD → skip.

Fallback is configured from Lua via `font.setFallback(primaryId, fallbackId)`. The `lib/fonts.lua` module auto-loads fallback fonts based on the selected language.

### Language Packs

Drop-in folders at `/languages/{code}/` on the SD card. Each contains:
- `lang.json` — metadata (name, direction, font family) and UI string translations
- `fonts/` — script-specific .cfont files (e.g., NotoSansHebrew)

The settings plugin auto-discovers language packs. The `lib/lang.lua` module provides `lang.tr(key)` for translated UI strings with English fallback.

See `docs/language-packs.md` for the full specification.
See `docs/font-packs.md` for how to create and install font packs.
See `docs/cfont-format.md` for the binary font format specification.

## Sleep Screen

A boot font (Ubuntu-12-Regular) is loaded in C at startup (font slot 0) before any Lua runs. This enables text rendering for crash screens, sleep overlays, and the firmware-home rescue UI without depending on Lua. The font is loaded from SD when present, with a firmware-bundled copy in `.rodata` as fallback — see [Firmware Home Fallback](#firmware-home-fallback) below.

Sleep screen modes (set via `system.setSleepMode()` from Lua):
- **Blank** — clear screen to white
- **Single** — show a specific BMP wallpaper from `/wallpapers/`
- **Cycle** — show wallpapers in order, advancing each sleep
- **Random** — random pick from `/wallpapers/`
- **Clear** — keep current page content, overlay "SLEEP" + battery %

The BMP decoder (`lib/renderer/bmp_decoder.c`) streams images from SD row-by-row with Bayer 4x4 ordered dithering for 24-bit→1-bit conversion. No full-image allocation.

Plugins can register a Lua callback via `system.setSleepHook(func)` to draw custom content (text, boxes, images) on top of the sleep screen. The hook runs after the base screen renders but before the display refresh. This enables features like quote overlays. The hook is auto-cleared on plugin exit and errors are caught safely.

See `docs/sleep-screen.md` for the full specification.

## Error Recovery

When `plugin.loop()` raises a Lua error, the plugin manager catches it via `lua_pcall`, stops the plugin, and shows a crash screen with the plugin name and error message using the boot font. The user presses any button to return to home. This ensures the device never gets stuck on a crashed plugin.

## Firmware Home Fallback

The runtime ships with a minimal rescue UI bundled into firmware so the device always boots to a usable screen, even when the SD card is missing, unmounted, or has no `/plugins/` directory. The premise: SD failure must not produce a black screen.

### What's bundled

Two assets land in `.rodata` (flash) at build time, generated by `tools/embed_assets.py`:

| Source | Generated header | Purpose |
|--------|------------------|---------|
| `src/embedded/firmware_home.lua` | `lib/plugin/firmware_home_lua.h` | Rescue UI: "PLEASE INSERT SD" + Reload SD action + Confirm hint cell. No `require()` — uses only the boot font and the core `display.*` / `input.*` / `system.*` / `font.*` APIs. |
| `sdcard/fonts/Ubuntu/Ubuntu-12-Regular.cfont` | `lib/font/embedded_boot_font.h` | Firmware-resident copy of the boot font, so text can render in the rescue UI when SD itself is the failure. |

Each is emitted as a `const unsigned char[]` plus a `_len` constant. The embed script is idempotent — if the source hasn't changed, the header isn't rewritten and downstream compilation isn't invalidated. Both generated headers are in `.gitignore`; the source files are committed.

### Plugin-manager fallback path

`plugin_manager_start("home", NULL)` is the only call site that triggers the fallback:

1. Resolve target id (saved state → "home" → embedded fallback if no plugins were discovered).
2. `find_plugin_by_id("home")` — if the SD home is present, normal load proceeds.
3. If `target_id == "home"` and no SD plugin matches, `start_firmware_home()` runs:
   - `api_create_state()` builds a fresh Lua state with all stock bindings.
   - `luaL_loadbuffer` parses `firmware_home_lua[]` with chunkname `=firmware_home`.
   - The chunk runs (sets the `plugin` global), nav functions register, `plugin.onEnter()` fires.
   - `active_index = -1` is the sentinel meaning "active plugin is firmware-bundled, not in `plugins[]`." The crash-screen path in `dispatch_loop` guards on this and uses the literal label `"Firmware Home"` instead of indexing `plugins[-1]`.

When the user presses Confirm in the rescue UI, `system.reload()` (see contract below) reinitializes SD and restarts. If the card now mounts and `/plugins/home.lua` is present, the SD home replaces the firmware copy. If not, the rescue UI re-renders.

Boot-font handling mirrors this pattern: `main.cpp` tries `font_manager_load("/fonts/Ubuntu/Ubuntu-12-Regular.cfont")` first; on failure it calls `font_manager_load_buffer(embedded_boot_font, embedded_boot_font_len)`. The embedded copy is only parsed when SD's copy is unavailable, so RAM cost is identical to the SD path in the common case.

### `system.reload()` deferred-reload contract

`system.reload()` (Lua) reinitializes the SD card and restarts the active plugin. The naive implementation closed the running Lua state synchronously, which is a use-after-free: the function is invoked from inside a `lua_pcall` on that very state.

The corrected contract:

1. `l_system_reload()` calls `plugin_manager_request_reload()`, which sets a `reload_pending` flag and returns immediately. The current `plugin.loop()` finishes naturally; the pcall unwinds; control returns to `dispatch_loop`.
2. At the top of the next `dispatch_loop` tick — *outside* any pcall — the flag is observed. The dispatcher then calls `plugin_manager_stop()` (closes the state cleanly, runs `onExit`), `hal_storage_reinit()`, `plugin_manager_reinit()`, and `plugin_manager_start("home", NULL)`. The new active state may be the SD home or, if SD is still unavailable, another firmware-home instance.

`plugin_manager_request_reload()` is the public API; `plugin_manager_reinit()` remains exposed but is documented as MUST NOT be called from inside a Lua call on the active state. Any future Lua-callable that wants to re-enter the plugin manager should follow this same defer-and-return pattern.

### Bypass behavior

The fallback fires only when SD-side discovery doesn't return a plugin with id `"home"`. If `home.lua` is on SD but errors at parse or in `onEnter`, that's the regular crash-recovery path's problem — the firmware home is not a catch-all crash net, only an SD-availability net.

### Memory accounting

Phase 8.5 adds ~68 KB of flash (~52 KB embedded `.cfont`, ~2.5 KB embedded Lua source, the rest is C-side fallback code). RAM impact when SD is healthy is ~80 bytes — both flash buffers stay in `.rodata` until parsed, and parsing only happens when SD-side loads fail.

## Power Button

- **Short press** (0.5-2s): Enter deep sleep with sleep screen
- **Long press** (>2s): Re-initialize SD card and restart plugins from home (SD hot-swap reload)
- **Auto-sleep**: Configurable timeout, auto-suppressed when USB is connected

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
