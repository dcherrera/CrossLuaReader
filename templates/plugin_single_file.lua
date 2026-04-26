-- plugin_single_file.lua — Single-file plugin template for CrossLua Reader.
--
-- To use: copy this file to /plugins/your_plugin.lua and edit.
-- For plugins with multiple files, use the community_plugin/ template instead.

local ui = require("lib.ui")
local theme = require("lib.theme")
local buttons = require("lib.buttons")
local fonts = require("lib.fonts")
local settings = require("lib.settings")

plugin = {
    name = "My Plugin",
    id = "my_plugin",
    type = "activity",
    menuEntry = "My Plugin",
}

local needs_render = true

function plugin.onEnter(arg)
    fonts.init()
    needs_render = true
end

function plugin.loop()
    if input.wasPressed(input.BACK) then
        plugin.goHome()
        return
    end

    if needs_render then
        needs_render = false
        render()
    end
end

function render()
    local t = theme.get()
    display.clear()

    if fonts.ui then
        ui.draw_header(fonts.ui, plugin.name)
        local cx, cy, cw, ch = display.contentArea()
        local y = cy + t.header_height + t.vertical_spacing
        display.drawText(fonts.ui, cx + t.side_padding, y, "Hello from " .. plugin.name .. "!")
        ui.draw_button_hints(fonts.ui, buttons.get("default", settings.get("orientation", 0)))
    end

    display.refresh()
end

function plugin.onExit()
    fonts.cleanup()
end
