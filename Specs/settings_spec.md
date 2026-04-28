# Settings & Persistence System Spec

## Problem

Currently CrossLua Reader has no persistence:
- Font choice, orientation, button layout don't survive reboot
- Every plugin hardcodes its own font loading
- Orientation only affects the renderer, not the whole app
- Button labels are cosmetic only — no actual remapping
- Settings plugin writes /settings.json but nothing reads it back

## Goals

1. **Persistent settings** — survive reboot, stored as JSON on SD
2. **System fonts** — UI and reader fonts loaded once, shared across all plugins
3. **Global orientation** — applies to everything (home, browser, settings, readers)
4. **Button remapping** — pre-defined layouts per orientation, user custom layout option
5. **Settings apply immediately** — no reboot required for most changes

## Settings Storage

### File: `/settings.json`

```json
{
  "fontFamily": "NotoSans",
  "fontSize": "14",
  "orientation": 0,
  "screenMargin": 10,
  "refreshFrequency": 15,
  "theme": "lyra",
  "buttonLayout": "default",
  "language": "en"
}
```

- `buttonLayout`: `"default"` = use orientation-aware defaults. `"custom"` = use user's custom layout from buttons.lua.
- Written by settings plugin on change
- Read by Lua plugins via `lib/settings.lua`

## Settings Module (lib/settings.lua)

Pure Lua module — reads/writes `/settings.json` via `storage.*` API.

```lua
local settings = require("lib.settings")
settings.load()                           -- read from SD
local val = settings.get("orientation", 0) -- get with default
settings.set("orientation", 1)            -- set in memory
settings.save()                           -- write to SD
```

Includes a simple JSON parser/encoder for flat key-value objects (no nested objects needed for settings).

## System Fonts (lib/fonts.lua)

Instead of every plugin doing `font.load(...)`, a shared module loads fonts based on settings:

```lua
local fonts = require("lib.fonts")
fonts.init()                -- loads UI + reader fonts from settings

display.drawText(fonts.ui, x, y, "Menu Title")       -- Ubuntu 12 UI font
display.drawText(fonts.reader, x, y, "Book text")     -- user's chosen reader font
```

- `fonts.ui` — Ubuntu 12 Regular (system UI font, always loaded)
- `fonts.reader` — user's choice from settings (family + size)
- Called in every plugin's `onEnter()`, cleaned up in `onExit()`
- Font paths derived from settings: `/fonts/{family}-{size}-Regular.cfont`

### Font IDs Across Plugin Switches

Fonts are per-Lua-state. When plugin manager switches plugins, old state is destroyed. Each plugin calls `fonts.init()` in `onEnter()` which re-loads from settings. Since settings are on SD, they're consistent.

## Global Orientation

- Stored in settings as `orientation` (0-3)
- Applied at boot by home plugin: `display.setOrientation(settings.get("orientation", 0))`
- Applied immediately when changed in settings plugin
- Renderer orientation is a C global — persists across plugin switches without re-applying
- Affects ALL screens: home, browser, settings, readers

## Button System

### buttons.lua Structure

`buttons.lua` serves three purposes:
1. **Pre-defined layouts** — orientation-aware defaults, never modified by code
2. **Custom layout** — user's personal mapping, only section that gets written
3. **Label sets** — context-specific display labels (home, browser, settings, reader)

