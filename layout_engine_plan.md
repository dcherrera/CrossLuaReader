# Layout Engine Build Plan

## Overview

The layout engine is a foundational C-side system that owns all display area calculations for CrossLua Reader. It replaces the current scattered viewport logic (spread across `reader_utils.lua`, `ui.lua`, `renderer.c`, and `api_display.c`) with a single authoritative source of truth.

See `docs/layout-engine-spec.md` for the full specification.

## Phase 1: Core Engine

Build the layout engine C module and Lua bindings. No integration with existing code yet — just the engine itself, testable in isolation.

- [ ] Create `lib/layout/layout_engine.h` — public API with `layout_state_t` struct (fields ordered largest-to-smallest per coding standards), all setter/getter declarations
- [ ] Create `lib/layout/layout_engine.c` — singleton state, `layout_init()` reads physical display dimensions from HAL, `layout_recalculate()` computes all derived values
  - Setters: `layout_set_header_height()`, `layout_set_footer_height()`, `layout_set_margins()`, `layout_set_line_spacing()`, `layout_set_line_height()`, `layout_set_font()`, `layout_set_orientation()`
  - Getters: `layout_lines_per_page()`, `layout_line_height()`, `layout_header_area()`, `layout_body_area()`, `layout_body_area_raw()`, `layout_footer_area()`
  - All setters call `layout_recalculate()` internally
  - Use `uint16_t`/`int16_t` for pixel values (max 800px fits in 16 bits)
  - Validate inputs: clamp negatives to 0, clamp heights to display bounds
  - Zero heap allocation — single static struct
- [ ] Create `lib/lua_api/api_layout.h` — declare `api_layout_register()`
- [ ] Create `lib/lua_api/api_layout.c` — Lua bindings for all setters/getters, register as `layout.*` global
- [ ] Register in `api_register.c` — add `#include "api_layout.h"` and `api_layout_register(L)` call
- [ ] Call `layout_init()` in `main.cpp` setup after `renderer_init()`
- [ ] Build passes: `pio run` compiles with no errors
- [ ] Test: Lua plugin can call `layout.setHeaderHeight(84)`, `layout.setFooterHeight(40)`, `layout.setMargin(10)`, `layout.linesPerPage()` and get sane values
- [ ] **Document**: Update `docs/lua-api.md` with `layout.*` API reference
- [ ] **Update plan**: Check off completed tasks

## Phase 2: Text API Integration

Wire the `text.*` C functions to query the layout engine directly instead of receiving viewport dimensions as parameters.

- [ ] Modify `text.indexPages()` — remove `viewportWidth` and `linesPerPage` parameters, query `layout_body_area()` and `layout_lines_per_page()` internally
- [ ] Modify `text.getPageLines()` — remove `viewportWidth` and `linesPerPage` parameters, query layout engine
- [ ] Modify `text.indexMarkdownPages()` — remove viewport parameters, query layout engine
- [ ] Modify `text.renderMarkdownPage()` — remove viewport parameters, query layout engine
- [ ] Modify `text.wrapString()` — remove `viewportWidth` parameter, query `layout_body_area()` for width
- [ ] Update `txt_reader.lua` — remove viewport parameter passing to `text.*` calls, call `layout.setFont()` and `layout.setFooterHeight()` in `onEnter()`
- [ ] Update `md_reader.lua` — same changes as txt_reader
- [ ] Build passes
- [ ] Test: TXT reader opens file, indexes correctly using layout engine values, pages match screen
- [ ] Test: MD reader opens file, indexes and renders correctly
- [ ] **Document**: Update `docs/lua-api.md` with updated `text.*` function signatures
- [ ] **Update plan**: Check off completed tasks

## Phase 3: Reader Plugin Migration

Replace `reader_utils.get_viewport()` with layout engine calls in all reader infrastructure.

- [ ] Update `reader_utils.lua` — remove `get_viewport()` function, replace with layout engine queries
- [ ] Update `reader_utils.draw_page_chrome()` — draw status bar using `layout.footerArea()` bounds
- [ ] Update `txt_reader.lua` — use `layout.bodyArea()` for text positioning, `layout.linesPerPage()` for page control
- [ ] Update `md_reader.lua` — same migration
- [ ] Update `status_bar.lua` — draw within `layout.footerArea()` bounds instead of hardcoded bottom position
- [ ] Remove `display.contentArea()` usage from reader plugins
- [ ] Build passes
- [ ] Test: TXT reader renders text within body area, status bar in footer area, no overlap
- [ ] Test: MD reader same verification
- [ ] Test: All 4 orientations — layout recalculates correctly
- [ ] **Document**: Update `docs/plugin-guide.md` with `layout.*` usage for reader plugins
- [ ] **Update plan**: Check off completed tasks

