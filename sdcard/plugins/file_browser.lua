-- file_browser.lua — SD card file browser for CrossLua Reader.
-- Navigate folders, select files to open in reader plugins.

local ui = require("lib.ui")
local theme = require("lib.theme")
local buttons = require("lib.buttons")
local fonts = require("lib.fonts")

plugin = {
    name = "File Browser",
    id = "file_browser",
    type = "activity",
    menuEntry = "Browse Files",
}

-- font_id replaced by fonts.ui from lib/fonts
local current_path = "/"
local entries = {}
local selected = 1
local scroll_offset = 0
local needs_render = true

-- Supported file extensions
local supported = { epub = true, txt = true, md = true }

local function is_supported(name)
    local ext = name:match("%.(%w+)$")
    return ext and supported[ext:lower()]
end

local function load_directory(path)
    entries = {}
    selected = 1
    scroll_offset = 0

    -- Add parent directory entry (unless at root)
    if path ~= "/" then
        entries[#entries + 1] = { label = ".. (back)", name = "..", isDir = true }
    end

    local items = storage.list(path)
    if not items then return end

    -- Directories first, then files
    local dirs = {}
    local files = {}
    for _, item in ipairs(items) do
        if item.name:sub(1, 1) ~= "." then  -- hide dotfiles
            if item.isDir then
                dirs[#dirs + 1] = { label = "[" .. item.name .. "]", name = item.name, isDir = true }
            elseif is_supported(item.name) then
                files[#files + 1] = { label = item.name, name = item.name, isDir = false }
            end
        end
    end

    -- Sort alphabetically
    table.sort(dirs, function(a, b) return a.name:lower() < b.name:lower() end)
    table.sort(files, function(a, b) return a.name:lower() < b.name:lower() end)

    for _, d in ipairs(dirs) do entries[#entries + 1] = d end
    for _, f in ipairs(files) do entries[#entries + 1] = f end
end

function plugin.onEnter(path)
    fonts.init()
    current_path = path or "/"
    load_directory(current_path)
    needs_render = true
end

function plugin.loop()
    input.poll()
    local t = theme.get()
    local max_visible = math.floor((display.height() - t.header_height - t.button_hints_height) / t.list_row_height)

    if input.wasPressed(input.DOWN) then
        if selected < #entries then
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
        local entry = entries[selected]
        if entry then
            if entry.name == ".." then
                -- Go up
                current_path = current_path:match("(.+)/[^/]+/?$") or "/"
                load_directory(current_path)
                needs_render = true
            elseif entry.isDir then
                -- Enter directory
                if current_path == "/" then
                    current_path = "/" .. entry.name
                else
                    current_path = current_path .. "/" .. entry.name
                end
                load_directory(current_path)
                needs_render = true
            else
                -- Open file
                local file_path = current_path
                if file_path == "/" then
                    file_path = "/" .. entry.name
                else
                    file_path = current_path .. "/" .. entry.name
                end

                -- Save as last opened book
                storage.write("/crosslua_last_book.txt", file_path)

                -- Find reader by extension
                local ext = entry.name:match("%.(%w+)$")
                if ext then
                    plugin.navigate(ext:lower() .. "_reader", file_path)
                end
            end
        end
    elseif input.wasPressed(input.BACK) or input.wasPressed(input.LEFT) then
        if current_path ~= "/" then
            current_path = current_path:match("(.+)/[^/]+/?$") or "/"
            load_directory(current_path)
            needs_render = true
        else
            plugin.goHome()
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
        ui.draw_header(fonts.ui, current_path)

        local list_y = t.header_height + t.vertical_spacing
        local max_visible = math.floor((display.height() - t.header_height - t.button_hints_height) / t.list_row_height)

        if #entries == 0 then
            display.drawText(fonts.ui, t.side_padding, list_y, "No files found")
        else
            ui.draw_list(fonts.ui, entries, selected, list_y, max_visible, scroll_offset)
        end

        ui.draw_button_hints(fonts.ui, buttons.get("browser"))
    end

    display.refresh()
end

function plugin.onExit()
    fonts.cleanup()
    entries = {}
end
