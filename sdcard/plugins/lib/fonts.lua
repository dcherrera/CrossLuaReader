-- fonts.lua — System font manager for CrossLua Reader.
-- Loads UI and reader fonts based on settings. All plugins use
-- fonts.ui and fonts.reader instead of loading fonts directly.

local M = {}

M.ui = nil      -- Ubuntu 12 UI font ID (always loaded)
M.reader = nil  -- Reader font ID (from settings: family + size)

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

    if not M.ui and not M.reader then
        system.log("ERROR: No fonts available!")
    end
end

--- Unload all fonts. Call in every plugin's onExit().
function M.cleanup()
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
-- UI font stays loaded.
function M.reload_reader()
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
end

return M
