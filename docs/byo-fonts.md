# Bring Your Own Font

CrossLua Reader can use any TTF or OTF font you have rights to. The firmware reads its own `.cfont` binary format — a small, fast, e-ink-tuned bitmap container — but the conversion from TTF/OTF to `.cfont` is a one-line command using the bundled converter.

This guide covers:

- Converting your own fonts
- Recommended open-source fonts for reading on e-ink
- Legal considerations for commercial fonts (Kindle, Apple, Microsoft, Adobe, etc.)
- Tips for sizing, hinting, and language coverage

## Quickstart: Convert a TTF

Prerequisites:

- Python 3
- `freetype-py` and `fontTools` (`pip install freetype-py fonttools`)

From the repo root:

```bash
cd tools/cfont-convert

python3 cfont_convert.py \
    "myfont_14_regular" \
    14 \
    /path/to/MyFont-Regular.ttf \
    --2bit \
    --compress \
    --cfont ../../sdcard/fonts/MyFont/MyFont-14-Regular.cfont
```

Arguments in order:

| Argument | Meaning |
|---|---|
| `"myfont_14_regular"` | Internal name (lowercase, underscores). Used for identification inside the binary. |
| `14` | Pixel size to render at. One `.cfont` file per size. |
| `/path/to/MyFont-Regular.ttf` | Source font file. Multiple paths are allowed and stack in priority order (first hit wins per glyph). |
| `--2bit` | 2-bit greyscale (4 levels). Looks better than 1-bit on e-ink. Drop this for pure black-and-white. |
| `--compress` | DEFLATE-compress glyph bitmaps. Required for any production font. |
| `--cfont <path>` | Output `.cfont` binary. Place under `sdcard/fonts/<FamilyName>/`. |

Repeat for each style and size you want (Regular, Italic, Bold, BoldItalic at 12, 14, 16, 18 — eight files for a full reader font family).

Once dropped on the SD card, the firmware discovers the new family automatically and exposes it in the font picker. No firmware reflash.

## Recommended sizes

For body text in a reader plugin: **12, 14, 16, 18**.
For UI fonts: **10, 12**.
For headings: **20–24** if you want them; most plugins scale up via the renderer instead.

Each size is a separate `.cfont` file. Skipping sizes is fine — readers fall back to the nearest available.

## Multi-script fonts (Hebrew, Arabic, CJK)

The converter accepts a font *stack*: list multiple TTFs after the size argument, and the converter walks them in order looking up each codepoint. Use this to bolt non-Latin scripts onto a Latin base font.

Example — Latin + Hebrew:

```bash
python3 cfont_convert.py \
    "mybook_14_regular" \
    14 \
    /path/to/MyBook-Regular.ttf \
    /path/to/NotoSansHebrew-Regular.ttf \
    --additional-intervals 0x0590,0x05FF \
    --2bit --compress \
    --cfont ../../sdcard/fonts/MyBook/MyBook-14-Regular.cfont
```

`--additional-intervals` declares extra Unicode ranges to include beyond the default Latin set. Common ranges:

| Script | Range |
|---|---|
| Hebrew | `0x0590,0x05FF` |
| Arabic | `0x0600,0x06FF` |
| Greek | `0x0370,0x03FF` |
| Cyrillic | `0x0400,0x04FF` |
| Devanagari | `0x0900,0x097F` |

CJK is possible but produces large files (thousands of glyphs). Subset to what you actually read.

## Hinting and e-ink legibility

E-ink is unforgiving of poor hinting at small sizes. The converter uses the font's native TrueType hints by default. If a font has weak or no hints (common with otherwise-beautiful display fonts), use the FreeType auto-hinter:

```bash
python3 cfont_convert.py ... --force-autohint ...
```

This produces more consistent stem widths and better small-size legibility, at the cost of a slight loss of designer intent.

**Rule of thumb:** if a font looks crunchy or uneven at 14px, try `--force-autohint`.

## Fonts to consider (open-source, redistributable)

These are SIL Open Font License or Apache 2.0 — free to ship, modify, and convert:

### Body text (serif, reading-optimized)

| Font | Vibe | Where it shines |
|---|---|---|
| **Source Serif Pro** (Adobe) | Modern transitional | Classic, comfortable for long reads |
| **Crimson Pro** | Old-style book serif | Closest legal sibling to Bookerly |
| **Literata** (Google Fonts) | Custom-designed for Google Books | Excellent Bookerly substitute |
| **EB Garamond** | Classical Garamond revival | Beautiful but slightly delicate at 12px |
| **Libre Caslon Text** | Caslon revival | Warm and traditional |
| **Spectral** | Designed for screens | Very strong on e-ink |

### Body text (sans-serif)

| Font | Vibe | Where it shines |
|---|---|---|
| **Source Sans Pro** | Humanist sans | Excellent default, very legible |
| **IBM Plex Sans** | Modern grotesque | Slightly more character than Source Sans |
| **Inter** | Optimized for screens | Crisp at small sizes |
| **Noto Sans** | Universal coverage | Best multi-script choice |

### Dyslexia-friendly

