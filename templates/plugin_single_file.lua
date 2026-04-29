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
        -- Configure layout engine for app mode
        layout.setHeaderHeight(t.header_height)
        layout.setFooterHeight(t.button_hints_height)
        layout.setMargin(0)

        ui.draw_header(fonts.ui, plugin.name)
        local bx, by, bw, bh = layout.bodyArea()
        display.drawText(fonts.ui, bx + t.side_padding, by, "Hello from " .. plugin.name .. "!")
        ui.draw_button_hints(fonts.ui, buttons.get("default", settings.get("orientation", 0)))
    end

    display.refresh()
end

function plugin.onExit()
    fonts.cleanup()
end
