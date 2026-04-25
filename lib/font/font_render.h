/**
 * @file font_render.h
 * @brief Text drawing and measurement API. Handles glyph lookup,
 *        kerning, ligatures, combining marks, and BiDi reordering.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "font_types.h"

#include <stdbool.h>

/**
 * Draw a UTF-8 text string. Handles combining marks, kerning,
 * ligatures, and BiDi reordering for mixed LTR/RTL text.
 * y is the top of the text line (ascender added internally).
 *
 * @param font      Loaded font data
 * @param font_path SD path for bitmap reads
 * @param x         Left edge X position (logical coordinates)
 * @param y         Top of text line Y position (logical coordinates)
 * @param text      UTF-8 encoded string
 * @param black     true = black pixels, false = white pixels
 */
void font_render_draw_text(const font_data_t *font, const char *font_path,
                           int x, int y, const char *text, bool black);

/**
 * Measure text advance width (cursor movement distance).
 * Accounts for kerning, ligatures, and combining marks.
 *
 * @param font Loaded font data
 * @param text UTF-8 encoded string
 * @return     Advance width in pixels
 */
int font_render_get_text_advance(const font_data_t *font, const char *text);

/**
 * Measure text bounding box width.
 * Uses glyph bounding boxes rather than advance widths.
 *
 * @param font Loaded font data
 * @param text UTF-8 encoded string
 * @return     Bounding box width in pixels
 */
int font_render_get_text_width(const font_data_t *font, const char *text);

/** @return Line height (advance_y) for the font. */
int font_render_get_line_height(const font_data_t *font);

/** @return Ascender height above baseline. */
int font_render_get_ascender(const font_data_t *font);
