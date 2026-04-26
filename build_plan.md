# Build Plan: CrossLua Reader (C Runtime + C++ SDK Bridge)

## Phase 1: Bare Metal Foundation ✅

- \[x\] PlatformIO project, logging, SDK bridge files, C HAL layer, framebuffer renderer

- \[x\] Build: Flash 5.7% (376KB), RAM 20.9% (68KB)

## Phase 2: Font System ✅

- \[x\] .cfont format, converter, font loader, LRU cache, text renderer, BiDi, UTF-8

- \[x\] Build: Flash 6.0% (396KB), RAM 21.7% (71KB)

## Phase 3: Lua Interpreter ✅

- \[x\] Lua 5.4.7, API bindings (display, input, storage, system, font), SD file loading

- \[x\] Build: Flash 8.4% (552KB), RAM 21.7% (71KB)

## Phase 4: Plugin Manager ✅

- \[x\] Discovery, manifest, lifecycle, switching, navigation, error handling, state persistence

- \[x\] Build: Flash 8.5% (554KB), RAM 22.9% (75KB)

## Phase 5: Core UI Plugins ✅

- \[x\] Home, file browser, settings, shared modules (theme, ui, buttons, status_bar), templates

- \[x\] Gray dithered selection, 4-button CrossPoint-style hints, input fix, float coords

- \[x\] Build: Flash 8.5% (555KB), RAM 22.9% (75KB)

- \[x\] Tested on device: home screen, navigation, file browser all working

## Phase 6: Settings & Persistence ✅
- [x] C-side: configurable sleep timeout (`hal_power_set_sleep_timeout`, `hal_power_suppress_sleep`)
- [x] C-side: `system.setSleepTimeout(minutes)` and `system.suppressSleep(bool)` Lua bindings
- [x] C-side: physical coordinate drawing (`renderer_draw_pixel/line/rect_physical`) for button hints
- [x] C-side: `renderer_get_content_area()` — orientation-aware content bounds excluding button bar
- [x] C-side: `display.contentArea()`, `display.drawLinePhysical()`, `display.drawRectPhysical()`, `display.drawTextPhysical()` Lua bindings
- [x] Fixed all `luaL_checkinteger` → `lua_tointeger` across all API bindings (Lua 5.4 float/int mismatch)
- [x] Created `plugins/lib/settings.lua` — JSON read/write, defaults, load/get/set/save API
- [x] Created `plugins/lib/fonts.lua` — system font manager (fonts.ui = Ubuntu 12, fonts.reader from settings)
- [x] Created `plugins/lib/progress.lua` — reading progress persistence ({book}_progress files)
- [x] Rewrote `plugins/lib/buttons.lua` — pre-defined orientation layouts, custom slot, context labels
- [x] Rewrote `plugins/settings.lua` — driven by lib/settings, applies changes immediately (orientation, font, theme, sleep)
- [x] Updated `plugins/home.lua` — loads settings on boot, applies orientation/theme/sleep, uses fonts.ui
- [x] Updated `plugins/file_browser.lua` — uses fonts.ui instead of hardcoded font
- [x] Updated `plugins/lib/ui.lua` — content area bounds, physical button hints, no content/button overlap
- [x] Button hints render at physical bottom in all orientations
- [x] Content area correctly excludes button bar zone per orientation (portrait=bottom, landscape_cw=left, inverted=top, landscape_ccw=right)
- [x] Build passes: Flash 8.6% (562KB), RAM 22.9% (75KB)
- [x] Tested on device: settings persist, orientation works globally, content/buttons don't overlap
- [x] **Document**: Written `docs/settings-schema.md`, updated `docs/lua-api.md`, updated `docs/architecture.md`
- [x] **Update plan**: Completed

## Phase 7: Font Fallback & Language Packs

- \[ \] C-side: add fallback font support to `font_render.c`

  - `font_render_set_fallback(primary, fallback, fallback_path)` — glyph lookup tries primary → fallback

  - `font.setFallback(primaryId, fallbackId)` Lua binding

