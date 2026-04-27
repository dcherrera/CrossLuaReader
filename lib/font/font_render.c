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
#include "font_manager.h"
#include "font_cache.h"
#include "hal_storage.h"
#include "utf8.h"
#include "bidi.h"
#include "renderer.h"
#include "logging.h"

#include <string.h>

/* ── Glyph lookup (binary search on intervals) ──────────────────────── */

/**
 * Read a glyph from the on-demand cache. On cache miss, reads 14 bytes
 * from SD at the glyph's file offset. LRU eviction.
 *
 * @return Pointer to cached glyph, or NULL on read error
 */
static const font_glyph_t *cache_get_glyph(font_data_t *font, const char *font_path,
                                             uint32_t glyph_index) {
    /* Check cache */
    int lru_slot = 0;
    uint32_t lru_tick = UINT32_MAX;

    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        if (font->glyph_cache[i].valid && font->glyph_cache[i].glyph_index == glyph_index) {
            font->glyph_cache[i].access_tick = ++font->glyph_cache_tick;
            return &font->glyph_cache[i].glyph;
        }
        if (!font->glyph_cache[i].valid) {
            lru_slot = i;
            lru_tick = 0;
        } else if (font->glyph_cache[i].access_tick < lru_tick) {
            lru_tick = font->glyph_cache[i].access_tick;
            lru_slot = i;
        }
    }

    /* Cache miss — read from SD */
    hal_file_t f = hal_storage_open(font_path, HAL_FILE_READ);
    if (!f) return NULL;

    uint32_t offset = font->glyphs_file_offset + glyph_index * sizeof(font_glyph_t);
    hal_storage_file_seek(f, offset);

    font_glyph_t g;
    int read = hal_storage_file_read(f, &g, sizeof(font_glyph_t));
    hal_storage_file_close(f);

    if (read != sizeof(font_glyph_t)) return NULL;

    /* Store in cache */
    font->glyph_cache[lru_slot].glyph_index = glyph_index;
    font->glyph_cache[lru_slot].glyph = g;
    font->glyph_cache[lru_slot].access_tick = ++font->glyph_cache_tick;
    font->glyph_cache[lru_slot].valid = true;

    return &font->glyph_cache[lru_slot].glyph;
}

/**
 * Exact glyph lookup — binary search on intervals (in RAM), then
 * on-demand read of glyph metrics from SD via cache.
 *
 * @return Glyph pointer, or NULL if codepoint not in this font
 */
static const font_glyph_t *get_glyph_exact(font_data_t *font, const char *font_path,
                                             uint32_t cp, uint32_t *out_index) {
    int lo = 0, hi = (int)font->interval_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (font->intervals[mid].first <= cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo == 0) return NULL;

    const font_interval_t *iv = &font->intervals[lo - 1];
    if (cp >= iv->first && cp <= iv->last) {
        uint32_t idx = iv->offset + (cp - iv->first);
        if (idx < font->glyph_count) {
            if (out_index) *out_index = idx;
            return cache_get_glyph(font, font_path, idx);
        }
    }
    return NULL;
}

/**
 * Substitute missing Unicode characters with ASCII equivalents.
 * Covers box-drawing, typography, and common symbols that many
 * fonts lack. Returns 0 if no substitution available.
 */
