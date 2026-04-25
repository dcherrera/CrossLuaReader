/**
 * @file font_loader.h
 * @brief Load .cfont binary font files from SD card into RAM.
 *        Metadata tables go to RAM; bitmap data stays on SD.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "font_types.h"

#include <stdbool.h>

/**
 * Load a .cfont file from SD card.
 * Reads the header and all metadata tables into a single heap allocation.
 * Bitmap data stays on SD — only bitmap_file_offset is stored.
 *
 * @param path     SD card path (e.g., "/fonts/NotoSans-14-Regular.cfont")
 * @param out_font Pointer to font_data_t to populate
 * @return         true on success
 */
bool font_loader_load(const char *path, font_data_t *out_font);

/**
 * Free all memory associated with a loaded font.
 * Zeroes all pointers in the font_data_t.
 *
 * @param font Font to unload
 */
void font_loader_unload(font_data_t *font);
