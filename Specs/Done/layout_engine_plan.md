# Layout Engine Build Plan

## Overview

The layout engine is a foundational C-side system that owns all display area calculations for CrossLua Reader. It replaces the current scattered viewport logic (spread across `reader_utils.lua`, `ui.lua`, `renderer.c`, and `api_display.c`) with a single authoritative source of truth.

See `docs/layout-engine-spec.md` for the full specification.

## Phase 1: Core Engine ✅

Build the layout engine C module and Lua bindings. No integration with existing code yet — just the engine itself, testable in isolation.

- \[x\] Create `lib/layout/layout_engine.h` — public API with `layout_state_t` struct (fields ordered largest-to-smallest per coding standards), all setter/getter declarations

- \[x\] Create `lib/layout/layout_engine.c` — singleton state, `layout_init()` reads physical display dimensions from HAL, `layout_recalculate()` computes all derived values

  - Setters: `layout_set_header_height()`, `layout_set_footer_height()`, `layout_set_margins()`, `layout_set_line_spacing()`, `layout_set_line_height()`, `layout_set_font()`, `layout_set_orientation()`

  - Getters: `layout_lines_per_page()`, `layout_line_height()`, `layout_header_area()`, `layout_body_area()`, `layout_body_area_raw()`, `layout_footer_area()`

  - All setters call `layout_recalculate()` internally

  - Use `int16_t` for pixel values (max 800 fits in 16 bits)

  - Validate inputs: clamp negatives to 0, clamp heights to display bounds

  - Zero heap allocation — single static struct (\~74 bytes)

- \[x\] Create `lib/lua_api/api_layout.h` — declare `api_layout_register()`

- \[x\] Create `lib/lua_api/api_layout.c` — Lua bindings for all setters/getters, register as `layout.*` global

- \[x\] Register in `api_register.c` — add `#include "api_layout.h"` and `api_layout_register(L)` call

- \[x\] Call `layout_init()` in `main.cpp` setup after `renderer_init()`

- \[x\] Build passes: RAM 23.5% (77KB), Flash 8.8% (579KB)

- \[x\] Test: Lua plugin calls `layout.setHeaderHeight(84)`, `layout.setFooterHeight(40)`, `layout.setMargin(10)`, `layout.setFont(0)` → lpp=22, lh=29, body=13,97,448,650 ✓

- \[x\] **Document**: Updated `docs/lua-api.md` with full `layout.*` API reference

- \[x\] **Update plan**: Completed

## Phase 2: Text API Integration ✅

Wire the `text.*` C functions to query the layout engine directly instead of receiving viewport dimensions as parameters.

- \[x\] Modify `text.indexPages()` — remove `viewportWidth` and `linesPerPage` parameters, query `layout_body_area()` and `layout_lines_per_page()` internally

- \[x\] Modify `text.getPageLines()` — remove `viewportWidth` and `linesPerPage` parameters, query layout engine

- \[x\] Modify `text.indexMarkdownPages()` — remove viewport parameters, query layout engine

- \[x\] Modify `text.renderMarkdownPage()` — remove viewport parameters, query layout engine

- \[x\] Modify `text.wrapString()` — remove `viewportWidth` parameter, query `layout_body_area()` for width

- \[x\] Update `txt_reader.lua` — remove viewport parameter passing to `text.*` calls, call `layout.setFont()` and `layout.setFooterHeight()` in `onEnter()`

- \[x\] Update `md_reader.lua` — same changes as txt_reader

- \[x\] Build passes

- \[x\] Test: TXT reader opens file, indexes correctly using layout engine values, pages match screen — portrait ✓

- \[x\] Test: MD reader opens file, indexes and renders correctly — portrait ✓

- \[x\] Known issue: landscape orientation layout calculations need work (deferred)

- \[x\] **Document**: Updated `docs/lua-api.md` with new `text.*` function signatures (no viewport params)

- \[x\] **Update plan**: Completed

## Phase 3: Reader Plugin Migration

Replace `reader_utils.get_viewport()` with layout engine calls in all reader infrastructure.

- \[x\] Update `reader_utils.lua` — removed `get_viewport()` function and `theme` require

- \[x\] Update `status_bar.lua` — draws within `layout.footerArea()` bounds instead of hardcoded `display.width()/height()`

- \[x\] Update `txt_reader.lua` — uses `layout.bodyArea()` for text positioning, `layout.linesPerPage()` for page control

- \[x\] Update `md_reader.lua` — same migration

