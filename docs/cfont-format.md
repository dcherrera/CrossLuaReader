# .cfont Binary Font Format

## Overview

`.cfont` is a self-contained binary font file for CrossLua Reader. It stores font metadata and compressed glyph bitmaps in a format designed for efficient loading on ESP32-C3 (380KB RAM, SD card storage).

The font loader uses on-demand architecture: only intervals, compression groups, kerning tables, and ligature pairs are loaded into RAM. Glyph metrics (the largest metadata section) and glyph bitmaps stay on SD and are read through LRU caches. This reduces per-font RAM from ~25-31KB to ~2-8KB.

## File Layout

```
Offset  Section              Size
------  -------------------  -------------------------
0x00    File Header          32 bytes (fixed)
0x20    Intervals            interval_count × 12 bytes
        Glyphs               glyph_count × 14 bytes
        Groups               group_count × 18 bytes
        Kern Left Classes    kern_left_entry_count × 3 bytes
        Kern Right Classes   kern_right_entry_count × 3 bytes
        Kern Matrix          left_classes × right_classes bytes
        Ligature Pairs       ligature_count × 8 bytes
        Bitmap Data          (DEFLATE streams, referenced by groups)
```

All multi-byte fields are **little-endian** (native RISC-V ESP32-C3).

## File Header (32 bytes)

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | char[4] | magic | `"CFNT"` |
| 0x04 | 1 | uint8 | version | Format version (1) |
| 0x05 | 1 | uint8 | flags | Bit 0: is_2bit |
| 0x06 | 1 | uint8 | font_size | Point size |
| 0x07 | 1 | uint8 | style | 0=regular 1=bold 2=italic 3=bold-italic |
| 0x08 | 1 | uint8 | advance_y | Line height |
| 0x09 | 1 | int8 | ascender | Max above baseline |
| 0x0A | 1 | int8 | descender | Max below baseline |
| 0x0B | 1 | uint8 | kern_left_class_count | Distinct left classes |
| 0x0C | 1 | uint8 | kern_right_class_count | Distinct right classes |
| 0x0D | 1 | - | reserved | 0 |
| 0x0E | 2 | uint16 | glyph_count | Total glyphs |
| 0x10 | 2 | uint16 | interval_count | Unicode intervals |
| 0x12 | 2 | uint16 | group_count | Compression groups (0 = uncompressed) |
| 0x14 | 2 | uint16 | kern_left_entry_count | Left kern table entries |
| 0x16 | 2 | uint16 | kern_right_entry_count | Right kern table entries |
| 0x18 | 2 | uint16 | ligature_count | Ligature pairs |
| 0x1A | 2 | - | reserved | 0 |
| 0x1C | 4 | uint32 | bitmap_offset | Absolute file offset to bitmap data |

## Data Sections

### Unicode Interval (12 bytes)
Maps codepoint ranges to glyph indices.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | first | First codepoint (inclusive) |
| 4 | 4 | last | Last codepoint (inclusive) |
| 8 | 4 | offset | Index into glyph array |

### Glyph (14 bytes, packed)
Per-glyph bitmap metadata.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 | width | Bitmap width in pixels |
| 1 | 1 | height | Bitmap height in pixels |
| 2 | 2 | advance_x | Cursor advance, 12.4 fixed-point |
| 4 | 2 | left | X offset from cursor (signed) |
| 6 | 2 | top | Y offset from cursor (signed) |
| 8 | 2 | data_length | Packed bitmap size in bytes |
| 10 | 4 | data_offset | Within-group offset (compressed) or file offset (uncompressed) |

### Group (18 bytes, packed)
Compression group containing multiple glyphs.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | compressed_offset | Offset into bitmap data section |
| 4 | 4 | compressed_size | DEFLATE stream bytes |
| 8 | 4 | uncompressed_size | Decompressed size |
| 12 | 2 | glyph_count | Glyphs in this group |
| 14 | 4 | first_glyph_index | First glyph in global array |

### Kern Class Entry (3 bytes, packed)
Sorted by codepoint for binary search.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 2 | codepoint | Unicode codepoint |
| 2 | 1 | class_id | 1-based class ID |

### Kern Matrix
Flat `left_class_count × right_class_count` array of `int8` (4.4 fixed-point pixels). Row-major: `matrix[(lc-1) * right_count + (rc-1)]`.

### Ligature Pair (8 bytes, packed)
Sorted by pair for binary search.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | pair | (left_cp << 16) \| right_cp |
| 4 | 4 | ligature_cp | Replacement codepoint |

### Bitmap Data
Raw DEFLATE streams concatenated. Each group's `compressed_offset` is relative to the start of this section (`bitmap_offset` in the header gives the absolute file position).

## Fixed-Point Conventions

- **advance_x**: 12.4 unsigned (uint16). Pixel = `(val + 8) >> 4`.
- **kern_matrix**: 4.4 signed (int8). Same shift.
- Both share 4 fractional bits and can be summed before snapping.

## Generating .cfont Files

```bash
python3 tools/cfont-convert/cfont_convert.py fontname 14 \
  NotoSans-Regular.ttf --2bit --compress --cfont output.cfont
```
