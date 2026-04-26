-- home_classic.lua — Classic-style home screen for CrossLua Reader.
-- Simple box layout without rounded corners.
-- To use: copy this file to /plugins/home.lua on the SD card.

local ui = require("lib.ui")
local theme = require("lib.theme")

plugin = {
    name = "Home",
    id = "home",
    type = "activity",
    menuEntry = nil,
}

local font_id = nil
local selected = 1
local needs_render = true

local menu_items = {
    { label = "Continue Reading", action = "continue" },
    { label = "Browse Files",     action = "browser" },
    { label = "Settings",         action = "settings" },
}

function plugin.onEnter()
    -- Force classic theme
    theme.set("classic")

    font_id = font.load("/fonts/NotoSans-14-Regular.cfont")
    selected = 1
    needs_render = true
end

function plugin.loop()
    input.poll()

    if input.wasPressed(input.DOWN) then
        if selected < #menu_items then
            selected = selected + 1
            needs_render = true
        end
    elseif input.wasPressed(input.UP) then
        if selected > 1 then
            selected = selected - 1
            needs_render = true
        end
    elseif input.wasPressed(input.CONFIRM) or input.wasPressed(input.RIGHT) then
        local action = menu_items[selected].action
        if action == "browser" then
            plugin.navigate("file_browser")
        elseif action == "settings" then
            plugin.navigate("settings")
        elseif action == "continue" then
            local last = storage.read("/crosslua_last_book.txt")
            if last and storage.exists(last) then
                local ext = last:match("%.(%w+)$")
                if ext then
                    plugin.navigate(ext .. "_reader", last)
                end
            end
        end
    end

    if needs_render then
        needs_render = false
        render()
    end
end

function render()
    local t = theme.get()
    display.clear()

    if font_id then
        -- Simple centered title
        local title = "CrossLua Reader"
        local tw = display.getTextWidth(font_id, title)
        display.drawText(font_id, (display.width() - tw) / 2, 10, title)
        display.drawLine(20, 40, display.width() - 20, 40)

        -- Simple menu with box selection
        ui.draw_menu(font_id, menu_items, selected, 60)
        ui.draw_button_hints(font_id, {"", "Select", ""})
    end

    display.refresh()
end

function plugin.onExit()
    if font_id then
        font.unload(font_id)
        font_id = nil
    end
end
