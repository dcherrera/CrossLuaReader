-- epub_metadata_dump.lua — Phase 9.A diagnostic.
-- Opens an EPUB and renders metadata + spine/TOC summary to the screen.
-- This is a smoke test for zip / xml / epub capabilities; it does not
-- render chapter content (that's Phase 9.B).

local fonts = require("lib.fonts")
local settings = require("lib.settings")
local lang = require("lib.lang")

plugin = {
    name = "EPUB Metadata Dump",
    id = "epub_metadata_dump",
    type = "reader",
    fileExtensions = {"epub", "epub3"},
    requires = {"zip", "xml", "epub"},
    system = true,
}

local file_path = nil
local book = nil
local err_msg = nil
local needs_render = true

local function safe(s) return s or "—" end

local function render()
    display.clear()

    local fid = fonts.ui
    if not fid then
        display.refresh()
        return
    end

    local lh = display.getLineHeight(fid)
    local x = 20
    local y = 20

    display.drawText(fid, x, y, "EPUB Metadata Dump")
    y = y + lh
    display.drawLine(x, y, 460, y)
    y = y + lh

    if err_msg then
        display.drawText(fid, x, y, "Open failed:")
        y = y + lh
        display.drawText(fid, x, y, err_msg)
        y = y + lh * 2
        display.drawText(fid, x, y, "Press BACK to return.")
        display.refresh()
        return
    end

    if not book then
        display.drawText(fid, x, y, "No book opened.")
        display.refresh()
        return
    end

    local m = book:metadata()
    display.drawText(fid, x, y, "Title:    " .. safe(m.title));    y = y + lh
    display.drawText(fid, x, y, "Author:   " .. safe(m.author));   y = y + lh
    display.drawText(fid, x, y, "Language: " .. safe(m.language)); y = y + lh
    display.drawText(fid, x, y, "Version:  " .. tostring(m.epub_version)); y = y + lh
    display.drawText(fid, x, y, "Direction: " .. m.page_progression_direction); y = y + lh
    display.drawText(fid, x, y, "Modified: " .. safe(m.modified)); y = y + lh
    y = y + lh / 2

    local spine_n = book:spine_count()
    local manif_n = book:manifest_count()
    display.drawText(fid, x, y, "Manifest items: " .. manif_n); y = y + lh
    display.drawText(fid, x, y, "Spine entries:  " .. spine_n); y = y + lh

    local toc = book:toc()
    local toc_top = 0
    if toc then for _ in ipairs(toc) do toc_top = toc_top + 1 end end
    display.drawText(fid, x, y, "TOC top-level: " .. toc_top); y = y + lh

    -- First few TOC entries
    if toc and #toc > 0 then
        y = y + lh / 2
        display.drawText(fid, x, y, "First TOC entries:"); y = y + lh
        local n = math.min(8, #toc)
        for i = 1, n do
            local label = toc[i].label or "(unlabeled)"
            if #label > 50 then label = label:sub(1, 47) .. "..." end
            display.drawText(fid, x + 10, y, i .. ". " .. label)
            y = y + lh
        end
    end

    y = y + lh
    display.drawText(fid, x, y, "Press BACK to return.")

    display.refresh()
end

function plugin.onEnter(arg)
    file_path = arg
    settings.load()
    fonts.init({skip_reader = true})
    lang.load(settings.get("language", "en"))

    -- Configure layout for full-screen activity mode (no header/footer/buttons)
    layout.setHeaderHeight(0)
    layout.setFooterHeight(0)
    layout.setButtonBar(0)
    layout.setMargin(10)
    layout.setFont(fonts.ui)

    if not file_path then
        err_msg = "no file path given"
        return
    end

    local b, errcode, errstr = epub.open(file_path, "/cache/epub_reader")
    if not b then
        err_msg = (errcode or "?") .. ": " .. (errstr or "")
        system.log("epub.open failed for " .. file_path .. ": " .. err_msg)
        return
    end
    book = b
    needs_render = true
end

function plugin.loop()
    if input.wasPressed(input.BACK) then
        plugin.goHome()
        return
    end
    if needs_render then
        needs_render = false
        render()
    end
end

function plugin.onExit()
    if book then
        book:close()
        book = nil
    end
    fonts.cleanup()
end
