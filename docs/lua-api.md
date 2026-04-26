# Lua API Reference

## Overview

CrossLua Reader exposes five modules to Lua plugins: `display`, `input`, `storage`, `system`, and `font`. These are registered automatically when a Lua state is created via `api_create_state()`.

Standard Lua libraries available: `base`, `string`, `table`, `math`, `utf8`, `coroutine`.

Excluded: `io`, `os`, `debug` (replaced by CrossLua HAL APIs).

---

## display

Screen rendering and measurement.

### display.clear([color])
Fill the framebuffer. `color`: `0xFF` = white (default), `0x00` = black.

### display.refresh([mode])
Push framebuffer to e-ink display. `mode`: `0` = full, `1` = half, `2` = fast (default).

### display.refreshFull()
Full refresh (best quality, clears ghosting).

### display.drawText(fontId, x, y, text)
Draw UTF-8 text. `fontId` from `font.load()`. `x`, `y` are logical coordinates (top-left of text line). If a fallback font is set via `font.setFallback()`, missing glyphs are automatically rendered from the fallback font.

### display.drawLine(x1, y1, x2, y2)
Draw a black line between two points.

### display.drawRect(x, y, w, h)
Draw a rectangle outline (1px border).

### display.fillRect(x, y, w, h)
Draw a filled rectangle.

### display.fillRoundedRect(x, y, w, h, [radius])
Draw a filled black rectangle with rounded corners. `radius` defaults to 6.

### display.fillRoundedRectGray(x, y, w, h, [radius])
Draw a filled rectangle with rounded corners using a dithered gray checkerboard pattern. `radius` defaults to 6. **This is the recommended selection highlight** — text remains readable on the gray background, matching CrossPoint's Lyra theme.

### display.drawTextInverted(fontId, x, y, text)
Draw white text (for rendering text on a dark/selected background). Same as `drawText` but draws white pixels instead of black.

### display.setOrientation(n)
Set the screen orientation. `0` = portrait, `1` = landscape CW, `2` = inverted, `3` = landscape CCW. Affects all subsequent drawing and dimension queries.

### display.getOrientation() → int
Returns the current orientation (0-3).

### display.contentArea() → x, y, w, h
Returns the usable content area bounds, excluding the physical button bar zone. The button bar occupies 40px at the physical bottom of the device, which maps to different logical edges depending on orientation:
- Portrait: bottom edge → content height reduced
- Landscape CW: left edge → content x offset increased
- Inverted: top edge → content y offset increased
- Landscape CCW: right edge → content width reduced

Use this for all content rendering to prevent overlap with button hints.

### display.width() → int
Logical screen width for current orientation.

### display.height() → int
Logical screen height for current orientation.

### display.drawLinePhysical(x1, y1, x2, y2)
Draw a line in physical (portrait) coordinates, bypassing orientation. Used by button hints to always render at the physical bottom.

### display.drawRectPhysical(x, y, w, h)
Draw a rectangle in physical coordinates.

### display.drawTextPhysical(fontId, x, y, text)
Draw text in physical coordinates. Used by button hints.

### display.getTextWidth(fontId, text) → int
Measure text advance width in pixels.

### display.getLineHeight(fontId) → int
Line height for the font.

**Example:**
```lua
local f = font.load("/fonts/NotoSans-14-Regular.cfont")
display.clear()
display.drawText(f, 20, 20, "Hello, World!")
display.drawLine(20, 50, 400, 50)
display.refresh()
```

---

## input

Button state queries and constants.

### input.poll()
No-op — the main loop polls buttons automatically before calling your `loop()`. This function exists for API compatibility but does nothing. **Do not call `hal_gpio_poll()` manually** — it would clear button edge states.

### input.isPressed(button) → bool
True if the **logical** button is currently held down. Scans all physical buttons through the remap table.

### input.wasPressed(button) → bool
True if the **logical** button was pressed since last frame. Orientation-aware via `setMapping`.

### input.wasAnyPressed() → bool
True if any physical button was pressed since last frame.

### input.wasReleased(button) → bool
True if the **logical** button was released since last frame. Orientation-aware via `setMapping`.

### input.wasAnyReleased() → bool
True if any physical button was released since last frame.

### input.getHeldTime() → int
Milliseconds the current button(s) have been held.

### input.waitButton() → int
Block until a button is pressed. Returns the **logical** button ID (remapped via `setMapping`). Yields to FreeRTOS while waiting.

### input.setMapping(table)
Set the orientation-aware button remap. The table maps logical actions to physical hardware indices: `{back=0, confirm=1, left=2, right=3, up=4, down=5}`. Internally inverted to a physical→logical lookup table. Called automatically by `home.lua` and `settings.lua` using `buttons.get_mapping(orientation)`.

### input.resetMapping()
Reset button mapping to identity (physical == logical). No remap.

### Button Constants
| Constant | Value | Description |
|----------|-------|-------------|
| `input.BACK` | 0 | Back button |
| `input.CONFIRM` | 1 | Confirm/OK |
| `input.LEFT` | 2 | Left |
| `input.RIGHT` | 3 | Right |
| `input.UP` | 4 | Up (side) |
| `input.DOWN` | 5 | Down (side) |
| `input.POWER` | 6 | Power button |

**Example:**
```lua
input.poll()
if input.wasPressed(input.CONFIRM) then
    system.log("Confirm pressed!")
end

-- Or block until any button:
local btn = input.waitButton()
system.log("Button " .. btn .. " pressed")
```

---

## storage

SD card file and directory operations.

### storage.read(path) → string | nil, errmsg
Read entire file as a string. Max 256KB. Returns `nil, errmsg` on failure.