static uint32_t substitute_glyph(uint32_t cp) {
    switch (cp) {
        /* Box Drawing Light */
        case 0x2500: return '-';   /* ─ horizontal */
        case 0x2502: return '|';   /* │ vertical */
        case 0x250C: return '+';   /* ┌ down and right */
        case 0x2510: return '+';   /* ┐ down and left */
        case 0x2514: return '+';   /* └ up and right */
        case 0x2518: return '+';   /* ┘ up and left */
        case 0x251C: return '+';   /* ├ vertical and right */
        case 0x2524: return '+';   /* ┤ vertical and left */
        case 0x252C: return '+';   /* ┬ horizontal and down */
        case 0x2534: return '+';   /* ┴ horizontal and up */
        case 0x253C: return '+';   /* ┼ cross */
        /* Box Drawing Double */
        case 0x2550: return '=';   /* ═ */
        case 0x2551: return '|';   /* ║ */
        case 0x2552: return '+';   /* ╒ */
        case 0x2553: return '+';   /* ╓ */
        case 0x2554: return '+';   /* ╔ */
        case 0x2555: return '+';   /* ╕ */
        case 0x2556: return '+';   /* ╖ */
        case 0x2557: return '+';   /* ╗ */
        case 0x2558: return '+';   /* ╘ */
        case 0x2559: return '+';   /* ╙ */
        case 0x255A: return '+';   /* ╚ */
        case 0x255B: return '+';   /* ╛ */
        case 0x255C: return '+';   /* ╜ */
        case 0x255D: return '+';   /* ╝ */
        case 0x255E: return '+';   /* ╞ */
        case 0x255F: return '+';   /* ╟ */
        case 0x2560: return '+';   /* ╠ */
        case 0x2561: return '+';   /* ╡ */
        case 0x2562: return '+';   /* ╢ */
        case 0x2563: return '+';   /* ╣ */
        case 0x2564: return '+';   /* ╤ */
        case 0x2565: return '+';   /* ╥ */
        case 0x2566: return '+';   /* ╦ */
        case 0x2567: return '+';   /* ╧ */
        case 0x2568: return '+';   /* ╨ */
        case 0x2569: return '+';   /* ╩ */
        case 0x256A: return '+';   /* ╪ */
        case 0x256B: return '+';   /* ╫ */
        case 0x256C: return '+';   /* ╬ */
        /* Common Typography */
        case 0x2013: return '-';   /* – en dash */
        case 0x2014: return '-';   /* — em dash */
        case 0x2018: return '\'';  /* ' left single quote */
        case 0x2019: return '\'';  /* ' right single quote */
        case 0x201C: return '"';   /* " left double quote */
        case 0x201D: return '"';   /* " right double quote */
        case 0x2026: return '.';   /* … ellipsis */
        case 0x2022: return '*';   /* • bullet */
        case 0x00B7: return '.';   /* · middle dot */
        default: return 0;
    }
}

/**
 * Glyph lookup with substitution + U+FFFD fallback.
 * Tries: exact → ASCII substitute → U+FFFD → NULL.
 */
static const font_glyph_t *get_glyph(font_data_t *font, const char *font_path,
                                       uint32_t cp, uint32_t *out_index) {
    const font_glyph_t *g = get_glyph_exact(font, font_path, cp, out_index);
    if (g) return g;

    /* Try ASCII substitution for missing glyphs */
    uint32_t sub = substitute_glyph(cp);
    if (sub) {
        g = get_glyph_exact(font, font_path, sub, out_index);
        if (g) return g;
    }

    if (cp != REPLACEMENT_GLYPH) return get_glyph_exact(font, font_path, REPLACEMENT_GLYPH, out_index);
    return NULL;
}

/**
 * Glyph lookup with font fallback chain.
 * Tries: primary exact → fallback exact → primary U+FFFD → NULL.
 * Sets out_font/out_path to whichever font provided the glyph.
 */