- \[ \] Create `plugins/lib/fonts.lua` `detect_fallbacks(text_sample)` function

  - Scan text for non-Latin codepoints (Hebrew, Arabic, CJK ranges)

  - Auto-load language-specific font as fallback from language pack

- \[ \] Language pack system — drop-in `/languages/{code}/` folders

  - `lang.json`: metadata, direction, font family, unicode ranges, UI string translations

  - `fonts/`: language-specific .cfont files

  - Auto-discovered by settings plugin

- \[ \] Ship default language packs: English + Hebrew

- \[ \] Settings: language selection from discovered packs

- \[ \] `settings.tr(key)` — translated UI string, falls back to English

- \[ \] Generate Hebrew language pack fonts (NotoSansHebrew .cfont files with Hebrew ranges)

- \[ \] Test: open Hebrew EPUB with NotoSans selected → auto-falls back to Hebrew font

- \[ \] **Document**: Write `docs/language-packs.md`. Update `docs/lua-api.md`. Update `docs/architecture.md`.

- \[ \] **Update plan**: Check off completed tasks.

## Phase 8: Sleep Screen & Error Recovery

- \[ \] Sleep screen system

  - `/wallpapers/` folder on SD — user drops BMP images

  - Settings: single wallpaper, cycle (top-to-bottom loop), random, or clear (stay on page)

  - "Clear sleep" for reading: stays on current page, "SLEEP" text by page count

  - Trigger: long-hold power button OR idle timeout

  - C-side: render sleep screen before entering deep sleep

- \[ \] Plugin crash screen (C-side, no Lua needed)

  - Shows offending plugin name + error message

  - "Press Confirm to return to Home"

  - C side loads UI font at boot (before Lua) for crash screen rendering

  - Never leaves user stuck — always recovers to home

- \[ \] Plugin manager UI in settings

  - List all discovered plugins: system (always on) vs user (manual activation)

  - Show dependencies, block activation if missing

  - RAM usage bar

  - File extension priority (which reader handles .txt, .epub, etc.)

  - Plugin on/off persists via settings

- \[ \] USB connection detection — suppress auto-sleep when USB connected

- \[ \] Test: sleep screen renders, crash recovery works, plugin manager functional

- \[ \] **Document**: Update `docs/plugin-guide.md` with manifest extensions. Update `docs/architecture.md`.

- \[ \] **Update plan**: Check off completed tasks.

## Phase 9: Reader Plugins

- \[ \] Implement `zip.*` C API (open, list, read, close) — wraps miniz/uzlib

- \[ \] Implement `xml.*` C API (parse, find, attr, text) — wraps expat or simple SAX

- \[ \] Write shared reader library (`/plugins/lib/reader_utils.lua`)

  - Page turn handling (forward, back, long-press chapter skip)

  - Progress save/load via lib/progress.lua

  - Status bar integration via lib/status_bar.lua

  - Orientation-aware viewport calculation

  - Screen refresh management (full refresh every N pages, configurable)

  - Cache directory convention: `/cache/{plugin_id}/`

- \[ \] Write TXT reader plugin (`/plugins/txt_reader.lua`)

  - Streaming file reading (8KB chunks, not full file in memory)

  - Word wrapping with viewport awareness

  - Page offset indexing and caching to `/cache/txt_reader/`

  - RTL detection and right-alignment

  - Progress persistence via lib/progress.lua

  - Font fallback via lib/fonts.lua

- \[ \] Write MD reader plugin (`/plugins/md_reader.lua`)

  - Markdown parsing: headers, bold/italic, lists, blockquotes, code blocks, horizontal rules, links, images, task lists

  - Styled span rendering with BiDi support

  - Nested list indentation

  - Page indexing with code block state tracking

- \[ \] Write EPUB reader plugin (`/plugins/epub_reader.lua`)

  - ZIP extraction via `zip.*` API

  - HTML/XML parsing via `xml.*` API

  - CSS parsing: fonts, alignment, margins, text-indent, display

  - Word layout and line breaking (may need native C helper for performance)

  - Justified text with proportional word spacing

  - Chapter navigation and table of contents

  - Page caching to `/cache/epub_reader/`

  - Footnote support with navigation

  - Image display (BMP, JPG via native helper)

  - RTL paragraph detection and right-alignment

  - Embedded style vs user style override

