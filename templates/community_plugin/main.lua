-- main.lua — Community plugin template for CrossLua Reader.
--
-- To use this template:
--   1. Copy this folder to /plugins/your_plugin_name/
--   2. Rename and edit as needed
--   3. The plugin manager auto-discovers /plugins/your_plugin_name/main.lua
--
-- Your plugin folder can contain any additional files:
--   /plugins/your_plugin_name/
--     main.lua          ← this file (entry point, required)
--     helpers.lua       ← additional modules (require with relative path)
--     data/             ← assets, configs, etc.
--
-- See docs/plugin-guide.md for the full API reference.

local ui = require("lib.ui")
local theme = require("lib.theme")
local buttons = require("lib.buttons")
local fonts = require("lib.fonts")
local settings = require("lib.settings")

-- ══════════════════════════════════════════════════════════════════
-- PLUGIN MANIFEST (required)
-- ══════════════════════════════════════════════════════════════════

plugin = {
    name = "My Plugin",           -- human-readable name
    id = "my_plugin",             -- unique ID (lowercase, no spaces)
    type = "activity",            -- "activity", "reader", or "service"
    menuEntry = "My Plugin",      -- text shown in home menu (nil = hidden)
    -- version = "1.0.0",         -- optional
    -- author = "Your Name",      -- optional
}

-- ══════════════════════════════════════════════════════════════════
-- STATE
-- ══════════════════════════════════════════════════════════════════

local needs_render = true

-- ══════════════════════════════════════════════════════════════════
-- LIFECYCLE
-- ══════════════════════════════════════════════════════════════════

--- Called when the plugin becomes active.
-- @param arg Optional argument (e.g., file path for readers)
function plugin.onEnter(arg)
    fonts.init()
    needs_render = true
end

--- Called every frame (~1ms when awake).
-- Handle input and trigger re-renders here.
function plugin.loop()
    if input.wasPressed(input.BACK) then
        plugin.goHome()
        return
    end

    -- Add your input handling here

    if needs_render then
        needs_render = false
        render()
    end
end

--- Called when leaving the plugin.
-- Free resources, save state.
function plugin.onExit()
    fonts.cleanup()
end

-- ══════════════════════════════════════════════════════════════════
-- RENDERING
-- ══════════════════════════════════════════════════════════════════

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
        local y = by + display.getLineHeight(fonts.ui) + 10

        display.drawText(fonts.ui, bx + t.side_padding, y, "Press Back to return home")

        ui.draw_button_hints(fonts.ui, buttons.get("default", settings.get("orientation", 0)))
    end

    display.refresh()
end