```lua
-- lib/buttons.lua
local M = {}

-- ══════════════════════════════════════════════
-- PRE-DEFINED LAYOUTS (DO NOT MODIFY)
-- These are the built-in button mappings per orientation.
-- ══════════════════════════════════════════════

M.layouts = {
    portrait = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back=0, confirm=1, left=2, right=3},
    },
    landscape_cw = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back=0, confirm=1, left=2, right=3},
    },
    inverted = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back=0, confirm=1, left=2, right=3},
    },
    landscape_ccw = {
        labels = {"Back", "Confirm", "Left", "Right"},
        mapping = {back=0, confirm=1, left=2, right=3},
    },
}

-- Maps orientation index (0-3) to layout name
M.orientation_layout = {
    [0] = "portrait",
    [1] = "landscape_cw",
    [2] = "inverted",
    [3] = "landscape_ccw",
}

-- ══════════════════════════════════════════════
-- CONTEXT LABELS
-- Display labels for the 4 front buttons per screen context.
-- ══════════════════════════════════════════════

M.context = {
    home     = {"Back", "Select", "", ""},
    browser  = {"Back", "Open", "", ""},
    settings = {"Back", "Change", "", ""},
    reader   = {"Back", "", "Prev", "Next"},
    confirm  = {"Cancel", "OK", "", ""},
    default  = {"Back", "Confirm", "Left", "Right"},
}

-- ══════════════════════════════════════════════
-- CUSTOM LAYOUT (user-defined, written by settings)
-- Set to nil to use orientation defaults.
-- ══════════════════════════════════════════════

M.custom = nil
-- Example custom layout:
-- M.custom = {
--     labels = {"Exit", "OK", "Prev", "Next"},
--     mapping = {back=3, confirm=0, left=1, right=2},
-- }

-- ══════════════════════════════════════════════
-- API
-- ══════════════════════════════════════════════

-- Get the active button mapping based on settings
function M.get_mapping(orientation)
    if M.custom then
        return M.custom.mapping
    end
    local layout_name = M.orientation_layout[orientation or 0]
    return M.layouts[layout_name].mapping
end

-- Get context-specific labels for button hints
function M.get(context)
    return M.context[context] or M.context.default
end

return M
```

### How Remapping Works

1. **Default behavior**: No custom layout set. Buttons use the pre-defined layout for the current orientation. When orientation changes, button mapping changes automatically.

2. **User sets custom layout**: In settings, user picks "Custom" button layout and assigns physical buttons to logical roles. This writes `M.custom = {...}` to buttons.lua. Custom layout overrides orientation defaults.

3. **User resets to default**: In settings, user picks "Default". This sets `M.custom = nil` in buttons.lua. Orientation-aware defaults resume.

4. **Pre-defined layouts are never modified** — only `M.custom` is written/cleared.

### Orientation-Aware Defaults

The exact mapping per orientation is TBD (user will specify later). The structure is ready — just fill in the mapping tables.

## Boot Sequence with Settings

```
1. C runtime boots (HAL, display, SD, power, renderer, font cache)
2. Plugin manager discovers plugins
3. Plugin manager starts "home" plugin
4. home.lua onEnter():
   a. settings = require("lib.settings"); settings.load()
   b. display.setOrientation(settings.get("orientation", 0))
   c. fonts = require("lib.fonts"); fonts.init()
   d. render home screen
5. User navigates to settings → changes orientation:
   a. settings.set("orientation", 2)
   b. display.setOrientation(2)  -- applies immediately
   c. settings.save()  -- persists to SD
   d. re-render settings screen in new orientation
6. User goes back to home → already in correct orientation
```

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `sdcard/plugins/lib/settings.lua` | NEW | Settings read/write/persist with JSON parser |
| `sdcard/plugins/lib/fonts.lua` | NEW | System font loading from settings |
| `sdcard/plugins/lib/buttons.lua` | REWRITE | Pre-defined layouts, custom slot, orientation mapping |
| `sdcard/plugins/home.lua` | MODIFY | Apply settings on boot, use lib/fonts |
| `sdcard/plugins/file_browser.lua` | MODIFY | Use lib/fonts instead of hardcoded font |
| `sdcard/plugins/settings.lua` | REWRITE | Read/write via lib/settings, apply changes immediately |

## Auto Font Fallback for Languages

### Problem

A user reading a Hebrew EPUB with NotoSans selected won't see Hebrew text — NotoSans only has Latin/Cyrillic glyphs. The user shouldn't have to manually switch fonts per language.

### Solution: Automatic Font Fallback

The font system detects non-Latin codepoints in text content and automatically loads the appropriate language-specific font as a fallback.

### How It Works