| Font | License | Notes |
|---|---|---|
| **OpenDyslexic** | OFL | Already shipped in the firmware |

### UI / monospace

| Font | License | Notes |
|---|---|---|
| **Ubuntu** | Ubuntu Font License | Used for UI in current builds |
| **JetBrains Mono** | OFL | Strong monospace |
| **IBM Plex Mono** | OFL | Strong monospace alternative |

### Hebrew

| Font | License | Notes |
|---|---|---|
| **Noto Sans Hebrew** | OFL | Already shipped |
| **Frank Ruhl Libre** | OFL | Beautiful traditional Hebrew serif |
| **Heebo** | OFL | Modern sans-serif Hebrew |

## Commercial fonts: legal considerations

You can technically convert any TTF/OTF, including commercial ones. **Whether you may** depends on the font's license. Read the license before converting.

### Kindle-shipped fonts (Bookerly, Caecilia, Palatino, Helvetica Neue LT, Futura)

These are TTFs Amazon licenses from foundries (Bookerly was custom-made by Dalton Maag for Amazon; Caecilia is from Linotype). The licenses are typically restricted to Kindle devices.

- **Personal use on your own CrossLua device:** technically still a license violation, practically unenforced. You're on your own.
- **Redistributing converted `.cfont` files in your repo, on your SD card images, in plugins, or anywhere else:** **don't.** This is the part that gets noticed.
- **Suggested alternative for Bookerly:** Crimson Pro or Literata. Both are visually similar and OFL-licensed.

### Apple system fonts (San Francisco, New York)

Licensed for use only on Apple platforms. **Do not convert and redistribute.**

### Microsoft fonts (Calibri, Cambria, Segoe UI)

Bundled with Windows / Office. Licensed for use on those platforms only. **Do not convert and redistribute.**

### Adobe fonts (commercial Typekit fonts)

Licensed per-subscription. Conversion to `.cfont` for any use is a license violation. Adobe's own *open-source* fonts (Source Serif, Source Sans, Source Code) are free to convert — those are explicitly OFL.

### Fonts embedded in ebooks

EPUB and KF8 books can embed fonts. The license usually permits embedding-for-display only, not extraction-for-reuse. **Don't extract and redistribute.** A future "embedded font" feature in the reader plugin would render the embedded font for that book only without writing a `.cfont` to disk; that's a different scope.

### Free-from-foundry fonts

Some commercial foundries release individual fonts as free downloads (often "personal use" or "trial" tier). Read the license carefully — "free" rarely means redistributable, and "personal use" rarely means "convert to a different format and ship in your firmware."

## Bulk conversion

For multiple fonts and sizes, write a shell script. The repo includes `tools/cfont-convert/convert_all.sh` as an example — it converts the bundled CrossPoint fonts (Noto Sans, Bookerly, OpenDyslexic, Ubuntu) at the standard sizes and styles. Adapt it for your own font collection.

## Verifying a converted font

After conversion, the `.cfont` file lands in `sdcard/fonts/<Family>/`. The simplest verification path:

1. Copy the SD card image to your device.
2. Boot CrossLua.
3. Open Settings → Fonts (or any plugin that exposes a font picker).
4. Select your new family and size.
5. The font should render. If glyphs are missing, your codepoint coverage didn't include them — re-run with `--additional-intervals` covering the missing range.

If a font doesn't appear at all, check:

- File is in the right folder (`sdcard/fonts/<Family>/<Family>-<size>-<style>.cfont`)
- Naming matches the convention exactly (case-sensitive on Linux/macOS, case-insensitive on FAT32 SD cards but be consistent)
- File is well-formed (re-run the converter; check for errors in output)

## Troubleshooting

**"Glyph X is missing" warnings during conversion**
The font doesn't contain that codepoint. Either: it's expected (font doesn't cover that script — fine), add a fallback font in the stack, or pass `--additional-intervals` to declare the range you need.

**Output looks crunchy or uneven on the device**
Try `--force-autohint`. Most fonts benefit from it at e-ink sizes.

**Output file is huge**
Make sure `--compress` is set. Without it, glyph bitmaps are uncompressed and a single 14px family can hit several hundred KB.

**Font picker doesn't show the family**
Check the folder/filename naming convention matches `Family/Family-Size-Style.cfont` exactly. Reboot the device or remount the SD card after copying.

## Contributing fonts back

If you build a clean conversion of an open-source font that the community would benefit from, open a PR adding the `.cfont` files to `sdcard/fonts/<Family>/`. Include:

- Source font's license file in the same directory
- A short note in the PR identifying the source and confirming the license permits redistribution
- All standard sizes and styles you converted (Regular, Italic, Bold, BoldItalic at 12, 14, 16, 18 if it's a body font)

Plugins that need a font not in the default set should bundle the `.cfont` inside their own bundle (`fonts/` directory in a `.ticl` plugin bundle) rather than depending on something the user must convert manually.

## See also

- [`docs/font-packs.md`](font-packs.md) — Folder layout and naming convention for installed fonts
- [`docs/cfont-format.md`](cfont-format.md) — Binary format details for the curious
- [`docs/language-packs.md`](language-packs.md) — Bundling a font with a translation
