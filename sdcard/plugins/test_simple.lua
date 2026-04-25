-- test_simple.lua — Minimal test plugin with no font rendering.
-- Tests: display primitives, input, plugin lifecycle.

plugin = {
    name = "Simple Test",
    id = "test_simple",
    type = "activity",
    menuEntry = "Test",
}

local needs_render = true

function plugin.onEnter()
    system.log("Simple test plugin started")
    needs_render = true
end

function plugin.loop()
    input.poll()

    if input.wasPressed(input.CONFIRM) then
        system.log("Confirm pressed!")
        needs_render = true
    end

    if input.wasPressed(input.BACK) then
        system.log("Back pressed — exiting")
        plugin.goHome()
        return
    end

    if needs_render then
        needs_render = false

        display.clear()

        -- Draw some geometric shapes (no fonts needed)
        display.fillRect(20, 20, 200, 60)
        display.drawRect(20, 100, 200, 60)
        display.drawLine(20, 180, 440, 180)
        display.drawLine(240, 20, 240, 300)
        display.fillRoundedRect(260, 20, 180, 60, 10)
        display.drawRect(260, 100, 180, 60)

        display.refresh()
        system.log("Rendered test pattern")
    end
end

function plugin.onExit()
    system.log("Simple test plugin exiting")
end