1. **Content detection**: When a reader opens a file, `lib/fonts.lua` scans the first chunk for non-Latin codepoints using the BiDi classifier ranges
2. **Fallback loading**: If Hebrew codepoints are detected, the Hebrew font is loaded into an additional font slot
3. **Render-time fallback**: `display.drawText` checks if a glyph exists in the primary font's intervals. If not, it tries the fallback font. This requires a C-side change to `font_render_draw_text`.
4. **Automatic**: No user action needed — just open the file

### Settings

```json
{
  "languageFonts": {
    "hebrew": "NotoSansHebrew",
    "arabic": "NotoSansArabic",
    "default": "NotoSans"
  }
}
```

### Font Fallback Chain

```
Primary font (user's choice: NotoSans-14)
  → glyph found? render it
  → glyph NOT found? check language fallback fonts
    → Hebrew font (NotoSansHebrew-14) if Hebrew codepoints detected
    → Arabic font (NotoSansArabic-14) if Arabic detected (future)
    → Replacement glyph (U+FFFD) if no fallback has it
```

### C-Side Changes Needed

`font_render_draw_text` currently takes a single `font_data_t`. For fallback, it needs access to a fallback font. Two approaches:

**Option A: Fallback in font_render (recommended)**
- Add `font_render_set_fallback(font_data_t *fallback, const char *path)`
- `render_glyph` checks primary → if glyph missing, checks fallback
- Simple, contained change

**Option B: Fallback in Lua**
- Lua detects missing glyphs and splits text into runs per font
- More flexible but slower and complex

### lib/fonts.lua with Fallback

```lua
local M = {}

M.ui = nil          -- Ubuntu UI font
M.reader = nil      -- Primary reader font
M.fallbacks = {}    -- Language fallback font IDs

function M.init()
    local settings = require("lib.settings")

    -- UI font (always Ubuntu)
    M.ui = font.load("/fonts/Ubuntu-12-Regular.cfont")

    -- Reader font (user's choice)
    local family = settings.get("fontFamily", "NotoSans")
    local size = settings.get("fontSize", "14")
    M.reader = font.load("/fonts/" .. family .. "-" .. size .. "-Regular.cfont")
end

-- Call after opening a file to detect and load needed fallbacks
function M.detect_fallbacks(text_sample)
    local settings = require("lib.settings")
    local size = settings.get("fontSize", "14")
    local lang_fonts = settings.get("languageFonts", {})

    -- Check for Hebrew codepoints in the sample
    if bidi_has_rtl(text_sample) then  -- need to expose bidi_has_rtl to Lua
        local hebrew_family = lang_fonts.hebrew or "NotoSansHebrew"
        local path = "/fonts/" .. hebrew_family .. "-" .. size .. "-Regular.cfont"
        if storage.exists(path) and not M.fallbacks.hebrew then
            M.fallbacks.hebrew = font.load(path)
            font.setFallback(M.reader, M.fallbacks.hebrew)  -- C-side link
        end
    end
end

function M.cleanup()
    if M.ui then font.unload(M.ui); M.ui = nil end
    if M.reader then font.unload(M.reader); M.reader = nil end
    for k, fid in pairs(M.fallbacks) do
        font.unload(fid)
    end
    M.fallbacks = {}
end

return M
```

### New Lua API Functions Needed

- `font.setFallback(primaryId, fallbackId)` — link a fallback font to a primary font
- Possibly: `display.hasGlyph(fontId, codepoint)` — check if a font has a specific glyph (optional, fallback can be implicit)

### Files to Create/Modify

| File | Change |
|------|--------|
| `lib/font/font_render.c` | Add fallback font support to glyph lookup |
| `lib/font/font_render.h` | Add `font_render_set_fallback()` |
| `lib/lua_api/api_font.c` | Add `font.setFallback()` binding |
| `sdcard/plugins/lib/fonts.lua` | Add `detect_fallbacks()` with content scanning |

## Language Packs

### Concept

A language pack is a folder dropped into `/languages/` on the SD card. The settings screen auto-discovers installed packs and lets the user switch languages. No firmware changes needed — just drop the folder.

### Language Pack Structure

