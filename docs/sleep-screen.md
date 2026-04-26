# Sleep Screen

The sleep screen renders on the e-ink display before the device enters deep sleep. It supports BMP wallpapers, a stay-on-page mode for readers, and a plugin hook for custom content like quotes or status info.

## Sleep Modes

| Mode | Value | Description |
|------|-------|-------------|
| Blank | 0 | White screen with CrossLua Reader branding |
| Single | 1 | Show a specific wallpaper from `/wallpapers/` |
| Cycle | 2 | Cycle through `/wallpapers/` in order, advancing each sleep |
| Random | 3 | Random pick from `/wallpapers/` |
| Clear | 4 | Keep current page content, overlay "SLEEP" + battery % |

Set the mode in Settings or from Lua:
```lua
system.setSleepMode(3)  -- random wallpaper
```

## Sleep Triggers

| Trigger | Behavior |
|---------|----------|
| Idle timeout | Auto-sleep after configurable period (default 10 min) |
| Power button short press (0.5-2s) | Manual sleep |
| `system.sleep()` from Lua | Programmatic sleep |

Auto-sleep is suppressed when USB is connected.

## Wallpapers

### Setup

1. Create a `/wallpapers/` folder on the SD card
2. Drop BMP images into it
3. Set sleep mode to Single, Cycle, or Random in Settings

### Supported Formats

| Format | Supported | Notes |
|--------|-----------|-------|
| 1-bit BMP | Yes | Black and white, smallest files |
| 8-bit BMP | Yes | Grayscale, dithered to 1-bit |
| 24-bit BMP | Yes | Full color, dithered to 1-bit with Bayer 4x4 |
| Compressed BMP (RLE) | No | Must be uncompressed (BI_RGB) |
| PNG, JPG | No | BMP only |

### Image Sizing

- The display is 800x480 pixels (physical panel, portrait orientation)
- Images are scaled to fit via nearest-neighbor if dimensions differ
- For best quality, use 800x480 1-bit BMP images pre-dithered for e-ink

### Creating Wallpapers

Any image editor can export BMP. For best results on e-ink:

1. Resize to 800x480
2. Convert to grayscale
3. Apply a dither (Floyd-Steinberg or ordered) in the editor
4. Save as uncompressed BMP (24-bit or 8-bit)

The device applies Bayer 4x4 ordered dithering automatically for 8-bit and 24-bit images, so pre-dithering is optional but produces better results when done in a full image editor.

### Single Mode

Set a specific wallpaper:
```lua
system.setSleepMode(1)
system.setSleepWallpaper("sunset.bmp")
```

The file must exist at `/wallpapers/sunset.bmp`.

### Cycle Mode

Shows wallpapers in alphabetical order, advancing one per sleep. The current position persists across deep sleep via `/crosslua_sleep_idx.txt` on the SD card.

### Random Mode

Picks a random wallpaper each time using the ESP32-C3 hardware RNG.

## Clear Mode (Stay on Page)

For readers: the current page stays on screen. A small box in the bottom-right corner shows "SLEEP" and the battery percentage. This lets you see what you were reading when you pick up the device.

## Sleep Hook (Custom Content)

Plugins can register a Lua callback to draw custom content on top of the sleep screen. The hook runs after the base screen (wallpaper/blank/clear) renders but before the display refresh.

```lua
-- Register a sleep hook
system.setSleepHook(function()
    local f = fonts.reader or fonts.ui
    if not f then return end

    -- Draw a bordered quote box
    display.fillRect(20, 600, 440, 120)
    display.drawRect(20, 600, 440, 120)
    display.drawText(f, 30, 610, '"The only way to do great work"')
    display.drawText(f, 30, 640, '"is to love what you do."')
    display.drawText(f, 30, 680, "-- Steve Jobs")
end)

-- Clear the hook
system.setSleepHook(nil)
```

### Hook Behavior

- The hook can use any `display.*` API (drawText, fillRect, drawLine, etc.)
- Errors in the hook are caught and logged — sleep always proceeds
- The hook is automatically cleared when the plugin exits or crashes
- Only one hook can be active at a time (last one wins)
- The hook draws on top of whatever the base sleep mode rendered

### Use Cases

- Quote of the day overlay
- Reading progress summary
- Calendar/date display
- Custom branding

## Lua API Reference

| Function | Description |
|----------|-------------|
| `system.setSleepMode(mode)` | Set base sleep screen mode (0-4) |
| `system.setSleepWallpaper(filename)` | Set wallpaper for single mode |
| `system.setSleepHook(func)` | Register Lua callback for custom content |
| `system.setSleepHook(nil)` | Clear the sleep hook |
| `system.sleep()` | Enter sleep immediately |
| `system.setSleepTimeout(minutes)` | Set auto-sleep timeout (0 = disable) |
| `system.suppressSleep(bool)` | Manually suppress/restore auto-sleep |

## Settings

| Key | Type | Default | Values |
|-----|------|---------|--------|
| `sleepMode` | string | `"blank"` | `"blank"`, `"single"`, `"cycle"`, `"random"`, `"clear"` |
| `sleepWallpaper` | string | `""` | Filename in `/wallpapers/` |
| `sleepTimeout` | number | `10` | Minutes (1-30) |

## SD Card Structure

```
/wallpapers/
  nature.bmp
  quote_bg.bmp
  pattern.bmp
  ...
```

The `/wallpapers/` folder is scanned when a wallpaper mode (single/cycle/random) first triggers. Up to 32 BMP files are indexed. Non-BMP files and subdirectories are ignored.
