# Future Features

## Partial Region Refresh

The X4 display hardware supports windowed partial updates via `EInkDisplay::displayWindow(x, y, w, h)` (marked EXPERIMENTAL in open-x4-sdk). This would allow refreshing only a rectangular region of the screen instead of the full panel.

**Why it matters:** Combined with the layout engine's region bounds (`layout.headerArea()`, `layout.bodyArea()`, `layout.footerArea()`), plugins could refresh individual regions independently. Use cases:
- Update the footer/status bar without reflashing the body text
- Turn the button hint area into an interactive menu with fast local updates
- Animate a selection highlight without full-screen flicker

**Implementation path:**
1. Add HAL wrapper: `hal_display_refresh_window(x, y, w, h)`
2. Add renderer function: `renderer_refresh_region(x, y, w, h)` (coordinate transform for orientation)
3. Add Lua binding: `display.refreshRegion(x, y, w, h)`
4. Test stability — SDK marks this as EXPERIMENTAL

**SDK reference:** `open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h:61`
