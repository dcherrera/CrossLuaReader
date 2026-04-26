# Language Packs

Language packs provide non-Latin font fallback and UI translations for CrossLua Reader. They are drop-in folders on the SD card — no firmware change required.

## Folder Structure

```
/languages/
  en/
    lang.json              # English (default, always loaded as fallback)
  he/
    lang.json              # Hebrew metadata + UI translations
    fonts/
      NotoSansHebrew-12-Regular.cfont
      NotoSansHebrew-14-Regular.cfont
      NotoSansHebrew-16-Regular.cfont
      NotoSansHebrew-18-Regular.cfont
```

## lang.json Schema

```json
{
  "code": "he",
  "name": "עברית",
  "direction": "rtl",
  "fontFamily": "NotoSansHebrew",
  "strings": {
    "home": "בית",
    "settings": "הגדרות",
    "browse_files": "עיון בקבצים",
    "continue_reading": "המשך בקריאה",
    "back": "חזרה",
    "select": "בחר",
    "open": "פתח",
    "change": "שנה",
    "cancel": "ביטול",
    "ok": "אישור",
    "prev": "הקודם",
    "next": "הבא",
    "no_files": "לא נמצאו קבצים",
    "no_book": "אין ספר להמשיך",
    "language": "שפה",
    "font_family": "גופן",
    "font_size": "גודל גופן",
    "orientation": "כיוון",
    "screen_margin": "שוליים",
    "refresh_freq": "תדירות רענון",
    "sleep_timeout": "זמן שינה",
    "theme": "ערכת נושא"
  }
}
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| code | string | Yes | ISO 639-1 language code (e.g. "he", "ar") |
| name | string | Yes | Display name in the language's own script |
| direction | string | No | "ltr" (default) or "rtl" |
| fontFamily | string | No | Font family name for fallback (e.g. "NotoSansHebrew"). Null for languages that use Latin glyphs. |
| strings | object | No | UI string translations. Missing keys fall back to English. |

### String Keys

All keys used by the core plugins:

| Key | Default (English) | Used In |
|-----|-------------------|---------|
| home | Home | home.lua header |
| settings | Settings | home.lua menu, settings.lua header |
| browse_files | Browse Files | home.lua menu |
| continue_reading | Continue Reading | home.lua menu |
| back | Back | button hints |
| select | Select | button hints |
| open | Open | button hints |
| change | Change | button hints |
| cancel | Cancel | button hints |
| ok | OK | button hints |
| prev | Prev | button hints |
| next | Next | button hints |
| no_files | No files found | file_browser.lua |
| no_book | No book to continue | home.lua |
| language | Language | settings.lua |
| font_family | Font Family | settings.lua |
| font_size | Font Size | settings.lua |
| orientation | Orientation | settings.lua |
| screen_margin | Screen Margin | settings.lua |
| refresh_freq | Refresh Frequency | settings.lua |
| sleep_timeout | Sleep Timeout | settings.lua |
| theme | Theme | settings.lua |

## How Font Fallback Works

1. User selects a language in Settings (e.g., Hebrew)
2. `fonts.init()` checks the language setting and loads the fallback font from `/languages/he/fonts/NotoSansHebrew-{size}-Regular.cfont`
3. `font.setFallback(reader_font, hebrew_font)` links them in the C runtime
4. When `display.drawText()` encounters a codepoint missing from the reader font (e.g., a Hebrew letter), the renderer automatically tries the fallback font
5. Latin text renders from the reader font, Hebrew from the fallback — seamless in one `drawText` call

## Creating a New Language Pack

### 1. Create the folder

```
/languages/xx/
  lang.json
  fonts/    (optional, only if the language needs non-Latin glyphs)
```

### 2. Write lang.json

Copy `en/lang.json` and translate the strings. Set `code`, `name`, and `direction`.

If the language uses a non-Latin script, set `fontFamily` to the name of the .cfont files (without size/style suffix).

### 3. Generate fonts (if needed)

Use the cfont converter to generate script-specific fonts:

```bash
cd tools/cfont-convert

python3 cfont_convert.py hebrew_14 14 \
  /path/to/NotoSansHebrew-Regular.ttf \
  --additional-intervals 0x0590,0x05FF \
  --2bit --compress \
  --cfont ../../sdcard/languages/he/fonts/NotoSansHebrew-14-Regular.cfont
```

Generate one file per size (12, 14, 16, 18) to match the reader font size options.

The `--additional-intervals` flag specifies the Unicode range for the script. Common ranges:
- Hebrew: `0x0590,0x05FF`
- Arabic: `0x0600,0x06FF`
- CJK Unified: `0x4E00,0x9FFF` (large — will produce a big .cfont file)
- Cyrillic: `0x0400,0x04FF` (already in default NotoSans)

### 4. Copy to SD card

Drop the folder into `/languages/` on the SD card. The settings plugin discovers it automatically — no firmware update needed.

## Pre-Built Language Packs

Pre-built language packs are available in the `lang_packs/` directory of the repository. Copy the desired language folders to `/languages/` on your SD card.

Currently available:
- `en/` — English (default)
- `he/` — Hebrew (NotoSansHebrew, sizes 12-18)

## Lua API

```lua
local lang = require("lib.lang")

-- Discover available packs
local packs = lang.discover()
-- → {{code="en", name="English"}, {code="he", name="עברית"}}

-- Load a language
lang.load("he")

-- Translate a UI string
lang.tr("settings")  -- → "הגדרות"
lang.tr("unknown")   -- → "unknown" (key returned as-is if missing)

-- Query language properties
lang.get_direction()    -- → "rtl"
lang.get_font_family()  -- → "NotoSansHebrew"
lang.get_code()         -- → "he"
```
