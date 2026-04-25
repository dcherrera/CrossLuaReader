/**
 * @file font_loader.c
 * @brief .cfont file loader implementation. Reads header + metadata into
 *        a single contiguous heap allocation, sets up pointers.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "font_loader.h"
#include "hal_storage.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

bool font_loader_load(const char *path, font_data_t *out_font) {
    if (!path || !out_font) return false;
    memset(out_font, 0, sizeof(font_data_t));

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        LOG_ERR("FONT", "Failed to open: %s", path);
        return false;
    }

    /* Read 32-byte header */
    uint8_t hdr[32];
    if (hal_storage_file_read(f, hdr, 32) != 32) {
        LOG_ERR("FONT", "Header read failed: %s", path);
        hal_storage_file_close(f);
        return false;
    }

    /* Validate magic */
    if (hdr[0] != 'C' || hdr[1] != 'F' || hdr[2] != 'N' || hdr[3] != 'T') {
        LOG_ERR("FONT", "Bad magic in %s", path);
        hal_storage_file_close(f);
        return false;
    }

    /* Validate version */
    if (hdr[4] != 1) {
        LOG_ERR("FONT", "Unsupported version %d in %s", hdr[4], path);
        hal_storage_file_close(f);
        return false;
    }

    /* Extract header fields (little-endian, alignment-safe via memcpy) */
    out_font->is_2bit = (hdr[5] & 0x01) != 0;
    out_font->advance_y = hdr[8];
    out_font->ascender = (int8_t)hdr[9];
    out_font->descender = (int8_t)hdr[10];
    out_font->kern_left_class_count = hdr[11];
    out_font->kern_right_class_count = hdr[12];

    memcpy(&out_font->glyph_count, &hdr[14], 2);
    memcpy(&out_font->interval_count, &hdr[16], 2);
    memcpy(&out_font->group_count, &hdr[18], 2);
    memcpy(&out_font->kern_left_entry_count, &hdr[20], 2);
    memcpy(&out_font->kern_right_entry_count, &hdr[22], 2);
    memcpy(&out_font->ligature_count, &hdr[24], 2);
    memcpy(&out_font->bitmap_file_offset, &hdr[28], 4);

    /* Calculate metadata section sizes */
    size_t intervals_sz = out_font->interval_count * sizeof(font_interval_t);
    size_t glyphs_sz = out_font->glyph_count * sizeof(font_glyph_t);
    size_t groups_sz = out_font->group_count * sizeof(font_group_t);
    size_t kern_left_sz = out_font->kern_left_entry_count * sizeof(font_kern_class_t);
    size_t kern_right_sz = out_font->kern_right_entry_count * sizeof(font_kern_class_t);
    size_t kern_matrix_sz = (size_t)out_font->kern_left_class_count *
                            out_font->kern_right_class_count;
    size_t ligatures_sz = out_font->ligature_count * sizeof(font_ligature_t);

    size_t total = intervals_sz + glyphs_sz + groups_sz +
                   kern_left_sz + kern_right_sz + kern_matrix_sz + ligatures_sz;

    if (total == 0 || total > 100000) {
        LOG_ERR("FONT", "Metadata size out of range: %zu bytes", total);
        hal_storage_file_close(f);
        return false;
    }

    /* Single contiguous allocation */
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        LOG_ERR("FONT", "malloc failed: %zu bytes for %s", total, path);
        hal_storage_file_close(f);
        return false;
    }

    /* Read all metadata in one call */
    int read = hal_storage_file_read(f, buf, total);
    hal_storage_file_close(f);

    if (read != (int)total) {
        LOG_ERR("FONT", "Metadata read: got %d, expected %zu", read, total);
        free(buf);
        return false;
    }

    /* Set up pointers into the contiguous buffer */
    size_t offset = 0;

    out_font->intervals = (font_interval_t *)(buf + offset);
    offset += intervals_sz;

    out_font->glyphs = (font_glyph_t *)(buf + offset);
    offset += glyphs_sz;

    out_font->groups = (out_font->group_count > 0)
                       ? (font_group_t *)(buf + offset) : NULL;
    offset += groups_sz;

    out_font->kern_left = (out_font->kern_left_entry_count > 0)
                          ? (font_kern_class_t *)(buf + offset) : NULL;
    offset += kern_left_sz;

    out_font->kern_right = (out_font->kern_right_entry_count > 0)
                           ? (font_kern_class_t *)(buf + offset) : NULL;
    offset += kern_right_sz;

    out_font->kern_matrix = (kern_matrix_sz > 0)
                            ? (int8_t *)(buf + offset) : NULL;
    offset += kern_matrix_sz;

    out_font->ligatures = (out_font->ligature_count > 0)
                          ? (font_ligature_t *)(buf + offset) : NULL;

    out_font->metadata_buf = buf;

    LOG_INF("FONT", "Loaded %s: %d glyphs, %d groups, %zu bytes metadata",
            path, out_font->glyph_count, out_font->group_count, total);

    return true;
}

void font_loader_unload(font_data_t *font) {
    if (!font) return;
    if (font->metadata_buf) {
        free(font->metadata_buf);
    }
    memset(font, 0, sizeof(font_data_t));
}
