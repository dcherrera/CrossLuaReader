# CrossLua Reader Plugin Developer Guide

## Quick Start

Plugins come in two formats:

### Single-file plugin
Create `/plugins/hello.lua` on the SD card:

```lua
plugin = {
  name = "Hello World",
  id = "hello",
  type = "activity",
  menuEntry = "Hello",
}

function plugin.onEnter()
  local fonts = require("lib.fonts")
  fonts.init()
  display.clear()
  display.drawText(fonts.ui, 50, 100, "Hello from Lua!")
  display.refresh()
end

function plugin.loop()
  if input.wasPressed(input.BACK) then
    plugin.goHome()
  end
end

function plugin.onExit()
  require("lib.fonts").cleanup()
end
```

### Folder plugin (community plugins)
Create `/plugins/hello/main.lua`:

```
/plugins/hello/
  main.lua          ← entry point (required, contains plugin table)
  helpers.lua       ← additional modules
  data/             ← assets, configs, images
```

`main.lua` uses the same format as a single-file plugin. The folder can contain any additional files the plugin needs — helper modules, data files, assets, etc.

Reboot the device (or long-press power to reload). The plugin appears in the home menu.

### Templates

Ready-to-use templates are in `/templates/` on the SD card:

| Template | Description |
|----------|-------------|
| `plugin_single_file.lua` | Single-file plugin starter |
| `community_plugin/` | Folder plugin starter with comments and structure |
| `home_lyra.lua` | Lyra-style home screen |
| `home_classic.lua` | Classic home screen |

Copy a template to `/plugins/` and edit to create your plugin.

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

## Plugin Discovery

The plugin manager scans `/plugins/` on boot (or on SD reload via power long-press). Discovery is done entirely in C by reading the first 1KB of each plugin file and extracting manifest fields with string matching — **no Lua state is created** during discovery, making it fast (~12ms for 3 plugins) and zero heap overhead.

1. **Single-file plugins**: Any `.lua` file directly in `/plugins/` (e.g., `/plugins/my_tool.lua`)
2. **Folder plugins**: Any subdirectory containing `main.lua` (e.g., `/plugins/my_tool/main.lua`)

Plugins are only loaded into a Lua state when the user navigates to them. Discovery just reads the manifest.

Skipped automatically:
- `/plugins/lib/` — reserved for shared Lua modules
- Dotfiles and dot-directories (`.hidden`)
- Directories without a `main.lua`
- Non-`.lua` files

Both formats use the same manifest and lifecycle — the only difference is where the entry point file lives.

**Important**: The `plugin = { ... }` table must be in the first 1KB of the file for discovery to find it. Keep the manifest near the top.

### When to use a folder plugin
- Your plugin has multiple `.lua` files (helpers, modules, screens)
- Your plugin ships with data files (configs, assets, templates)
- You're distributing your plugin for others to install (drop one folder on SD)

### When to use a single file
- Simple plugins with all logic in one file
- Core system plugins (home, settings, file_browser)

## Plugin Manifest

Every plugin must define a `plugin` table (in `main.lua` for folder plugins, or in the `.lua` file for single-file plugins):

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

-- Discover available font packs from /fonts/ subdirectories:
local families = fonts.discover_families()   -- returns e.g. {"Bookerly", "NotoSans"}

-- Script detection for reader plugins:
local scripts = fonts.detect_scripts(text)   -- returns e.g. {"hebrew"}
fonts.detect_fallbacks(text)                 -- auto-load fallback from text content
```

See `docs/font-packs.md` for how to create and install font packs.

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

## Sleep & Error Recovery

### Crash Recovery
If your plugin's `loop()` function throws a Lua error, the device shows a crash screen with the error message and waits for the user to press any button, then returns to home. Your plugin is stopped but not disabled — it can be launched again.

### Sleep Screen
Users can drop BMP images into `/wallpapers/` on the SD card. The settings plugin lets them choose the sleep screen mode (blank, single wallpaper, cycle, random, or stay-on-page).

### Sleep Hook (Custom Sleep Content)
Plugins can register a callback to draw custom content on the sleep screen:

```lua
system.setSleepHook(function()
    -- Draw after the base sleep screen (wallpaper/blank/clear) renders,
    -- before the display refresh. Use any display.* API.
    display.fillRect(20, 600, 440, 120)
    display.drawRect(20, 600, 440, 120)
    display.drawText(fonts.reader, 30, 610, quote_text)
end)
```

The hook is cleared automatically on plugin exit. Errors in the hook are caught and logged — they don't prevent sleep.

See `docs/sleep-screen.md` for wallpaper formats, modes, and full API reference.

### Power Button
- Short press (0.5-2s): Manual sleep
- Long press (>2s): SD card reload — re-mounts SD and restarts plugins from home. Useful after editing Lua files without reflashing.

### SD Reload from Lua
```lua
system.reload()  -- reinit SD card and restart from home
```

## Templates

Templates are in the `templates/` directory of the repository:

| Template | Type | Description |
|----------|------|-------------|
| `plugin_single_file.lua` | File | Minimal single-file plugin starter |
| `community_plugin/` | Folder | Full folder plugin with comments and structure |
| `home_lyra.lua` | File | Lyra-style home screen |
| `home_classic.lua` | File | Classic home screen |

### Creating a plugin from a template

**Single-file:**
1. Copy `plugin_single_file.lua` from `templates/` to `/plugins/my_plugin.lua` on your SD card
2. Edit the `plugin` table (change name, id, menuEntry)
3. Reboot or long-press power to reload

**Folder plugin (community):**
1. Copy `community_plugin/` from `templates/` to `/plugins/my_plugin/` on your SD card
2. Edit `main.lua` — change the manifest, add your logic
3. Add helper files, data, assets as needed
4. Reboot or long-press power to reload

**Custom home screen:**
1. Copy `home_lyra.lua` from `templates/` to `/plugins/home.lua` on your SD card
2. Edit to customize — must keep `id = "home"`
3. Reboot

### Distributing plugins

To share a plugin with others:
- **Single-file**: share the `.lua` file — user drops it in `/plugins/`
- **Folder plugin**: share the folder as a zip — user extracts to `/plugins/`

Folder plugins are self-contained: all files live in one directory, nothing to configure.

## Tips

### Memory
- The device has ~89KB free heap available for plugin data
- The Lua state itself uses ~80-90KB — this is the largest single consumer
- Fonts use on-demand glyph loading (~2-8KB RAM per font instead of ~25-31KB)
- Use `{skip_reader = true}` in `fonts.init()` if your plugin only needs the UI font
- Avoid loading entire files into memory — use `storage.readBytes()` for streaming
- Release large tables when done: `myTable = nil; collectgarbage()`
- Check available memory: `system.freeHeap()` returns bytes free

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
