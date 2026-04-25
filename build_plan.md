# Build Plan: Lua Plugin Runtime (C Runtime + C++ SDK Bridge)

## Phase 1: Bare Metal Foundation
- [ ] Create new PlatformIO project targeting ESP32-C3
- [ ] Write C++ SDK bridge files (~50 lines total, `extern "C"` wrappers):
  - `bridge_display.cpp` — wraps `EInkDisplay` class methods
  - `bridge_input.cpp` — wraps `InputManager` class methods
  - `bridge_storage.cpp` — wraps `SDCardManager` class methods
  - `bridge_battery.cpp` — wraps `BatteryMonitor` class methods
- [ ] Write pure C HAL layer (calls bridge functions):
  - `hal_display.c/h` — display API: init, clear, write buffer, refresh modes
  - `hal_gpio.c/h` — input API: button init, poll, ISR handling, debouncing
  - `hal_storage.c/h` — storage API: open, read, write, list, mkdir
  - `hal_power.c/h` — power API: battery level, sleep, wake, watchdog
  - `hal_system.c/h` — system API: boot, restart, heap stats, uptime
- [ ] Implement framebuffer renderer in pure C:
  - `renderer.c/h` — drawPixel, drawLine, drawRect, fillRect, clearScreen
  - Port the pixel/line drawing from GfxRenderer (already pure math, easy port)
- [ ] Verify: blank screen renders, buttons respond, SD card mounts
- [ ] Measure flash/RAM baseline
- [ ] **Document**: Write `docs/hal-api.md` — HAL function reference, bridge pattern explanation. Update `docs/architecture.md` with actual measurements.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 2: Font Renderer
- [ ] Design `.cfont` binary format (header, intervals, glyphs, compressed bitmaps, kerning)
- [ ] Build `cfont-convert` tool (Python):
  - Reuse logic from existing `fontconvert.py`
  - TTF → .cfont with --2bit, --compress, --size, --style
  - Convert all existing fonts (Bookerly, NotoSans, NotoSansHebrew, OpenDyslexic, Ubuntu)
- [ ] Implement font loader in C:
  - `font_loader.c/h` — read .cfont from SD, parse into runtime structures
  - `font_cache.c/h` — LRU decompression cache (port from existing FontDecompressor)
  - `font_render.c/h` — glyph lookup, drawText, getTextWidth, getTextAdvanceX
  - Combining mark support (nikkud, diacriticals)
  - BiDi/RTL reordering (port ScriptDetector logic)
- [ ] Test: render Latin and Hebrew text from SD-loaded fonts
- [ ] Verify: identical glyph rendering to current firmware
- [ ] **Document**: Write `docs/cfont-format.md` — binary format spec. Write `tools/cfont-convert/README.md` — usage guide. Update `docs/architecture.md` with font system details.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 3: Lua Interpreter Integration
- [ ] Add Lua 5.4 source to project (`lib/lua/`, MIT license, ~25 .c files)
- [ ] Configure Lua for ESP32-C3:
  - Disable file I/O module (use our storage API instead)
  - Disable os module (use our system API instead)
  - Set appropriate stack/memory limits
- [ ] Build and measure flash/RAM cost (~100KB flash, ~50KB RAM expected)
- [ ] Implement C → Lua binding helpers:
  - `lua_api.c/h` — register C functions, type conversion helpers, error handling
- [ ] Implement `display.*` Lua bindings (clear, drawText, drawLine, refresh, dimensions)
- [ ] Implement `input.*` Lua bindings (waitButton, poll, isPressed, constants)
- [ ] Implement `storage.*` Lua bindings (read, write, exists, list, mkdir, remove, fileSize)
- [ ] Implement `system.*` Lua bindings (freeHeap, battery, millis, delay, log, version)
- [ ] Implement `font.*` Lua bindings (load, unload, list)
- [ ] Test: "Hello World" plugin from SD card — renders text, responds to buttons
- [ ] **Document**: Write `docs/lua-api.md` — full Lua API reference (display, input, storage, system, font). Update `docs/plugin-guide.md` with working examples tested on device.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 4: Plugin Manager
- [ ] Implement plugin discovery — scan `/plugins/` on SD for `.lua` files
- [ ] Implement plugin manifest parsing (plugin table with name, id, type, fileExtensions)
- [ ] Implement plugin lifecycle management (onEnter, loop, onExit)
- [ ] Implement plugin switching — unload current, load next, manage Lua state
- [ ] Implement error handling — pcall wrappers, error display, recovery to menu
- [ ] Implement boot sequence:
  1. Hardware init
  2. Mount SD, load fonts
  3. Init Lua, register APIs
  4. Scan plugins, build menu
  5. Restore last state or show home
