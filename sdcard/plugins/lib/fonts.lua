-- fonts.lua — System font manager for CrossLua Reader.
-- Loads UI and reader fonts based on settings. All plugins use
-- fonts.ui and fonts.reader instead of loading fonts directly.
-- Supports fallback fonts for non-Latin scripts (Phase 7).

local M = {}

M.ui = nil        -- Ubuntu 12 UI font ID (always loaded)
M.reader = nil    -- Reader font ID (from settings: family + size)
M.fallback = nil  -- Fallback font ID for non-Latin scripts

-- Unicode script ranges for auto-detection
local SCRIPT_RANGES = {
    hebrew = {0x0590, 0x05FF},
    arabic = {0x0600, 0x06FF},
}

-- Script → language code mapping for font path resolution
local SCRIPT_LANG = {
    hebrew = "he",
    arabic = "ar",
}

--- Scan text for non-Latin scripts.
-- @param text_sample UTF-8 text to analyze
-- @return Table of script names found (e.g. {"hebrew"})
function M.detect_scripts(text_sample)
    if not text_sample or text_sample == "" then return {} end

    local found = {}
    local seen = {}
    for _, cp in utf8.codes(text_sample) do
        for script, range in pairs(SCRIPT_RANGES) do
            if cp >= range[1] and cp <= range[2] and not seen[script] then
                seen[script] = true
                found[#found + 1] = script
            end
        end
    end
    return found
end

--- Load a fallback font for a specific script.
-- Looks in the language pack fonts directory.
-- @param script Script name (e.g. "hebrew")
-- @return true if fallback loaded successfully
function M.load_fallback_for_script(script)
    local settings = require("lib.settings")
    local lang_code = SCRIPT_LANG[script]
    if not lang_code then return false end

    -- Determine font family from language pack
    local lang = require("lib.lang")
    local family = lang.get_font_family()
    if not family then
        -- Default family per script
        if script == "hebrew" then family = "NotoSansHebrew" end
    end
    if not family then return false end

    local size = settings.get("fontSize", "14")
    local path = "/languages/" .. lang_code .. "/fonts/" .. family .. "-" .. size .. "-Regular.cfont"

    if not storage.exists(path) then
        system.log("Fallback font not found: " .. path)
        return false
    end

    -- Unload existing fallback
    if M.fallback then
        if M.reader then font.clearFallback(M.reader) end
        font.unload(M.fallback)
        M.fallback = nil
    end

    M.fallback = font.load(path)
    if not M.fallback then
        system.log("Failed to load fallback: " .. path)
        return false
    end

    -- Set as fallback for reader font
    if M.reader then
        font.setFallback(M.reader, M.fallback)
    end

    system.log("Fallback loaded: " .. path)
    return true
end

--- Auto-detect scripts in text and load appropriate fallback.
-- @param text_sample UTF-8 text to analyze
function M.detect_fallbacks(text_sample)
    local scripts = M.detect_scripts(text_sample)
    for _, script in ipairs(scripts) do
        M.load_fallback_for_script(script)
        return  -- only one fallback font at a time (4-slot limit)
    end
end

--- Initialize fonts based on current settings.
-- Call in every plugin's onEnter(). Safe to call multiple times.
function M.init()
    local settings = require("lib.settings")

    -- UI font: always Ubuntu 12
    if not M.ui then
        M.ui = font.load("/fonts/Ubuntu-12-Regular.cfont")
        if not M.ui then
            system.log("WARN: Failed to load UI font, trying NotoSans")
            M.ui = font.load("/fonts/NotoSans-12-Regular.cfont")
        end
    end

    -- Reader font: from settings
    if not M.reader then
        local family = settings.get("fontFamily", "NotoSans")
        local size = settings.get("fontSize", "14")
        local path = "/fonts/" .. family .. "-" .. size .. "-Regular.cfont"

        M.reader = font.load(path)
        if not M.reader then
            system.log("WARN: Failed to load reader font " .. path .. ", falling back")
            M.reader = font.load("/fonts/NotoSans-14-Regular.cfont")
        end
    end

    -- Auto-load fallback based on language setting
    if not M.fallback then
        local lang_code = settings.get("language", "en")
        if lang_code == "he" then
            M.load_fallback_for_script("hebrew")
        elseif lang_code == "ar" then
            M.load_fallback_for_script("arabic")
        end
    end

    if not M.ui and not M.reader then
        system.log("ERROR: No fonts available!")
    end
end

--- Unload all fonts. Call in every plugin's onExit().
function M.cleanup()
    if M.reader and M.fallback then
        font.clearFallback(M.reader)
    end
    if M.fallback then
        font.unload(M.fallback)
        M.fallback = nil
    end
    if M.ui then
        font.unload(M.ui)
        M.ui = nil
    end
    if M.reader then
        font.unload(M.reader)
        M.reader = nil
    end
end

--- Reload reader font after settings change (e.g., font family or size changed).
-- Re-establishes fallback on the new reader font.
function M.reload_reader()
    -- Clear fallback from old reader
    local had_fallback = M.fallback ~= nil
    if M.reader and M.fallback then
        font.clearFallback(M.reader)
    end

    -- Unload and reload reader
    if M.reader then
        font.unload(M.reader)
        M.reader = nil
    end

    local settings = require("lib.settings")
    local family = settings.get("fontFamily", "NotoSans")
    local size = settings.get("fontSize", "14")
    local path = "/fonts/" .. family .. "-" .. size .. "-Regular.cfont"

    M.reader = font.load(path)
    if not M.reader then
        system.log("WARN: Failed to reload reader font " .. path)
        M.reader = font.load("/fonts/NotoSans-14-Regular.cfont")
    end

    -- Re-establish fallback
    if had_fallback and M.reader and M.fallback then
        -- Fallback font size may have changed, reload it
        font.unload(M.fallback)
        M.fallback = nil
        local lang_code = settings.get("language", "en")
        if lang_code == "he" then
            M.load_fallback_for_script("hebrew")
        elseif lang_code == "ar" then
            M.load_fallback_for_script("arabic")
        end
    end
end

return M
