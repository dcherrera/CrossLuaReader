-- firmware_home.lua — minimal rescue UI bundled in firmware.
-- Loaded by plugin manager when the SD copy of /plugins/home.lua is unavailable.
-- Hard constraints: no require(), boot font only, no SD reads, no lib/*.

plugin = {
    name = "Firmware Home",
    id = "home",
    type = "activity",
    system = true,
}

local FID = -1
local needs_render = true

local function bold_text(x, y, s)
    -- Double-strike fake bold: draw twice with 1px X offset.
    display.drawText(FID, x, y, s)
    display.drawText(FID, x + 1, y, s)
end

-- Fontless rescue: the boot font is loaded from SD, so without an SD card we
-- have no font at all. Render a high-contrast, unmistakable shape so the user
-- knows the device is alive and that this is the rescue path firing.
-- Two thick horizontal bands top + bottom and a big diagonal cross in between.
local function render_no_font(W, H)
    local band = 24
    -- Top + bottom alert bands
    display.fillRect(0, 0, W, band)
    display.fillRect(0, H - band, W, band)
    -- Centered hollow box
    local bx = math.floor(W / 6)
    local by = math.floor(H / 4)
    local bw = W - bx * 2
    local bh = H - by * 2
    for i = 0, 3 do
        display.drawRect(bx + i, by + i, bw - i * 2, bh - i * 2)
    end
    -- Big diagonal cross inside the box
    for i = -2, 2 do
        display.drawLine(bx + i, by, bx + bw + i, by + bh)
        display.drawLine(bx + bw + i, by, bx + i, by + bh)
    end
    display.refreshFull()
end

-- Render a single button hint at the physical confirm-button position.
-- Mirrors lib/ui.lua's draw_button_hints layout (4 equal cells across the
-- bottom of the physical 480x800 portrait), but only fills cell 2 (confirm).
-- Uses physical coords so the hint sits over the actual hardware button
-- regardless of any future orientation override.
local function render_confirm_hint(label)
    if FID < 0 then return end

    local PHYS_W = 480
    local PHYS_H = 800
    local BAR_H = 40   -- matches lib/theme.lua button_hints_height
    local SIDE_PAD = 20  -- matches lib/theme.lua side_padding

    local cell_w = math.floor((PHYS_W - 2 * SIDE_PAD) / 4)
    local cell_x = SIDE_PAD + cell_w  -- cell 2: confirm
    local y = PHYS_H - BAR_H

    -- Cell border (rectangle around just the confirm cell)
    display.drawRectPhysical(cell_x, y, cell_w, BAR_H)

    -- Label centered in cell
    local tw = display.getTextWidth(FID, label)
    local tx = math.floor(cell_x + (cell_w - tw) / 2)
    local ty = math.floor(y + 8)
    display.drawTextPhysical(FID, tx, ty, label)
end

local function render()
    local W = display.width()
    local H = display.height()

    display.clear()

    if FID < 0 then
        render_no_font(W, H)
        return
    end

    local lh = display.getLineHeight(FID)
    local title = "PLEASE INSERT SD"
    local sub   = "SD card not detected."
    local btn   = "Reload SD"
    local hint  = "Press CONFIRM to retry"

    local title_w = display.getTextWidth(FID, title) + 1  -- +1 for bold
    local sub_w   = display.getTextWidth(FID, sub)
    local btn_w   = display.getTextWidth(FID, btn)
    local hint_w  = display.getTextWidth(FID, hint)

    local y = math.floor(H / 4)
    bold_text(math.floor((W - title_w) / 2), y, title)

    -- Underline
    y = y + lh + 4
    display.drawLine(math.floor((W - title_w) / 2), y,
                     math.floor((W - title_w) / 2) + title_w, y)

    -- Subtitle
    y = y + lh + 8
    display.drawText(FID, math.floor((W - sub_w) / 2), y, sub)

    -- Action button (filled rect, inverted text) — visual focus
    y = y + lh * 3
    local pad_x, pad_y = 14, 6
    local box_w = btn_w + pad_x * 2
    local box_h = lh + pad_y * 2
    local box_x = math.floor((W - box_w) / 2)
    display.fillRoundedRect(box_x, y, box_w, box_h, 6)
    display.drawTextInverted(FID, box_x + pad_x, y + pad_y, btn)

    -- Hint below the action button
    y = y + box_h + lh
    display.drawText(FID, math.floor((W - hint_w) / 2), y, hint)

    -- Single hint cell at the physical confirm button
    render_confirm_hint("Confirm")

    display.refreshFull()
end

function plugin.onEnter()
    -- Force a known orientation: SD-less means no settings, no remap.
    display.setOrientation(0)
    input.resetMapping()

    FID = font.boot()
    needs_render = true
end

function plugin.loop()
    if input.wasPressed(input.CONFIRM) or input.wasPressed(input.RIGHT) then
        system.reload()
        return
    end
    if needs_render then
        needs_render = false
        render()
    end
end

function plugin.onExit()
    -- Nothing to clean up: we don't own any fonts or settings.
end
