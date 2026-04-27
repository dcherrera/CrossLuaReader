-- md_reader.lua — Markdown reader for CrossLua Reader.
-- Supports headers, lists, blockquotes, code blocks, bold, italic, links.

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
local page_code_state = {}  -- true/false: was in_code_block at page start
local current_page = 1
local total_pages = 0
local viewport = nil
local needs_render = true
local is_rtl = false
local title = ""

local CHUNK_SIZE = 8192
local CACHE_MAGIC = "MDRI2"

-- ══════════════════════════════════════════════════════════════════
-- MARKDOWN PARSING
-- ══════════════════════════════════════════════════════════════════

--- Parse a source line into a block-level element.
-- @return {type, text, indent, ordered, number}
local function parse_block(line, in_code_block)
    -- Code fence toggle
    if line:match("^```") then
        if in_code_block then
            return {type = "code_end"}, false
        else
            return {type = "code_start"}, true
        end
    end

    if in_code_block then
        return {type = "code", text = line}, true
    end

    -- Blank line
    if line:match("^%s*$") then
        return {type = "blank"}, false
    end

    -- Horizontal rule
    if line:match("^%-%-%-+%s*$") or line:match("^%*%*%*+%s*$") or line:match("^___+%s*$") then
        return {type = "hr"}, false
    end

    -- Headers
    local h3 = line:match("^###%s+(.+)")
    if h3 then return {type = "h3", text = h3}, false end

    local h2 = line:match("^##%s+(.+)")
    if h2 then return {type = "h2", text = h2}, false end

    local h1 = line:match("^#%s+(.+)")
    if h1 then return {type = "h1", text = h1}, false end

    -- Blockquote
    local bq = line:match("^>%s?(.*)")
    if bq then return {type = "blockquote", text = bq}, false end

    -- Unordered list
    local indent, ul = line:match("^(%s*)[%-*+]%s+(.*)")
    if ul then
        local depth = math.floor(#indent / 2)
        return {type = "list", text = ul, indent = depth, ordered = false}, false
    end

    -- Ordered list
    local oi, num, ol = line:match("^(%s*)(%d+)%.%s+(.*)")
    if ol then
        local depth = math.floor(#oi / 2)
        return {type = "list", text = ol, indent = depth, ordered = true, number = tonumber(num)}, false
    end

    -- Regular paragraph
    return {type = "paragraph", text = line}, false
end

--- Parse inline markdown spans from text.
-- Returns list of {text, style} where style is "normal", "bold", "italic", "code", "link"
local function parse_inline(text)
    if not text or text == "" then return {{text = "", style = "normal"}} end

    local spans = {}
    local pos = 1
    local len = #text

    while pos <= len do
        -- Inline code: `text`
        if text:sub(pos, pos) == "`" then
            local close = text:find("`", pos + 1, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 1, close - 1), style = "code"}
                pos = close + 1
            else
                spans[#spans + 1] = {text = "`", style = "normal"}
                pos = pos + 1
            end
        -- Bold: **text** or __text__
        elseif text:sub(pos, pos + 1) == "**" then
            local close = text:find("**", pos + 2, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 2, close - 1), style = "bold"}
                pos = close + 2
            else
                spans[#spans + 1] = {text = "*", style = "normal"}
                pos = pos + 1
            end
        elseif text:sub(pos, pos + 1) == "__" then
            local close = text:find("__", pos + 2, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 2, close - 1), style = "bold"}
                pos = close + 2
            else
                spans[#spans + 1] = {text = "_", style = "normal"}
                pos = pos + 1
            end
        -- Italic: *text* or _text_
        elseif text:sub(pos, pos) == "*" and text:sub(pos + 1, pos + 1) ~= "*" then
            local close = text:find("*", pos + 1, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 1, close - 1), style = "italic"}
                pos = close + 1
            else
                spans[#spans + 1] = {text = "*", style = "normal"}
                pos = pos + 1
            end
        elseif text:sub(pos, pos) == "_" and text:sub(pos + 1, pos + 1) ~= "_" then
            local close = text:find("_", pos + 1, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 1, close - 1), style = "italic"}
                pos = close + 1
            else
                spans[#spans + 1] = {text = "_", style = "normal"}
                pos = pos + 1
            end
        -- Link: [text](url)
        elseif text:sub(pos, pos) == "[" then
            local close_bracket = text:find("]", pos + 1, true)
            if close_bracket and text:sub(close_bracket + 1, close_bracket + 1) == "(" then
                local close_paren = text:find(")", close_bracket + 2, true)
                if close_paren then
                    spans[#spans + 1] = {text = text:sub(pos + 1, close_bracket - 1), style = "link"}
                    pos = close_paren + 1
                else
                    spans[#spans + 1] = {text = "[", style = "normal"}
                    pos = pos + 1
                end
            else
                spans[#spans + 1] = {text = "[", style = "normal"}
                pos = pos + 1
            end
        else
            -- Collect normal text until next special character
            local next_special = len + 1
            for _, ch in ipairs({"`", "*", "_", "["}) do
                local found = text:find(ch, pos + 1, true)
                if found and found < next_special then
                    next_special = found
                end
            end
            spans[#spans + 1] = {text = text:sub(pos, next_special - 1), style = "normal"}
            pos = next_special
        end
    end

    return spans
end

-- ══════════════════════════════════════════════════════════════════
-- RENDERING
-- ══════════════════════════════════════════════════════════════════

--- Render inline spans with styling at position x, y.
-- Returns x position after rendering.
local function render_spans(font_id, spans, x, y, line_height)
    for _, span in ipairs(spans) do
        if span.text == "" then goto continue end

        local w = display.getTextWidth(font_id, span.text)

        if span.style == "bold" then
            display.drawText(font_id, x, y, span.text)
            display.drawText(font_id, x + 1, y, span.text)  -- double-strike
        elseif span.style == "italic" then
            display.drawText(font_id, x, y, span.text)
            display.drawLine(x, y + line_height - 2, x + w, y + line_height - 2)  -- underline
        elseif span.style == "code" then
            display.drawRect(x - 2, y - 1, w + 4, line_height + 2)
            display.drawText(font_id, x, y, span.text)
        elseif span.style == "link" then
            display.drawText(font_id, x, y, span.text)
            display.drawLine(x, y + line_height - 2, x + w, y + line_height - 2)
        else
            display.drawText(font_id, x, y, span.text)
        end

        x = x + w
        ::continue::
    end
    return x
end

--- Get plain text from inline spans (for word wrap measurement).
local function spans_plain_text(text)
    local plain = text
    plain = plain:gsub("`([^`]*)`", "%1")
    plain = plain:gsub("%*%*(.-)%*%*", "%1")
    plain = plain:gsub("__(.-)__", "%1")
    plain = plain:gsub("%*(.-)%*", "%1")
    plain = plain:gsub("_(.-)_", "%1")
    plain = plain:gsub("%[(.-)%]%(.-%)","% 1")
    return plain
end

-- ══════════════════════════════════════════════════════════════════
-- PAGE INDEXING
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

--- Estimate display lines for a text block without calling getTextWidth.
-- Uses average character width estimation (~7px per char at size 14).
-- Fast: no SD reads, no font system calls.
local function estimate_wrap_lines(text, avail_width)
    if not text or text == "" then return 1 end
    local plain = spans_plain_text(text)
    local chars_per_line = math.max(1, math.floor(avail_width / 7))
    return math.max(1, math.ceil(utf8.len(plain) / chars_per_line))
end

local function build_page_index()
    display.clear()
    if fonts.ui then
        display.drawText(fonts.ui, 30, 100, "Indexing...")
    end
    display.refresh(2)

    page_offsets = {0}
    page_code_state = {false}
    local offset = 0
    local in_code = false
    local display_lines_on_page = 0

    while offset < file_size do
        local read_len = math.min(CHUNK_SIZE, file_size - offset)
        local chunk = storage.readBytes(file_path, offset, read_len)
        if not chunk or #chunk == 0 then break end

        local safe_len = text_layout.utf8_safe_length(chunk, #chunk)
        chunk = chunk:sub(1, safe_len)

        local pos = 1
        while pos <= #chunk do
            local nl = chunk:find("\n", pos, true)
            local line
            if nl then
                line = chunk:sub(pos, nl - 1):gsub("\r$", "")
                pos = nl + 1
            else
                line = chunk:sub(pos)
                pos = #chunk + 1
            end

            local block
            block, in_code = parse_block(line, in_code)

            -- Estimate display lines (fast, no font measurement)
            local block_lines = 0
            if block.type == "blank" then
                block_lines = 1
            elseif block.type == "hr" then
                block_lines = 1
            elseif block.type == "code_start" or block.type == "code_end" then
                block_lines = 0
            elseif block.type == "h1" or block.type == "h2" then
                block_lines = estimate_wrap_lines(block.text, viewport.w) + 1
            elseif block.type == "h3" then
                block_lines = estimate_wrap_lines(block.text, viewport.w)
            elseif block.type == "list" then
                local indent_px = (block.indent + 1) * 20
                block_lines = estimate_wrap_lines(block.text, viewport.w - indent_px)
            elseif block.type == "blockquote" then
                block_lines = estimate_wrap_lines(block.text, viewport.w - 20)
            elseif block.type == "code" then
                block_lines = 1
            else
                block_lines = estimate_wrap_lines(block.text, viewport.w)
            end

            display_lines_on_page = display_lines_on_page + block_lines

            if display_lines_on_page >= viewport.lines_per_page then
                local page_start = offset + pos - 1
                if page_start < file_size then
                    page_offsets[#page_offsets + 1] = page_start
                    page_code_state[#page_code_state + 1] = in_code
                end
                display_lines_on_page = 0
            end
        end

        offset = offset + #chunk

        if #page_offsets % 100 == 0 then
            system.delay(1)
        end
    end

    total_pages = #page_offsets
    system.log("MD indexed " .. total_pages .. " pages for " .. file_path)
end

-- ══════════════════════════════════════════════════════════════════
-- PAGE RENDERING
-- ══════════════════════════════════════════════════════════════════

local function render()
    if total_pages == 0 then return end

    local offset = page_offsets[current_page] or 0
    local in_code = page_code_state[current_page] or false
    local read_len = math.min(CHUNK_SIZE, file_size - offset)
    local chunk = storage.readBytes(file_path, offset, read_len)

    if not chunk or #chunk == 0 then return end

    local safe_len = text_layout.utf8_safe_length(chunk, #chunk)
    chunk = chunk:sub(1, safe_len)

    display.clear()

    local fid = fonts.reader or fonts.ui
    local lh = viewport.line_height
    local y = viewport.y
    local lines_drawn = 0

    -- Process line by line
    local pos = 1
    while pos <= #chunk and lines_drawn < viewport.lines_per_page do
        local nl = chunk:find("\n", pos, true)
        local line
        if nl then
            line = chunk:sub(pos, nl - 1):gsub("\r$", "")
            pos = nl + 1
        else
            line = chunk:sub(pos)
            pos = #chunk + 1
        end

        local block
        block, in_code = parse_block(line, in_code)

        if block.type == "blank" then
            y = y + lh
            lines_drawn = lines_drawn + 1

        elseif block.type == "hr" then
            display.drawLine(viewport.x, y + lh / 2, viewport.x + viewport.w, y + lh / 2)
            y = y + lh
            lines_drawn = lines_drawn + 1

        elseif block.type == "code_start" or block.type == "code_end" then
            -- No visual output for fence markers

        elseif block.type == "code" then
            -- Gray background for code
            display.fillRoundedRectGray(viewport.x, y, viewport.w, lh, 0)
            display.drawText(fid, viewport.x + 8, y, block.text)
            y = y + lh
            lines_drawn = lines_drawn + 1

        elseif block.type == "h1" or block.type == "h2" or block.type == "h3" then
            local text = block.text or ""
            local wrapped = text_layout.word_wrap(fid, spans_plain_text(text), viewport.w)

            -- Extra spacing before header
            if block.type ~= "h3" and lines_drawn > 0 then
                y = y + lh / 2
                lines_drawn = lines_drawn + 1
            end

            for _, wline in ipairs(wrapped) do
                if lines_drawn >= viewport.lines_per_page then break end
                local spans = parse_inline(wline)
                -- Bold double-strike for all header text
                local x = viewport.x
                for _, span in ipairs(spans) do
                    if span.text ~= "" then
                        display.drawText(fid, x, y, span.text)
                        display.drawText(fid, x + 1, y, span.text)
                        x = x + display.getTextWidth(fid, span.text)
                    end
                end
                y = y + lh
                lines_drawn = lines_drawn + 1
            end

            -- Underline for h1/h2
            if block.type == "h1" or block.type == "h2" then
                display.drawLine(viewport.x, y - 2, viewport.x + viewport.w, y - 2)
            end

        elseif block.type == "blockquote" then
            local text = block.text or ""
            local wrapped = text_layout.word_wrap(fid, spans_plain_text(text), viewport.w - 20)
            for _, wline in ipairs(wrapped) do
                if lines_drawn >= viewport.lines_per_page then break end
                -- Left border
                display.drawLine(viewport.x + 4, y, viewport.x + 4, y + lh)
                display.drawLine(viewport.x + 5, y, viewport.x + 5, y + lh)
                local spans = parse_inline(wline)
                render_spans(fid, spans, viewport.x + 16, y, lh)
                y = y + lh
                lines_drawn = lines_drawn + 1
            end

        elseif block.type == "list" then
            local indent_px = (block.indent + 1) * 20
            local text = block.text or ""
            local wrapped = text_layout.word_wrap(fid, spans_plain_text(text), viewport.w - indent_px)

            for j, wline in ipairs(wrapped) do
                if lines_drawn >= viewport.lines_per_page then break end
                local x = viewport.x + indent_px

                -- Bullet/number on first wrapped line only
                if j == 1 then
                    if block.ordered and block.number then
                        display.drawText(fid, viewport.x + indent_px - 16, y, block.number .. ".")
                    else
                        display.fillRect(viewport.x + indent_px - 10, y + lh / 2 - 2, 4, 4)
                    end
                end

                local spans = parse_inline(wline)
                render_spans(fid, spans, x, y, lh)
                y = y + lh
                lines_drawn = lines_drawn + 1
            end

        else -- paragraph
            local text = block.text or ""
            local wrapped = text_layout.word_wrap(fid, spans_plain_text(text), viewport.w)
            for _, wline in ipairs(wrapped) do
                if lines_drawn >= viewport.lines_per_page then break end
                local spans = parse_inline(wline)

                local x = viewport.x
                if is_rtl then
                    -- Measure total width for right-alignment
                    local total_w = 0
                    for _, s in ipairs(spans) do
                        total_w = total_w + display.getTextWidth(fid, s.text)
                    end
                    x = viewport.x + viewport.w - total_w
                end

                render_spans(fid, spans, x, y, lh)
                y = y + lh
                lines_drawn = lines_drawn + 1
            end
        end
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

    local sample = storage.readBytes(path, 0, math.min(256, file_size))
    if sample then
        is_rtl = text_layout.detect_rtl(sample)
        if is_rtl then
            fonts.detect_fallbacks(sample)
        end
    end

    viewport = reader_utils.get_viewport(fonts.reader or fonts.ui)

    if not load_cache() then
        build_page_index()
        save_cache()
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
