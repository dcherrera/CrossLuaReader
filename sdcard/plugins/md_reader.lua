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

-- ══════════════════════════════════════════════════════════════════
-- MARKDOWN PARSING
-- ══════════════════════════════════════════════════════════════════

--- Parse a source line into a block-level element.
local function parse_block(line, in_code_block)
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

    if line:match("^%s*$") then
        return {type = "blank"}, false
    end

    if line:match("^%-%-%-+%s*$") or line:match("^%*%*%*+%s*$") or line:match("^___+%s*$") then
        return {type = "hr"}, false
    end

    local h3 = line:match("^###%s+(.+)")
    if h3 then return {type = "h3", text = h3}, false end

    local h2 = line:match("^##%s+(.+)")
    if h2 then return {type = "h2", text = h2}, false end

    local h1 = line:match("^#%s+(.+)")
    if h1 then return {type = "h1", text = h1}, false end

    local bq = line:match("^>%s?(.*)")
    if bq then return {type = "blockquote", text = bq}, false end

    local indent, ul = line:match("^(%s*)[%-*+]%s+(.*)")
    if ul then
        local depth = math.floor(#indent / 2)
        return {type = "list", text = ul, indent = depth, ordered = false}, false
    end

    local oi, num, ol = line:match("^(%s*)(%d+)%.%s+(.*)")
    if ol then
        local depth = math.floor(#oi / 2)
        return {type = "list", text = ol, indent = depth, ordered = true, number = tonumber(num)}, false
    end

    return {type = "paragraph", text = line}, false
end

--- Strip inline markdown syntax for plain text.
local function strip_md(text)
    if not text then return "" end
    local s = text
    s = s:gsub("`([^`]*)`", "%1")
    s = s:gsub("%*%*(.-)%*%*", "%1")
    s = s:gsub("__(.-)__", "%1")
    s = s:gsub("%*(.-)%*", "%1")
    s = s:gsub("_(.-)_", "%1")
    s = s:gsub("%[(.-)%]%(.-%))", "%1")
    return s
end

--- Parse inline markdown spans from text.
local function parse_inline(text)
    if not text or text == "" then return {{text = "", style = "normal"}} end

    local spans = {}
    local pos = 1
    local len = #text

    while pos <= len do
        if text:sub(pos, pos) == "`" then
            local close = text:find("`", pos + 1, true)
            if close then
                spans[#spans + 1] = {text = text:sub(pos + 1, close - 1), style = "code"}
                pos = close + 1
            else
                spans[#spans + 1] = {text = "`", style = "normal"}
                pos = pos + 1
            end
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
-- RENDERING HELPERS
-- ══════════════════════════════════════════════════════════════════

--- Render styled spans sequentially.
local function render_spans(fid, spans, x, y, lh)
    for _, span in ipairs(spans) do
        if span.text ~= "" then
            local w = display.getTextWidth(fid, span.text)

            if span.style == "bold" then
                display.drawText(fid, x, y, span.text)
                display.drawText(fid, x + 1, y, span.text)
            elseif span.style == "italic" then
                display.drawText(fid, x, y, span.text)
                display.drawLine(x, y + lh - 2, x + w, y + lh - 2)
            elseif span.style == "code" then
                display.drawRect(x - 2, y - 1, w + 4, lh + 2)
                display.drawText(fid, x, y, span.text)
            elseif span.style == "link" then
                display.drawText(fid, x, y, span.text)
                display.drawLine(x, y + lh - 2, x + w, y + lh - 2)
            else
                display.drawText(fid, x, y, span.text)
            end

            x = x + w
        end
    end
    return x
end

--- Render a text block with inline styling using span-aware wrapping.
-- Parses inline spans from original text, concatenates plain text for
-- C-side word wrap, then walks wrapped lines and spans together.
local function render_styled_block(fid, original_text, x, y, lh, avail_w, is_header)
    local lines_drawn = 0

    -- Parse inline spans from original text (with markdown markers)
    local spans = parse_inline(original_text)

    -- Build plain text from spans for wrapping
    local plain_parts = {}
    for _, span in ipairs(spans) do
        plain_parts[#plain_parts + 1] = span.text
    end
    local plain = table.concat(plain_parts)

    -- C-side word wrap on plain text
    local wrapped = text.wrapString(fid, plain, avail_w)
    if not wrapped or #wrapped == 0 then
        wrapped = {""}
    end

    -- Walk wrapped lines + spans together
    local span_idx = 1
    local span_char_offset = 0  -- chars consumed in current span

    for _, wline in ipairs(wrapped) do
        local chars_remaining = utf8.len(wline) or 0
        local draw_x = x

        while chars_remaining > 0 and span_idx <= #spans do
            local span = spans[span_idx]
            local span_text = span.text
            local span_total_chars = utf8.len(span_text) or 0
            local span_chars_left = span_total_chars - span_char_offset
            local take = math.min(chars_remaining, span_chars_left)

            if take > 0 then
                -- Extract portion of this span for this line
                local portion
                if span_char_offset == 0 and take == span_total_chars then
                    portion = span_text
                else
                    -- UTF-8 aware substring
                    local byte_start = utf8.offset(span_text, span_char_offset + 1) or 1
                    local byte_end = utf8.offset(span_text, span_char_offset + take + 1)
                    if byte_end then
                        portion = span_text:sub(byte_start, byte_end - 1)
                    else
                        portion = span_text:sub(byte_start)
                    end
                end

                local pw = display.getTextWidth(fid, portion)

                -- Render with style
                if is_header or span.style == "bold" then
                    display.drawText(fid, draw_x, y, portion)
                    display.drawText(fid, draw_x + 1, y, portion)
                elseif span.style == "italic" then
                    display.drawText(fid, draw_x, y, portion)
                    display.drawLine(draw_x, y + lh - 2, draw_x + pw, y + lh - 2)
                elseif span.style == "code" then
                    display.drawRect(draw_x - 2, y - 1, pw + 4, lh + 2)
                    display.drawText(fid, draw_x, y, portion)
                elseif span.style == "link" then
                    display.drawText(fid, draw_x, y, portion)
                    display.drawLine(draw_x, y + lh - 2, draw_x + pw, y + lh - 2)
                else
                    display.drawText(fid, draw_x, y, portion)
                end

                draw_x = draw_x + pw
                span_char_offset = span_char_offset + take
                chars_remaining = chars_remaining - take
            end

            if span_char_offset >= span_total_chars then
                span_idx = span_idx + 1
                span_char_offset = 0
            end
        end

        y = y + lh
        lines_drawn = lines_drawn + 1
    end

    return lines_drawn, y
end

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
            -- no visual output

        elseif block.type == "code" then
            -- Code block: indent + left border, NO gray fill
            display.drawLine(viewport.x + 2, y, viewport.x + 2, y + lh)
            display.drawLine(viewport.x + 3, y, viewport.x + 3, y + lh)
            display.drawText(fid, viewport.x + 12, y, block.text)
            y = y + lh
            lines_drawn = lines_drawn + 1

        elseif block.type == "h1" or block.type == "h2" or block.type == "h3" then
            -- Extra spacing before headers
            if lines_drawn > 0 and (block.type == "h1" or block.type == "h2") then
                y = y + lh / 2
                lines_drawn = lines_drawn + 1
                if lines_drawn >= viewport.lines_per_page then break end
            end

            local drawn, new_y = render_styled_block(fid, block.text, viewport.x, y, lh, viewport.w, true)
            lines_drawn = lines_drawn + drawn
            y = new_y

            -- Underline for h1/h2
            if block.type == "h1" or block.type == "h2" then
                display.drawLine(viewport.x, y - 2, viewport.x + viewport.w, y - 2)
            end

        elseif block.type == "blockquote" then
            local bq_x = viewport.x + 16
            local bq_w = viewport.w - 20
            local start_y = y

            local drawn, new_y = render_styled_block(fid, block.text, bq_x, y, lh, bq_w, false)

            -- Left border for all blockquote lines
            display.drawLine(viewport.x + 4, start_y, viewport.x + 4, new_y)
            display.drawLine(viewport.x + 5, start_y, viewport.x + 5, new_y)

            lines_drawn = lines_drawn + drawn
            y = new_y

        elseif block.type == "list" then
            local indent_px = (block.indent + 1) * 20
            local list_x = viewport.x + indent_px
            local list_w = viewport.w - indent_px

            -- Bullet on first line
            if block.ordered and block.number then
                display.drawText(fid, viewport.x + indent_px - 16, y, block.number .. ".")
            else
                display.fillRect(viewport.x + indent_px - 10, y + lh / 2 - 2, 4, 4)
            end

            local drawn, new_y = render_styled_block(fid, block.text, list_x, y, lh, list_w, false)
            lines_drawn = lines_drawn + drawn
            y = new_y

        else -- paragraph
            local px = viewport.x
            local pw = viewport.w

            if is_rtl then
                -- Right-align for RTL
                local stripped = strip_md(block.text or "")
                local tw = display.getTextWidth(fid, stripped)
                if tw < pw then
                    px = viewport.x + pw - tw
                end
            end

            local drawn, new_y = render_styled_block(fid, block.text or "", px, y, lh, pw, false)
            lines_drawn = lines_drawn + drawn
            y = new_y
        end
    end

    reader_utils.draw_page_chrome(fonts.ui or fid, current_page, total_pages, title)
    display.refresh(1)  -- half refresh for clean page turns
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