- \[x\] Remove `display.contentArea()` usage from reader plugins (was only in `get_viewport()`, now deleted)

- \[x\] Build passes: RAM 23.5%, Flash 8.8%

- \[x\] Test: TXT reader renders text within body area, status bar in footer area, no overlap

- \[x\] Test: MD reader same verification

- \[x\] Test: All 4 orientations — layout recalculates correctly (fixed: `display.setOrientation()` now syncs layout engine)

- \[x\] **Document**: Updated `docs/lua-api.md` with reader/app layout examples showing `footerArea()` usage

- \[x\] **Update plan**: Completed

## Phase 4: UI Plugin Migration

Migrate home, settings, and file browser to use the layout engine.

- [x] Update `ui.lua` — `draw_header()` uses `layout.headerArea()`, `draw_menu()`/`draw_list()` use `layout.bodyArea()`. Removed `content_area()`/`content_max_y()` dead helpers. Button hints stay as physical coords (correct).
- [x] Update `home.lua` — calls `layout.setHeaderHeight(t.header_height)`, `layout.setFooterHeight(t.button_hints_height)` in `onEnter()`, `render()` uses `layout.bodyArea()` for menu start
- [x] Update `settings.lua` — same layout setup, `get_max_visible()` uses `layout.bodyArea()`, theme change reconfigures layout
- [x] Update `file_browser.lua` — same layout setup, max_visible uses `layout.bodyArea()`
- [x] Update `theme.lua` — added comments noting header_height/button_hints_height feed into layout engine
- [x] Build passes: RAM 23.5%, Flash 8.8%
- [x] Test: Home screen renders correctly with header, menu in body, button hints in footer
- [x] Test: Settings scrolling works within body bounds
- [x] Test: File browser listing stays within body bounds
- [x] Test: Switching between reader and app plugins — layout reconfigures correctly per plugin
- [x] **Document**: Updated theme.lua with layout engine integration notes
- [x] **Update plan**: Completed

## Phase 5: Dead Code Removal — C Side

Remove old C-side viewport code incrementally. Build and test after each removal.

- [x] Remove `BUTTON_BAR_HEIGHT` constant from `renderer.c`
- [x] Remove `renderer_get_content_area()` from `renderer.c` and declaration from `renderer.h`
- [x] Remove `l_display_content_area()` binding from `api_display.c` and function table
- [x] Physical coordinate Lua bindings (`drawLinePhysical`, `drawRectPhysical`, `drawTextPhysical`) — **kept**, still used by `draw_button_hints()` for orientation-independent button labels
- [x] Build passes
- [x] **Document**: Updated `docs/lua-api.md` — removed `display.contentArea()` reference
- [x] **Update plan**: Completed

## Phase 6: Dead Code Removal — Lua Libraries

Remove old Lua-side viewport calculations. Copy updated files to SD and test after each change.

- [x] Remove `M.get_viewport()` from `reader_utils.lua` — done in Phase 3
- [x] `get_max_visible()` in `settings.lua` — rewritten to use `layout.bodyArea()` in Phase 4 (still needed for scrolling)
- [x] Remove `M.content_area()` and `M.content_max_y()` from `ui.lua` — done in Phase 4
- [x] Hardcoded `phys_w`/`phys_h` in `draw_button_hints()` — **kept**, correct for physical button positioning
- [x] Theme height constants (`header_height`, `button_hints_height`) — **kept**, plugins pass them to layout engine in `onEnter()`
- [x] **Update plan**: Completed

## Phase 7: Final Cleanup & Documentation

Remove any remaining dead code, finalize all documentation.

- \[ \] Audit all plugins one final time for manual coordinate calculations — grep for `display.contentArea`, `display.width()`, `display.height()`, hardcoded pixel values

- \[ \] Remove any remaining `display.contentArea()` calls from plugins

- \[ \] Remove `deferred refresh` code from `ui.lua` (request_refresh/check_refresh) if unused after migration

- \[ \] Remove `text_layout.lua` functions if fully replaced by C-side `text.wrapString()` — check all callers

- \[ \] Clean up magic number indentation in `md_reader.lua` (16px code indent, 20px list indent) — parameterize or move to layout engine constants

- \[ \] Final build verification: `pio run` clean, zero warnings

- \[ \] Test: Full regression — home, settings, file browser, TXT reader, MD reader

- \[ \] Test: All 4 orientations

- \[ \] Test: Language switch (Hebrew RTL) — layout bounds respected

- \[ \] Test: Font size change in settings — `lines_per_page` recalculates, page cache invalidates