- [ ] Test: switch between multiple plugins without memory leaks
- [ ] **Document**: Write `docs/plugin-lifecycle.md` — discovery, manifest format, switching behavior, error handling, memory model. Update `docs/plugin-guide.md` with plugin switching examples.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 5: Core UI Plugins
- [ ] Implement `json.*` Lua API (parse, encode) — native C for performance
- [ ] Implement `i18n.*` Lua API — load translation files from SD, get/set language
- [ ] Write translation loader — YAML or JSON translation files on SD (`/i18n/en.json`, `/i18n/he.json`)
- [ ] Write home screen plugin (`/plugins/home.lua`)
  - Menu: Continue Reading, Browse Files, Network, Settings
  - Recent books list with last-read tracking
  - Sleep screen (dark, light, cover, custom image)
  - Boot screen / splash
- [ ] Write file browser plugin (`/plugins/file_browser.lua`)
  - Directory navigation, file filtering by extension
  - Dispatch to reader plugin based on file type
  - Show/hide hidden files option
  - Sort by name/date
- [ ] Write settings plugin (`/plugins/settings.lua`)
  - Display: sleep screen, battery display, anti-aliasing, sunlight fix, UI theme
  - Reader: font family, font size, line spacing, screen margin, paragraph alignment, hyphenation, images, embedded style
  - Controls: reading orientation, side button layout, front button remap, short power button action, long-press chapter skip
  - System: language, time to sleep, refresh frequency, clear cache, check updates, KOReader sync config
  - Settings persistence to `/settings.json` on SD
- [ ] Write WiFi manager plugin (`/plugins/wifi_manager.lua`)
  - Network scan, connect with password, save/forget networks
  - Hotspot mode (create WiFi network)
  - Connection status display
- [ ] Write on-screen keyboard plugin (`/plugins/keyboard.lua`)
  - Text entry for WiFi passwords, URLs, search queries
  - Used as a sub-plugin by other plugins
- [ ] Write status bar renderer (shared Lua module `/plugins/lib/status_bar.lua`)
  - Book/chapter progress, page count, battery, title
  - Configurable: show/hide elements, progress bar style
- [ ] Write button remap plugin (`/plugins/button_remap.lua`)
  - Interactive front button reassignment
- [ ] Test: full device operation — boot, browse, settings, WiFi connect
- [ ] **Document**: Write `docs/settings-schema.md` — all settings keys, types, defaults. Write `docs/i18n-guide.md` — how to add a new language translation. Update `docs/plugin-guide.md` with keyboard and status bar integration examples.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 6: Reader Plugins
- [ ] Implement `zip.*` C API (open, list, read, close) — wraps miniz/uzlib
- [ ] Implement `xml.*` C API (parse, find, attr, text) — wraps expat or simple SAX
- [ ] Write shared reader library (`/plugins/lib/reader_utils.lua`)
  - Page turn handling (forward, back, long-press chapter skip)
  - Progress save/load to SD
  - Status bar integration
  - Orientation-aware viewport calculation
  - Auto-sleep on idle
- [ ] Write TXT reader plugin (`/plugins/txt_reader.lua`)
  - Streaming file reading (8KB chunks, not full file in memory)
  - Word wrapping with viewport awareness
  - Page offset indexing and caching
  - RTL detection and right-alignment
  - Progress persistence
- [ ] Write MD reader plugin (`/plugins/md_reader.lua`)
  - Markdown parsing: headers, bold/italic, lists, blockquotes, code blocks, horizontal rules, links, images, task lists
  - Styled span rendering with BiDi support
  - Nested list indentation
  - Page indexing with code block state tracking
- [ ] Write EPUB reader plugin (`/plugins/epub_reader.lua`)
  - ZIP extraction via `zip.*` API
  - HTML/XML parsing via `xml.*` API
  - CSS parsing: fonts, alignment, margins, text-indent, display
  - Word layout and line breaking (may need native C helper for performance)
  - Justified text with proportional word spacing
  - Chapter navigation and table of contents
  - Page caching to SD for fast re-open
  - Footnote support with navigation
  - Image display (BMP, JPG via native helper)
  - RTL paragraph detection and right-alignment
  - Embedded style vs user style override
