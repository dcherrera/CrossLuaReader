/**
 * @file font_loader.c
 * @brief .cfont file loader. On-demand architecture: glyph metrics stay
 *        on SD and are cached on demand. Intervals, groups, kerning, and
 *        ligatures are loaded into RAM (small, needed for fast lookup).
 *
 * @status Phase 8 — on-demand glyph loading
 * @issues None
 * @todo None
 */

#include "font_loader.h"
#include "hal_storage.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 32

bool font_loader_load(const char *path, font_data_t *out_font) {
    if (!path || !out_font) return false;
    memset(out_font, 0, sizeof(font_data_t));

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        LOG_ERR("FONT", "Failed to open: %s", path);
        return false;
    }

    /* Read 32-byte header */
    uint8_t hdr[HEADER_SIZE];
    if (hal_storage_file_read(f, hdr, HEADER_SIZE) != HEADER_SIZE) {
        LOG_ERR("FONT", "Header read failed: %s", path);
        hal_storage_file_close(f);
        return false;
    }

    if (hdr[0] != 'C' || hdr[1] != 'F' || hdr[2] != 'N' || hdr[3] != 'T') {
        LOG_ERR("FONT", "Bad magic in %s", path);
        hal_storage_file_close(f);
        return false;
    }

    if (hdr[4] != 1) {
        LOG_ERR("FONT", "Unsupported version %d in %s", hdr[4], path);
        hal_storage_file_close(f);
        return false;
    }

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

    /* Section sizes */
    size_t intervals_sz  = out_font->interval_count * sizeof(font_interval_t);
    size_t glyphs_sz     = out_font->glyph_count * sizeof(font_glyph_t);
    size_t groups_sz     = out_font->group_count * sizeof(font_group_t);
    size_t kern_left_sz  = out_font->kern_left_entry_count * sizeof(font_kern_class_t);
    size_t kern_right_sz = out_font->kern_right_entry_count * sizeof(font_kern_class_t);
    size_t kern_matrix_sz = (size_t)out_font->kern_left_class_count *
                            out_font->kern_right_class_count;
    size_t ligatures_sz  = out_font->ligature_count * sizeof(font_ligature_t);

    /*
     * Load everything EXCEPT glyphs into RAM.
     * Glyphs (the biggest section) stay on SD, read on demand via cache.
     */
    size_t ram_total = intervals_sz + groups_sz +
                       kern_left_sz + kern_right_sz + kern_matrix_sz + ligatures_sz;

    /* Record glyph array file offset (for on-demand reads) */
    out_font->glyphs_file_offset = HEADER_SIZE + (uint32_t)intervals_sz;

    /* Allocate single buffer for all RAM-resident sections */
    uint8_t *buf = NULL;
    if (ram_total > 0) {
        buf = (uint8_t *)malloc(ram_total);
        if (!buf) {
            LOG_ERR("FONT", "malloc failed: %zu bytes for %s", ram_total, path);
            hal_storage_file_close(f);
            return false;
        }
    }

    /* File cursor is at HEADER_SIZE (32). Read intervals first. */
    size_t buf_offset = 0;

    if (intervals_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, intervals_sz) != (int)intervals_sz) {
            LOG_ERR("FONT", "Intervals read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->intervals = (font_interval_t *)(buf + buf_offset);
        buf_offset += intervals_sz;
    }

    /* Skip glyphs section (stays on SD) */
    hal_storage_file_seek(f, HEADER_SIZE + (uint32_t)intervals_sz + (uint32_t)glyphs_sz);

    /* Read groups */
    if (groups_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, groups_sz) != (int)groups_sz) {
            LOG_ERR("FONT", "Groups read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->groups = (font_group_t *)(buf + buf_offset);
        buf_offset += groups_sz;
    }

    /* Read kerning left classes */
    if (kern_left_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, kern_left_sz) != (int)kern_left_sz) {
            LOG_ERR("FONT", "Kern left read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->kern_left = (font_kern_class_t *)(buf + buf_offset);
        buf_offset += kern_left_sz;
    }

    /* Read kerning right classes */
    if (kern_right_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, kern_right_sz) != (int)kern_right_sz) {
            LOG_ERR("FONT", "Kern right read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->kern_right = (font_kern_class_t *)(buf + buf_offset);
        buf_offset += kern_right_sz;
    }

    /* Read kerning matrix */
    if (kern_matrix_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, kern_matrix_sz) != (int)kern_matrix_sz) {
            LOG_ERR("FONT", "Kern matrix read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->kern_matrix = (int8_t *)(buf + buf_offset);
        buf_offset += kern_matrix_sz;
    }

    /* Read ligatures */
    if (ligatures_sz > 0) {
        if (hal_storage_file_read(f, buf + buf_offset, ligatures_sz) != (int)ligatures_sz) {
            LOG_ERR("FONT", "Ligatures read failed");
            free(buf);
            hal_storage_file_close(f);
            return false;
        }
        out_font->ligatures = (font_ligature_t *)(buf + buf_offset);
    }

    hal_storage_file_close(f);

    out_font->metadata_buf = buf;
    memset(out_font->glyph_cache, 0, sizeof(out_font->glyph_cache));
    out_font->glyph_cache_tick = 0;

    LOG_INF("FONT", "Loaded %s: %d glyphs, %d intervals, %d groups, %zu bytes RAM (on-demand)",
            path, out_font->glyph_count, out_font->interval_count,
            out_font->group_count, ram_total + sizeof(out_font->glyph_cache));

    return true;
}

void font_loader_unload(font_data_t *font) {
    if (!font) return;
    if (font->metadata_buf) {
        free(font->metadata_buf);
    }
    memset(font, 0, sizeof(font_data_t));
}
