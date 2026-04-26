/**
 * @file bmp_decoder.c
 * @brief Streaming BMP decoder for e-ink wallpapers.
 *        Reads row-by-row from SD, converts to 1bpp framebuffer.
 *        Heap: one row buffer (~2.4KB), freed after decode.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */

#include "bmp_decoder.h"
#include "hal_storage.h"
#include "hal_display.h"
#include "logging.h"

#include <string.h>
#include <stdlib.h>

/* BMP header offsets */
#define BMP_MAGIC        0x4D42
#define BMP_HDR_SIZE     14
#define BMP_DIB_SIZE_OFF 14
#define BMP_WIDTH_OFF    18
#define BMP_HEIGHT_OFF   22
#define BMP_BPP_OFF      28
#define BMP_COMP_OFF     30
#define BMP_DATA_OFF     10

/* Bayer 4x4 ordered dither matrix (0-15 range, scaled to 0-255) */
static const uint8_t BAYER4[4][4] = {
    {  0, 136,  34, 170},
    {204,  68, 238, 102},
    { 51, 187,  17, 153},
    {255, 119, 221,  85},
};

/* Read a little-endian uint16 from buffer */
static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Read a little-endian uint32 from buffer */
static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read a little-endian int32 from buffer */
static int32_t read_i32(const uint8_t *p) {
    uint32_t v = read_u32(p);
    return (int32_t)v;
}

/**
 * Set a pixel in the physical framebuffer (portrait orientation).
 * The e-ink panel is 800 wide × 480 tall in physical memory.
 * Framebuffer: 1bpp, MSB first, row-major, 800 bits per row = 100 bytes.
 */
static void set_pixel_physical(uint8_t *fb, int x, int y, bool white,
                                int panel_w_bytes) {
    int byte_idx = y * panel_w_bytes + (x / 8);
    uint8_t mask = 0x80 >> (x % 8);
    if (white) {
        fb[byte_idx] |= mask;
    } else {
        fb[byte_idx] &= ~mask;
    }
}

