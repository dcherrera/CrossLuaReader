-- text_layout.lua — Shared text layout engine for CrossLua Reader.
-- Word wrapping, UTF-8 chunk safety, RTL detection.
-- Used by TXT and MD reader plugins.

local M = {}

--- Find the largest byte count that doesn't split a UTF-8 sequence.
-- Scans backward from max_bytes to find a valid UTF-8 boundary.
-- @param str The string to check
-- @param max_bytes Maximum byte count
-- @return Safe byte count (<= max_bytes)
function M.utf8_safe_length(str, max_bytes)
    if #str <= max_bytes then return #str end

    local i = max_bytes
    -- Scan backward past continuation bytes (10xxxxxx = 0x80-0xBF)
    while i > 0 and str:byte(i) >= 0x80 and str:byte(i) <= 0xBF do
        i = i - 1
    end
    -- Now i points to the start byte of a multi-byte sequence.
    -- Check if the full sequence fits within max_bytes.
    local b = str:byte(i)
    local seq_len = 1
    if b >= 0xC0 and b <= 0xDF then seq_len = 2
    elseif b >= 0xE0 and b <= 0xEF then seq_len = 3
    elseif b >= 0xF0 and b <= 0xF7 then seq_len = 4
    end
    if i + seq_len - 1 > max_bytes then
        -- Sequence doesn't fit, cut before it
        return i - 1
    end
    return max_bytes
end

--- Detect if text contains RTL characters (Hebrew/Arabic).
-- @param text UTF-8 text to scan
-- @return true if RTL characters found
function M.detect_rtl(text)
    if not text or text == "" then return false end
    for _, cp in utf8.codes(text) do
        if (cp >= 0x0590 and cp <= 0x05FF) or
           (cp >= 0x0600 and cp <= 0x06FF) then
            return true
        end
    end
    return false
end

--- Word-wrap a single line of text (no newlines) to fit viewport width.
-- Measures word-by-word using display.getTextWidth().
-- Breaks long words at UTF-8 character boundaries.
-- @param font_id Font ID for measurement
-- @param text Single line of text (no newlines)
-- @param viewport_width Maximum width in pixels
-- @return Table of wrapped display line strings
function M.word_wrap(font_id, text, viewport_width)
    if not text or text == "" then return {""} end

    -- If entire line fits, return as-is
    local full_w = display.getTextWidth(font_id, text)
    if full_w <= viewport_width then return {text} end

    local space_w = display.getTextWidth(font_id, " ")
    local lines = {}
    local current = ""
    local current_w = 0

    for word in text:gmatch("%S+") do
        local word_w = display.getTextWidth(font_id, word)

        -- Word itself exceeds viewport — break at character boundaries
        if word_w > viewport_width then
            -- Flush current line first
            if current ~= "" then
                lines[#lines + 1] = current
                current = ""
                current_w = 0
            end

            -- Break the long word
            local remaining = word
            while remaining ~= "" do
                local fit = ""
                local fit_w = 0
                for _, cp in utf8.codes(remaining) do
                    local ch = utf8.char(cp)
                    local test_w = display.getTextWidth(font_id, fit .. ch)
                    if test_w > viewport_width and fit ~= "" then
                        break
                    end
                    fit = fit .. ch
                    fit_w = test_w
                end
                if fit == "" then
                    -- Single character wider than viewport (shouldn't happen)
                    fit = remaining:sub(1, 1)
                    fit_w = display.getTextWidth(font_id, fit)
                end
                lines[#lines + 1] = fit
                remaining = remaining:sub(#fit + 1)
            end
        elseif current_w + (current_w > 0 and space_w or 0) + word_w > viewport_width then
            -- Word doesn't fit on current line — flush and start new line
            if current ~= "" then
                lines[#lines + 1] = current
            end
            current = word
            current_w = word_w
        else
            -- Word fits — append to current line
            if current == "" then
                current = word
                current_w = word_w
            else
                current = current .. " " .. word
                current_w = current_w + space_w + word_w
            end
        end
    end

    -- Flush remaining
    if current ~= "" then
        lines[#lines + 1] = current
    end

    return #lines > 0 and lines or {""}
end

--- Process a text chunk into wrapped display lines with byte tracking.
-- Handles \r\n and \n line endings. Stops at max_lines.
-- @param font_id Font ID for measurement
-- @param chunk Raw text chunk (may contain newlines)
-- @param viewport_width Maximum width in pixels
-- @param max_lines Maximum number of display lines to produce
-- @return lines (table of strings), bytes_consumed (int)
function M.lines_from_chunk(font_id, chunk, viewport_width, max_lines)
    local lines = {}
    local pos = 1
    local chunk_len = #chunk

    while pos <= chunk_len and #lines < max_lines do
        -- Find next line ending
        local nl_start, nl_end = chunk:find("\r?\n", pos)

        local source_line
        local next_pos

        if nl_start then
            source_line = chunk:sub(pos, nl_start - 1)
            next_pos = nl_end + 1
        else
            -- No more newlines — rest of chunk is one line
            source_line = chunk:sub(pos)
            next_pos = chunk_len + 1
        end

        -- Word-wrap this source line
        local wrapped = M.word_wrap(font_id, source_line, viewport_width)

        for _, wline in ipairs(wrapped) do
            if #lines >= max_lines then
                -- We've hit the limit mid-source-line.
                -- Return bytes consumed up to where we stopped in this source line.
                -- We consumed the previous complete source lines, but not this one fully.
                -- For simplicity, return pos (start of this source line) so the next
                -- page re-processes it. The re-processing will skip already-shown lines.
                -- Actually, we need to be smarter: return the byte offset of the
                -- beginning of this source line so pagination can continue from here.
                return lines, pos - 1
            end
            lines[#lines + 1] = wline
        end

        pos = next_pos
    end

    return lines, pos - 1
end

return M
