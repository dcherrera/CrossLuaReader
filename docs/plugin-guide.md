# CrossLua Reader Plugin Developer Guide

## Quick Start

Create a file on the SD card at `/plugins/hello.lua`:

```lua
plugin = {
  name = "Hello World",
  id = "hello",
  type = "activity",
  menuEntry = "Hello",
}

function plugin.onEnter()
  display.clear()
  display.drawText(font.UI_12, 50, 100, "Hello from Lua!")
  display.drawText(font.UI_10, 50, 130, "Press any button to exit")
  display.refresh()
end

function plugin.loop()
  local btn = input.poll()
  if btn == input.BACK then
    system.log("Goodbye!")
    plugin.finish()
  end
end

function plugin.onExit()
  -- cleanup if needed
end
```

Reboot the device. "Hello" appears in the home menu.

## Plugin Types

### Activity

A full-screen interactive plugin (browser, settings, tools):

```lua
plugin = {
  type = "activity",
  menuEntry = "My Tool",    -- shown in home menu, nil = hidden
}
```

### Reader

A file format reader. Registers file extensions it can open:

```lua
plugin = {
  type = "reader",
  fileExtensions = { "epub", "epub3" },
}

function plugin.canOpen(path)
  return true
end

function plugin.open(path)
  -- parse file, build pages
end

function plugin.renderPage(pageNum)
  -- draw current page
end

function plugin.getPageCount()
  return totalPages
end
```

### Service

Background functionality (sync, auto-download). No UI:

```lua
plugin = {
  type = "service",
  menuEntry = nil,
}

function plugin.onEvent(event)
  if event == "wifi_connected" then
    -- sync progress, check updates, etc.
  end
end
```

## Plugin Manifest

Every plugin must define a `plugin` table:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | Yes | Human-readable name |
| id | string | Yes | Unique identifier (lowercase, no spaces) |
| version | string | No | Semantic version |
| author | string | No | Author name |
| type | string | Yes | "activity", "reader", or "service" |
| menuEntry | string | No | Text shown in home menu (nil = hidden) |
| fileExtensions | table | Readers only | File extensions this plugin handles |

## Lifecycle

```
onEnter(arg)    → Called when plugin becomes active
                  arg is optional (e.g. file path for readers)

loop()          → Called every frame (~60Hz when awake)
                  Handle input, update display

onExit()        → Called when leaving plugin
                  Free resources, save state
```

## Navigation

```lua
-- Switch to another plugin
plugin.navigate("settings")

-- Switch to a reader with a file
plugin.navigate("epub_reader", "/books/my_book.epub")

-- Go back to previous plugin / home
plugin.finish()

-- Go directly home
plugin.goHome()
```

## API Reference

See `build_spec.md` for the complete API documentation for:
- `display.*` — rendering
- `input.*` — button handling
- `storage.*` — file I/O
- `wifi.*` — network
- `font.*` — font management
- `zip.*` — archive access
- `xml.*` — XML/HTML parsing
- `json.*` — JSON parsing
- `system.*` — device info
- `i18n.*` — translations

## Shared Lua Modules

CrossLua Reader ships shared modules in `/plugins/lib/` that plugins can `require`:

### lib.settings
Persistent settings. All plugins should use this instead of reading/writing JSON directly.

```lua
local settings = require("lib.settings")
settings.load()                              -- read from SD (once per plugin)
local val = settings.get("orientation", 0)   -- get with default
settings.set("orientation", 1)               -- change in memory
settings.save()                              -- persist to SD
```

See `docs/settings-schema.md` for all available keys, types, and defaults.

### lib.fonts
System font manager. Loads UI font, reader font, and optional fallback font based on settings and language.

```lua
local fonts = require("lib.fonts")
fonts.init()                                 -- load fonts + auto-load fallback (call in onEnter)
display.drawText(fonts.ui, x, y, "Menu")     -- Ubuntu 12 UI font
display.drawText(fonts.reader, x, y, "Book") -- user's reader font (with fallback if set)
fonts.reload_reader()                        -- reload after font/language settings change
fonts.cleanup()                              -- unload all (call in onExit)

-- Script detection for reader plugins:
local scripts = fonts.detect_scripts(text)   -- returns e.g. {"hebrew"}
fonts.detect_fallbacks(text)                 -- auto-load fallback from text content
```

### lib.progress
Reading progress persistence. Stores progress alongside book files.

