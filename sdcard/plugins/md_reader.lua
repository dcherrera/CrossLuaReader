-- md_reader.lua — Markdown reader for CrossLua Reader.
-- Uses C-side text.indexMarkdownPages() for fast indexing.
-- Renders headers, lists, blockquotes, code blocks, bold, italic, links.

local fonts = require("lib.fonts")
local settings = require("lib.settings")
local reader_utils = require("lib.reader_utils")
local text_layout = require("lib.text_layout")
local lang = require("lib.lang")

plugin = {
    name = "MD Reader",
    id = "md_reader",
    type = "reader",
    fileExtensions = {"md"},
    system = true,
}

local file_path = nil
local file_size = 0
local page_offsets = {}
local page_code_state = {}
local current_page = 1
local total_pages = 0
local viewport = nil
local needs_render = true
local is_rtl = false
local title = ""

local CHUNK_SIZE = 4096
local CACHE_MAGIC = "MDRI3"

-- All markdown parsing (block detection, inline spans, stripping,
-- word wrapping) is handled in C by text.renderMarkdownPage().
-- The Lua side only handles layout and drawing.

-- ══════════════════════════════════════════════════════════════════
-- PAGE INDEX CACHE
-- ══════════════════════════════════════════════════════════════════

local function cache_key()
    return string.format("%d,%d,%d,%s,%s,%d",
        file_size, viewport.w, viewport.lines_per_page,
        settings.get("fontSize", "14"),
        settings.get("fontFamily", "NotoSans"),
        settings.get("screenMargin", 10))
end

