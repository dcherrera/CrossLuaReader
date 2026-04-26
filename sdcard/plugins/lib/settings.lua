-- settings.lua — Persistent settings module for CrossLua Reader.
-- Reads/writes /settings.json on SD card. All plugins use this
-- instead of handling JSON directly.

local M = {}

local SETTINGS_FILE = "/settings.json"

-- In-memory settings table
local data = {}
local loaded = false

-- Defaults for first boot (no settings.json exists)
local defaults = {
    fontFamily = "NotoSans",
    fontSize = "14",
    orientation = 0,
    screenMargin = 10,
    refreshFrequency = 15,
    theme = "lyra",
    buttonLayout = "default",
    sleepTimeout = 10,
    language = "en",
}

-- ── Simple JSON parser for flat objects ─────────────────────────────

local function parse_json(str)
    local result = {}
    if not str then return result end

    -- Match "key": "value" or "key": number
    for key, val in str:gmatch('"([^"]+)"%s*:%s*"?([^",}]+)"?') do
        -- Try to convert to number
        local num = tonumber(val)
        if num then
            result[key] = num
        else
            -- Strip trailing whitespace
            result[key] = val:match("^%s*(.-)%s*$")
        end
    end

    return result
end

local function to_json(tbl)
    local parts = {"{"}
    local keys = {}
    for k in pairs(tbl) do keys[#keys + 1] = k end
    table.sort(keys)

    for i, k in ipairs(keys) do
        local v = tbl[k]
        local comma = i < #keys and "," or ""
        if type(v) == "number" then
            parts[#parts + 1] = string.format('  "%s": %s%s', k, tostring(v), comma)
        else
            parts[#parts + 1] = string.format('  "%s": "%s"%s', k, tostring(v), comma)
        end
    end

    parts[#parts + 1] = "}"
    return table.concat(parts, "\n")
end

-- ── Public API ──────────────────────────────────────────────────────

--- Load settings from SD card. Call once at boot (in home plugin's onEnter).
-- If no settings file exists, uses defaults.
function M.load()
    if loaded then return end

    -- Start with defaults
    for k, v in pairs(defaults) do
        data[k] = v
    end

    -- Overlay with saved values
    local content = storage.read(SETTINGS_FILE)
    if content then
        local saved = parse_json(content)
        for k, v in pairs(saved) do
            data[k] = v
        end
        system.log("Settings loaded from " .. SETTINGS_FILE)
    else
        system.log("No settings file — using defaults")
    end

    loaded = true
end

--- Get a setting value.
-- @param key Setting key name
-- @param default Value to return if key not found (optional)
-- @return Setting value, or default, or nil
function M.get(key, default)
    if not loaded then M.load() end
    local val = data[key]
    if val == nil then return default end
    return val
end

--- Set a setting value in memory.
-- Call save() to persist to SD.
-- @param key Setting key name
-- @param value New value
function M.set(key, value)
    data[key] = value
end

--- Write current settings to SD card.
function M.save()
    local json = to_json(data)
    if storage.write(SETTINGS_FILE, json) then
        system.log("Settings saved")
    else
        system.log("ERROR: Failed to save settings")
    end
end

--- Get all settings as a table (for debugging).
-- @return Copy of settings table
function M.get_all()
    local copy = {}
    for k, v in pairs(data) do copy[k] = v end
    return copy
end

--- Reset all settings to defaults and save.
function M.reset()
    data = {}
    for k, v in pairs(defaults) do
        data[k] = v
    end
    M.save()
    system.log("Settings reset to defaults")
end

return M
