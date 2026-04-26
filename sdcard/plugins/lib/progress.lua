-- progress.lua — Reading progress persistence for CrossLua Reader.
-- Stores progress alongside book files as {book_path}_progress.

local M = {}

--- Save reading progress for a book.
-- @param book_path Path to the book file on SD
-- @param data Table with progress fields: {page, totalPages, offset, chapter, percentage}
function M.save(book_path, data)
    if not book_path or not data then return false end

    local progress_path = book_path .. "_progress"
    local parts = {"{"}
    local keys = {"page", "totalPages", "offset", "chapter", "percentage"}

    local count = 0
    for _, k in ipairs(keys) do
        if data[k] ~= nil then count = count + 1 end
    end

    local i = 0
    for _, k in ipairs(keys) do
        if data[k] ~= nil then
            i = i + 1
            local comma = i < count and "," or ""
            parts[#parts + 1] = string.format('  "%s": %s%s', k, tostring(data[k]), comma)
        end
    end

    parts[#parts + 1] = "}"
    local json = table.concat(parts, "\n")

    if storage.write(progress_path, json) then
        return true
    else
        system.log("ERROR: Failed to save progress for " .. book_path)
        return false
    end
end

--- Load reading progress for a book.
-- @param book_path Path to the book file on SD
-- @return Table with progress fields, or nil if no progress saved
function M.load(book_path)
    if not book_path then return nil end

    local progress_path = book_path .. "_progress"
    local content = storage.read(progress_path)
    if not content then return nil end

    -- Parse simple JSON
    local result = {}
    for key, val in content:gmatch('"([^"]+)"%s*:%s*([%d%.]+)') do
        result[key] = tonumber(val)
    end

    if next(result) == nil then return nil end
    return result
end

--- Delete saved progress for a book.
-- @param book_path Path to the book file on SD
function M.clear(book_path)
    if not book_path then return end
    storage.remove(book_path .. "_progress")
end

return M