```
/languages/
├── en/                      ← ships by default
│   ├── lang.json            ← metadata + UI string translations
│   └── (no extra fonts — Latin covered by default NotoSans)
├── he/
│   ├── lang.json            ← Hebrew metadata + UI strings
│   └── fonts/
│       ├── NotoSansHebrew-12-Regular.cfont
│       ├── NotoSansHebrew-14-Regular.cfont
│       ├── NotoSansHebrew-14-Bold.cfont
│       └── ...
├── zh/
│   ├── lang.json
│   └── fonts/
│       ├── NotoSansCJK-12-Regular.cfont
│       └── ...
├── ar/
│   ├── lang.json
│   └── fonts/
│       ├── NotoSansArabic-14-Regular.cfont
│       └── ...
└── ...
```

### lang.json Format

```json
{
  "code": "he",
  "name": "עברית",
  "nameEnglish": "Hebrew",
  "direction": "rtl",
  "fontFamily": "NotoSansHebrew",
  "unicodeRanges": ["0590-05FF", "FB1D-FB4F"],
  "strings": {
    "back": "חזור",
    "select": "בחר",
    "settings": "הגדרות",
    "browse_files": "עיון בקבצים",
    "continue_reading": "המשך קריאה",
    "font_size": "גודל גופן",
    "orientation": "כיוון",
    "language": "שפה",
    "battery": "סוללה"
  }
}
```

### How It Works

1. **Discovery**: Settings plugin scans `/languages/` for subdirectories containing `lang.json`
2. **Language menu**: Shows all discovered languages by name (e.g., "English", "עברית", "中文")
3. **Switch**: User selects a language → settings saves `"language": "he"` to `/settings.json`
4. **Font loading**: `lib/fonts.lua` checks the active language pack and loads language-specific fonts as fallbacks
5. **UI strings**: `lib/settings.lua` provides `settings.tr("back")` which returns the translated string, falling back to English if the key is missing
6. **Auto-fallback**: When reading content, the font fallback system uses `unicodeRanges` from the language pack to detect which fallback font to load

### Target Languages

#### Tier 1 — Ship with v1.0 (Latin/Cyrillic, covered by existing NotoSans)
| Code | Language | Script | Extra Font Needed |
|------|----------|--------|-------------------|
| en | English | Latin | No |
| es | Spanish | Latin | No |
| fr | French | Latin | No |
| de | German | Latin | No |
| pt | Portuguese | Latin | No |
| it | Italian | Latin | No |
| nl | Dutch | Latin | No |
| pl | Polish | Latin | No |
| ru | Russian | Cyrillic | No |
| uk | Ukrainian | Cyrillic | No |
| sv | Swedish | Latin | No |
| da | Danish | Latin | No |
| fi | Finnish | Latin | No |
| ro | Romanian | Latin | No |
| cs | Czech | Latin | No |
| tr | Turkish | Latin | No |

#### Tier 2 — RTL Languages (need specific fonts)
| Code | Language | Script | Font Source |
|------|----------|--------|------------|
| he | Hebrew | Hebrew | NotoSansHebrew (already have it) |
| ar | Arabic | Arabic | NotoSansArabic |
| fa | Persian/Farsi | Arabic | NotoSansArabic (shared) |

#### Tier 3 — CJK (need large fonts, significant flash/SD space)
| Code | Language | Script | Font Source | Notes |
|------|----------|--------|------------|-------|
| zh | Chinese (Simplified) | CJK | NotoSansSC | Large font (~5-10MB .cfont) |
| zh-TW | Chinese (Traditional) | CJK | NotoSansTC | Large font |
| ja | Japanese | CJK + Kana | NotoSansJP | Large font |
| ko | Korean | Hangul | NotoSansKR | Large font |

#### Tier 4 — Indic & Southeast Asian (need specific fonts + shaping)
| Code | Language | Script | Font Source | Notes |
|------|----------|--------|------------|-------|
| hi | Hindi | Devanagari | NotoSansDevanagari | Complex shaping |
| bn | Bengali | Bengali | NotoSansBengali | Complex shaping |
| th | Thai | Thai | NotoSansThai | |
| vi | Vietnamese | Latin Extended | No (already in NotoSans) | |

