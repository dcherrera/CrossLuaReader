# Font Packs

Font packs provide drop-in font families for CrossLua Reader. Each pack is a folder in `/fonts/` on the SD card containing `.cfont` files. The system discovers them automatically — no firmware change required.

## Folder Structure

```
/fonts/
  NotoSans/
    NotoSans-12-Regular.cfont
    NotoSans-14-Regular.cfont
    NotoSans-16-Regular.cfont
    NotoSans-18-Regular.cfont
    NotoSans-14-Bold.cfont
    NotoSans-14-Italic.cfont
    NotoSans-14-BoldItalic.cfont
    ...
  Bookerly/
    Bookerly-12-Regular.cfont
    Bookerly-14-Regular.cfont
    ...
  Ubuntu/
    Ubuntu-12-Regular.cfont
    ...
```

## Naming Convention

Font files must follow this pattern:

```
{FamilyName}-{size}-{style}.cfont
```

| Part | Values | Examples |
|------|--------|---------|
| FamilyName | Must match the folder name | `NotoSans`, `Bookerly`, `MyFont` |
| size | Point size as integer | `12`, `14`, `16`, `18` |
| style | Font style | `Regular`, `Bold`, `Italic`, `BoldItalic` |

The folder name IS the family name. The system constructs paths as:
```
/fonts/{family}/{family}-{size}-{style}.cfont
```

## Required Sizes

The settings plugin offers font sizes Small (12), Medium (14), Large (16), and X-Large (18). A font pack should include at least the Regular style for each size:

| File | Required |
|------|----------|
| `{Family}-12-Regular.cfont` | Yes |
| `{Family}-14-Regular.cfont` | Yes |
| `{Family}-16-Regular.cfont` | Yes |
| `{Family}-18-Regular.cfont` | Yes |
| `{Family}-{size}-Bold.cfont` | Optional (for EPUB bold text) |
| `{Family}-{size}-Italic.cfont` | Optional (for EPUB italic text) |
| `{Family}-{size}-BoldItalic.cfont` | Optional |

If a size is missing, the system falls back to NotoSans at that size.

## Discovery

The settings plugin scans `/fonts/` on boot for subdirectories containing at least one `.cfont` file. Found families appear in the Font Family setting automatically.

```lua
local fonts = require("lib.fonts")
local families = fonts.discover_families()
-- → {"Bookerly", "NotoSans", "OpenDyslexic", "Ubuntu"}
```

## Creating a Font Pack

### 1. Get a TTF/OTF source font

Download or obtain the font file (e.g., from Google Fonts).

### 2. Generate .cfont files

Use the cfont converter to generate each size:

```bash
cd tools/cfont-convert

# Generate Regular for all sizes
for size in 12 14 16 18; do
  python3 cfont_convert.py myfont $size \
    /path/to/MyFont-Regular.ttf \
    --2bit --compress \
    --cfont ../../sdcard/fonts/MyFont/MyFont-${size}-Regular.cfont
done

# Optional: generate Bold
for size in 12 14 16 18; do
  python3 cfont_convert.py myfont_bold $size \
    /path/to/MyFont-Bold.ttf \
    --2bit --compress \
    --cfont ../../sdcard/fonts/MyFont/MyFont-${size}-Bold.cfont
done
```

### 3. Copy to SD card

Drop the folder into `/fonts/` on the SD card. It appears in Settings immediately.

### Converter Options

| Flag | Purpose |
|------|---------|
| `--2bit` | Generate 2-bit greyscale bitmaps (4 shading levels, better quality) |
| `--compress` | DEFLATE compression with group-based caching (smaller files) |
| `--force-autohint` | Use FreeType auto-hinter for consistent rendering |
| `--pnum` | Enable proportional numeral spacing |
| `--additional-intervals min,max` | Add custom Unicode ranges (e.g., `0x0590,0x05FF` for Hebrew) |

## Pre-Built Font Packs

Pre-built font packs are available in the `font_packs/` directory of the repository. Copy the desired family folders to `/fonts/` on your SD card.

Currently available:

| Family | Sizes | Styles | Description |
|--------|-------|--------|-------------|
| NotoSans | 8, 12, 14, 16, 18 | Regular, Bold, Italic, BoldItalic | Google's universal font, good Latin/Cyrillic/Greek coverage |
| Bookerly | 12, 14, 16, 18 | Regular, Bold, Italic, BoldItalic | Amazon's reading font, optimized for e-ink |
| OpenDyslexic | 8, 10, 12, 14 | Regular, Bold, Italic, BoldItalic | Dyslexia-friendly font with weighted bottoms |
| Ubuntu | 10, 12 | Regular, Bold | UI font, clean and compact |

## Font Packs vs Language Packs

| | Font Pack | Language Pack |
|---|-----------|---------------|
| Location | `/fonts/{Family}/` | `/languages/{code}/` |
| Purpose | Primary reader font | Fallback for non-Latin scripts + UI translation |
| Selected by | Font Family setting | Language setting |
| Used as | Primary font (slot 1) | Fallback font (slot 2) |
| Discovery | `fonts.discover_families()` | `lang.discover()` |

A font pack is the main reading font. A language pack provides fallback glyphs for scripts the main font doesn't cover (e.g., Hebrew, Arabic) and translated UI strings.

## Lua API

```lua
local fonts = require("lib.fonts")

-- Discover available font packs
local families = fonts.discover_families()
-- → {"Bookerly", "NotoSans", "OpenDyslexic", "Ubuntu"}

-- Fonts are loaded automatically based on settings:
fonts.init()                    -- loads UI + reader + fallback
display.drawText(fonts.reader, x, y, "Hello")  -- uses selected family
fonts.cleanup()                 -- unload all
```

## .cfont Format

See `docs/cfont-format.md` for the binary font file format specification.
