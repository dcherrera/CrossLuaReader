-- buttons.lua — Button layout system for CrossLua Reader.
--
-- Three sections:
-- 1. PRE-DEFINED LAYOUTS — orientation-aware defaults (DO NOT MODIFY)
-- 2. CONTEXT LABELS — display labels for button hint bar per screen
-- 3. CUSTOM LAYOUT — user-defined mapping (only part written by settings)

local M = {}

-- ══════════════════════════════════════════════════════════════════
-- PRE-DEFINED LAYOUTS (DO NOT MODIFY)
-- Default button mappings per orientation.
-- mapping: logical role → physical button index (0=BACK, 1=CONFIRM, 2=LEFT, 3=RIGHT)
-- ══════════════════════════════════════════════════════════════════

M.layouts = {
    portrait = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back = 0, confirm = 1, left = 2, right = 3},
    },
    landscape_cw = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back = 0, confirm = 1, left = 2, right = 3},
    },
    inverted = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back = 0, confirm = 1, left = 2, right = 3},
    },
    landscape_ccw = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back = 0, confirm = 1, left = 2, right = 3},
    },
}

-- Maps orientation index (0-3) to layout name
M.orientation_layout = {
    [0] = "portrait",
    [1] = "landscape_cw",
    [2] = "inverted",
    [3] = "landscape_ccw",
}

-- ══════════════════════════════════════════════════════════════════
-- CONTEXT LABELS
-- Display labels for the 4 front buttons per screen context.
-- Used by ui.draw_button_hints().
-- ══════════════════════════════════════════════════════════════════

M.context = {
    home     = {"Back", "Select", "", ""},
    browser  = {"Back", "Open", "", ""},
    settings = {"Back", "Change", "", ""},
    reader   = {"Back", "", "Prev", "Next"},
    confirm  = {"Cancel", "OK", "", ""},
    default  = {"Back", "Confirm", "Left", "Right"},
}

-- ══════════════════════════════════════════════════════════════════
-- CUSTOM LAYOUT (user-defined, written by settings plugin)
-- Set to nil to use orientation defaults.
-- ══════════════════════════════════════════════════════════════════

M.custom = nil

-- Example custom layout:
-- M.custom = {
--     labels = {"Exit", "OK", "Prev", "Next"},
--     mapping = {back = 3, confirm = 0, left = 1, right = 2},
-- }

-- ══════════════════════════════════════════════════════════════════
-- API
-- ══════════════════════════════════════════════════════════════════

--- Get the active button mapping.
-- Returns custom layout if set, otherwise orientation-aware default.
-- @param orientation Orientation index (0-3), defaults to 0
-- @return Table with back, confirm, left, right → physical button indices
function M.get_mapping(orientation)
    if M.custom then
        return M.custom.mapping
    end
    local layout_name = M.orientation_layout[orientation or 0]
    local layout = M.layouts[layout_name or "portrait"]
    return layout.mapping
end

--- Get context-specific labels for button hints.
-- @param context String: "home", "browser", "settings", "reader", "confirm"
-- @return Table of 4 strings for the button hint bar
function M.get(context)
    return M.context[context] or M.context.default
end

return M