### storage.readBytes(path, offset, length) → string | nil, errmsg
Read a byte range from a file. Max 64KB per read.

### storage.write(path, content) → bool
Write string to file (creates/overwrites).

### storage.exists(path) → bool
Check if file or directory exists.

### storage.mkdir(path) → bool
Create directory (and parents).

### storage.remove(path) → bool
Delete a file.

### storage.fileSize(path) → int | nil
Get file size in bytes. Returns `nil` if file doesn't exist.

### storage.list(path) → table | nil
List directory contents. Returns array of `{name=string, isDir=bool}`.

**Example:**
```lua
-- Write and read back
storage.write("/test.txt", "Hello from Lua!")
local content = storage.read("/test.txt")
system.log("Read: " .. content)

-- List a directory
local entries = storage.list("/books")
if entries then
    for _, e in ipairs(entries) do
        local kind = e.isDir and "[DIR]" or "[FILE]"
        system.log(kind .. " " .. e.name)
    end
end
```

---

## system

Device information, timing, and logging.

### system.freeHeap() → int
Current free heap in bytes.

### system.totalHeap() → int
Total heap size in bytes.

### system.batteryPercent() → int
Battery level 0-100%. Cached (polled every 30 seconds).

### system.millis() → int
Milliseconds since boot.

### system.delay(ms)
Yield to FreeRTOS for `ms` milliseconds. Use this in tight loops to prevent watchdog timeouts.

### system.log(message)
Print a message to the serial log.

### system.version() → string
Firmware version string.

### system.restart()
Reboot the device. Does not return.

### system.sleep()
Enter deep sleep. Does not return. Wake with power button.

### system.setSleepTimeout(minutes)
Set the auto-sleep timeout. `minutes`: 1-60. `0` = disable auto-sleep.

### system.suppressSleep(bool)
Suppress or restore auto-sleep. Use when WiFi server is active or during downloads. `true` = prevent sleep, `false` = restore. Note: USB connection is auto-detected and suppresses sleep automatically.

### system.setSleepMode(mode)
Set the sleep screen mode. `mode`: `0` = blank (white screen), `1` = single wallpaper, `2` = cycle through `/wallpapers/`, `3` = random from `/wallpapers/`, `4` = clear (keep current page, overlay "SLEEP" text). Called from home plugin on boot to push persisted setting to C.

### system.setSleepWallpaper(filename)
Set the wallpaper filename for single mode (e.g., `"sunset.bmp"`). The file must exist in `/wallpapers/` on the SD card.

### system.setSleepHook(func)
Register a Lua callback that runs after the base sleep screen renders but before the display refresh. The callback can draw anything to the framebuffer using `display.drawText()`, `display.fillRect()`, etc. Pass `nil` to clear the hook.

The hook is automatically cleared when the plugin exits or crashes. If the hook itself errors, the error is logged and sleep proceeds normally.

```lua
-- Example: quote overlay on sleep screen
system.setSleepHook(function()
    local f = fonts.reader or fonts.ui
    if not f then return end
    -- Draw a bordered box near the bottom
    display.fillRect(20, 620, 440, 100)       -- white fill
    display.drawRect(20, 620, 440, 100)       -- black border
    display.drawText(f, 30, 630, '"Be the change you wish to see."')
    display.drawText(f, 30, 670, "— Gandhi")
end)

-- Clear the hook:
system.setSleepHook(nil)
```

### system.reload()
Re-initialize the SD card and restart the plugin system from home. Use after SD card hot-swap. Also triggered automatically by holding the power button for >2 seconds.

**Example:**
```lua
system.log("CrossLua Reader v" .. system.version())
system.log("Free heap: " .. system.freeHeap() .. " bytes")
system.log("Battery: " .. system.batteryPercent() .. "%")
```

---

## font

Load, unload, and configure .cfont font files from the SD card.

### font.load(path) → fontId | nil, errmsg
Load a `.cfont` file. Returns an integer font ID for use with `display.drawText()` and `display.getTextWidth()`. Max 4 fonts loaded simultaneously.

### font.unload(fontId)
Free a loaded font and its memory. Also clears any fallback references pointing to this font.

### font.setFallback(primaryId, fallbackId) → bool
Set a fallback font for a slot. When a glyph is missing from the primary font, the renderer automatically tries the fallback font before rendering the replacement character. Used for non-Latin script support (e.g., NotoSans primary with NotoSansHebrew fallback).

Returns `false` if either font is not loaded, if IDs are the same, or if it would create a circular chain.

### font.clearFallback(fontId)
Remove the fallback font from a slot.

**Example:**
```lua
local reader = font.load("/fonts/NotoSans-14-Regular.cfont")
local hebrew = font.load("/languages/he/fonts/NotoSansHebrew-14-Regular.cfont")
font.setFallback(reader, hebrew)

-- Now drawText with reader font automatically renders Hebrew from the fallback:
display.drawText(reader, 20, 20, "Hello שלום")  -- seamless mixed script

font.clearFallback(reader)
font.unload(hebrew)
font.unload(reader)
```

---

## Complete Plugin Example

```lua
-- /plugins/hello.lua

local font_id = font.load("/fonts/NotoSans-14-Regular.cfont")

display.clear()
display.drawText(font_id, 20, 20, "Hello, CrossLua Reader!")
display.drawText(font_id, 20, 60, "Free heap: " .. system.freeHeap())
display.drawText(font_id, 20, 100, "Battery: " .. system.batteryPercent() .. "%")
display.drawLine(20, 140, display.width() - 20, 140)
display.drawText(font_id, 20, 160, "Press any button to exit")
display.refresh()

local btn = input.waitButton()
system.log("Exit button: " .. btn)

font.unload(font_id)
```