### Font Sources

All target fonts are available from Google's Noto Font project:
- https://github.com/notofonts/ (per-script repos)
- https://fonts.google.com/noto (download page)

Each language pack's fonts go through the same `cfont-convert` pipeline we already built. The converter just needs the right Unicode ranges added via `--additional-intervals`.

### Size Estimates for .cfont Files

| Script | Glyph Count | Estimated .cfont Size (per size/style) |
|--------|-------------|---------------------------------------|
| Latin/Cyrillic | ~1,070 | ~55KB |
| Hebrew | ~88 extra | ~5KB additional |
| Arabic | ~300 | ~20KB |
| CJK (Chinese) | ~7,000+ | ~500KB-2MB |
| Devanagari | ~200 | ~15KB |
| Korean | ~2,400 | ~150KB |

CJK is the outlier — Chinese fonts are massive. These would be optional downloads, not shipped by default.

### Implementation Priority

1. **Phase 5.5**: Build the language pack system (discovery, lang.json, settings integration)
2. **v1.0 release**: Ship with Tier 1 languages (just lang.json files, no extra fonts needed) + Hebrew
3. **Post-release**: Arabic, CJK, Indic packs as community contributions

### What Ships by Default

- `/languages/en/lang.json` — English (default)
- `/languages/he/lang.json` + `/languages/he/fonts/*.cfont` — Hebrew (since we already have the fonts)
- All other languages: downloadable packs (future: from a repository or website)

## Sleep Screen

### Behavior

- **Trigger**: Long-hold power button OR idle timeout (configurable, default 10 min)
- **While reading**: User can choose wallpaper sleep OR "clear sleep" (stays on current page, "SLEEP" text shows by page count)
- **From menus**: Always uses wallpaper sleep

### Wallpaper System

```
/wallpapers/
├── sleep_01.bmp
├── sleep_02.bmp
├── my_custom.bmp
└── ...
```

- Settings: choose a specific wallpaper, cycle top-to-bottom, or randomize
- Stored in settings: `"sleepMode": "single"` / `"cycle"` / `"random"` / `"clear"`
- `"sleepWallpaper": "sleep_01.bmp"` (for single mode)

### Settings

```json
{
  "sleepMode": "single",
  "sleepWallpaper": "sleep_01.bmp",
  "sleepTimeout": 10
}
```

## Reading Progress

### Per-book progress files

Stored alongside the book with a matching name:

```
/books/
├── my_book.epub
├── my_book.epub_progress        ← progress file
├── notes.txt
├── notes.txt_progress
└── torah.md
    torah.md_progress
```

### Progress file format (JSON)

```json
{
  "plugin": "txt_reader",
  "page": 42,
  "totalPages": 210,
  "offset": 34567,
  "chapter": 3,
  "percentage": 20.0,
  "lastRead": "2026-04-25T19:30:00"
}
```

### lib/progress.lua

```lua
local progress = require("lib.progress")
progress.save(book_path, {page=42, totalPages=210, offset=34567})
local p = progress.load(book_path)  -- returns table or nil
```

Reader plugins use this standard module. Progress saved on page turn and plugin exit.

## Plugin Management

### Plugin Settings Area

A section in Settings that shows all discovered plugins:

```
Plugin Manager
─────────────────────────
[ON]  Home           (system)
[ON]  File Browser   (system)
[ON]  Settings       (system)
[ON]  TXT Reader     (system)
[OFF] Sefaria        (user)     ← tap to activate
[OFF] Dictionary     (user)     ← shows missing deps
─────────────────────────
RAM: ████░░░░ 45% used
```

- **System plugins**: always on, cannot be disabled
- **User plugins**: must be manually activated, persists once on
- **Dependencies**: shown before activation. If missing, plugin cannot be enabled
- **RAM bar**: shows current memory usage so user can judge if they can enable more plugins
- **Priority**: if two plugins register the same file extension, user sets which one is the default reader for that extension