static const font_glyph_t *get_glyph_with_fallback(
    int primary_id,
    font_data_t *primary_font, const char *primary_path,
    uint32_t cp, uint32_t *out_index,
    font_data_t **out_font, const char **out_path) {

    /* Try primary font */
    const font_glyph_t *g = get_glyph_exact(primary_font, primary_path, cp, out_index);
    if (g) {
        *out_font = primary_font;
        *out_path = primary_path;
        return g;
    }

    /* Try fallback font */
    if (primary_id >= 0) {
        const char *fb_path = NULL;
        const font_data_t *fb = font_manager_get_fallback(primary_id, &fb_path);
        if (fb) {
            g = get_glyph_exact((font_data_t *)fb, fb_path, cp, out_index);
            if (g) {
                *out_font = (font_data_t *)fb;
                *out_path = fb_path;
                return g;
            }
        }
    }

    /* Try ASCII substitution */
    uint32_t sub = substitute_glyph(cp);
    if (sub) {
        g = get_glyph_exact(primary_font, primary_path, sub, out_index);
        if (g) {
            *out_font = primary_font;
            *out_path = primary_path;
            return g;
        }
    }

    /* Last resort: U+FFFD from primary */
    if (cp != REPLACEMENT_GLYPH) {
        g = get_glyph_exact(primary_font, primary_path, REPLACEMENT_GLYPH, out_index);
        if (g) {
            *out_font = primary_font;
            *out_path = primary_path;
            return g;
        }
    }

    return NULL;
}

/* ── Kerning (class-based lookup + matrix) ──────────────────────────── */

/**
 * Binary search a kern class table (in RAM) for a codepoint.
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
 * Get kerning adjustment between two codepoints (from RAM).
 * Returns 4.4 fixed-point value.
 */
static int8_t get_kerning(const font_data_t *font, const char *font_path,
                            uint32_t left_cp, uint32_t right_cp) {
    (void)font_path;
    if (!font->kern_left || !font->kern_right || !font->kern_matrix) return 0;

    uint8_t lc = lookup_kern_class(font->kern_left, font->kern_left_entry_count, left_cp);
    uint8_t rc = lookup_kern_class(font->kern_right, font->kern_right_entry_count, right_cp);

    if (lc == 0 || rc == 0) return 0;

    int idx = (lc - 1) * font->kern_right_class_count + (rc - 1);
    return font->kern_matrix[idx];
}

/* ── Ligatures (greedy chaining via binary search) ──────────────────── */

/**
 * Look up a ligature pair (from RAM).
 *
 * @return Replacement codepoint, or 0 if no ligature
 */
