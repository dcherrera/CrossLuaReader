-- home.lua — CrossLua Reader home screen (Lyra style).
-- This is the default home plugin. Customize by editing or replacing
-- with a template from /templates/.

local ui = require("lib.ui")
local theme = require("lib.theme")

plugin = {
    name = "Home",
    id = "home",
    type = "activity",
    menuEntry = nil,  -- home is not shown in its own menu
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
    font_id = font.load("/fonts/NotoSans-14-Regular.cfont")
    if not font_id then
        system.log("Home: failed to load font")
    end
    selected = 1
    needs_render = true
end

function plugin.loop()
    input.poll()

    -- Navigation
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
            -- Load last opened book path from state file
            local last = storage.read("/crosslua_last_book.txt")
            if last and storage.exists(last) then
                -- Find reader for this file extension
                local ext = last:match("%.(%w+)$")
                if ext then
                    plugin.navigate(ext .. "_reader", last)
                end
            else
                system.log("No book to continue")
            end
        end
    end

    -- Render
    if needs_render then
        needs_render = false
        render()
    end
end

function render()
    local t = theme.get()
    display.clear()

    -- Header
    if font_id then
        ui.draw_header(font_id, "CrossLua Reader")
    end

    -- Menu
    local menu_y = t.header_height + t.vertical_spacing
    if font_id then
        ui.draw_menu(font_id, menu_items, selected, menu_y)
    end

    -- Button hints
    if font_id then
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
