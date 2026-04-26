/**
 * @file font_cache.c
 * @brief LRU decompression cache for compressed font glyph groups.
 *        Uses uzlib for raw DEFLATE decompression.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "font_cache.h"
#include "hal_storage.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

#include "uzlib.h"

#define CACHE_SLOTS      3
#define MAX_GLYPH_SIZE   2048  /* scratch buffer for single glyph compaction */

typedef struct {
    uint8_t            *data;         /* decompressed group (byte-aligned) */
    uint32_t            data_size;    /* allocation size */
    const font_data_t  *font;         /* which font */
    uint16_t            group_idx;    /* which group */
    uint32_t            access_tick;  /* LRU counter */
    bool                valid;
} cache_slot_t;

static cache_slot_t slots[CACHE_SLOTS];
static uint32_t tick = 0;
static uint8_t compact_buf[MAX_GLYPH_SIZE];

bool font_cache_init(void) {
    memset(slots, 0, sizeof(slots));
    tick = 0;
    return true;
}

void font_cache_clear(void) {
    for (int i = 0; i < CACHE_SLOTS; i++) {
        if (slots[i].data) {
            free(slots[i].data);
            slots[i].data = NULL;
        }
        slots[i].valid = false;
    }
}

/**
 * Find which group a glyph belongs to (linear scan, max ~15 groups).
 *
 * @return Group index, or -1 if not found
 */
static int find_group_for_glyph(const font_data_t *font, uint32_t glyph_index) {
    for (uint16_t g = 0; g < font->group_count; g++) {
        uint32_t first = font->groups[g].first_glyph_index;
        uint32_t last = first + font->groups[g].glyph_count;
        if (glyph_index >= first && glyph_index < last) {
            return (int)g;
        }
    }
    return -1;
}

/**
 * Find a cache slot that already has this font+group.
 *
 * @return Slot index, or -1 if not cached
 */
static int find_cached_slot(const font_data_t *font, uint16_t group_idx) {
    for (int i = 0; i < CACHE_SLOTS; i++) {
        if (slots[i].valid && slots[i].font == font && slots[i].group_idx == group_idx) {
            return i;
        }
    }
    return -1;
}

/**
 * Find the LRU (least recently used) cache slot.
 *
 * @return Slot index
 */
static int find_lru_slot(void) {
    int lru = 0;
    uint32_t min_tick = slots[0].access_tick;
    for (int i = 1; i < CACHE_SLOTS; i++) {
        if (!slots[i].valid) return i;  /* empty slot = best choice */
        if (slots[i].access_tick < min_tick) {
            min_tick = slots[i].access_tick;
            lru = i;
        }
    }
    return lru;
}

/**
 * Decompress a group from SD into a cache slot.
 *
 * @return true on success
 */
static bool decompress_group(int slot_idx, const font_data_t *font,
                              uint16_t group_idx, const char *font_path) {
    const font_group_t *grp = &font->groups[group_idx];
    cache_slot_t *slot = &slots[slot_idx];

    /* Free previous data if any */
    if (slot->data) {
        free(slot->data);
        slot->data = NULL;
    }
    slot->valid = false;

    /* Read compressed data from SD */
    hal_file_t f = hal_storage_open(font_path, HAL_FILE_READ);
    if (!f) {
        LOG_ERR("FCACHE", "Failed to open %s for group %d", font_path, group_idx);
        return false;
    }

    uint32_t file_offset = font->bitmap_file_offset + grp->compressed_offset;
    if (!hal_storage_file_seek(f, file_offset)) {
        LOG_ERR("FCACHE", "Seek failed to offset %u", file_offset);
        hal_storage_file_close(f);
        return false;
    }

    uint8_t *comp_buf = (uint8_t *)malloc(grp->compressed_size);
    if (!comp_buf) {
        LOG_ERR("FCACHE", "malloc failed for compressed: %u bytes", grp->compressed_size);
        hal_storage_file_close(f);
        return false;
    }

    int read = hal_storage_file_read(f, comp_buf, grp->compressed_size);
    hal_storage_file_close(f);

    if (read != (int)grp->compressed_size) {
        LOG_ERR("FCACHE", "Read %d, expected %u", read, grp->compressed_size);
        free(comp_buf);
        return false;
    }

    /* Allocate decompression buffer */
    slot->data = (uint8_t *)malloc(grp->uncompressed_size);
    if (!slot->data) {
        LOG_ERR("FCACHE", "malloc failed for decompress: %u bytes", grp->uncompressed_size);
        free(comp_buf);
        return false;
    }

    /* Decompress with uzlib (raw DEFLATE, no zlib header) */
    struct uzlib_uncomp d;
    uzlib_uncompress_init(&d, NULL, 0);
    d.source = comp_buf;
    d.source_limit = comp_buf + grp->compressed_size;
    d.dest_start = slot->data;
    d.dest = slot->data;
    d.dest_limit = slot->data + grp->uncompressed_size;

    /* uzlib_uncompress returns TINF_OK (0) for more data, TINF_DONE (1) when finished */
    int res;
    while ((res = uzlib_uncompress(&d)) == TINF_OK) {
        /* keep going */
    }
    free(comp_buf);

    if (res != TINF_DONE) {
        LOG_ERR("FCACHE", "Decompress failed: %d (group %d)", res, group_idx);
        free(slot->data);
        slot->data = NULL;
        return false;
    }

    slot->data_size = grp->uncompressed_size;
    slot->font = font;
    slot->group_idx = group_idx;
    slot->access_tick = ++tick;
    slot->valid = true;

    LOG_DBG("FCACHE", "Decompressed group %d: %u -> %u bytes",
            group_idx, grp->compressed_size, grp->uncompressed_size);

    return true;
}

