-- home.lua — CrossLua Reader home screen.
-- Two render styles, chosen by settings.homeStyle:
--   "list"    — single-column list (default, Lyra style)
--   "biscuit" — 2-column tile grid (subtitle per tile)
-- Customize by editing or replacing with a template from /templates/.

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
local home_style = "list"
local TILE_COLS = 2

function plugin.onEnter()
    -- Load and apply persisted settings
    settings.load()
    local orient = settings.get("orientation", 0)
    display.setOrientation(orient)
    theme.set(settings.get("theme", "lyra"))
    system.setSleepTimeout(settings.get("sleepTimeout", 10))

    -- Apply sleep screen settings
    local mode_map = {blank=0, single=1, cycle=2, random=3, clear=4}
    system.setSleepMode(mode_map[settings.get("sleepMode", "blank")] or 0)
    local wp = settings.get("sleepWallpaper", "")
    if wp ~= "" then system.setSleepWallpaper(wp) end

    -- Apply button mapping for this orientation
    input.setMapping(buttons.get_mapping(orient))

    -- Load language pack
    lang.load(settings.get("language", "en"))

    -- Load fonts (skip reader font — home only uses UI font)
    fonts.init({skip_reader = true})

    -- Configure layout engine for app mode
    local t = theme.get()
    layout.setHeaderHeight(t.header_height)
    layout.setFooterHeight(t.button_hints_height)
    layout.setMargin(0)

    -- Build menu with translated labels.
    menu_items = {
        { label = lang.tr("continue_reading"), action = "continue" },
        { label = lang.tr("browse_files"),     action = "browser" },
        { label = lang.tr("settings"),         action = "settings" },
    }

    home_style = settings.get("homeStyle", "list")
    selected = 1
    needs_render = true
end

-- Movement in the biscuit (tile-grid) layout: UP/DOWN move by row, LEFT/RIGHT
-- by column. Cells beyond the last item are skipped on Right and clamped on Down.
local function move_biscuit(dx, dy)
    local n = #menu_items
    if n == 0 then return end
    local idx0 = selected - 1            -- 0-based for math
    local row = math.floor(idx0 / TILE_COLS)
    local col = idx0 % TILE_COLS
    local rows = math.ceil(n / TILE_COLS)

    col = col + dx
    if col < 0 or col >= TILE_COLS then return end
    row = row + dy
    if row < 0 or row >= rows then return end

    local target = row * TILE_COLS + col
    if target >= n then return end       -- empty trailing cell
    selected = target + 1
    needs_render = true
end

local function is_biscuit() return home_style == "biscuit_small" or home_style == "biscuit_large" end

function plugin.loop()
    if is_biscuit() then
        if input.wasPressed(input.DOWN)  then move_biscuit(0,  1)
        elseif input.wasPressed(input.UP)    then move_biscuit(0, -1)
        elseif input.wasPressed(input.LEFT)  then move_biscuit(-1, 0)
        elseif input.wasPressed(input.RIGHT) then move_biscuit( 1, 0)
        end
    elseif input.wasPressed(input.DOWN) then
        if selected < #menu_items then
            selected = selected + 1
            needs_render = true
        end
    elseif input.wasPressed(input.UP) then
        if selected > 1 then
            selected = selected - 1
            needs_render = true
        end
    end

    if input.wasPressed(input.CONFIRM) or
       (home_style == "list" and input.wasPressed(input.RIGHT)) then
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

-- Biscuit renderer: 2-column tile grid. Title only. Tiles are top-anchored
-- at a fixed compact height per style — no stretching. Selected tile is
-- filled (inverted text); unselected tiles have a 2px hollow border.
-- Empty trailing cells are skipped.
local TILE_HEIGHT_SMALL = 70
local TILE_HEIGHT_LARGE = 140

local function render_biscuit_grid(font_id, bx, by, bw, bh)
    local cols = TILE_COLS
    if #menu_items < 1 then return end

    local gap = 8
    local cell_w = math.floor((bw - gap * (cols + 1)) / cols)
    local cell_h = (home_style == "biscuit_large") and TILE_HEIGHT_LARGE or TILE_HEIGHT_SMALL
    local lh = display.getLineHeight(font_id)

    for i, item in ipairs(menu_items) do
        local idx0 = i - 1
        local r = math.floor(idx0 / cols)
        local c = idx0 % cols
        local x = bx + gap + c * (cell_w + gap)
        local y = by + gap + r * (cell_h + gap)

        -- Vertical centering of the single label line within the tile.
        local text_y = y + math.floor((cell_h - lh) / 2)

        if i == selected then
            display.fillRoundedRect(x, y, cell_w, cell_h, 6)
            display.drawTextInverted(font_id, x + 12, text_y, item.label)
        else
            display.drawRect(x,     y,     cell_w,     cell_h)
            display.drawRect(x + 1, y + 1, cell_w - 2, cell_h - 2)
            display.drawText(font_id, x + 12, text_y, item.label)
        end
    end
end

function render()
    local t = theme.get()
    display.clear()

    if fonts.ui then
        ui.draw_header(fonts.ui, "CrossLua Reader")

        local bx, by, bw, bh = layout.bodyArea()
        if is_biscuit() then
            render_biscuit_grid(fonts.ui, bx, by, bw, bh)
        else
            ui.draw_menu(fonts.ui, menu_items, selected, by)
        end

        ui.draw_button_hints(fonts.ui, buttons.get("home", settings.get("orientation", 0)))
    end

    display.refresh()
end

function plugin.onExit()
    fonts.cleanup()
end
