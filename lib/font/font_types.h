/**
 * @file font_types.h
 * @brief Font data structures, fixed-point helpers, and combining mark
 *        positioning. C equivalents of CrossPoint's EpdFontData.h.
 *
 * @status Phase 8 — on-demand glyph loading
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Fixed-point 4 helpers (12.4 advances, 4.4 kerning) ────────────── */

#define FP4_FRAC_BITS   4
#define FP4_HALF        (1 << (FP4_FRAC_BITS - 1))  /* 8 */

/** Convert integer pixels to 12.4 fixed-point. */
#define fp4_from_pixel(px)  ((int32_t)(px) << FP4_FRAC_BITS)

/** Snap 12.4 fixed-point to nearest integer pixel. */
#define fp4_to_pixel(fp)    ((int)(((fp) + FP4_HALF) >> FP4_FRAC_BITS))

/* ── Combining mark positioning ─────────────────────────────────────── */

#define COMBINING_MARK_MIN_GAP  1

/**
 * X position to center a combining mark over a base glyph's bitmap.
 */
static inline int combining_mark_center_over(int base_cursor, int base_left,
                                              int base_width, int mark_left,
                                              int mark_width) {
    return base_cursor + base_left + base_width / 2 - mark_width / 2 - mark_left;
}

/**
 * Pixels to raise a combining mark above the base glyph.
 * Returns 0 for marks that extend to or below the baseline.
 */
static inline int combining_mark_raise_above(int mark_top, int mark_height,
                                              int base_top) {
    if (mark_top - mark_height <= 0) return 0;
    int gap = mark_top - mark_height - base_top;
    return (gap < COMBINING_MARK_MIN_GAP) ? (COMBINING_MARK_MIN_GAP - gap) : 0;
}

/* ── Font data structures (match CrossPoint's binary layout) ────────── */

#define REPLACEMENT_GLYPH  0xFFFD
#define FONT_MAX_LOADED    3
#define FONT_MAX_PATH      64

/** On-demand glyph cache size (number of glyph metrics kept in RAM).
 *  48 covers a typical screen (~30-40 unique glyphs) with headroom. */
#define GLYPH_CACHE_SIZE   48

/** Per-glyph metrics — 14 bytes, packed for binary compatibility. */
typedef struct __attribute__((packed)) {
    uint8_t  width;         /**< Bitmap width in pixels */
    uint8_t  height;        /**< Bitmap height in pixels */
    uint16_t advance_x;     /**< Cursor advance, 12.4 fixed-point */
    int16_t  left;          /**< X offset from cursor to glyph UL corner */
    int16_t  top;           /**< Y offset from cursor to glyph UL corner */
    uint16_t data_length;   /**< Packed bitmap size in bytes */
    uint32_t data_offset;   /**< Offset into bitmap data (or within-group) */
} font_glyph_t;

/** Unicode interval mapping — 12 bytes. */
typedef struct {
    uint32_t first;   /**< First codepoint (inclusive) */
    uint32_t last;    /**< Last codepoint (inclusive) */
    uint32_t offset;  /**< Index of first glyph in array */
} font_interval_t;

/** Compressed glyph group — 18 bytes, packed. */
typedef struct __attribute__((packed)) {
    uint32_t compressed_offset;   /**< Byte offset into bitmap section */
    uint32_t compressed_size;     /**< DEFLATE stream size */
    uint32_t uncompressed_size;   /**< Decompressed size */
    uint16_t glyph_count;         /**< Glyphs in this group */
    uint32_t first_glyph_index;   /**< First glyph in global array */
} font_group_t;

/** Kerning class entry — 3 bytes, packed, sorted by codepoint. */
typedef struct __attribute__((packed)) {
    uint16_t codepoint;  /**< Unicode codepoint */
    uint8_t  class_id;   /**< 1-based class ID (0 = no kerning) */
} font_kern_class_t;

/** Ligature pair — 8 bytes, packed, sorted by pair. */
typedef struct __attribute__((packed)) {
    uint32_t pair;         /**< (left_cp << 16) | right_cp */
    uint32_t ligature_cp;  /**< Replacement codepoint */
} font_ligature_t;

/** .cfont file header — 32 bytes. */
typedef struct {
    uint8_t  magic[4];                   /**< "CFNT" */
    uint8_t  version;                    /**< Format version (1) */
    uint8_t  flags;                      /**< Bit 0: is_2bit */
    uint8_t  font_size;                  /**< Point size */
    uint8_t  style;                      /**< 0=regular 1=bold 2=italic 3=bold-italic */
    uint8_t  advance_y;                  /**< Line height */
    int8_t   ascender;                   /**< Max above baseline */
    int8_t   descender;                  /**< Max below baseline */
    uint8_t  kern_left_class_count;      /**< Distinct left kern classes */
    uint8_t  kern_right_class_count;     /**< Distinct right kern classes */
    uint8_t  reserved1;
    uint16_t glyph_count;
    uint16_t interval_count;
    uint16_t group_count;                /**< 0 = uncompressed */
    uint16_t kern_left_entry_count;
    uint16_t kern_right_entry_count;
    uint16_t ligature_count;
    uint16_t reserved2;
    uint32_t bitmap_offset;              /**< Absolute file offset to bitmap data */
} cfont_header_t;

/** Cached glyph entry for on-demand loading. */
typedef struct {
    uint32_t     glyph_index;  /**< Index in the font's glyph array */
    font_glyph_t glyph;       /**< The glyph metrics */
    uint32_t     access_tick;  /**< LRU counter */
    bool         valid;
} glyph_cache_entry_t;

/** Runtime font data — loaded from .cfont. Intervals, groups, kerning,
 *  and ligatures are in RAM. Glyph metrics are read on demand from SD. */
typedef struct {
    /* Always in RAM */
    font_interval_t      *intervals;
    font_group_t         *groups;
    font_kern_class_t    *kern_left;
    font_kern_class_t    *kern_right;
    int8_t               *kern_matrix;
    font_ligature_t      *ligatures;

    /* On-demand glyph cache */
    glyph_cache_entry_t  glyph_cache[GLYPH_CACHE_SIZE];
    uint32_t             glyph_cache_tick;

    /* File offset for on-demand glyph reads */
    uint32_t glyphs_file_offset;

    uint16_t glyph_count;
    uint16_t interval_count;
    uint16_t group_count;
    uint16_t kern_left_entry_count;
    uint16_t kern_right_entry_count;
    uint8_t  kern_left_class_count;
    uint8_t  kern_right_class_count;
    uint16_t ligature_count;

    uint8_t  advance_y;
    int8_t   ascender;
    int8_t   descender;
    bool     is_2bit;

    uint32_t bitmap_file_offset;  /**< Absolute offset in .cfont file */
    uint8_t  *metadata_buf;       /**< Allocation for intervals + groups */

    /* Embedded-source backing: when non-NULL, the .cfont content lives in
     * firmware flash (.rodata) and all on-demand reads (glyph metadata,
     * compressed bitmap groups) come from this buffer instead of the SD
     * card. NULL for SD-loaded fonts; populated by font_loader_load_buffer. */
    const uint8_t *embedded_data;
    uint32_t       embedded_size;
} font_data_t;
