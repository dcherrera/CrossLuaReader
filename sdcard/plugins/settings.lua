-- settings.lua — Device settings for CrossLua Reader.
-- Reads/writes via lib/settings module. Changes apply immediately.

local ui = require("lib.ui")
local theme = require("lib.theme")
local buttons = require("lib.buttons")
local settings = require("lib.settings")
local fonts = require("lib.fonts")
local lang = require("lib.lang")

plugin = {
    name = "Settings",
    id = "settings",
    type = "activity",
    menuEntry = "Settings",
    system = true,
}

local selected = 1
local needs_render = true
local needs_full_refresh = false

-- Static settings menu items
local menu_items = {
    { key = "language",         label = "", values = {}, display = {} },  -- populated dynamically
    { key = "theme",            label = "", values = {"lyra", "classic"},                      display = {"Lyra", "Classic"} },
    { key = "fontFamily",       label = "", values = {"NotoSans", "Bookerly", "OpenDyslexic"}, display = {"Noto Sans", "Bookerly", "OpenDyslexic"} },
    { key = "fontSize",         label = "", values = {"12", "14", "16", "18"},                 display = {"Small", "Medium", "Large", "X-Large"} },
    { key = "orientation",      label = "", values = {0, 1, 2, 3},                             display = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"} },
    { key = "screenMargin",     label = "", values = {0, 5, 10, 15, 20},                       display = {"0", "5", "10", "15", "20"} },
    { key = "refreshFrequency", label = "", values = {1, 5, 10, 15, 30},                       display = {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"} },
    { key = "sleepTimeout",     label = "", values = {1, 5, 10, 15, 30},                       display = {"1 min", "5 min", "10 min", "15 min", "30 min"} },
    { key = "sleepMode",        label = "", values = {"blank", "single", "cycle", "random", "clear"},
                                                      display = {"Blank", "Wallpaper", "Cycle", "Random", "Stay on page"} },
}

local function find_value_index(item)
    local current = settings.get(item.key)
    for i, v in ipairs(item.values) do
        if tostring(v) == tostring(current) then return i end
    end
    return 1
end

local function update_labels()
    for _, item in ipairs(menu_items) do
        local idx = find_value_index(item)
        local name = item.key:gsub("(%l)(%u)", "%1 %2"):gsub("^%l", string.upper)
        item.label = name .. ": " .. item.display[idx]
    end
end

local function apply_setting(item, value)
    settings.set(item.key, value)
    settings.save()

    if item.key == "orientation" then
        display.setOrientation(value)
        input.setMapping(buttons.get_mapping(value))
        needs_full_refresh = true
    elseif item.key == "theme" then
        theme.set(value)
    elseif item.key == "fontFamily" or item.key == "fontSize" then
        fonts.reload_reader()
    elseif item.key == "sleepTimeout" then
        system.setSleepTimeout(value)
    elseif item.key == "language" then
        lang.load(value)
        fonts.reload_reader()
    elseif item.key == "sleepMode" then
        local mode_map = {blank=0, single=1, cycle=2, random=3, clear=4}
        system.setSleepMode(mode_map[value] or 0)
    end
end

function plugin.onEnter()
    settings.load()

    -- Discover language packs and populate menu item
    local packs = lang.discover()
    local lang_item = menu_items[1]  -- language is first item
    lang_item.values = {}
    lang_item.display = {}
    for _, p in ipairs(packs) do
        lang_item.values[#lang_item.values + 1] = p.code
        lang_item.display[#lang_item.display + 1] = p.name
    end
    -- Ensure at least English if no packs found
    if #lang_item.values == 0 then
        lang_item.values = {"en"}
        lang_item.display = {"English"}
    end

    fonts.init()
    update_labels()
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
        local item = menu_items[selected]
        local idx = find_value_index(item)
        idx = (idx % #item.values) + 1
        apply_setting(item, item.values[idx])
        update_labels()
        needs_render = true
    elseif input.wasPressed(input.LEFT) then
        local item = menu_items[selected]
        local idx = find_value_index(item)
        idx = idx - 1
        if idx < 1 then idx = #item.values end
        apply_setting(item, item.values[idx])
        update_labels()
        needs_render = true
    elseif input.wasPressed(input.BACK) then
        plugin.goHome()
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
        ui.draw_header(fonts.ui, "Settings")
        local cx, cy, cw, ch = display.contentArea()
        local menu_y = cy + t.header_height + t.vertical_spacing
        ui.draw_menu(fonts.ui, menu_items, selected, menu_y)
        ui.draw_button_hints(fonts.ui, buttons.get("settings", settings.get("orientation", 0)))
    end

    if needs_full_refresh then
        needs_full_refresh = false
        display.refresh(0)  -- full refresh to clear ghosting
    else
        display.refresh()
    end
end

function plugin.onExit()
    fonts.cleanup()
end
