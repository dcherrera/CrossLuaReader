-- buttons.lua — Shared button hint labels for CrossLua Reader.
-- Edit this file to customize button labels across all plugins.
-- Labels map to the 4 front buttons: Back, Confirm, Left, Right.

local M = {}

-- Default button layouts for different contexts
M.home = {"Back", "Select", "", ""}
M.browser = {"Back", "Open", "", ""}
M.settings = {"Back", "Change", "", ""}
M.reader = {"Back", "", "Prev", "Next"}
M.confirm = {"Cancel", "OK", "", ""}

-- Generic fallback
M.default = {"Back", "Confirm", "Left", "Right"}

return M