```lua
local progress = require("lib.progress")
progress.save("/books/my_book.epub", {page=42, totalPages=210, offset=34567})
local p = progress.load("/books/my_book.epub")  -- returns table or nil
progress.clear("/books/my_book.epub")            -- delete saved progress
```

### lib.theme
Theme metrics for consistent layout. Returns Lyra (modern) or Classic constants.

```lua
local theme = require("lib.theme")
local t = theme.get()  -- { menu_row_height=64, corner_radius=6, side_padding=20, ... }

-- Switch theme:
theme.set("classic")  -- or "lyra" (default)
```

### lib.ui
Shared UI drawing helpers. Uses theme metrics automatically. Selection highlights use dithered gray (readable text on gray background, matching CrossPoint's Lyra theme).

```lua
local ui = require("lib.ui")

ui.draw_header(font_id, "My Plugin")        -- top bar with battery
ui.draw_menu(font_id, items, selected, y)    -- scrollable menu with gray selection
ui.draw_list(font_id, items, selected, y, max_visible, scroll_offset)  -- file/item list
ui.draw_button_hints(font_id, {"Back", "Select", "", ""})  -- CrossPoint-style 4-button bar
```

Menu items format: `{ {label="Browse Files"}, {label="Settings"} }`

Button hints: pass a table of 4 strings for the 4 front buttons `{Back, Confirm, Left, Right}`. Use `""` for empty buttons. Rendered as bordered cells matching CrossPoint's button hint bar.

### lib.buttons
Orientation-aware button mapping and hint labels. Maps logical actions (up, down, left, right, back, confirm) to physical buttons per orientation.

```lua
local buttons = require("lib.buttons")

-- Get hint labels for the 4 front buttons (orientation-aware):
ui.draw_button_hints(font_id, buttons.get("home", orientation))
ui.draw_button_hints(font_id, buttons.get("browser", orientation))
ui.draw_button_hints(font_id, buttons.get("settings", orientation))
ui.draw_button_hints(font_id, buttons.get("reader", orientation))

-- Apply button remap for an orientation (call on boot and orientation change):
input.setMapping(buttons.get_mapping(orientation))
```

Format in buttons.lua is `logical_action = "physical_button"` — read as "to perform [action], press [button]". Edit `/plugins/lib/buttons.lua` to customize mappings or labels.

### lib.lang
Language pack discovery and UI translation. Falls back to English for missing keys.

```lua
local lang = require("lib.lang")
lang.load("he")                              -- load Hebrew language pack
local label = lang.tr("settings")            -- → "הגדרות" (or "Settings" if key missing)
local dir = lang.get_direction()             -- → "rtl"
local family = lang.get_font_family()        -- → "NotoSansHebrew"
local packs = lang.discover()                -- scan /languages/ for available packs
```

### lib.json
Shared recursive JSON parser. Handles objects, arrays, strings, numbers, booleans, null.

```lua
local json = require("lib.json")
local data = json.decode('{"name": "test", "count": 42}')
-- data.name == "test", data.count == 42
```

### lib.status_bar
Reader status bar for book progress.

```lua
local status_bar = require("lib.status_bar")
status_bar.draw(font_id, progress_pct, current_page, total_pages, "Book Title")
```

## Templates

Home screen templates live in `/templates/` on the SD card. To customize:

1. Pick a template (e.g., `home_classic.lua`)
2. Copy it to `/plugins/home.lua`
3. Reboot — your custom home screen loads

Anyone can create and share templates. They're just Lua plugins that define `id = "home"`.

## Tips

### Memory
- The device has ~150KB available for plugin data
- Avoid loading entire files into memory — use `storage.readBytes()` for streaming
- Release large tables when done: `myTable = nil; collectgarbage()`

### Performance
- E-ink refresh takes 400ms-1.5s — Lua computation is rarely the bottleneck
- For heavy loops, call `system.delay(1)` periodically to yield to the watchdog
- Text measurement (`display.getTextWidth`) is a native call — fast

### Display
- Always call `display.refresh()` after drawing — nothing appears until refresh
- Use `display.refreshFull()` every 10-15 pages to clear ghosting
- Get screen dimensions with `display.width()` / `display.height()` — don't hardcode 480/800

### Files
- Plugin state/cache goes in `/plugins/.cache/<plugin_id>/`
- Books are in `/books/` or user-chosen directories
- Font files are in `/fonts/`