local function load_cache()
    local path = reader_utils.cache_path("md_reader", file_path)
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
    page_code_state = {}
    for i = 3, #lines do
        local off, cs = lines[i]:match("^(%d+),(%d+)$")
        if off then
            page_offsets[#page_offsets + 1] = tonumber(off)
            page_code_state[#page_code_state + 1] = (cs == "1")
        end
    end

    total_pages = #page_offsets
    return total_pages > 0
end

local function save_cache()
    local path = reader_utils.cache_path("md_reader", file_path)
    local parts = {CACHE_MAGIC, cache_key()}
    for i, off in ipairs(page_offsets) do
        parts[#parts + 1] = tostring(off) .. "," .. (page_code_state[i] and "1" or "0")
    end
    storage.write(path, table.concat(parts, "\n"))
end

-- ══════════════════════════════════════════════════════════════════
-- PAGE RENDERING
-- ══════════════════════════════════════════════════════════════════

-- Style constants (match C side)
local STYLE_NORMAL = 0
local STYLE_BOLD   = 1
local STYLE_ITALIC = 2
local STYLE_CODE   = 3
local STYLE_LINK   = 4

--- Try to load a header font into a free slot.
-- Does NOT unload any existing fonts — only uses free slots.
-- Returns font id or nil if no slot available.
local function load_header_font(size)
    local family = settings.get("fontFamily", "NotoSans")
    local path = "/fonts/" .. family .. "/" .. family .. "-" .. size .. "-Regular.cfont"
    local hfid = font.load(path)
    if not hfid then
        path = "/fonts/NotoSans/NotoSans-" .. size .. "-Regular.cfont"
        hfid = font.load(path)
    end
    return hfid
end

-- Header size mapping
local header_font_size = {
    h1 = "18",
    h2 = "16",
    h3 = nil,   -- use reader font (no swap)
    h4 = "12",
    h5 = nil,   -- use reader font (no swap)
}

-- Cached header font for current page render
local page_header_fid = nil
local page_header_size = nil

local function render()
    if total_pages == 0 then return end

    local offset = page_offsets[current_page] or 0
    local in_code = page_code_state[current_page] or false

    display.clear()

    local fid = fonts.reader or fonts.ui
    local lh = viewport.line_height
    local y = viewport.y
    local bq_start_y = nil

    -- C does all the heavy lifting: parse, strip, wrap, span.
    -- Request more lines than needed — we stop drawing when y exceeds viewport.
    -- This ensures no text gaps between pages.
    local max_y = viewport.y + viewport.h
    local stopped = false
    text.renderMarkdownPage(fid, file_path, offset,
        viewport.w, viewport.lines_per_page + 10, in_code,
        function(block_type, line_text, spans, indent)
            if stopped then return end
            if y + viewport.line_height > max_y then
                stopped = true
                return
            end

            local x = viewport.x

            if block_type == "blank" then
                -- End blockquote border if we were in one
                if bq_start_y then
                    display.drawLine(viewport.x + 4, bq_start_y, viewport.x + 4, y)
                    display.drawLine(viewport.x + 5, bq_start_y, viewport.x + 5, y)
                    bq_start_y = nil
                end
                y = y + lh
                return
            end

            if block_type == "hr" then
                if bq_start_y then
                    display.drawLine(viewport.x + 4, bq_start_y, viewport.x + 4, y)
                    display.drawLine(viewport.x + 5, bq_start_y, viewport.x + 5, y)
                    bq_start_y = nil
                end
                display.drawLine(viewport.x, y + lh / 2, viewport.x + viewport.w, y + lh / 2)
                y = y + lh
                return
            end

            -- End blockquote border when block type changes
            if block_type ~= "blockquote" and bq_start_y then
                display.drawLine(viewport.x + 4, bq_start_y, viewport.x + 4, y)
                display.drawLine(viewport.x + 5, bq_start_y, viewport.x + 5, y)
                bq_start_y = nil
            end

            -- Adjust x for indented blocks
            if block_type == "code_fence" then
                -- Code block: left border + indent
                display.drawLine(viewport.x + 2, y, viewport.x + 2, y + lh)
                display.drawLine(viewport.x + 3, y, viewport.x + 3, y + lh)
                x = viewport.x + 16
            elseif block_type == "blockquote" then
                if not bq_start_y then bq_start_y = y end
                x = viewport.x + 16
            elseif block_type == "list" then
                local indent_px = (indent + 1) * 20
                -- Draw bullet on first wrapped line of list item
                display.fillRect(viewport.x + indent_px - 10, y + lh / 2 - 2, 4, 4)
                x = viewport.x + indent_px
            end

            -- Header handling
            local is_header = block_type == "h1" or block_type == "h2" or
                              block_type == "h3" or block_type == "h4" or block_type == "h5"
            local draw_fid = fid
            local want_size = header_font_size[block_type]

            -- Try to load header font (reuse if same size as last header)
            if is_header and want_size then
                if page_header_fid and page_header_size == want_size then
                    draw_fid = page_header_fid
                else
                    -- Unload previous header font if different size
                    if page_header_fid then
                        font.unload(page_header_fid)
                        page_header_fid = nil
                        page_header_size = nil
                    end
                    local hfid = load_header_font(want_size)
                    if hfid then
                        page_header_fid = hfid
                        page_header_size = want_size
                        draw_fid = hfid
                    end
                    -- If load fails, draw_fid stays as reader font (graceful fallback)
                end
            end

            -- Render spans
            local draw_x = x
            for _, span in ipairs(spans) do
                local s = span.style
                local t = span.text
                if t and t ~= "" then
                    local w = display.getTextWidth(draw_fid, t)

                    if is_header then
                        if block_type == "h5" then
                            -- H5: italic + underline, body size
                            display.drawText(fid, draw_x, y, t)
                            display.drawLine(draw_x, y + lh - 2, draw_x + w, y + lh - 2)
                        else
                            -- H1-H4: bold (double-strike)
                            display.drawText(draw_fid, draw_x, y, t)
                            display.drawText(draw_fid, draw_x + 1, y, t)
                        end
                    elseif s == STYLE_BOLD then
                        display.drawText(fid, draw_x, y, t)
                        display.drawText(fid, draw_x + 1, y, t)
                    elseif s == STYLE_ITALIC then
                        display.drawText(fid, draw_x, y, t)
                        display.drawLine(draw_x, y + lh - 2, draw_x + w, y + lh - 2)
                    elseif s == STYLE_CODE then
                        display.drawRect(draw_x - 2, y - 1, w + 4, lh + 2)
                        display.drawText(fid, draw_x, y, t)
                    elseif s == STYLE_LINK then
                        display.drawText(fid, draw_x, y, t)
                        display.drawLine(draw_x, y + lh - 2, draw_x + w, y + lh - 2)
                    else
                        display.drawText(fid, draw_x, y, t)
                    end

                    draw_x = draw_x + w
                end
            end

            -- H1: underline
            if block_type == "h1" then
                display.drawLine(viewport.x, y + lh - 1, viewport.x + viewport.w, y + lh - 1)
            end

            -- H1/H2: 2-line height, others: 1-line
            if block_type == "h1" or block_type == "h2" then
                y = y + lh * 2
            else
                y = y + lh
            end
        end
    )

    -- Close any remaining blockquote border
    if bq_start_y then
        display.drawLine(viewport.x + 4, bq_start_y, viewport.x + 4, y)
        display.drawLine(viewport.x + 5, bq_start_y, viewport.x + 5, y)
    end

    -- Unload any cached header font from this page
    if page_header_fid then
        font.unload(page_header_fid)
        page_header_fid = nil
        page_header_size = nil
    end

    reader_utils.draw_page_chrome(fonts.ui or fid, current_page, total_pages, title)
    display.refresh(1)
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

    local sample = storage.readBytes(path, 0, math.min(256, file_size))
    if sample then
        is_rtl = text_layout.detect_rtl(sample)
        if is_rtl then
            fonts.detect_fallbacks(sample)
        end
    end

    viewport = reader_utils.get_viewport(fonts.reader or fonts.ui)

    if not load_cache() then
        display.clear()
        if fonts.ui then
            display.drawText(fonts.ui, 30, 100, "Indexing...")
        end
        display.refresh(2)

        -- C-side markdown-aware indexing
        local offsets, code_states = text.indexMarkdownPages(
            fonts.reader or fonts.ui, path,
            viewport.w, viewport.lines_per_page)

        if offsets and code_states then
            page_offsets = offsets
            page_code_state = code_states
            total_pages = #page_offsets
            save_cache()
        else
            system.log("MD indexing failed for " .. path)
            plugin.goHome()
            return
        end
    end

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
    page_code_state = {}
end
