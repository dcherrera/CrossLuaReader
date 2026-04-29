-- reader_utils.lua — Shared reader infrastructure for CrossLua Reader.
-- Page turn handling, progress, refresh management.
-- Used by TXT, MD, and EPUB reader plugins.

local settings = require("lib.settings")
local progress = require("lib.progress")
local status_bar = require("lib.status_bar")
local buttons = require("lib.buttons")
local ui = require("lib.ui")

local M = {}

-- Refresh cycle tracking
local pages_since_refresh = 0
local last_saved_page = -1

--- Handle reader input (page turns, back, jumps).
-- @return Action string: "prev", "next", "back", "jump_prev", "jump_next", or nil
function M.handle_input()
    -- Long-press detection for chapter skip
    local held = input.getHeldTime()

    if input.wasPressed(input.BACK) then
        return "back"
    elseif input.wasPressed(input.RIGHT) or input.wasPressed(input.DOWN) then
        if held > 1000 then
            return "jump_next"
        end
        return "next"
    elseif input.wasPressed(input.LEFT) or input.wasPressed(input.UP) then
        if held > 1000 then
            return "jump_prev"
        end
        return "prev"
    end

    return nil
end

--- Refresh the display with ghosting management.
-- Tracks pages since last full refresh. Uses refreshFrequency setting.
-- @param force_full Force a full refresh regardless of counter
function M.do_refresh(force_full)
    local freq = settings.get("refreshFrequency", 15)

    pages_since_refresh = pages_since_refresh + 1

    if force_full or pages_since_refresh >= freq then
        display.refresh(0)  -- full refresh
        pages_since_refresh = 0
    else
        display.refresh(2)  -- fast refresh
    end
end

--- Draw status bar and button hints (reader "chrome").
-- @param font_id UI font ID for chrome text
-- @param page Current page (1-based)
-- @param total_pages Total number of pages
-- @param title Book title (filename)
function M.draw_page_chrome(font_id, page, total_pages, title)
    local pct = total_pages > 0 and (page / total_pages * 100) or 0
    status_bar.draw(font_id, pct, page, total_pages, title)
end

--- Save reading progress (debounced — only writes if page changed).
-- @param book_path Path to the book file
-- @param page Current page (1-based)
-- @param total_pages Total pages
-- @param byte_offset Byte offset in file for this page
function M.save_progress(book_path, page, total_pages, byte_offset)
    if page == last_saved_page then return end
    last_saved_page = page

    progress.save(book_path, {
        page = page,
        totalPages = total_pages,
        offset = byte_offset or 0,
        percentage = total_pages > 0 and math.floor(page / total_pages * 100) or 0,
    })
end

--- Load saved progress for a book.
-- @param book_path Path to the book file
-- @return Progress table {page, totalPages, offset} or nil
function M.load_progress(book_path)
    return progress.load(book_path)
end

--- Ensure cache directory exists for a plugin.
-- @param plugin_id Plugin ID (e.g., "txt_reader")
-- @return Cache directory path
function M.ensure_cache_dir(plugin_id)
    local dir = "/cache/" .. plugin_id
    if not storage.exists("/cache") then
        storage.mkdir("/cache")
    end
    if not storage.exists(dir) then
        storage.mkdir(dir)
    end
    return dir
end

--- Generate a deterministic cache filename for a file.
-- Uses DJB2 hash of the file path.
-- @param plugin_id Plugin ID
-- @param file_path Path to the book file
-- @return Full cache file path
function M.cache_path(plugin_id, file_path)
    -- DJB2 hash
    local hash = 5381
    for i = 1, #file_path do
        hash = ((hash * 33) + file_path:byte(i)) % 0xFFFFFFFF
    end
    local dir = M.ensure_cache_dir(plugin_id)
    return string.format("%s/%08x", dir, hash)
end

--- Reset refresh counter (call on reader enter).
function M.reset()
    pages_since_refresh = 0
    last_saved_page = -1
end

--- Extract filename from path for display as title.
-- @param path Full file path
-- @return Filename without extension
function M.title_from_path(path)
    local name = path:match("([^/]+)$") or path
    return name:match("(.+)%..+$") or name
end

return M
