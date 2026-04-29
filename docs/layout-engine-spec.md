# Layout Engine Specification

## Overview

The layout engine is a C-side system that owns all display area calculations. It divides the physical display into two non-overlapping regions — Content and Chrome — and provides authoritative line/column metrics that all rendering (C and Lua) must respect.

**Goal:** One source of truth for "how many lines fit" and "where does content go." Eliminates pagination mismatches between indexer and renderer.

## Display Model

Three non-overlapping regions, stacked vertically:

```
+--------------------------------------------------+
|                  HEADER                           |
|  Title, battery, breadcrumbs (0 = hidden)        |
+--------------------------------------------------+
|                                                  |
|                   BODY                            |
|                                                  |
|  Gets ALL remaining space after header + footer. |
|  Configurable margins (top, right, bottom, left) |
|  Optional: lines_per_page computed for uniform   |
|  text layouts (readers). Plugins that need        |
|  variable layouts just use the bounds and manage  |
|  their own y cursor.                              |
|                                                  |
+--------------------------------------------------+
|                  FOOTER                           |
|  Button hints, status bar, progress (0 = hidden) |
+--------------------------------------------------+
```

- **Physical display**: 800×480 (X4), orientation-aware via renderer
- **Header + Body + Footer = total usable area** (physical display minus bezel margins)
- Resizing any region automatically adjusts Body (Body gets the remainder)
- Setting Header to 0: Body extends to top
- Setting Footer to 0: Body extends to bottom
- Setting both to 0: Body gets the full display
- Regions can NEVER overlap

### Usage by plugin type

| Plugin | Header | Body | Footer |
|--------|--------|------|--------|
| Reader (TXT/MD) | 0 | Full content, uses `lines_per_page` | Status bar (page/battery) |
| Home | 84px (title + battery) | Menu items | 40px (button hints) |
| Settings | 84px (title + battery) | Scrollable list | 40px (button hints) |
| File browser | 84px (path + battery) | File list | 40px (button hints) |
| EPUB reader | 0 | Mixed-size content, manages own y | Status bar |

### Body area: two modes of use

**Uniform line mode** (readers): Body provides `lines_per_page` based on font size, margins, and line spacing. The indexer and renderer both query this single value. Pagination is deterministic.

**Free drawing mode** (apps, EPUB): Body provides bounds (`x, y, w, h`). The plugin draws whatever it wants within those bounds, tracking its own y position. The layout engine doesn't care about line heights or font sizes — it just guarantees the plugin stays within the body region.

## Layout Properties (C-side, authoritative)

### Configurable (set from Lua, trigger recalculate)

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `header_height` | int | 0 | Header region height (0 = hidden) |
| `footer_height` | int | 40 | Footer region height (0 = hidden) |
| `margin_top` | int | 10 | Body top margin |
| `margin_bottom` | int | 10 | Body bottom margin |
| `margin_left` | int | 10 | Body left margin |
| `margin_right` | int | 10 | Body right margin |
| `line_height` | int | (from font) | Pixels per text line (0 = derive from font) |
| `line_spacing` | int | 0 | Extra pixels between lines |

### Computed (read-only, recalculated automatically)

| Property | Type | Description |
|----------|------|-------------|
| `header_x/y/w/h` | int | Header region bounds |
| `body_x/y/w/h` | int | Body region bounds (after margins) |
| `footer_x/y/w/h` | int | Footer region bounds |
| `lines_per_page` | int | How many uniform lines fit in body (for readers) |
| `body_raw_x/y/w/h` | int | Body region WITHOUT margins (for free drawing) |

### Computation

```
total_usable_h = physical_height - bezel_top - bezel_bottom
total_usable_w = physical_width - bezel_left - bezel_right

header: x=bezel_left, y=bezel_top, w=total_usable_w, h=header_height
footer: x=bezel_left, y=physical_height-bezel_bottom-footer_height, w=total_usable_w, h=footer_height

body_raw: x=bezel_left, y=header.y+header.h, w=total_usable_w, h=footer.y - (header.y+header.h)
body: x=body_raw.x+margin_left, y=body_raw.y+margin_top,
      w=body_raw.w-margin_left-margin_right, h=body_raw.h-margin_top-margin_bottom

lines_per_page = floor(body.h / (line_height + line_spacing))
```

When any configurable property changes, all computed values recalculate automatically.

## Lua API

### Configuration (triggers recalculate)

```lua
-- Region heights
layout.setHeaderHeight(height)      -- 0 = hidden, body grows
layout.setFooterHeight(height)      -- 0 = hidden, body grows

-- Body margins
layout.setMargins(top, right, bottom, left)
layout.setMargin(value)             -- uniform all sides

-- Line metrics (for uniform line mode)
layout.setLineSpacing(pixels)       -- extra space between lines
layout.setLineHeight(pixels)        -- override font-derived height
layout.setFont(font_id)             -- derive line_height from font

-- Orientation
layout.setOrientation(n)            -- recalculates everything
```

### Queries (read-only, authoritative)

```lua
-- Line metrics
local lpp = layout.linesPerPage()   -- THE number. Single source of truth.
local lh = layout.lineHeight()      -- line_height + line_spacing

-- Region bounds
local x, y, w, h = layout.headerArea()
local x, y, w, h = layout.bodyArea()      -- with margins applied
local x, y, w, h = layout.bodyAreaRaw()   -- without margins (for free drawing)
local x, y, w, h = layout.footerArea()

-- Convenience
local w = layout.bodyWidth()        -- body width (with margins)
local h = layout.bodyHeight()       -- body height (with margins)
```

