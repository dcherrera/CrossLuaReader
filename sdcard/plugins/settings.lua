-- settings.lua — Device settings for CrossLua Reader.
-- Reads/writes /settings.json on SD card.

local ui = require("lib.ui")
local theme = require("lib.theme")

plugin = {
    name = "Settings",
    id = "settings",
    type = "activity",
    menuEntry = "Settings",
}

local font_id = nil
local selected = 1
local needs_render = true
local settings = {}

local menu_items = {
    { label = "Font Size: Medium",     key = "fontSize",    values = {"Small", "Medium", "Large", "X-Large"}, idx = 2 },
    { label = "Orientation: Portrait", key = "orientation", values = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"}, idx = 1 },
    { label = "Screen Margin: 10",     key = "screenMargin", values = {"0", "5", "10", "15", "20"}, idx = 3 },
    { label = "Refresh: 15 pages",     key = "refreshFreq", values = {"1", "5", "10", "15", "30"}, idx = 4 },
    { label = "Theme: Lyra",           key = "theme",       values = {"Lyra", "Classic"}, idx = 1 },
}

local function load_settings()
    local data = storage.read("/settings.json")
    if data then
        -- Simple JSON-like parsing for key:value pairs
        for _, item in ipairs(menu_items) do
            local pattern = '"' .. item.key .. '"%s*:%s*"?([^",}]+)"?'
            local val = data:match(pattern)
            if val then
                for i, v in ipairs(item.values) do
                    if v == val then
                        item.idx = i
                        break
                    end
                end
            end
        end
    end
    update_labels()
end

local function save_settings()
    local parts = {"{"}
    for i, item in ipairs(menu_items) do
        local comma = i < #menu_items and "," or ""
        parts[#parts + 1] = string.format('  "%s": "%s"%s', item.key, item.values[item.idx], comma)
    end
    parts[#parts + 1] = "}"
    storage.write("/settings.json", table.concat(parts, "\n"))
end

function update_labels()
    for _, item in ipairs(menu_items) do
        local name = item.key:gsub("(%l)(%u)", "%1 %2")  -- camelCase → spaced
        name = name:sub(1, 1):upper() .. name:sub(2)
        item.label = name .. ": " .. item.values[item.idx]
    end
end

function plugin.onEnter()
    font_id = font.load("/fonts/NotoSans-14-Regular.cfont")
    selected = 1
    load_settings()
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
        -- Cycle to next value
        local item = menu_items[selected]
        item.idx = (item.idx % #item.values) + 1
        update_labels()
        save_settings()
        needs_render = true
    elseif input.wasPressed(input.LEFT) then
        -- Cycle to previous value
        local item = menu_items[selected]
        item.idx = item.idx - 1
        if item.idx < 1 then item.idx = #item.values end
        update_labels()
        save_settings()
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

    if font_id then
        ui.draw_header(font_id, "Settings")
        local menu_y = t.header_height + t.vertical_spacing
        ui.draw_menu(font_id, menu_items, selected, menu_y)
        ui.draw_button_hints(font_id, {"< Back", "Change", ""})
    end

    display.refresh()
end

function plugin.onExit()
    if font_id then
        font.unload(font_id)
        font_id = nil
    end
end