### Plugin Manifest Extensions

```lua
plugin = {
    name = "Sefaria Browser",
    id = "sefaria",
    type = "activity",
    system = false,           -- true = ships with firmware, always on
    dependencies = {"lib.ui", "lib.fonts"},  -- required lib modules
    menuEntry = "Sefaria",
}
```

## Error Recovery

### Plugin Crash Handling

When a plugin crashes (pcall error in loop/onEnter):

1. Stop the crashed plugin (onExit + close Lua state)
2. Display crash screen:
   ```
   ┌─────────────────────────────────┐
   │         Plugin Crashed          │
   │                                 │
   │  Plugin: "Sefaria Browser"      │
   │  Error: attempt to index nil    │
   │                                 │
   │  The plugin has been stopped.   │
   │                                 │
   │  [Confirm] Return to Home      │
   └─────────────────────────────────┘
   ```
3. Wait for button press → start home plugin
4. **Never leave the user stuck** — always recover to home

### Crash screen rendering

The crash screen must render WITHOUT Lua (since Lua state may be corrupt). Use the C-side `font_render_draw_text` directly from `plugin_manager.c`. This means the C side needs a font loaded for the crash screen — load the UI font in the C boot sequence before any Lua runs.

## Cache Management

### Convention

Plugins that need to cache data use `/cache/{plugin_id}/`:

```
/cache/
├── txt_reader/
│   └── {book_hash}_index.bin
├── epub_reader/
│   └── {book_hash}/
│       ├── spine.bin
│       └── sections/
└── sefaria/
    └── index_cache.json
```

### Clear cache

Settings → System → Clear Cache:
- Shows total cache size
- Clears all contents of `/cache/`
- Does NOT clear reading progress (that's stored with the books)

## Screen Refresh Management

### Ghosting Prevention

Shared via `lib/reader_utils.lua`:

```lua
local reader_utils = require("lib.reader_utils")

-- Track pages since last full refresh
reader_utils.page_turned()  -- call on every page turn

-- Auto-decides: fast refresh normally, full refresh every N pages
reader_utils.refresh()      -- calls display.refresh() or display.refreshFull()
```

- Default: full refresh every 15 pages (matches CrossPoint)
- Configurable in settings: `"refreshFrequency": 15`
- Factory default tuned for minimal ghosting

## Auto-Sleep

### Settings

```json
{
  "sleepTimeout": 10
}
```

Options: 1, 5, 10, 15, 30 minutes.

### Exceptions

- **Web server running**: sleep suppressed entirely while WiFi server is active
- **USB connected**: sleep suppressed (prevents bricking during flash)
- **Active download**: sleep suppressed

### Implementation

`hal_power_check_sleep()` already tracks idle time. Add a `hal_power_suppress_sleep(bool suppress)` function that plugins can call when they need to stay awake.

## Multitasking (Side Question Answer)

The ESP32-C3 is single-core with ~380KB RAM. True multitasking (web server + reader simultaneously) is technically possible using FreeRTOS tasks, but:

- WiFi stack uses ~50KB RAM when active
- Lua state uses ~50KB per plugin
- Running two plugins simultaneously would need ~100KB extra RAM

**Practical approach**: The web server could run as a FreeRTOS background task (C-side, not Lua) while a reader plugin runs in the foreground. The reader wouldn't use WiFi, so there's no conflict. The web server task just handles uploads and stays idle otherwise.

This is a Phase 7+ feature. For now, WiFi features are foreground-only.

## First Boot Experience

When no `/settings.json` exists:

1. All settings use sane defaults (English, NotoSans 14, Portrait, Lyra theme)
2. Home screen shows normally
3. No "welcome wizard" — just works out of the box
4. Settings are created on first change and saved

## Open Questions

1. Exact button mapping per orientation — user will specify later
2. Should reader font bold/italic also be pre-loaded, or loaded on demand by reader plugins?
3. Should Ubuntu UI font be available in multiple sizes (10, 12) or just 12?
4. CJK fonts are very large (500KB-2MB per .cfont). Should we support partial glyph loading or require the full font?
