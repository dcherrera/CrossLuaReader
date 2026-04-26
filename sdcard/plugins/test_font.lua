-- test_font.lua — Test font loading and text rendering.

plugin = {
    name = "Font Test",
    id = "test_font",
    type = "activity",
}

function plugin.onEnter()
    system.log("Font test: loading font...")
    local fid = font.load("/fonts/NotoSans-14-Regular.cfont")
    if not fid then
        system.log("Font test: FAILED to load font")
        return
    end
    system.log("Font test: font loaded, id=" .. fid)

    system.log("Font test: clearing screen...")
    display.clear()

    system.log("Font test: drawing text...")
    display.drawText(fid, 50, 50, "A")

    system.log("Font test: refreshing...")
    display.refresh()

    system.log("Font test: done!")
end

function plugin.loop()
    -- do nothing, static display
end

function plugin.onExit()
end
