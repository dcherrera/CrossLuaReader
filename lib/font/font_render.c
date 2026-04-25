/**
 * @file font_render.c
 * @brief Text rendering implementation. Ported from CrossPoint's
 *        GfxRenderer::drawText and EpdFont glyph/kerning/ligature lookups.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "font_render.h"
#include "font_cache.h"
#include "utf8.h"
#include "bidi.h"
#include "renderer.h"
#include "logging.h"

#include <string.h>

/* ── Glyph lookup (binary search on intervals) ──────────────────────── */

/**
 * Look up a glyph by codepoint. Binary search through unicode intervals.
 * Ported from EpdFont::getGlyph.
 *
 * @param font Font data
 * @param cp   Unicode codepoint
 * @param out_index Set to the glyph's index in font->glyphs (for cache)
 * @return     Pointer to glyph, or NULL if not found
 */
static const font_glyph_t *get_glyph(const font_data_t *font, uint32_t cp,
                                       uint32_t *out_index) {
    /* Binary search: find first interval where first > cp, then check previous */
    int lo = 0, hi = (int)font->interval_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (font->intervals[mid].first <= cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo == 0) {
        /* Try replacement glyph */
        if (cp != REPLACEMENT_GLYPH) return get_glyph(font, REPLACEMENT_GLYPH, out_index);
        return NULL;
    }

    const font_interval_t *iv = &font->intervals[lo - 1];
    if (cp >= iv->first && cp <= iv->last) {
        uint32_t idx = iv->offset + (cp - iv->first);
        if (idx < font->glyph_count) {
            if (out_index) *out_index = idx;
            return &font->glyphs[idx];
        }
    }

    if (cp != REPLACEMENT_GLYPH) return get_glyph(font, REPLACEMENT_GLYPH, out_index);
    return NULL;
}

/* ── Kerning (class-based lookup + matrix) ──────────────────────────── */

/**
 * Binary search a kern class table for a codepoint.
 *
 * @return Class ID (1-based), or 0 if not found
 */
static uint8_t lookup_kern_class(const font_kern_class_t *entries, uint16_t count,
                                  uint32_t cp) {
    if (!entries || count == 0 || cp > 0xFFFF) return 0;

    uint16_t target = (uint16_t)cp;
    int lo = 0, hi = (int)count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (entries[mid].codepoint < target) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < (int)count && entries[lo].codepoint == target) {
        return entries[lo].class_id;
    }
    return 0;
}

/**
 * Get kerning adjustment between two codepoints.
 * Returns 4.4 fixed-point value.
 */
static int8_t get_kerning(const font_data_t *font, uint32_t left_cp, uint32_t right_cp) {
    if (!font->kern_left || !font->kern_right || !font->kern_matrix) return 0;

    uint8_t lc = lookup_kern_class(font->kern_left, font->kern_left_entry_count, left_cp);
    uint8_t rc = lookup_kern_class(font->kern_right, font->kern_right_entry_count, right_cp);

    if (lc == 0 || rc == 0) return 0;

    int idx = (lc - 1) * font->kern_right_class_count + (rc - 1);
    return font->kern_matrix[idx];
}

/* ── Ligatures (greedy chaining via binary search) ──────────────────── */

/**
 * Look up a ligature pair.
 *
 * @return Replacement codepoint, or 0 if no ligature
 */
static uint32_t get_ligature(const font_data_t *font, uint32_t left_cp, uint32_t right_cp) {
    if (!font->ligatures || font->ligature_count == 0) return 0;

    uint32_t key = (left_cp << 16) | (right_cp & 0xFFFF);
    int lo = 0, hi = (int)font->ligature_count;

    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (font->ligatures[mid].pair < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < (int)font->ligature_count && font->ligatures[lo].pair == key) {
        return font->ligatures[lo].ligature_cp;
    }
    return 0;
}

/**
 * Apply greedy ligature chaining starting from cp.
 * Advances *text past consumed codepoints.
 *
 * @return Final codepoint (possibly a ligature replacement)
 */
static uint32_t apply_ligatures(const font_data_t *font, uint32_t cp,
                                 const uint8_t **text) {
    while (1) {
        const uint8_t *save = *text;
        uint32_t next = utf8_next_codepoint(text);
        if (next == 0) {
            *text = save;
            return cp;
        }

        uint32_t lig = get_ligature(font, cp, next);
        if (lig != 0) {
            cp = lig;  /* chain: try (ligature, next_next) */
        } else {
            *text = save;  /* rewind — next char not consumed */
            return cp;
        }
    }
}

/* ── Glyph pixel rendering ──────────────────────────────────────────── */

/**
 * Render a single glyph's bitmap to the framebuffer.
 * Handles both 1-bit and 2-bit bitmaps.
 *
 * @param font       Font data
 * @param font_path  SD path for cache reads
 * @param glyph      Glyph to render
 * @param glyph_idx  Glyph index (for cache)
 * @param cursor_x   Cursor X position
 * @param cursor_y   Cursor Y position (baseline-adjusted)
 * @param black      Pixel color
 */
