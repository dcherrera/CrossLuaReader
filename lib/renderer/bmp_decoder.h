/**
 * @file bmp_decoder.h
 * @brief Streaming BMP decoder. Reads BMP files from SD card directly
 *        to the e-ink framebuffer, row by row, with no full-image allocation.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** BMP image info (populated by bmp_decode_to_framebuffer). */
typedef struct {
    int32_t  width;
    int32_t  height;
    uint16_t bpp;
} bmp_info_t;

/**
 * Decode a BMP file from SD card directly to the framebuffer.
 * Supports uncompressed 1-bit, 8-bit grayscale, and 24-bit RGB BMPs.
 * Scales to fit the physical panel (800x480) via nearest-neighbor.
 * Writes in physical coordinates (bypasses orientation transform).
 *
 * @param path  SD card path to .bmp file
 * @param info  Optional output: image dimensions and depth (can be NULL)
 * @return      true on success
 */
bool bmp_decode_to_framebuffer(const char *path, bmp_info_t *info);