## Phase 4: UI Plugin Migration

Migrate home, settings, and file browser to use the layout engine.

- [ ] Update `ui.lua` — `draw_header()` uses `layout.headerArea()`, `draw_button_hints()` uses `layout.footerArea()`, `draw_menu()`/`draw_list()` use `layout.bodyArea()`
- [ ] Update `home.lua` — call `layout.setHeaderHeight(84)`, `layout.setFooterHeight(40)` in `onEnter()`, remove manual coordinate calculations
- [ ] Update `settings.lua` — same layout setup, scrollable list uses `layout.bodyArea()` bounds
- [ ] Update `file_browser.lua` — same layout setup, file list uses `layout.bodyArea()` bounds
- [ ] Update `theme.lua` — header_height, button_hints_height values feed into layout engine defaults (or remove them if layout engine replaces their purpose)
- [ ] Build passes
- [ ] Test: Home screen renders correctly with header, menu in body, button hints in footer
- [ ] Test: Settings scrolling works within body bounds
- [ ] Test: File browser listing stays within body bounds
- [ ] Test: Switching between reader and app plugins — layout reconfigures correctly per plugin
- [ ] **Document**: Update `docs/plugin-guide.md` with `layout.*` usage for UI plugins
- [ ] **Update plan**: Check off completed tasks

## Phase 5: Dead Code Removal — C Side

Remove old C-side viewport code incrementally. Build and test after each removal.

- [ ] Remove `BUTTON_BAR_HEIGHT` constant from `renderer.c` (line 298)
  - Build + test
- [ ] Remove `renderer_get_content_area()` from `renderer.c` (lines 347-373) and its declaration from `renderer.h` (line 148)
  - Build + test
- [ ] Remove `l_display_content_area()` binding from `api_display.c` (lines 215-224)
  - Remove from `api_display_register()` function table
  - Build + test: verify no plugin crashes (all should use `layout.*` by now)
- [ ] Deprecate physical coordinate Lua bindings in `api_display.c`:
  - `l_display_draw_line_physical()` (lines 178-186)
  - `l_display_draw_rect_physical()` (lines 188-196)
  - `l_display_draw_text_physical()` (lines 198-213)
  - Keep functions but add LOG_DBG deprecation warning. Remove from public API docs.
  - Build + test
- [ ] **Document**: Note removals in changelog
- [ ] **Update plan**: Check off completed tasks

## Phase 6: Dead Code Removal — Lua Libraries

Remove old Lua-side viewport calculations. Copy updated files to SD and test after each change.

- [ ] Remove `M.get_viewport()` from `reader_utils.lua` (lines 22-45) — all callers migrated in Phase 3
  - Test: TXT and MD readers still open and render
- [ ] Remove `get_max_visible()` from `settings.lua` (lines 172-176) — replaced by layout engine query
  - Test: settings menu scrolls correctly
- [ ] Remove `M.content_area()` wrapper from `ui.lua` (lines 79-81) — redirected in Phase 4
  - Test: all UI plugins render correctly
- [ ] Remove `M.content_max_y()` helper from `ui.lua` (lines 85-88) — replaced by layout body bounds
  - Test: menus don't overflow
- [ ] Remove hardcoded `phys_w = 480` and `phys_h = 800` from `ui.lua` (lines 170-172) — layout engine provides footer bounds
  - Test: button hints render in correct position all orientations
- [ ] Deprecate theme height constants in `theme.lua`:
  - `header_height` (lines 8, 21) — add comment "deprecated: use layout.setHeaderHeight()"
  - `menu_row_height` (lines 9, 22) — keep as styling hint only
  - `list_row_height` (lines 10, 23) — keep as styling hint only
  - `button_hints_height` (lines 11, 24) — add comment "deprecated: use layout.setFooterHeight()"
  - Test: all plugins render correctly
- [ ] **Document**: Update `docs/plugin-guide.md` noting deprecated functions
- [ ] **Update plan**: Check off completed tasks

## Phase 7: Final Cleanup & Documentation

Remove any remaining dead code, finalize all documentation.

