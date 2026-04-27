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

## Phase 7: Font Fallback & Language Packs ✅

- [x] C-side: per-slot font fallback in `font_manager.c` — `set_fallback`, `clear_fallback`, `get_fallback`
- [x] C-side: fallback-aware glyph lookup in `font_render.c` — `get_glyph_with_fallback()`, `font_render_draw_text_fb()`, `font_render_get_advance_fb()`
- [x] C-side: `font.setFallback(primaryId, fallbackId)` and `font.clearFallback(fontId)` Lua bindings
- [x] C-side: `display.drawText`, `drawTextInverted`, `drawTextPhysical`, `getTextWidth` all use fallback-aware rendering
- [x] Created `plugins/lib/json.lua` — recursive-descent JSON parser (replaces flat regex parser in settings.lua)
- [x] Created `plugins/lib/lang.lua` — language pack discovery, loading, `tr()` translation with English fallback
- [x] Updated `plugins/lib/fonts.lua` — fallback font slot, `detect_scripts()`, `load_fallback_for_script()`, auto-load on init
- [x] Updated `plugins/lib/settings.lua` — uses lib/json for parsing
- [x] Language pack system — drop-in `/languages/{code}/` folders with `lang.json` + optional `fonts/`
- [x] Ship default language packs: English + Hebrew (in `lang_packs/` directory)
- [x] Settings: dynamic language selection from discovered packs
- [x] `lang.tr(key)` — translated UI string, falls back to English, then raw key
- [x] Generated Hebrew language pack fonts (NotoSansHebrew .cfont sizes 12, 14, 16, 18)
- [x] Updated `plugins/home.lua` — loads language pack, uses `lang.tr()` for menu labels
- [x] Updated `plugins/settings.lua` — language menu item, applies language + reloads fonts on change
- [ ] Test: open Hebrew EPUB with NotoSans selected → auto-falls back to Hebrew font
- [x] Build passes: Flash 8.6% (564KB), RAM 22.9% (75KB)
- [x] **Document**: Written `docs/language-packs.md`, updated `docs/lua-api.md`, `docs/plugin-guide.md`, `docs/architecture.md`
- [x] **Update plan**: Completed

## Phase 8: Sleep Screen & Error Recovery ✅

- [x] Boot font: NotoSans-12 loaded at C startup (slot 0), before Lua, for crash/sleep text
- [x] Plugin crash screen: shows plugin name + error message, waits for button, restarts home
- [x] BMP decoder: streaming row-by-row from SD, Bayer 4x4 dithering, supports 1/8/24-bit uncompressed
- [x] Sleep screen system: 5 modes (blank, single wallpaper, cycle, random, clear/stay-on-page)
  - `/wallpapers/` folder on SD — user drops BMP images
  - Cycle index persists to `/crosslua_sleep_idx.txt` across deep sleep
  - Clear mode overlays "SLEEP" + battery % on current page content
- [x] Power button: short press (0.5-2s) = sleep, long press (>2s) = SD reload + restart
- [x] SD card reload: `hal_storage_reinit()` + `plugin_manager_reinit()`, exposed as `system.reload()`
- [x] USB connection detection: auto-suppresses sleep when USB connected
- [x] Lua API: `system.setSleepMode()`, `system.setSleepWallpaper()`, `system.reload()`
- [x] Settings plugin: sleep mode menu item (blank/wallpaper/cycle/random/stay-on-page)
- [x] Home plugin: pushes sleep settings to C on boot
- [ ] Plugin manager UI in settings (deferred to later phase)
- [ ] Test: sleep screen renders, crash recovery works, SD reload works
- [x] Build passes: Flash 8.7% (568KB), RAM 23.6% (77KB)
- [x] **Document**: Updated `docs/lua-api.md`, `docs/plugin-guide.md`, `docs/architecture.md`
- [x] **Update plan**: Completed

## Phase 9: Reader Plugins

- [x] Implement `text.*` C API (indexPages, getPageLines) — streaming word-wrap and pagination in C
  - 8KB static chunk buffer, 1KB line buffer — zero stack/heap pressure
  - Word wrapping uses font_render measurement directly (glyph cache stays warm)
  - Single file handle kept open during full index scan
  - Returns page byte offsets as Lua table
- [x] Write shared reader library (`/plugins/lib/reader_utils.lua`)
  - Page turn handling (forward, back, long-press chapter skip)
  - Progress save/load via lib/progress.lua
  - Status bar integration via lib/status_bar.lua
  - Orientation-aware viewport calculation
  - Screen refresh management (full refresh every N pages, configurable)
  - Cache directory convention: `/cache/{plugin_id}/`
- [x] Write shared text layout library (`/plugins/lib/text_layout.lua`)
  - UTF-8 safe chunk splitting, RTL detection, word wrap (for MD inline rendering)
- [x] Write TXT reader plugin (`/plugins/txt_reader.lua`)
  - C-side streaming indexing via text.indexPages() — fast, zero Lua heap
  - C-side page rendering via text.getPageLines()
  - Page offset caching to `/cache/txt_reader/`
  - RTL detection and right-alignment
  - Progress persistence via lib/progress.lua
  - Font fallback via lib/fonts.lua
  - [ ] Fix: page count too low (viewport calculation issue)
  - [ ] Fix: fast refresh not updating screen (half refresh workaround)
- [x] Write MD reader plugin (`/plugins/md_reader.lua`)
  - Markdown parsing: headers, bold/italic, lists, blockquotes, code blocks, horizontal rules, links
  - Bold as double-strike, italic as underline, code with rect background
  - Nested list indentation
  - Page indexing with code block state tracking via character-count estimation
  - [ ] Test on device

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
