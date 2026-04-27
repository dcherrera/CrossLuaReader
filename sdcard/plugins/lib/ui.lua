-- ui.lua — Shared UI drawing helpers for CrossLua Reader plugins.
-- Uses theme metrics for consistent layout across plugins.

local theme = require("lib.theme")
local M = {}

--- Check if current language is RTL.
local function is_rtl()
    local ok, lang = pcall(require, "lib.lang")
    if ok and lang.get_direction then
        return lang.get_direction() == "rtl"
    end
    return false
end

--- Draw text, right-aligned if RTL.
local function draw_text_aligned(font_id, x, y, text, row_x, row_w, padding)
    if is_rtl() then
        local tw = display.getTextWidth(font_id, text)
        display.drawText(font_id, row_x + row_w - padding - tw, y, text)
    else
        display.drawText(font_id, x, y, text)
    end
end

--- Draw the header bar with battery indicator.
-- Uses content area bounds so header stays within usable region.
-- @param font_id Font ID for text
-- @param title Optional title string (displayed centered)
function M.draw_header(font_id, title)
    local t = theme.get()
    local cx, cy, cw, ch = display.contentArea()

    -- Battery percentage — opposite side from title in RTL
    local bat = system.batteryPercent()
    local bat_text = bat .. "%"
    local bat_w = display.getTextWidth(font_id, bat_text)
    if is_rtl() then
        display.drawText(font_id, cx + t.side_padding, cy + t.top_padding, bat_text)
    else
        display.drawText(font_id, math.floor(cx + cw - bat_w - t.side_padding), cy + t.top_padding, bat_text)
    end

    -- Title centered in content area
    if title then
        local tw = display.getTextWidth(font_id, title)
        display.drawText(font_id, math.floor(cx + (cw - tw) / 2), cy + t.top_padding, title)
    end
end

--- Get the content area bounds (excludes physical button bar zone).
-- @return x, y, w, h of usable content region
function M.content_area()
    return display.contentArea()
end

--- Get the maximum Y for content.
-- @return Maximum Y coordinate for content
function M.content_max_y()
    local cx, cy, cw, ch = display.contentArea()
    return cy + ch
end

--- Draw a vertical menu with gray dithered selection highlight.
-- Stops rendering items before the button hint zone.
-- @param font_id Font ID for labels
-- @param items Table of {label=string} entries
-- @param selected 1-based selected index
-- @param y_start Y position to start drawing
-- @return Y position after the last item
function M.draw_menu(font_id, items, selected, y_start)
    local t = theme.get()
    local cx, cy, cw, ch = display.contentArea()
    local max_y = cy + ch
    local y = y_start

    for i, item in ipairs(items) do
        local row_h = t.menu_row_height
        if y + row_h > max_y then break end

        local row_x = cx + t.side_padding
        local row_w = cw - 2 * t.side_padding

        if i == selected then
            display.fillRoundedRectGray(row_x, y, row_w, row_h, t.corner_radius)
        end

        draw_text_aligned(font_id, row_x + t.selection_padding, math.floor(y + t.selection_padding),
                          item.label, row_x, row_w, t.selection_padding)

        y = y + row_h + t.vertical_spacing
    end

    return y
end

--- Draw a file/item list with gray dithered selection.
-- @param font_id Font ID for labels
-- @param items Table of {label=string} entries
-- @param selected 1-based selected index
-- @param y_start Y position to start
-- @param max_visible Maximum visible items (for scrolling)
-- @param scroll_offset 0-based scroll offset
-- @return Y position after last visible item
function M.draw_list(font_id, items, selected, y_start, max_visible, scroll_offset)
    local t = theme.get()
    local cx, cy, cw, ch = display.contentArea()
    local max_y = cy + ch
    local y = y_start
    local offset = scroll_offset or 0

    for i = 1, math.min(max_visible or #items, #items - offset) do
        local idx = i + offset
        local item = items[idx]
        if not item then break end

        local row_x = cx + t.side_padding
        local row_w = cw - 2 * t.side_padding
        local row_h = t.list_row_height

        if y + row_h > max_y then break end

        if idx == selected then
            display.fillRoundedRectGray(row_x, y, row_w, row_h, t.corner_radius)
        end

        draw_text_aligned(font_id, row_x + t.selection_padding, math.floor(y + 2),
                          item.label, row_x, row_w, t.selection_padding)

        y = y + row_h
    end

    return y
end

--- Draw button hints at the physical bottom of the device.
-- Uses physical coordinate drawing so hints always align with
-- the actual hardware buttons regardless of screen orientation.
-- @param font_id Font ID for labels
-- @param labels Table with 4 strings: {back, confirm, left, right}
--               Use "" for empty buttons
function M.draw_button_hints(font_id, labels)
    local t = theme.get()
    -- Physical portrait dimensions (always 480x800 for X4)
    local phys_w = 480
    local phys_h = 800
    local bar_h = t.button_hints_height
    local y = phys_h - bar_h

    -- Outer border in physical coords
    display.drawRectPhysical(t.side_padding, y, phys_w - 2 * t.side_padding, bar_h)

    -- 4 equal-width button cells
    local cell_w = math.floor((phys_w - 2 * t.side_padding) / 4)

    for i = 1, 4 do
        local cell_x = t.side_padding + (i - 1) * cell_w

        -- Cell divider (skip first)
        if i > 1 then
            display.drawLinePhysical(cell_x, y, cell_x, y + bar_h)
        end

        -- Label centered in cell (rendered in physical coords)
        local label = labels[i] or ""
        if label ~= "" then
            local tw = display.getTextWidth(font_id, label)
            local text_x = math.floor(cell_x + (cell_w - tw) / 2)
            local text_y = math.floor(y + 8)
            display.drawTextPhysical(font_id, text_x, text_y, label)
        end
    end
end

return M