static uint32_t get_ligature(const font_data_t *font, const char *font_path,
                              uint32_t left_cp, uint32_t right_cp) {
    (void)font_path;
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
static uint32_t apply_ligatures(const font_data_t *font, const char *font_path,
                                 uint32_t cp, const uint8_t **text) {
    if (font->ligature_count == 0) return cp;
    while (1) {
        const uint8_t *save = *text;
        uint32_t next = utf8_next_codepoint(text);
        if (next == 0) {
            *text = save;
            return cp;
        }

        uint32_t lig = get_ligature(font, font_path, cp, next);
        if (lig != 0) {
            cp = lig;
        } else {
            *text = save;
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

    font_data_t *mfont = (font_data_t *)font;  /* need mutable for cache */
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
        if (utf8_is_combining_mark(cp)) {
            uint32_t mark_idx;
            const font_glyph_t *mark = get_glyph(mfont, font_path, cp, &mark_idx);
            if (!mark) continue;
            int raise = combining_mark_raise_above(mark->top, mark->height, last_base_top);
            int mark_x = combining_mark_center_over(cursor_x, last_base_left,
                                                     last_base_width, mark->left, mark->width);
            render_glyph(font, font_path, mark, mark_idx, mark_x, cursor_y - raise, black);
            continue;
        }

        cp = apply_ligatures(font, font_path, cp, &p);

        if (prev_cp != 0) {
            int8_t kern = get_kerning(font, font_path, prev_cp, cp);
            cursor_x += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t glyph_idx;
        const font_glyph_t *glyph = get_glyph(mfont, font_path, cp, &glyph_idx);
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

int font_render_get_text_advance(const font_data_t *font, const char *font_path,
                                  const char *text) {
    if (!font || !text || !*text || !font_path) return 0;

    font_data_t *mfont = (font_data_t *)font;
    const uint8_t *p = (const uint8_t *)text;
    uint32_t cp, prev_cp = 0;
    int width_px = 0;
    int32_t prev_advance_fp = 0;

    while ((cp = utf8_next_codepoint(&p))) {
        if (utf8_is_combining_mark(cp)) continue;

        cp = apply_ligatures(font, font_path, cp, &p);

        if (prev_cp != 0) {
            int8_t kern = get_kerning(font, font_path, prev_cp, cp);
            width_px += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t idx;
        const font_glyph_t *glyph = get_glyph(mfont, font_path, cp, &idx);
        prev_advance_fp = glyph ? glyph->advance_x : 0;
        prev_cp = cp;
    }

    width_px += fp4_to_pixel(prev_advance_fp);
    return width_px;
}

int font_render_get_text_width(const font_data_t *font, const char *font_path,
                                const char *text) {
    return font_render_get_text_advance(font, font_path, text);
}

int font_render_get_line_height(const font_data_t *font) {
    return font ? font->advance_y : 0;
}

int font_render_get_ascender(const font_data_t *font) {
    return font ? font->ascender : 0;
}

/* ── Fallback-aware variants ───────────────────────────────────────── */

void font_render_draw_text_fb(int font_id, int x, int y,
                              const char *text, bool black) {
    font_data_t *font = (font_data_t *)font_manager_get(font_id);
    const char *font_path = font_manager_get_path(font_id);
    if (!font || !text || !*text || !font_path) return;

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

    font_data_t *active_font = font;
    const char *active_path = font_path;

    const uint8_t *p = (const uint8_t *)render_text;
    uint32_t cp;

    while ((cp = utf8_next_codepoint(&p))) {
        if (utf8_is_combining_mark(cp)) {
            uint32_t mark_idx;
            font_data_t *mark_font;
            const char *mark_path;
            const font_glyph_t *mark = get_glyph_with_fallback(
                font_id, font, font_path, cp, &mark_idx,
                &mark_font, &mark_path);
            if (!mark) continue;
            int raise = combining_mark_raise_above(mark->top, mark->height, last_base_top);
            int mark_x = combining_mark_center_over(cursor_x, last_base_left,
                                                     last_base_width, mark->left, mark->width);
            render_glyph(mark_font, mark_path, mark, mark_idx, mark_x, cursor_y - raise, black);
            continue;
        }

        cp = apply_ligatures(font, font_path, cp, &p);

        if (prev_cp != 0) {
            int8_t kern = get_kerning(active_font, active_path, prev_cp, cp);
            cursor_x += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t glyph_idx;
        const font_glyph_t *glyph = get_glyph_with_fallback(
            font_id, font, font_path, cp, &glyph_idx,
            &active_font, &active_path);
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

        render_glyph(active_font, active_path, glyph, glyph_idx, cursor_x, cursor_y, black);
    }
}

int font_render_get_advance_fb(int font_id, const char *text) {
    font_data_t *font = (font_data_t *)font_manager_get(font_id);
    const char *font_path = font_manager_get_path(font_id);
    if (!font || !text || !*text || !font_path) return 0;

    const uint8_t *p = (const uint8_t *)text;
    uint32_t cp, prev_cp = 0;
    int width_px = 0;
    int32_t prev_advance_fp = 0;
    font_data_t *active_font = font;
    const char *active_path = font_path;

    while ((cp = utf8_next_codepoint(&p))) {
        if (utf8_is_combining_mark(cp)) continue;

        cp = apply_ligatures(font, font_path, cp, &p);

        if (prev_cp != 0) {
            int8_t kern = get_kerning(active_font, active_path, prev_cp, cp);
            width_px += fp4_to_pixel(prev_advance_fp + kern);
        }

        uint32_t idx;
        const font_glyph_t *glyph = get_glyph_with_fallback(
            font_id, font, font_path, cp, &idx,
            &active_font, &active_path);
        prev_advance_fp = glyph ? glyph->advance_x : 0;
        prev_cp = cp;
    }

    width_px += fp4_to_pixel(prev_advance_fp);
    return width_px;
}
