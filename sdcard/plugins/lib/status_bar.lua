-- status_bar.lua — Reader status bar for CrossLua Reader.
-- Shows page progress, battery, and optional title at the bottom of the screen.

local theme = require("lib.theme")
local M = {}

--- Draw the status bar at the bottom of the screen.
-- @param font_id Small font ID for status text
-- @param progress Reading progress 0-100 (percentage)
-- @param page Current page number
-- @param total Total pages
-- @param title Optional book/chapter title
function M.draw(font_id, progress, page, total, title)
    local t = theme.get()
    local w = display.width()
    local h = display.height()
    local bar_h = t.button_hints_height
    local y = h - bar_h

    -- Separator line
    display.drawLine(0, y, w, y)

    local text_y = y + 8

    -- Battery on far right
    local bat = system.batteryPercent() .. "%"
    local bat_w = display.getTextWidth(font_id, bat)
    display.drawText(font_id, w - bat_w - t.side_padding, text_y, bat)

    -- Page count on right
    if page and total then
        local pages = page .. "/" .. total
        local pages_w = display.getTextWidth(font_id, pages)
        display.drawText(font_id, w - bat_w - pages_w - t.side_padding * 2, text_y, pages)
    end

    -- Progress percentage on left
    if progress then
        local pct = string.format("%.0f%%", progress)
        display.drawText(font_id, t.side_padding, text_y, pct)
    end

    -- Title centered
    if title then
        local tw = display.getTextWidth(font_id, title)
        local max_w = w - 200  -- leave room for battery and pages
        if tw > max_w then
            -- Truncate title (simple: just cut it)
            while tw > max_w and #title > 3 do
                title = title:sub(1, -2)
                tw = display.getTextWidth(font_id, title)
            end
            title = title .. "..."
            tw = display.getTextWidth(font_id, title)
        end
        display.drawText(font_id, (w - tw) / 2, text_y, title)
    end
end

return M
