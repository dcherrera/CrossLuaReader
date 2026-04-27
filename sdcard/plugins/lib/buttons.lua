-- buttons.lua — Button layout system for CrossLua Reader.
--
-- PHYSICAL BUTTONS (named by their default portrait function):
--
--   Front buttons (left to right as you face the device):
--     "back"   = leftmost front button
--     "select" = second front button
--     "left"   = third front button
--     "right"  = rightmost front button
--
--   Side buttons (top to bottom along the right edge):
--     "power"  = top side button (power)
--     "up"     = middle side button
--     "down"   = bottom side button
--
-- LOGICAL ACTIONS (what plugins check for):
--     up, down, left, right, back, confirm
--
-- Each orientation maps: logical action → physical button name.
-- Example: in landscape CCW, you might want the physical "up" button
-- to act as logical "left" because it's now on the left side.

local M = {}

-- Physical button name → hardware index
M.physical = {
    back   = 0,   -- front leftmost
    select = 1,   -- front second
    left   = 2,   -- front third
    right  = 3,   -- front rightmost
    up     = 4,   -- side top
    down   = 5,   -- side bottom
    power  = 6,   -- side top (power)
}

-- ══════════════════════════════════════════════════════════════════
-- ORIENTATION MAPPINGS
-- Format:  logical_action = "physical_button"
--
-- Read as: "To perform [action], press the [button]"
--   e.g. up = "left" means "To go UP, press the LEFT button"
--   NOT "the UP button does LEFT"
-- ══════════════════════════════════════════════════════════════════

M.orientations = {
    -- Orientation 0: Portrait (front buttons at the bottom)
    [0] = {
        up      = "up",
        down    = "down",
        left    = "left",
        right   = "right",
        back    = "back",
        confirm = "select",
    },

    -- Orientation 1: Landscape CW (front buttons on the left side)
    [1] = {
        up      = "left",
        down    = "right",
        left    = "up",
        right   = "down",
        back    = "back",
        confirm = "select",
    },

    -- Orientation 2: Inverted (front buttons at the top)
    [2] = {
        up      = "down",
        down    = "up",
        left    = "right",
        right   = "left",
        back    = "back",
        confirm = "select",
    },

    -- Orientation 3: Landscape CCW (front buttons on the right side)
    [3] = {
        up      = "right",
        down    = "left",
        left    = "up",
        right   = "down",
        back    = "back",
        confirm = "select",
    },
}

-- ══════════════════════════════════════════════════════════════════
-- CONTEXT LABELS
-- What label to show for each logical action per screen context.
-- Empty string = don't show a label for that action.
-- ══════════════════════════════════════════════════════════════════

M.actions = {
    home     = {back = "Back", confirm = "Select", left = "",     right = "",      up = "Up",   down = "Down"},
    browser  = {back = "Back", confirm = "Open",   left = "",     right = "",      up = "Up",   down = "Down"},
    settings = {back = "Back", confirm = "Change", left = "Left", right = "Right", up = "Up",   down = "Down"},
    reader   = {back = "Back", confirm = "",       left = "Prev", right = "Next",  up = "Prev", down = "Next"},
    confirm  = {back = "Cancel", confirm = "OK",   left = "",     right = "",      up = "",     down = ""},
    default  = {back = "Back", confirm = "Confirm", left = "Left", right = "Right", up = "Up",  down = "Down"},
}

-- ══════════════════════════════════════════════════════════════════
-- CUSTOM LAYOUT (user-defined, set via settings plugin)
-- Set to nil to use orientation defaults above.
-- Same format as orientation entries but uses physical button names.
-- ══════════════════════════════════════════════════════════════════

M.custom = nil

-- Example:
-- M.custom = {
--     up = "down", down = "up", left = "right", right = "left",
--     back = "back", confirm = "select",
-- }

-- ══════════════════════════════════════════════════════════════════
-- API (do not edit below this line)
-- ══════════════════════════════════════════════════════════════════

--- Get the active button mapping for an orientation.
-- Resolves physical button names to hardware indices.
-- @param orientation Orientation index (0-3), defaults to 0
-- @return Table: {up=N, down=N, left=N, right=N, back=N, confirm=N} (hardware indices)
function M.get_mapping(orientation)
    local layout = M.custom or M.orientations[orientation or 0] or M.orientations[0]

    -- Resolve physical names to hardware indices
    local resolved = {}
    for action, button_name in pairs(layout) do
        resolved[action] = M.physical[button_name] or 0
    end
    return resolved
end

--- Get context-specific labels for the 4 front physical buttons.
-- @param context String: "home", "browser", "settings", "reader", "confirm"
-- @param orientation Orientation index (0-3), defaults to 0
-- @return Table of 4 strings (one per front button: back, select, left, right)
function M.get(context, orientation)
    local actions = M.actions[context] or M.actions.default
    local mapping = M.get_mapping(orientation)

    -- Translate labels through lang.tr() if available
    local ok, lang = pcall(require, "lib.lang")
    local function tr(s)
        if s == "" then return "" end
        if ok and lang.tr then
            return lang.tr(s:lower())
        end
        return s
    end

    -- Build labels for the 4 front buttons (hardware indices 0-3)
    local labels = {"", "", "", ""}
    local roles = {"back", "confirm", "left", "right", "up", "down"}

    for _, role in ipairs(roles) do
        local hw = mapping[role]
        if hw and hw >= 0 and hw <= 3 then
            local label = actions[role] or ""
            if label ~= "" and labels[hw + 1] == "" then
                labels[hw + 1] = tr(label)
            end
        end
    end

    return labels
end

return M
