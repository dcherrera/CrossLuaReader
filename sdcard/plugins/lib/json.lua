-- json.lua — Minimal recursive-descent JSON parser for CrossLua Reader.
-- Handles objects, arrays, strings, numbers, booleans, null.
-- Returns Lua tables. No encoder (settings.lua handles writes).

local M = {}

local function skip_ws(s, i)
    return s:match("^%s*()", i)
end

local function parse_string(s, i)
    -- i points at the opening quote
    local j = i + 1
    local parts = {}
    while j <= #s do
        local c = s:sub(j, j)
        if c == '"' then
            return table.concat(parts), j + 1
        elseif c == '\\' then
            j = j + 1
            local esc = s:sub(j, j)
            if esc == 'n' then parts[#parts + 1] = '\n'
            elseif esc == 't' then parts[#parts + 1] = '\t'
            elseif esc == 'r' then parts[#parts + 1] = '\r'
            elseif esc == 'u' then
                local hex = s:sub(j + 1, j + 4)
                local cp = tonumber(hex, 16)
                if cp then
                    parts[#parts + 1] = utf8.char(cp)
                end
                j = j + 4
            else
                parts[#parts + 1] = esc
            end
        else
            parts[#parts + 1] = c
        end
        j = j + 1
    end
    return nil, i
end

local parse_value  -- forward declaration

local function parse_object(s, i)
    i = skip_ws(s, i + 1)  -- skip '{'
    local obj = {}
    if s:sub(i, i) == '}' then return obj, i + 1 end

    while true do
        i = skip_ws(s, i)
        if s:sub(i, i) ~= '"' then return nil, i end
        local key
        key, i = parse_string(s, i)
        if not key then return nil, i end

        i = skip_ws(s, i)
        if s:sub(i, i) ~= ':' then return nil, i end
        i = skip_ws(s, i + 1)

        local val
        val, i = parse_value(s, i)
        obj[key] = val

        i = skip_ws(s, i)
        local c = s:sub(i, i)
        if c == '}' then return obj, i + 1 end
        if c ~= ',' then return nil, i end
        i = i + 1
    end
end

local function parse_array(s, i)
    i = skip_ws(s, i + 1)  -- skip '['
    local arr = {}
    if s:sub(i, i) == ']' then return arr, i + 1 end

    while true do
        i = skip_ws(s, i)
        local val
        val, i = parse_value(s, i)
        arr[#arr + 1] = val

        i = skip_ws(s, i)
        local c = s:sub(i, i)
        if c == ']' then return arr, i + 1 end
        if c ~= ',' then return nil, i end
        i = i + 1
    end
end

local function parse_number(s, i)
    local j = s:match("^%-?%d+%.?%d*[eE]?[+-]?%d*()", i)
    if not j then return nil, i end
    return tonumber(s:sub(i, j - 1)), j
end

parse_value = function(s, i)
    i = skip_ws(s, i)
    local c = s:sub(i, i)

    if c == '"' then return parse_string(s, i)
    elseif c == '{' then return parse_object(s, i)
    elseif c == '[' then return parse_array(s, i)
    elseif c == 't' then return true, i + 4
    elseif c == 'f' then return false, i + 5
    elseif c == 'n' then return nil, i + 4
    else return parse_number(s, i)
    end
end

--- Parse a JSON string into a Lua value.
-- @param str JSON string
-- @return Parsed value (table, string, number, boolean, or nil)
function M.decode(str)
    if not str or str == "" then return nil end
    local val, _ = parse_value(str, 1)
    return val
end

return M