- [ ] Write chapter selection sub-plugin (`/plugins/chapter_select.lua`)
  - List chapters from EPUB TOC
  - Jump to selected chapter
- [ ] Write go-to-percent sub-plugin (`/plugins/goto_percent.lua`)
  - Navigate to percentage position in book
- [ ] Test: all three readers match current CrossPoint quality
- [ ] Test: Hebrew EPUB, TXT, MD all render correctly
- [ ] Test: progress saves and restores across power cycles
- [ ] **Document**: Write `docs/reader-plugin-guide.md` — how to write a reader plugin (file format handling, page indexing, progress, status bar). Write `docs/lua-api.md` additions for `zip.*` and `xml.*` APIs. Document cache format for each reader.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 7: Network Plugins
- [ ] Implement `wifi.*` Lua bindings (connect, disconnect, isConnected, getIP, fetch, fetchJson, post, download)
- [ ] Write web server plugin (`/plugins/web_server.lua`)
  - Serve file upload page at device IP
  - Drag-and-drop book upload from any browser
  - Device info page (battery, storage, firmware version)
  - Extensible: other plugins can register routes
- [ ] Write OPDS browser plugin (`/plugins/opds_browser.lua`)
  - Configure server URL
  - Browse catalog, download books
  - Pagination support
- [ ] Write KOReader sync plugin (`/plugins/kosync.lua`)
  - Username/password auth
  - Push/pull reading progress
  - Document matching (filename or binary hash)
- [ ] Write OTA updater plugin (`/plugins/ota_updater.lua`)
  - Check for firmware updates
  - Download and flash via OTA partition
  - Version comparison
- [ ] Write Calibre connect plugin (`/plugins/calibre.lua`)
  - Wireless device connection for Calibre desktop
  - Receive books via Calibre's send-to-device
- [ ] Write Sefaria browser plugin (`/plugins/sefaria.lua`)
  - Browse text hierarchy (Tanakh, Talmud, Mishnah, etc.)
  - Download chapters/books to SD
  - Daily readings (Parashat HaShavua, Daf Yomi, etc.)
  - Search
- [ ] Test: all network features work on device
- [ ] **Document**: Write `docs/lua-api.md` additions for `wifi.*` API. Write `docs/web-server-guide.md` — how plugins register routes. Document each network plugin's configuration and usage.
- [ ] **Update plan**: Check off completed tasks, note any deferred items or scope changes.

## Phase 8: Polish & Release
- [ ] Plugin error screen (friendly message, option to disable plugin)
- [ ] Watchdog timer for runaway Lua scripts
- [ ] Crash handler — save crash info, display on next boot
- [ ] Default SD card image with all core plugins + fonts + translations
- [ ] Plugin developer documentation (full API reference with examples)
- [ ] Example plugin template (`/plugins/template.lua`)
- [ ] Write `docs/cfont-format.md` — .cfont binary format specification
- [ ] Performance profiling: boot time, page turn latency, memory usage
- [ ] Final flash size verification (~500KB target)
- [ ] Final RAM usage verification (<300KB during operation)
- [ ] LTR + RTL + Hebrew regression testing across all readers
- [ ] All 4 orientations testing
- [ ] Feature parity checklist vs CrossPoint:
  - [ ] EPUB 2 and EPUB 3 rendering
  - [ ] Image support in EPUB
  - [ ] Reading progress persistence
  - [ ] File browser with nested folders
  - [ ] Custom sleep screen (dark, light, cover, custom)
  - [ ] WiFi book upload
  - [ ] WiFi OTA updates
  - [ ] KOReader sync
  - [ ] Configurable font, layout, display options
  - [ ] Screen rotation (4 orientations)
  - [ ] Status bar customization
  - [ ] Front button remapping
  - [ ] Anti-aliasing
  - [ ] Footnotes
  - [ ] Multi-language UI (23 languages)
  - [ ] Hebrew/RTL support
  - [ ] Markdown reader
  - [ ] OPDS browser
  - [ ] Calibre wireless
- [ ] Git init, create GitHub repo, initial commit
