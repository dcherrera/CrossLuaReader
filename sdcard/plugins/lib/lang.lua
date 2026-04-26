-- lang.lua — Language pack loader and translation module.
-- Discovers /languages/{code}/ folders on SD, loads lang.json,
-- provides tr() for UI string translation with English fallback.

local json = require("lib.json")

local M = {}

local current_lang = nil    -- loaded lang.json table for active language
local english_lang = nil    -- English fallback (always loaded)
local discovered = {}       -- list of {code, name, direction, fontFamily}

--- Discover all language packs on SD card.
-- Scans /languages/ for subdirectories containing lang.json.
-- @return Table of discovered packs
function M.discover()
    discovered = {}
    local entries = storage.list("/languages")
    if not entries then return discovered end

    for _, e in ipairs(entries) do
        if e.isDir then
            local path = "/languages/" .. e.name .. "/lang.json"
            local content = storage.read(path)
            if content then
                local lang = json.decode(content)
                if lang and lang.code and lang.name then
                    discovered[#discovered + 1] = {
                        code = lang.code,
                        name = lang.name,
                        direction = lang.direction or "ltr",
                        fontFamily = lang.fontFamily,
                    }
                end
            end
        end
    end

    return discovered
end

--- Get previously discovered language packs.
function M.get_discovered()
    return discovered
end

--- Load a language pack by code.
-- Always loads English as fallback first.
-- @param code Language code (e.g. "en", "he")
-- @return true on success
function M.load(code)
    -- Always load English as fallback
    if not english_lang then
        local en_content = storage.read("/languages/en/lang.json")
        if en_content then
            english_lang = json.decode(en_content)
        end
    end

    if code == "en" then
        current_lang = english_lang
        return current_lang ~= nil
    end

    local path = "/languages/" .. code .. "/lang.json"
    local content = storage.read(path)
    if not content then return false end

    current_lang = json.decode(content)
    return current_lang ~= nil
end

--- Translate a key. Falls back to English, then to the raw key.
-- @param key String key (e.g. "home", "settings")
-- @return Translated string
function M.tr(key)
    if current_lang and current_lang.strings and current_lang.strings[key] then
        return current_lang.strings[key]
    end
    if english_lang and english_lang.strings and english_lang.strings[key] then
        return english_lang.strings[key]
    end
    return key
end

--- Get the current language's font family for fallback loading.
-- @return Font family string (e.g. "NotoSansHebrew") or nil
function M.get_font_family()
    if current_lang then return current_lang.fontFamily end
    return nil
end

--- Get the current language's text direction.
-- @return "ltr" or "rtl"
function M.get_direction()
    if current_lang then return current_lang.direction or "ltr" end
    return "ltr"
end

--- Get the current language code.
function M.get_code()
    if current_lang then return current_lang.code end
    return "en"
end

return M