### Typical usage

```lua
-- Reader setup
layout.setHeaderHeight(0)
layout.setFooterHeight(40)
layout.setMargin(10)
layout.setFont(fonts.reader)
local pages = text.indexPages(fonts.reader, path)  -- uses layout.linesPerPage() internally

-- App setup
layout.setHeaderHeight(84)
layout.setFooterHeight(40)
layout.setMargin(20)
-- Draw menu items freely in layout.bodyArea()
```

## C Implementation

### File: `lib/layout/layout_engine.c/h`

```c
typedef struct {
    /* Configurable */
    int header_height;
    int footer_height;
    int margin_top, margin_right, margin_bottom, margin_left;
    int line_spacing;
    int line_height;      /* 0 = derive from font */
    int font_id;          /* font to derive line_height from (-1 = none) */

    /* Computed (read-only externally) */
    int header_x, header_y, header_w, header_h;
    int body_x, body_y, body_w, body_h;           /* with margins */
    int body_raw_x, body_raw_y, body_raw_w, body_raw_h;  /* without margins */
    int footer_x, footer_y, footer_w, footer_h;
    int lines_per_page;
    int effective_line_height;  /* line_height + line_spacing */

    /* Physical display info */
    int display_w, display_h;
    int bezel_top, bezel_right, bezel_bottom, bezel_left;
    orientation_t orientation;
} layout_state_t;

/* Singleton — one layout state for the entire display */
void layout_init(void);
void layout_recalculate(void);  /* called automatically on any set_* */

/* Setters (each triggers recalculate) */
void layout_set_header_height(int height);
void layout_set_footer_height(int height);
void layout_set_margins(int top, int right, int bottom, int left);
void layout_set_line_spacing(int spacing);
void layout_set_line_height(int height);
void layout_set_font(int font_id);
void layout_set_orientation(orientation_t orient);

/* Getters */
int layout_lines_per_page(void);
int layout_line_height(void);   /* effective: line_height + line_spacing */
void layout_header_area(int *x, int *y, int *w, int *h);
void layout_body_area(int *x, int *y, int *w, int *h);
void layout_body_area_raw(int *x, int *y, int *w, int *h);
void layout_footer_area(int *x, int *y, int *w, int *h);
```

### File: `lib/lua_api/api_layout.c/h`

Lua bindings wrapping the C layout engine. Registered as `layout.*` global.

### Memory: zero heap allocation

All state lives in a single static `layout_state_t` struct. No malloc. ~100 bytes total.

## Integration Points

### text.* API
- `text.indexPages` and `text.indexMarkdownPages` query `layout_lines_per_page()` and `layout_body_area()` directly instead of receiving them as parameters
- `text.getPageLines` and `text.renderMarkdownPage` use body width from layout engine
- Eliminates parameter passing and ensures single source of truth

### Reader plugins
- `reader_utils.get_viewport()` replaced by `layout.bodyArea()` + `layout.linesPerPage()`
- Reader sets: `layout.setHeaderHeight(0)`, `layout.setFooterHeight(40)`, `layout.setFont(reader_font)`
- No more manual viewport calculation in Lua

### UI plugins (home, settings, browser)
- Set: `layout.setHeaderHeight(84)`, `layout.setFooterHeight(40)`
- Draw header content in `layout.headerArea()`
- Draw menu/list in `layout.bodyArea()` (free drawing mode — no lines_per_page needed)
- Draw button hints in `layout.footerArea()`
- Replaces manual coordinate calculations in `ui.lua`

### Status bar / button hints
- Draw into footer area exclusively
- `layout.footerArea()` provides bounds
- Footer area stays at physical bottom, adjusts with orientation

### Header (title + battery)
- Draw into header area
- `layout.headerArea()` provides bounds
- `ui.draw_header()` uses header bounds instead of hardcoded offsets

## Orientation Behavior

When orientation changes:
1. Physical display dimensions swap (portrait ↔ landscape)
2. Bezel margins rotate
3. Chrome area stays at the physical bottom
4. Content area recalculates
5. `lines_per_page` recalculates
6. All cached page indices invalidate

## Migration Path

1. **Phase 1**: Build `layout_engine.c/h` + `api_layout.c/h` with core state, setters, getters
2. **Phase 2**: Update `text.*` API to query layout engine internally
3. **Phase 3**: Update reader plugins (txt_reader, md_reader) to use `layout.*`
4. **Phase 4**: Update UI plugins (home, settings, file_browser) to use `layout.*`
5. **Phase 5**: Remove `reader_utils.get_viewport()`, `display.contentArea()` deprecated in favor of `layout.*`

### Backward compatibility
During migration, `display.contentArea()` continues to work. Plugins can gradually adopt `layout.*` without breaking.

## Design Principles

- C is the authority on all spatial calculations
- Lua configures, C computes
- Computed values never cached in Lua — always queried from C
- Changing any input triggers automatic recalculation
- Content and Chrome areas are mutually exclusive — guaranteed no overlap
- All text rendering respects area boundaries
- Line count is deterministic: same inputs = same line count everywhere

## Coding Standards

Per `CrossLuaReader/coding-best-practices.md`:
- Pure C (no C++)
- Snake_case functions with `layout_` prefix
- File header with @status/@issues/@todo
- All configurable values validated (clamp to sane ranges)
- No heap allocation — all state in a single static struct
- Thread-safe reads (background input task may query during render)
