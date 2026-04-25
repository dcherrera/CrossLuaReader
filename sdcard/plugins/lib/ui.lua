-- ui.lua — Shared UI drawing helpers for CrossLua Reader plugins.
-- Uses theme metrics for consistent layout across plugins.

local theme = require("lib.theme")
local M = {}

--- Draw the header bar with battery indicator.
-- @param font_id Font ID for text
-- @param title Optional title string (displayed centered)
function M.draw_header(font_id, title)
    local t = theme.get()
    local w = display.width()

    -- Battery percentage in top-right
    local bat = system.batteryPercent()
    local bat_text = bat .. "%"
    local bat_w = display.getTextWidth(font_id, bat_text)
    display.drawText(font_id, w - bat_w - t.side_padding, t.top_padding, bat_text)

    -- Title centered if provided
    if title then
        local tw = display.getTextWidth(font_id, title)
        display.drawText(font_id, (w - tw) / 2, t.top_padding, title)
    end
end

--- Draw a vertical menu with rounded selection highlight.
-- @param font_id Font ID for labels
-- @param items Table of {label=string} entries
-- @param selected 1-based selected index
-- @param y_start Y position to start drawing
-- @return Y position after the last item
function M.draw_menu(font_id, items, selected, y_start)
    local t = theme.get()
    local w = display.width()
    local y = y_start

    for i, item in ipairs(items) do
        local row_x = t.side_padding
        local row_w = w - 2 * t.side_padding
        local row_h = t.menu_row_height

        if i == selected then
            -- Selected: filled rounded rect background
            if t.corner_radius > 0 then
                display.fillRoundedRect(row_x, y, row_w, row_h, t.corner_radius)
                display.drawTextInverted(font_id, row_x + t.selection_padding, y + t.selection_padding, item.label)
            else
                display.fillRect(row_x, y, row_w, row_h)
                display.drawTextInverted(font_id, row_x + t.selection_padding, y + t.selection_padding, item.label)
            end
        else
            display.drawText(font_id, row_x + t.selection_padding, y + t.selection_padding, item.label)
        end

        y = y + row_h + t.vertical_spacing
    end

    return y
end

--- Draw a file/item list (similar to menu but for browsing).
-- @param font_id Font ID for labels
-- @param items Table of {label=string, subtitle=string} entries
-- @param selected 1-based selected index
-- @param y_start Y position to start
-- @param max_visible Maximum visible items (for scrolling)
-- @param scroll_offset 0-based scroll offset
-- @return Y position after last visible item
function M.draw_list(font_id, items, selected, y_start, max_visible, scroll_offset)
    local t = theme.get()
    local w = display.width()
    local y = y_start
    local offset = scroll_offset or 0

    for i = 1, math.min(max_visible or #items, #items - offset) do
        local idx = i + offset
        local item = items[idx]
        if not item then break end

        local row_x = t.side_padding
        local row_w = w - 2 * t.side_padding
        local row_h = t.list_row_height

        if idx == selected then
            if t.corner_radius > 0 then
                display.fillRoundedRect(row_x, y, row_w, row_h, t.corner_radius)
                display.drawTextInverted(font_id, row_x + t.selection_padding, y + 2, item.label)
            else
                display.fillRect(row_x, y, row_w, row_h)
                display.drawTextInverted(font_id, row_x + t.selection_padding, y + 2, item.label)
            end
        else
            display.drawText(font_id, row_x + t.selection_padding, y + 2, item.label)
        end

        y = y + row_h
    end

    return y
end

--- Draw button hints at the bottom of the screen.
-- @param font_id Font ID for labels
-- @param labels Table with up to 4 strings: {back, confirm, left_hint, right_hint}
function M.draw_button_hints(font_id, labels)
    local t = theme.get()
    local w = display.width()
    local h = display.height()
    local y = h - t.button_hints_height

    -- Separator line
    display.drawLine(0, y, w, y)

    local hint_y = y + 8

    if labels[1] then
        display.drawText(font_id, t.side_padding, hint_y, labels[1])
    end
    if labels[2] then
        local tw = display.getTextWidth(font_id, labels[2])
        display.drawText(font_id, w / 2 - tw / 2, hint_y, labels[2])
    end
    if labels[3] then
        local tw = display.getTextWidth(font_id, labels[3])
        display.drawText(font_id, w - tw - t.side_padding, hint_y, labels[3])
    end
end

return M