static void render_glyph(const font_data_t *font, const char *font_path,
                          const font_glyph_t *glyph, uint32_t glyph_idx,
                          int cursor_x, int cursor_y, bool black) {
    if (glyph->width == 0 || glyph->height == 0 || glyph->data_length == 0) return;

    const uint8_t *bitmap = font_cache_get_bitmap(font, glyph, glyph_idx, font_path);
    if (!bitmap) return;

    int start_x = cursor_x + glyph->left;
    int start_y = cursor_y - glyph->top;

    if (font->is_2bit) {
        /* 2-bit bitmap: 4 pixels per byte, MSB first */
        uint32_t pos = 0;
        for (int row = 0; row < glyph->height; row++) {
            for (int col = 0; col < glyph->width; col++) {
                uint32_t byte_idx = pos / 4;
                uint8_t shift = (3 - (pos % 4)) * 2;
                uint8_t pixel = (bitmap[byte_idx] >> shift) & 0x03;
                pos++;

                /* 2-bit: 0=transparent, 1=light, 2=medium, 3=dark */
                if (pixel > 0) {
                    /* In BW mode, threshold at >= 2 */
                    bool draw = black ? (pixel >= 2) : (pixel < 2);
                    if (draw) {
                        renderer_draw_pixel(start_x + col, start_y + row, black);
                    }
                }
            }
        }
    } else {
        /* 1-bit bitmap: 8 pixels per byte, MSB first */
        uint32_t pos = 0;
        for (int row = 0; row < glyph->height; row++) {
            for (int col = 0; col < glyph->width; col++) {
                uint32_t byte_idx = pos / 8;
                uint8_t bit = 7 - (pos % 8);
                bool pixel_set = (bitmap[byte_idx] >> bit) & 1;
                pos++;

                if (pixel_set) {
                    renderer_draw_pixel(start_x + col, start_y + row, black);
                }
            }
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

void font_render_draw_text(const font_data_t *font, const char *font_path,
                           int x, int y, const char *text, bool black) {
    if (!font || !text || !*text || !font_path) return;

    /* BiDi reordering if needed */
    char visual_buf[512];
    const char *render_text = text;
    if (bidi_has_rtl(text)) {
        bidi_reorder_line(text, visual_buf, sizeof(visual_buf));
        render_text = visual_buf;
    }

    int cursor_y = y + font->ascender;
    int cursor_x = x;
    int32_t prev_advance_fp = 0;
    uint32_t prev_cp = 0;
    int last_base_left = 0;
    int last_base_width = 0;
    int last_base_top = 0;

    const uint8_t *p = (const uint8_t *)render_text;
    uint32_t cp;

    while ((cp = utf8_next_codepoint(&p))) {
        /* Combining marks: position over base glyph */
        if (utf8_is_combining_mark(cp)) {
            uint32_t mark_idx;
            const font_glyph_t *mark = get_glyph(font, cp, &mark_idx);
            if (!mark) continue;
            int raise = combining_mark_raise_above(mark->top, mark->height, last_base_top);
            int mark_x = combining_mark_center_over(cursor_x, last_base_left,
                                                     last_base_width, mark->left, mark->width);
            render_glyph(font, font_path, mark, mark_idx, mark_x, cursor_y - raise, black);
            continue;
        }

        /* Ligature chaining */
        cp = apply_ligatures(font, cp, &p);

        /* Differential rounding: (prev_advance + kern) snapped as one unit */
        if (prev_cp != 0) {
            int8_t kern = get_kerning(font, prev_cp, cp);
            cursor_x += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t glyph_idx;
        const font_glyph_t *glyph = get_glyph(font, cp, &glyph_idx);
        if (!glyph) {
            prev_cp = cp;
            prev_advance_fp = 0;
            continue;
        }

        last_base_left = glyph->left;
        last_base_width = glyph->width;
        last_base_top = glyph->top;
        prev_advance_fp = glyph->advance_x;
        prev_cp = cp;

        render_glyph(font, font_path, glyph, glyph_idx, cursor_x, cursor_y, black);
    }
}

int font_render_get_text_advance(const font_data_t *font, const char *text) {
    if (!font || !text || !*text) return 0;

    const uint8_t *p = (const uint8_t *)text;
    uint32_t cp, prev_cp = 0;
    int width_px = 0;
    int32_t prev_advance_fp = 0;

    while ((cp = utf8_next_codepoint(&p))) {
        if (utf8_is_combining_mark(cp)) continue;

        cp = apply_ligatures(font, cp, &p);

        if (prev_cp != 0) {
            int8_t kern = get_kerning(font, prev_cp, cp);
            width_px += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t idx;
        const font_glyph_t *glyph = get_glyph(font, cp, &idx);
        prev_advance_fp = glyph ? glyph->advance_x : 0;
        prev_cp = cp;
    }

    width_px += fp4_to_pixel(prev_advance_fp);
    return width_px;
}

int font_render_get_text_width(const font_data_t *font, const char *text) {
    /* For simplicity, use advance width. True bounding box would require
       tracking min/max glyph extents like EpdFont::getTextBounds. */
    return font_render_get_text_advance(font, text);
}

int font_render_get_line_height(const font_data_t *font) {
    return font ? font->advance_y : 0;
}

int font_render_get_ascender(const font_data_t *font) {
    return font ? font->ascender : 0;
}
