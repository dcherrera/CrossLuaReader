-- theme.lua — Shared theme metrics for CrossLua Reader plugins.
-- Provides Lyra (modern) and Classic layout constants.

local M = {}

M.lyra = {
    name = "Lyra",
    header_height = 84,
    menu_row_height = 64,
    list_row_height = 40,
    button_hints_height = 40,
    side_padding = 20,
    vertical_spacing = 16,
    corner_radius = 6,
    selection_padding = 8,
    top_padding = 5,
}

M.classic = {
    name = "Classic",
    header_height = 45,
    menu_row_height = 45,
    list_row_height = 30,
    button_hints_height = 40,
    side_padding = 20,
    vertical_spacing = 10,
    corner_radius = 0,
    selection_padding = 4,
    top_padding = 5,
}

-- Active theme (default to Lyra)
M.active = M.lyra

function M.set(name)
    if name == "classic" then
        M.active = M.classic
    else
        M.active = M.lyra
    end
end

function M.get()
    return M.active
end

return M