/**
 * Extract a single glyph's packed bitmap from byte-aligned group data.
 * The group stores bitmaps in byte-aligned format (row stride padded).
 * This compacts it to packed format matching the glyph's data_length.
 *
 * @param group_data  Decompressed group data (byte-aligned)
 * @param glyph       Glyph metadata
 * @param is_2bit     true for 2-bit bitmaps, false for 1-bit
 * @return            Pointer to compact_buf with packed data
 */
/**
 * Compute the byte-aligned offset of a glyph within decompressed group data.
 * Sums the aligned sizes of all preceding glyphs in the group.
 * Ported from CrossPoint's FontDecompressor::getAlignedOffset.
 */
static uint32_t get_aligned_offset(const font_data_t *font, uint16_t group_idx,
                                    uint32_t glyph_index) {
    const font_group_t *grp = &font->groups[group_idx];
    uint32_t offset = 0;

    for (uint32_t i = grp->first_glyph_index; i < glyph_index; i++) {
        const font_glyph_t *g = &font->glyphs[i];
        if (g->width > 0 && g->height > 0) {
            offset += ((g->width + 3) / 4) * g->height;
        }
    }

    return offset;
}

/**
 * Compact a single glyph from byte-aligned to packed format.
 * Ported from CrossPoint's FontDecompressor::compactSingleGlyph.
 */
static const uint8_t *compact_glyph(const uint8_t *aligned_src,
                                     const font_glyph_t *glyph, bool is_2bit) {
    if (glyph->data_length == 0 || glyph->width == 0 || glyph->height == 0) {
        return compact_buf;
    }

    uint32_t row_stride = (glyph->width + 3) / 4;

    if (!is_2bit) {
        size_t copy = row_stride * glyph->height;
        if (copy > MAX_GLYPH_SIZE) copy = MAX_GLYPH_SIZE;
        memcpy(compact_buf, aligned_src, copy);
        return compact_buf;
    }

    /* If width is a multiple of 4, byte-aligned == packed — just copy */
    if (glyph->width % 4 == 0) {
        size_t copy = row_stride * glyph->height;
        if (copy > MAX_GLYPH_SIZE) copy = MAX_GLYPH_SIZE;
        memcpy(compact_buf, aligned_src, copy);
        return compact_buf;
    }

    /* Compact: strip row padding */
    uint8_t out_byte = 0;
    uint8_t out_bits = 0;
    uint32_t write_idx = 0;

    for (uint8_t y = 0; y < glyph->height; y++) {
        for (uint8_t x = 0; x < glyph->width; x++) {
            uint8_t pixel = (aligned_src[y * row_stride + x / 4] >> ((3 - (x % 4)) * 2)) & 0x03;
            out_byte = (out_byte << 2) | pixel;
            out_bits += 2;
            if (out_bits == 8) {
                if (write_idx < MAX_GLYPH_SIZE) {
                    compact_buf[write_idx++] = out_byte;
                }
                out_byte = 0;
                out_bits = 0;
            }
        }
    }

    /* Flush remaining bits */
    if (out_bits > 0 && write_idx < MAX_GLYPH_SIZE) {
        compact_buf[write_idx++] = out_byte << (8 - out_bits);
    }

    return compact_buf;
}

const uint8_t *font_cache_get_bitmap(const font_data_t *font,
                                      const font_glyph_t *glyph,
                                      uint32_t glyph_index,
                                      const char *font_path) {
    if (!font || !glyph || !font_path) return NULL;

    /* Uncompressed fonts: read directly from SD */
    if (font->group_count == 0) {
        hal_file_t f = hal_storage_open(font_path, HAL_FILE_READ);
        if (!f) return NULL;
        uint32_t offset = font->bitmap_file_offset + glyph->data_offset;
        hal_storage_file_seek(f, offset);
        size_t read_size = glyph->data_length < MAX_GLYPH_SIZE ? glyph->data_length : MAX_GLYPH_SIZE;
        hal_storage_file_read(f, compact_buf, read_size);
        hal_storage_file_close(f);
        return compact_buf;
    }

    /* Find which group this glyph belongs to */
    int group_idx = find_group_for_glyph(font, glyph_index);
    if (group_idx < 0) {
        LOG_ERR("FCACHE", "No group for glyph %u", glyph_index);
        return NULL;
    }

    /* Check cache */
    int slot = find_cached_slot(font, (uint16_t)group_idx);
    if (slot < 0) {
        /* Cache miss — decompress into LRU slot */
        slot = find_lru_slot();
        if (!decompress_group(slot, font, (uint16_t)group_idx, font_path)) {
            return NULL;
        }
    }

    slots[slot].access_tick = ++tick;

    /* Compute byte-aligned offset of this glyph within decompressed group */
    uint32_t aligned_off = get_aligned_offset(font, (uint16_t)group_idx, glyph_index);
    const uint8_t *glyph_src = slots[slot].data + aligned_off;

    return compact_glyph(glyph_src, glyph, font->is_2bit);
}
