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
4. hal_storage_init()       → SD card mount
5. hal_power_init()         → Battery ADC, sleep timer
6. font_cache_init()        → LRU decompression cache
7. boot_font_load()         → Load Ubuntu-12 into slot 0 (crash/sleep screens)
8. plugin_manager_init()    → Scan /plugins/, parse manifests (C-side, no Lua)
9. plugin_manager_start()   → Create Lua state, load home plugin
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

A boot font (NotoSans-12) is loaded in C at startup (font slot 0) before any Lua runs. This enables text rendering for crash screens and sleep overlays without depending on Lua.

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