- \[ \] Write chapter selection sub-plugin (`/plugins/chapter_select.lua`)

- \[ \] Write go-to-percent sub-plugin (`/plugins/goto_percent.lua`)

- \[ \] Test: all three readers work, progress persists, cache clears properly

- \[ \] Test: Hebrew content auto-loads fallback font

- \[ \] **Document**: Write `docs/reader-plugin-guide.md`. Write `docs/lua-api.md` additions for `zip.*` and `xml.*`. Update `docs/architecture.md`.

- \[ \] **Update plan**: Check off completed tasks.

## Phase 10: Network Plugins

- \[ \] Implement `wifi.*` Lua bindings (connect, disconnect, isConnected, getIP, fetch, fetchJson, post, download)

  - Bridge: `bridge_wifi.cpp` wrapping Arduino WiFi library

  - HAL: `hal_wifi.c/h`

- \[ \] Write web server plugin (`/plugins/web_server.lua`)

  - Serve file upload page at device IP

  - Drag-and-drop book upload from any browser

  - Device info page (battery, storage, firmware version)

  - Extensible: other plugins can register routes

  - Sleep suppression while server is active

  - Possible: C-side FreeRTOS background task for server (allows reading while serving)

- \[ \] Write OPDS browser plugin (`/plugins/opds_browser.lua`)

- \[ \] Write KOReader sync plugin (`/plugins/kosync.lua`)

- \[ \] Write OTA updater plugin (`/plugins/ota_updater.lua`)

- \[ \] Write Calibre connect plugin (`/plugins/calibre.lua`)

- \[ \] Write Sefaria browser plugin (`/plugins/sefaria.lua`)

  - Browse text hierarchy (Tanakh, Talmud, Mishnah, etc.)

  - Download chapters/books to SD

  - Daily readings (Parashat HaShavua, Daf Yomi, etc.)

  - Search

- \[ \] Test: all network features work on device

- \[ \] **Document**: Write `docs/lua-api.md` additions for `wifi.*`. Write `docs/web-server-guide.md`. Update `docs/architecture.md`.

- \[ \] **Update plan**: Check off completed tasks.

## Phase 11: Polish & Release

- \[ \] Watchdog timer for runaway Lua scripts

- \[ \] Default SD card image with all core plugins + fonts + language packs

- \[ \] Plugin developer documentation (full API reference with examples)

- \[ \] Example plugin template (`/plugins/template.lua`)

- \[ \] On-screen keyboard plugin (`/plugins/keyboard.lua`) for text entry

- \[ \] Performance profiling: boot time, page turn latency, memory usage

- \[ \] Final flash size verification

- \[ \] Final RAM usage verification (<300KB during operation)

- \[ \] LTR + RTL + Hebrew regression testing across all readers

- \[ \] All 4 orientations testing

- \[ \] Feature parity checklist vs CrossPoint:

  - \[ \] EPUB 2 and EPUB 3 rendering

  - \[ \] Image support in EPUB

  - \[ \] Reading progress persistence

  - \[ \] File browser with nested folders

  - \[ \] Custom sleep screen (wallpaper/cycle/random/clear)

  - \[ \] WiFi book upload

  - \[ \] WiFi OTA updates

  - \[ \] KOReader sync

  - \[ \] Configurable font, layout, display options

  - \[ \] Screen rotation (4 orientations, global)

  - \[ \] Status bar customization

  - \[ \] Front button remapping with orientation defaults

  - \[ \] Anti-aliasing

  - \[ \] Footnotes

  - \[ \] Multi-language UI (language packs)

  - \[ \] Hebrew/RTL support with font fallback

  - \[ \] Markdown reader

  - \[ \] OPDS browser

  - \[ \] Calibre wireless

  - \[ \] Plugin manager with activation/priority

- \[ \] Update `docs/architecture.md` with final measurements

- \[ \] **Update plan**: Final review.
