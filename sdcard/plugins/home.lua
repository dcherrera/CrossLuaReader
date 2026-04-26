-- home.lua — CrossLua Reader home screen (Lyra style).
-- This is the default home plugin. Customize by editing or replacing
-- with a template from /templates/.

local ui = require("lib.ui")
local theme = require("lib.theme")
local buttons = require("lib.buttons")
local settings = require("lib.settings")
local fonts = require("lib.fonts")
local lang = require("lib.lang")

plugin = {
    name = "Home",
    id = "home",
    type = "activity",
    menuEntry = nil,
    system = true,
}

local selected = 1
local needs_render = true
local menu_items = {}

function plugin.onEnter()
    -- Load and apply persisted settings
    settings.load()
    local orient = settings.get("orientation", 0)
    display.setOrientation(orient)
    theme.set(settings.get("theme", "lyra"))
    system.setSleepTimeout(settings.get("sleepTimeout", 10))

    -- Apply button mapping for this orientation
    input.setMapping(buttons.get_mapping(orient))

    -- Load language pack
    lang.load(settings.get("language", "en"))

    -- Load fonts (includes fallback based on language)
    fonts.init()

    -- Build menu with translated labels
    menu_items = {
        { label = lang.tr("continue_reading"), action = "continue" },
        { label = lang.tr("browse_files"),     action = "browser" },
        { label = lang.tr("settings"),         action = "settings" },
    }

    selected = 1
    needs_render = true
end

function plugin.loop()
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
            else
                system.log("No book to continue")
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

    if fonts.ui then
        ui.draw_header(fonts.ui, "CrossLua Reader")

        local cx, cy, cw, ch = display.contentArea()
        local menu_y = cy + t.header_height + t.vertical_spacing
        ui.draw_menu(fonts.ui, menu_items, selected, menu_y)

        ui.draw_button_hints(fonts.ui, buttons.get("home", settings.get("orientation", 0)))
    end

    display.refresh()
end

function plugin.onExit()
    fonts.cleanup()
end
