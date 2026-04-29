-- status_bar.lua — Reader status bar for CrossLua Reader.
-- Draws page progress, battery, and optional title within layout.footerArea().

local theme = require("lib.theme")
local M = {}

--- Draw the status bar within the layout engine's footer region.
-- @param font_id Small font ID for status text
-- @param progress Reading progress 0-100 (percentage)
-- @param page Current page number
-- @param total Total pages
-- @param title Optional book/chapter title
function M.draw(font_id, progress, page, total, title)
    local t = theme.get()
    local fx, fy, fw, fh = layout.footerArea()

    -- Separator line at top of footer
    display.drawLine(fx, fy, fx + fw, fy)

    local text_y = fy + 8

    -- Battery on far right
    local bat = system.batteryPercent() .. "%"
    local bat_w = display.getTextWidth(font_id, bat)
    display.drawText(font_id, fx + fw - bat_w - t.side_padding, text_y, bat)

    -- Page count on right
    if page and total then
        local pages = page .. "/" .. total
        local pages_w = display.getTextWidth(font_id, pages)
        display.drawText(font_id, fx + fw - bat_w - pages_w - t.side_padding * 2, text_y, pages)
    end

    -- Progress percentage on left
    if progress then
        local pct = string.format("%.0f%%", progress)
        display.drawText(font_id, fx + t.side_padding, text_y, pct)
    end

    -- Title centered
    if title then
        local tw = display.getTextWidth(font_id, title)
        local max_w = fw - 200  -- leave room for battery and pages
        if tw > max_w then
            while tw > max_w and #title > 3 do
                title = title:sub(1, -2)
                tw = display.getTextWidth(font_id, title)
            end
            title = title .. "..."
            tw = display.getTextWidth(font_id, title)
        end
        display.drawText(font_id, fx + (fw - tw) / 2, text_y, title)
    end
end

return M
