# Settings Schema

## Overview

Settings are stored in `/settings.json` on the SD card. The `lib/settings.lua` module handles reading, writing, and providing defaults.

## Settings Keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `fontFamily` | string | `"NotoSans"` | Reader font family. Options: `"NotoSans"`, `"Bookerly"`, `"OpenDyslexic"` |
| `fontSize` | string | `"14"` | Reader font size in points. Options: `"12"`, `"14"`, `"16"`, `"18"` |
| `orientation` | number | `0` | Screen orientation. `0`=Portrait, `1`=Landscape CW, `2`=Inverted, `3`=Landscape CCW |
| `screenMargin` | number | `10` | Reader screen margin in pixels. Options: `0`, `5`, `10`, `15`, `20` |
| `refreshFrequency` | number | `15` | Full e-ink refresh every N pages. Options: `1`, `5`, `10`, `15`, `30` |
| `theme` | string | `"lyra"` | UI theme. Options: `"lyra"` (modern, rounded), `"classic"` (simple boxes) |
| `buttonLayout` | string | `"default"` | Button mapping. `"default"` = orientation-aware, `"custom"` = user-defined |
| `sleepTimeout` | number | `10` | Auto-sleep after N minutes of inactivity. Options: `1`, `5`, `10`, `15`, `30`. `0` = disabled |
| `language` | string | `"en"` | UI language code. Reads from `/languages/{code}/lang.json` |

## Example `/settings.json`

```json
{
  "buttonLayout": "default",
  "fontFamily": "NotoSans",
  "fontSize": "14",
  "language": "en",
  "orientation": 0,
  "refreshFrequency": 15,
  "screenMargin": 10,
  "sleepTimeout": 10,
  "theme": "lyra"
}
```

## First Boot

When no `/settings.json` exists, all settings use the defaults listed above. The file is created on the first settings change.

## Using Settings in Plugins

```lua
local settings = require("lib.settings")

-- Load from SD (call once in onEnter, or let it auto-load on first get)
settings.load()

-- Read a value (with fallback default)
local orient = settings.get("orientation", 0)

-- Change a value
settings.set("orientation", 1)

-- Persist to SD
settings.save()

-- Apply immediately (orientation example)
display.setOrientation(settings.get("orientation", 0))
```

## Settings That Apply Immediately

| Setting | Apply Method | Notes |
|---------|-------------|-------|
| `orientation` | `display.setOrientation(value)` | Global — affects all screens |
| `theme` | `theme.set(value)` | Changes Lyra/Classic metrics |
| `fontFamily` / `fontSize` | `fonts.reload_reader()` | Reloads reader font from new path |
| `sleepTimeout` | `system.setSleepTimeout(value)` | Updates C-side timer |

Other settings (screenMargin, refreshFrequency) are read by reader plugins when they render.

## Sleep Timeout

The C-side auto-sleep timer is configurable via `system.setSleepTimeout(minutes)`:

```lua
system.setSleepTimeout(10)      -- sleep after 10 minutes idle
system.setSleepTimeout(0)       -- disable auto-sleep
system.suppressSleep(true)      -- suppress sleep (e.g., during WiFi)
system.suppressSleep(false)     -- restore normal sleep behavior
```