- \[ \] Test: Plugin switching — layout reconfigures correctly per plugin type

- \[ \] **Document**: Write `docs/layout-engine.md` (user-facing guide, replaces spec). Update `docs/architecture.md` with layout engine section. Update `docs/plugin-guide.md` with complete `layout.*` reference and examples.

- \[ \] **Update plan**: Check off completed tasks

## Dead Code Audit Summary

Code identified for removal/deprecation across all phases:

### Remove Entirely (\~80 lines)

| File             | Item                                | Lines |
| ---------------- | ----------------------------------- | ----- |
| renderer.c       | BUTTON_BAR_HEIGHT                   | 1     |
| renderer.c       | renderer_get_content_area()         | 26    |
| renderer.h       | Content area declaration            | 8     |
| api_display.c    | l_display_content_area()            | 10    |
| reader_utils.lua | M.get_viewport()                    | 24    |
| settings.lua     | get_max_visible()                   | 5     |
| ui.lua           | M.content_area(), M.content_max_y() | 7     |
| ui.lua           | Hardcoded phys_w/phys_h             | 3     |

### Deprecate (keep but mark, \~20 references)

| File          | Item                                         |
| ------------- | -------------------------------------------- |
| api_display.c | Physical drawing Lua bindings (3 functions)  |
| theme.lua     | header_height, button_hints_height constants |

### Simplify (\~200 lines of manual math removed)

| File             | What gets simpler                                            |
| ---------------- | ------------------------------------------------------------ |
| ui.lua           | draw_header(), draw_menu(), draw_list(), draw_button_hints() |
| status_bar.lua   | Y position calculation                                       |
| home.lua         | Menu Y positioning                                           |
| settings.lua     | List viewport calculation                                    |
| file_browser.lua | Max visible calculation                                      |
| txt_reader.lua   | Viewport setup                                               |
| md_reader.lua    | Viewport setup + indent values                               |

## Files Summary

### New Files

| File                       | Purpose                               |
| -------------------------- | ------------------------------------- |
| lib/layout/layout_engine.c | Core layout engine implementation     |
| lib/layout/layout_engine.h | Public API — struct, setters, getters |
| lib/lua_api/api_layout.c   | Lua bindings for layout.*             |
| lib/lua_api/api_layout.h   | Lua binding declarations              |

### Modified Files

| File                                | Phase | Changes                                     |
| ----------------------------------- | ----- | ------------------------------------------- |
| lib/lua_api/api_register.c          | 1     | Register layout.* module                    |
| lib/lua_api/api_text.c              | 2     | Remove viewport params, query layout engine |
| src/main.cpp                        | 1     | Call layout_init()                          |
| sdcard/plugins/lib/reader_utils.lua | 3     | Remove get_viewport(), use layout engine    |
| sdcard/plugins/lib/status_bar.lua   | 3     | Use layout.footerArea()                     |
| sdcard/plugins/lib/ui.lua           | 4     | Use layout regions for header/body/footer   |
| sdcard/plugins/lib/theme.lua        | 4     | Remove heights replaced by layout engine    |
| sdcard/plugins/txt_reader.lua       | 2-3   | Use layout engine                           |
| sdcard/plugins/md_reader.lua        | 2-3   | Use layout engine                           |
| sdcard/plugins/home.lua             | 4     | Use layout engine                           |
| sdcard/plugins/settings.lua         | 4     | Use layout engine                           |
| sdcard/plugins/file_browser.lua     | 4     | Use layout engine                           |
| lib/renderer/renderer.c             | 5     | Remove renderer_get_content_area()          |
| lib/lua_api/api_display.c           | 5     | Deprecate display.contentArea()             |

## Coding Standards Checklist

Per `CrossLuaReader/coding-best-practices.md` and `CrossLuaReader/CLAUDE.md`:

- \[ \] Pure C, no C++ (except SDK bridges)

- \[ \] Snake_case functions with `layout_` prefix

- \[ \] File headers with `@status`/`@issues`/`@todo`

- \[ \] `uint16_t`/`int16_t` for pixel values, not bare `int`

- \[ \] Struct fields ordered largest-to-smallest (minimize padding)

- \[ \] No VLAs — fixed-size static struct only

- \[ \] No heap allocation — zero malloc

- \[ \] All setters validate inputs (clamp to sane ranges)

- \[ \] Functions under \~100 lines

- \[ \] Descriptive names: `layout_set_header_height()` not `layout_hdr()`

- \[ \] Inline comments explain WHY, not WHAT
