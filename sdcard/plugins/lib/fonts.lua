-- fonts.lua — System font manager for CrossLua Reader.
-- Loads UI and reader fonts based on settings. All plugins use
-- fonts.ui and fonts.reader instead of loading fonts directly.
-- Supports fallback fonts for non-Latin scripts (Phase 7).
-- Font packs: drop a folder in /fonts/ with .cfont files inside.

local M = {}

-- Resolve a font path: /fonts/{family}/{family}-{size}-{style}.cfont
local function font_path(family, size, style)
    return "/fonts/" .. family .. "/" .. family .. "-" .. size .. "-" .. style .. ".cfont"
end

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
    if not lang_code then
        system.log("FALLBACK: no lang_code for script " .. script)
        return false
    end

    -- Determine font family from language pack
    local lang = require("lib.lang")
    local family = lang.get_font_family()
    system.log("FALLBACK: script=" .. script .. " lang_code=" .. lang_code .. " family=" .. tostring(family))
    if not family then
        -- Default family per script
        if script == "hebrew" then family = "NotoSansHebrew" end
    end
    if not family then return false end

    local size = settings.get("fontSize", "14")
    local path = "/languages/" .. lang_code .. "/fonts/" .. family .. "-" .. size .. "-Regular.cfont"
    system.log("FALLBACK: trying path " .. path)

    if not storage.exists(path) then
        system.log("FALLBACK: file not found: " .. path)
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

    -- Set as fallback for both UI and reader fonts
    if M.ui then
        font.setFallback(M.ui, M.fallback)
    end
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
-- @param opts Optional table: {skip_reader=true} to skip reader font (saves ~25KB RAM)
-- Call in every plugin's onEnter(). Safe to call multiple times.
function M.init(opts)
    local settings = require("lib.settings")
    local skip_reader = opts and opts.skip_reader

    -- UI font: reuse boot font (slot 0, loaded in C before Lua)
    -- This avoids a duplicate 31KB allocation
    if not M.ui then
        local boot_id = 0  -- boot font is always slot 0
        local test = display.getTextWidth(boot_id, "X")
        if test and test > 0 then
            M.ui = boot_id
        else
            M.ui = font.load(font_path("Ubuntu", "12", "Regular"))
            if not M.ui then
                M.ui = font.load(font_path("NotoSans", "12", "Regular"))
            end
        end
    end

    -- Reader font: from settings (skip if not needed, e.g. settings plugin)
    if not skip_reader and not M.reader then
        local family = settings.get("fontFamily", "NotoSans")
        local size = settings.get("fontSize", "14")
        local path = font_path(family, size, "Regular")

        M.reader = font.load(path)
        if not M.reader then
            system.log("WARN: Failed to load reader font " .. path .. ", falling back")
            M.reader = font.load(font_path("NotoSans", "14", "Regular"))
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

    if not M.ui then
        system.log("ERROR: No fonts available!")
    end
end

--- Unload all fonts. Call in every plugin's onExit().
function M.cleanup()
    if M.fallback then
        if M.ui then font.clearFallback(M.ui) end
        if M.reader then font.clearFallback(M.reader) end
    end
    if M.fallback then
        font.unload(M.fallback)
        M.fallback = nil
    end
    if M.ui and M.ui ~= 0 then
        -- Only unload if it's NOT the boot font (slot 0)
        font.unload(M.ui)
    end
    M.ui = nil
    if M.reader then
        font.unload(M.reader)
        M.reader = nil
    end
end

--- Reload reader font after settings change (e.g., font family or size changed).
-- Re-establishes fallback on the new reader font.
function M.reload_reader()
    -- Clear fallback from old fonts
    local had_fallback = M.fallback ~= nil
    if M.fallback then
        if M.ui then font.clearFallback(M.ui) end
        if M.reader then font.clearFallback(M.reader) end
    end

    -- Unload and reload reader
    if M.reader then
        font.unload(M.reader)
        M.reader = nil
    end

    local settings = require("lib.settings")
    local family = settings.get("fontFamily", "NotoSans")
    local size = settings.get("fontSize", "14")
    local path = font_path(family, size, "Regular")

    M.reader = font.load(path)
    if not M.reader then
        system.log("WARN: Failed to reload reader font " .. path)
        M.reader = font.load(font_path("NotoSans", "14", "Regular"))
    end

    -- Unload old fallback if present
    if M.fallback then
        font.unload(M.fallback)
        M.fallback = nil
    end

    -- Load fallback based on current language setting
    local lang_code = settings.get("language", "en")
    if lang_code == "he" then
        M.load_fallback_for_script("hebrew")
    elseif lang_code == "ar" then
        M.load_fallback_for_script("arabic")
    end
end

--- Discover available font families by scanning /fonts/ subdirectories.
-- A valid font pack is a subfolder containing at least one .cfont file.
-- @return Sorted list of family names (e.g. {"Bookerly", "NotoSans", "OpenDyslexic"})
function M.discover_families()
    local families = {}
    local entries = storage.list("/fonts")
    if not entries then return families end

    for _, e in ipairs(entries) do
        if e.isDir and e.name:sub(1, 1) ~= "." then
            -- Check if folder has at least one .cfont file
            local files = storage.list("/fonts/" .. e.name)
            if files then
                for _, f in ipairs(files) do
                    if not f.isDir and f.name:match("%.cfont$") then
                        families[#families + 1] = e.name
                        break
                    end
                end
            end
        end
    end

    table.sort(families)
    return families
end

return M
