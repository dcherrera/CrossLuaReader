/**
 * @file font_cache.h
 * @brief LRU decompression cache for font glyph bitmaps.
 *        Decompresses DEFLATE groups from SD on demand, keeps 3 groups
 *        in RAM for fast re-access.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "font_types.h"

#include <stdbool.h>
#include <stdint.h>

/** Initialize the font cache. Call once at startup. */
bool font_cache_init(void);

/** Free all cached data. */
void font_cache_clear(void);

/**
 * Get the decompressed bitmap data for a glyph.
 * Handles group decompression and LRU caching transparently.
 *
 * @param font        Loaded font data
 * @param glyph       Glyph to get bitmap for
 * @param glyph_index Index of the glyph in font->glyphs array
 * @param font_path   SD path to the .cfont file (for reading bitmap data)
 * @return            Pointer to packed bitmap data. Valid until next call.
 *                    NULL on error.
 */
const uint8_t *font_cache_get_bitmap(const font_data_t *font,
                                      const font_glyph_t *glyph,
                                      uint32_t glyph_index,
                                      const char *font_path);
