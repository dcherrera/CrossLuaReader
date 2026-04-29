-- txt_reader.lua — Plain text reader for CrossLua Reader.
-- Uses C-side text.indexPages() and text.getPageLines() for fast
-- streaming pagination with zero Lua heap pressure during indexing.
-- Layout engine provides body/footer bounds and line metrics.

local fonts = require("lib.fonts")
local settings = require("lib.settings")
local reader_utils = require("lib.reader_utils")
local text_layout = require("lib.text_layout")
local lang = require("lib.lang")
local ui = require("lib.ui")
local buttons = require("lib.buttons")

plugin = {
    name = "TXT Reader",
    id = "txt_reader",
    type = "reader",
    fileExtensions = {"txt"},
    system = true,
}

local file_path = nil
local file_size = 0
local page_offsets = {}
local current_page = 1
local total_pages = 0
local viewport = nil
local needs_render = true
local is_rtl = false
local title = ""

-- ══════════════════════════════════════════════════════════════════
-- PAGE INDEX CACHE (stored on SD)
-- ══════════════════════════════════════════════════════════════════

local CACHE_MAGIC = "TXTI4"

local function cache_key()
    return string.format("%d,%d,%d,%s,%s,%d",
        file_size, viewport.w, viewport.lines_per_page,
        settings.get("fontSize", "14"),
        settings.get("fontFamily", "NotoSans"),
        settings.get("screenMargin", 10))
end

local function load_cache()
    local path = reader_utils.cache_path("txt_reader", file_path)
    local content = storage.read(path)
    if not content then return false end

    local lines = {}
    for line in content:gmatch("[^\n]+") do
        lines[#lines + 1] = line
    end

    if #lines < 3 then return false end
    if lines[1] ~= CACHE_MAGIC then return false end
    if lines[2] ~= cache_key() then return false end

    page_offsets = {}
    for i = 3, #lines do
        local off = tonumber(lines[i])
        if off then
            page_offsets[#page_offsets + 1] = off
        end
    end

    total_pages = #page_offsets
    return total_pages > 0
end

local function save_cache()
    local path = reader_utils.cache_path("txt_reader", file_path)
    local parts = {CACHE_MAGIC, cache_key()}
    for _, off in ipairs(page_offsets) do
        parts[#parts + 1] = tostring(off)
    end
    storage.write(path, table.concat(parts, "\n"))
end

-- ══════════════════════════════════════════════════════════════════
-- RENDERING
-- ══════════════════════════════════════════════════════════════════

local function render()
    if total_pages == 0 then return end

    local offset = page_offsets[current_page] or 0
    local fid = fonts.reader or fonts.ui

    -- C-side word-wrapped page lines — queries layout engine internally
    local lines = text.getPageLines(fid, file_path, offset)

    if not lines then return end

    display.clear()

    for i, line in ipairs(lines) do
        local x = viewport.x
        local y = viewport.y + (i - 1) * viewport.line_height

        if is_rtl and line ~= "" then
            local lw = display.getTextWidth(fid, line)
            x = viewport.x + viewport.w - lw
        end

        display.drawText(fid, x, y, line)
    end

    reader_utils.draw_page_chrome(fonts.ui or fid, current_page, total_pages, title)
    reader_utils.do_refresh()
end

-- ══════════════════════════════════════════════════════════════════
-- LIFECYCLE
-- ══════════════════════════════════════════════════════════════════

function plugin.onEnter(path)
    file_path = path
    settings.load()
    lang.load(settings.get("language", "en"))
    fonts.init()
    reader_utils.reset()

    title = reader_utils.title_from_path(path)
    file_size = storage.fileSize(path) or 0

    if file_size == 0 then
        system.log("Empty file: " .. path)
        plugin.goHome()
        return
    end

    -- Detect RTL from first 256 bytes
    local sample = storage.readBytes(path, 0, math.min(256, file_size))
    if sample then
        is_rtl = text_layout.detect_rtl(sample)
        if is_rtl then
            fonts.detect_fallbacks(sample)
        end
    end

    -- Configure layout engine for reader mode
    layout.setHeaderHeight(0)
    layout.setFooterHeight(40)
    layout.setMargin(settings.get("screenMargin", 10))
    layout.setFont(fonts.reader or fonts.ui)

    -- Query layout engine directly — matches C-side indexer values
    local bx, by, bw, bh = layout.bodyArea()
    viewport = {
        x = bx, y = by, w = bw, h = bh,
        lines_per_page = layout.linesPerPage(),
        line_height = layout.lineHeight(),
    }

    -- Try cached index, or build via C-side indexer
    if not load_cache() then
        -- Show indexing message
        display.clear()
        if fonts.ui then
            display.drawText(fonts.ui, 30, 100, "Indexing...")
        end
        display.refresh(2)

        -- C-side: queries layout engine for width/linesPerPage internally
        local offsets = text.indexPages(fonts.reader or fonts.ui, path)

        if offsets then
            page_offsets = offsets
            total_pages = #page_offsets
            save_cache()
        else
            system.log("Indexing failed for " .. path)
            plugin.goHome()
            return
        end
    end

    -- Restore progress
    local saved = reader_utils.load_progress(path)
    if saved and saved.page and saved.page >= 1 and saved.page <= total_pages then
        current_page = saved.page
    else
        current_page = 1
    end

    needs_render = true
end

function plugin.loop()
    local action = reader_utils.handle_input()

    if action == "next" then
        if current_page < total_pages then
            current_page = current_page + 1
            needs_render = true
        end
    elseif action == "prev" then
        if current_page > 1 then
            current_page = current_page - 1
            needs_render = true
        end
    elseif action == "jump_next" then
        current_page = math.min(current_page + 10, total_pages)
        needs_render = true
    elseif action == "jump_prev" then
        current_page = math.max(current_page - 10, 1)
        needs_render = true
    elseif action == "back" then
        reader_utils.save_progress(file_path, current_page, total_pages,
            page_offsets[current_page])
        plugin.goHome()
        return
    end

    if needs_render then
        needs_render = false
        render()
        reader_utils.save_progress(file_path, current_page, total_pages,
            page_offsets[current_page])
    end
end

function plugin.onExit()
    reader_utils.save_progress(file_path, current_page, total_pages,
        page_offsets[current_page])
    fonts.cleanup()
    page_offsets = {}
end