bool bmp_decode_to_framebuffer(const char *path, bmp_info_t *info) {
    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        LOG_ERR("BMP", "Cannot open: %s", path);
        return false;
    }

    /* Read BMP file header + DIB header (first 54 bytes) */
    uint8_t hdr[54];
    if (hal_storage_file_read(f, hdr, 54) != 54) {
        LOG_ERR("BMP", "Header read failed");
        hal_storage_file_close(f);
        return false;
    }

    /* Validate magic */
    if (read_u16(hdr) != BMP_MAGIC) {
        LOG_ERR("BMP", "Not a BMP file");
        hal_storage_file_close(f);
        return false;
    }

    uint32_t data_offset = read_u32(hdr + BMP_DATA_OFF);
    int32_t  bmp_w       = read_i32(hdr + BMP_WIDTH_OFF);
    int32_t  bmp_h       = read_i32(hdr + BMP_HEIGHT_OFF);
    uint16_t bpp         = read_u16(hdr + BMP_BPP_OFF);
    uint32_t compression = read_u32(hdr + BMP_COMP_OFF);

    /* Handle top-down BMPs (negative height) */
    bool top_down = false;
    if (bmp_h < 0) {
        bmp_h = -bmp_h;
        top_down = true;
    }

    if (info) {
        info->width = bmp_w;
        info->height = bmp_h;
        info->bpp = bpp;
    }

    LOG_INF("BMP", "%s: %dx%d %dbpp comp=%u", path, bmp_w, bmp_h, bpp, compression);

    /* Validate format */
    if (compression != 0) {
        LOG_ERR("BMP", "Compressed BMPs not supported (comp=%u)", compression);
        hal_storage_file_close(f);
        return false;
    }
    if (bpp != 1 && bpp != 8 && bpp != 24) {
        LOG_ERR("BMP", "Unsupported bpp: %d (need 1, 8, or 24)", bpp);
        hal_storage_file_close(f);
        return false;
    }
    if (bmp_w <= 0 || bmp_h <= 0 || bmp_w > 8000 || bmp_h > 8000) {
        LOG_ERR("BMP", "Invalid dimensions: %dx%d", bmp_w, bmp_h);
        hal_storage_file_close(f);
        return false;
    }

    /* Panel dimensions */
    int panel_w = hal_display_width();   /* physical: 800 */
    int panel_h = hal_display_height();  /* physical: 480 */
    int panel_w_bytes = hal_display_width_bytes();
    uint8_t *fb = hal_display_get_framebuffer();
    if (!fb) {
        LOG_ERR("BMP", "No framebuffer");
        hal_storage_file_close(f);
        return false;
    }

    /* BMP row size (padded to 4 bytes) */
    int bmp_row_bytes;
    if (bpp == 24) {
        bmp_row_bytes = ((bmp_w * 3 + 3) / 4) * 4;
    } else if (bpp == 8) {
        bmp_row_bytes = ((bmp_w + 3) / 4) * 4;
    } else { /* 1-bit */
        bmp_row_bytes = ((bmp_w + 31) / 32) * 4;
    }

    /* Allocate one row buffer. Max ~2.4KB for 800px 24-bit. */
    uint8_t *row_buf = (uint8_t *)malloc(bmp_row_bytes);
    if (!row_buf) {
        LOG_ERR("BMP", "Row buffer malloc failed: %d bytes", bmp_row_bytes);
        hal_storage_file_close(f);
        return false;
    }

    /* Read color table for 8-bit BMPs */
    uint8_t palette[256]; /* grayscale lookup for 8-bit */
    if (bpp == 8) {
        /* Seek to color table (right after DIB header) */
        uint32_t dib_size = read_u32(hdr + BMP_DIB_SIZE_OFF);
        hal_storage_file_seek(f, BMP_HDR_SIZE + dib_size);
        uint8_t ct[1024]; /* 256 entries × 4 bytes (BGRA) */
        int ct_read = hal_storage_file_read(f, ct, 1024);
        if (ct_read < 1024) {
            LOG_ERR("BMP", "Color table read failed");
            free(row_buf);
            hal_storage_file_close(f);
            return false;
        }
        /* Convert to grayscale luminance */
        for (int i = 0; i < 256; i++) {
            uint8_t b = ct[i * 4 + 0];
            uint8_t g = ct[i * 4 + 1];
            uint8_t r = ct[i * 4 + 2];
            palette[i] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        }
    }

    /* Clear framebuffer to white */
    memset(fb, 0xFF, panel_w_bytes * panel_h);

    /* Decode row by row */
    for (int py = 0; py < panel_h; py++) {
        /* Map panel row to BMP row (nearest-neighbor scaling) */
        int sy = (int)((int64_t)py * bmp_h / panel_h);
        if (sy >= bmp_h) sy = bmp_h - 1;

        /* BMP row index: bottom-up unless top-down */
        int bmp_row = top_down ? sy : (bmp_h - 1 - sy);

        /* Seek to the row in the file */
        uint32_t row_offset = data_offset + (uint32_t)bmp_row * bmp_row_bytes;
        hal_storage_file_seek(f, row_offset);

        if (hal_storage_file_read(f, row_buf, bmp_row_bytes) != bmp_row_bytes) {
            /* Partial read — stop decoding, show what we have */
            break;
        }

        /* Convert row pixels to framebuffer */
        for (int px = 0; px < panel_w; px++) {
            int sx = (int)((int64_t)px * bmp_w / panel_w);
            if (sx >= bmp_w) sx = bmp_w - 1;

            bool white;

            if (bpp == 24) {
                int off = sx * 3;
                uint8_t b = row_buf[off + 0];
                uint8_t g = row_buf[off + 1];
                uint8_t r = row_buf[off + 2];
                uint8_t lum = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                /* Bayer 4x4 ordered dithering */
                white = lum > BAYER4[py % 4][px % 4];
            } else if (bpp == 8) {
                uint8_t lum = palette[row_buf[sx]];
                white = lum > BAYER4[py % 4][px % 4];
            } else { /* 1-bit */
                int byte_idx = sx / 8;
                uint8_t bit = 7 - (sx % 8);
                white = (row_buf[byte_idx] >> bit) & 1;
            }

            set_pixel_physical(fb, px, py, white, panel_w_bytes);
        }
    }

    free(row_buf);
    row_buf = NULL;
    hal_storage_file_close(f);

    LOG_INF("BMP", "Decoded %s to framebuffer", path);
    return true;
}
