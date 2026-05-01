-- settings.lua — Device settings for CrossLua Reader.
-- Uses layout engine for header/body positioning.
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
local scroll_offset = 0
local needs_render = true
local needs_full_refresh = false

-- Settings menu item definitions
-- display_keys: translation keys for display values (looked up via lang.tr)
-- display: populated at runtime from display_keys
local menu_items = {
    { key = "language",         label = "", values = {}, display = {} },
    { key = "theme",            label = "", values = {"lyra", "classic"},  display = {}, display_raw = {"Lyra", "Classic"} },
    { key = "fontFamily",       label = "", values = {}, display = {} },
    { key = "fontSize",         label = "", values = {"12", "14", "16", "18"},
                                display = {}, display_keys = {"small", "medium", "large", "x-large"} },
    { key = "orientation",      label = "", values = {0, 1, 2, 3},
                                display = {}, display_keys = {"portrait", "landscape_cw", "inverted", "landscape_ccw"} },
    { key = "screenMargin",     label = "", values = {0, 5, 10, 15, 20},  display = {}, display_raw = {"0", "5", "10", "15", "20"} },
    { key = "refreshFrequency", label = "", values = {1, 5, 10, 15, 30},  display = {}, display_raw = {"1", "5", "10", "15", "30"} },
    { key = "sleepTimeout",     label = "", values = {1, 5, 10, 15, 30},  display = {}, display_raw = {"1 min", "5 min", "10 min", "15 min", "30 min"} },
    { key = "sleepMode",        label = "", values = {"blank", "single", "cycle", "random", "clear"},
                                display = {}, display_keys = {"blank", "wallpaper", "cycle", "random", "stay_on_page"} },
    { key = "homeStyle",        label = "", values = {"list", "biscuit"},
                                display = {}, display_raw = {"List", "Biscuit"} },
}

-- Rebuild display names from translation keys
local function rebuild_display()
    for _, item in ipairs(menu_items) do
        if item.display_keys then
            item.display = {}
            for _, k in ipairs(item.display_keys) do
                item.display[#item.display + 1] = lang.tr(k)
            end
        elseif item.display_raw then
            item.display = {}
            for _, v in ipairs(item.display_raw) do
                item.display[#item.display + 1] = v
            end
        end
    end
end

local function find_value_index(item)
    local current = settings.get(item.key)
    for i, v in ipairs(item.values) do
        if tostring(v) == tostring(current) then return i end
    end
    return 1
end

-- Map settings keys to lang.json translation keys
local tr_keys = {
    language = "language",
    theme = "theme",
    fontFamily = "font_family",
    fontSize = "font_size",
    orientation = "orientation",
    screenMargin = "screen_margin",
    refreshFrequency = "refresh_freq",
    sleepTimeout = "sleep_timeout",
    sleepMode = "sleep_mode",
    homeStyle = "home_style",
}

local function update_labels()
    for _, item in ipairs(menu_items) do
        local idx = find_value_index(item)
        local tr_key = tr_keys[item.key]
        local name = tr_key and lang.tr(tr_key) or item.key
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
        -- Reconfigure layout for new theme dimensions
        local t = theme.get()
        layout.setHeaderHeight(t.header_height)
        layout.setFooterHeight(t.button_hints_height)
    elseif item.key == "fontFamily" or item.key == "fontSize" then
        fonts.reload_reader()
    elseif item.key == "sleepTimeout" then
        system.setSleepTimeout(value)
    elseif item.key == "language" then
        lang.load(value)
        rebuild_display()
        -- Unload old fallback
        if fonts.fallback then
            if fonts.ui then font.clearFallback(fonts.ui) end
            font.unload(fonts.fallback)
            fonts.fallback = nil
        end
        -- Load new fallback for this language
        if value == "he" then
            fonts.load_fallback_for_script("hebrew")
        elseif value == "ar" then
            fonts.load_fallback_for_script("arabic")
        end
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

    -- Discover font families from /fonts/ subdirectories
    local families = fonts.discover_families()
    local font_item = nil
    for _, item in ipairs(menu_items) do
        if item.key == "fontFamily" then font_item = item; break end
    end
    if font_item then
        font_item.values = {}
        font_item.display = {}
        for _, name in ipairs(families) do
            font_item.values[#font_item.values + 1] = name
            font_item.display[#font_item.display + 1] = name
        end
        if #font_item.values == 0 then
            font_item.values = {"NotoSans"}
            font_item.display = {"NotoSans"}
        end
    end

    -- Load language and rebuild translated display names
    lang.load(settings.get("language", "en"))
    rebuild_display()

    fonts.init({skip_reader = true})

    -- Configure layout engine for app mode
    local t = theme.get()
    layout.setHeaderHeight(t.header_height)
    layout.setFooterHeight(t.button_hints_height)
    layout.setMargin(0)

    update_labels()
    selected = 1
    scroll_offset = 0
    needs_render = true
end

local function get_max_visible()
    local t = theme.get()
    local bx, by, bw, bh = layout.bodyArea()
    return math.floor(bh / (t.menu_row_height + t.vertical_spacing))
end

function plugin.loop()
    local max_visible = get_max_visible()

    if input.wasPressed(input.DOWN) then
        if selected < #menu_items then
            selected = selected + 1
            if selected > scroll_offset + max_visible then
                scroll_offset = selected - max_visible
            end
            needs_render = true
        end
    elseif input.wasPressed(input.UP) then
        if selected > 1 then
            selected = selected - 1
            if selected <= scroll_offset then
                scroll_offset = selected - 1
            end
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
        ui.draw_header(fonts.ui, lang.tr("settings"))
        local bx, by, bw, bh = layout.bodyArea()
        local max_visible = get_max_visible()
        ui.draw_list(fonts.ui, menu_items, selected, by, max_visible, scroll_offset)
        ui.draw_button_hints(fonts.ui, buttons.get("settings", settings.get("orientation", 0)))
    end

    if needs_full_refresh then
        needs_full_refresh = false
        display.refresh(0)
    else
        display.refresh()
    end
end

function plugin.onExit()
    fonts.cleanup()
end
