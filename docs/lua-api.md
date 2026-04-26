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
Draw UTF-8 text. `fontId` from `font.load()`. `x`, `y` are logical coordinates (top-left of text line).

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
True if button is currently held down.

### input.wasPressed(button) → bool
True if button was pressed since last frame.

### input.wasAnyPressed() → bool
True if any button was pressed since last `poll()`.

### input.wasReleased(button) → bool
True if button was released since last `poll()`.

### input.wasAnyReleased() → bool
True if any button was released since last `poll()`.

### input.getHeldTime() → int
Milliseconds the current button(s) have been held.

### input.waitButton() → int
Block until a button is pressed. Returns the button ID. Yields to FreeRTOS while waiting.

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
Suppress or restore auto-sleep. Use when WiFi server is active, USB is connected, or during downloads. `true` = prevent sleep, `false` = restore.

**Example:**
```lua
system.log("CrossLua Reader v" .. system.version())
system.log("Free heap: " .. system.freeHeap() .. " bytes")
system.log("Battery: " .. system.batteryPercent() .. "%")
```

---

## font

Load and unload .cfont font files from the SD card.

### font.load(path) → fontId | nil, errmsg
Load a `.cfont` file. Returns an integer font ID for use with `display.drawText()` and `display.getTextWidth()`. Max 4 fonts loaded simultaneously.

### font.unload(fontId)
Free a loaded font and its memory.

**Example:**
```lua
local ui = font.load("/fonts/Ubuntu-12-Regular.cfont")
local reader = font.load("/fonts/NotoSans-14-Regular.cfont")

display.clear()
display.drawText(ui, 10, 10, "Menu Title")
display.drawText(reader, 10, 40, "Book content goes here...")
display.refresh()

-- When done:
font.unload(reader)
font.unload(ui)
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