- [ ] Audit all plugins one final time for manual coordinate calculations — grep for `display.contentArea`, `display.width()`, `display.height()`, hardcoded pixel values
- [ ] Remove any remaining `display.contentArea()` calls from plugins
- [ ] Remove `deferred refresh` code from `ui.lua` (request_refresh/check_refresh) if unused after migration
- [ ] Remove `text_layout.lua` functions if fully replaced by C-side `text.wrapString()` — check all callers
- [ ] Clean up magic number indentation in `md_reader.lua` (16px code indent, 20px list indent) — parameterize or move to layout engine constants
- [ ] Final build verification: `pio run` clean, zero warnings
- [ ] Test: Full regression — home, settings, file browser, TXT reader, MD reader
- [ ] Test: All 4 orientations
- [ ] Test: Language switch (Hebrew RTL) — layout bounds respected
- [ ] Test: Font size change in settings — `lines_per_page` recalculates, page cache invalidates
- [ ] Test: Plugin switching — layout reconfigures correctly per plugin type
- [ ] **Document**: Write `docs/layout-engine.md` (user-facing guide, replaces spec). Update `docs/architecture.md` with layout engine section. Update `docs/plugin-guide.md` with complete `layout.*` reference and examples.
- [ ] **Update plan**: Check off completed tasks

## Dead Code Audit Summary

Code identified for removal/deprecation across all phases:

### Remove Entirely (~80 lines)
| File | Item | Lines |
|------|------|-------|
| `renderer.c` | `BUTTON_BAR_HEIGHT` | 1 |
| `renderer.c` | `renderer_get_content_area()` | 26 |
| `renderer.h` | Content area declaration | 8 |
| `api_display.c` | `l_display_content_area()` | 10 |
| `reader_utils.lua` | `M.get_viewport()` | 24 |
| `settings.lua` | `get_max_visible()` | 5 |
| `ui.lua` | `M.content_area()`, `M.content_max_y()` | 7 |
| `ui.lua` | Hardcoded `phys_w`/`phys_h` | 3 |

### Deprecate (keep but mark, ~20 references)
| File | Item |
|------|------|
| `api_display.c` | Physical drawing Lua bindings (3 functions) |
| `theme.lua` | `header_height`, `button_hints_height` constants |

### Simplify (~200 lines of manual math removed)
| File | What gets simpler |
|------|-------------------|
| `ui.lua` | `draw_header()`, `draw_menu()`, `draw_list()`, `draw_button_hints()` |
| `status_bar.lua` | Y position calculation |
| `home.lua` | Menu Y positioning |
| `settings.lua` | List viewport calculation |
| `file_browser.lua` | Max visible calculation |
| `txt_reader.lua` | Viewport setup |
| `md_reader.lua` | Viewport setup + indent values |

## Files Summary

### New Files

| File | Purpose |
|------|---------|
| `lib/layout/layout_engine.c` | Core layout engine implementation |
| `lib/layout/layout_engine.h` | Public API — struct, setters, getters |
| `lib/lua_api/api_layout.c` | Lua bindings for `layout.*` |
| `lib/lua_api/api_layout.h` | Lua binding declarations |

### Modified Files

| File | Phase | Changes |
|------|-------|---------|
| `lib/lua_api/api_register.c` | 1 | Register `layout.*` module |
| `lib/lua_api/api_text.c` | 2 | Remove viewport params, query layout engine |
| `src/main.cpp` | 1 | Call `layout_init()` |
| `sdcard/plugins/lib/reader_utils.lua` | 3 | Remove `get_viewport()`, use layout engine |
| `sdcard/plugins/lib/status_bar.lua` | 3 | Use `layout.footerArea()` |
| `sdcard/plugins/lib/ui.lua` | 4 | Use layout regions for header/body/footer |
| `sdcard/plugins/lib/theme.lua` | 4 | Remove heights replaced by layout engine |
| `sdcard/plugins/txt_reader.lua` | 2-3 | Use layout engine |
| `sdcard/plugins/md_reader.lua` | 2-3 | Use layout engine |
| `sdcard/plugins/home.lua` | 4 | Use layout engine |
| `sdcard/plugins/settings.lua` | 4 | Use layout engine |
| `sdcard/plugins/file_browser.lua` | 4 | Use layout engine |
| `lib/renderer/renderer.c` | 5 | Remove `renderer_get_content_area()` |
| `lib/lua_api/api_display.c` | 5 | Deprecate `display.contentArea()` |

## Coding Standards Checklist

Per `CrossLuaReader/coding-best-practices.md` and `CrossLuaReader/CLAUDE.md`:

- [ ] Pure C, no C++ (except SDK bridges)
- [ ] Snake_case functions with `layout_` prefix
- [ ] File headers with `@status`/`@issues`/`@todo`
- [ ] `uint16_t`/`int16_t` for pixel values, not bare `int`
- [ ] Struct fields ordered largest-to-smallest (minimize padding)
- [ ] No VLAs — fixed-size static struct only
- [ ] No heap allocation — zero malloc
- [ ] All setters validate inputs (clamp to sane ranges)
- [ ] Functions under ~100 lines
- [ ] Descriptive names: `layout_set_header_height()` not `layout_hdr()`
- [ ] Inline comments explain WHY, not WHAT
