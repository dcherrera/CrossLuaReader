/**
 * @file font_manager.h
 * @brief Multi-font management: load, unload, and look up fonts by ID.
 *        Wraps font_loader with slot-based ID assignment.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include "font_types.h"

#include <stdbool.h>

/**
 * Load a font from SD card and assign it an ID.
 *
 * @param path SD card path to .cfont file
 * @return     Font ID (0+), or -1 on failure (out of slots, load error)
 */
int font_manager_load(const char *path);

/**
 * Unload a font by ID. Frees all associated memory.
 *
 * @param font_id ID returned by font_manager_load
 */
void font_manager_unload(int font_id);

/**
 * Get the font_data_t for a loaded font.
 *
 * @param font_id ID returned by font_manager_load
 * @return        Pointer to font data, or NULL if not loaded
 */
const font_data_t *font_manager_get(int font_id);

/**
 * Get the SD path for a loaded font (needed by cache for bitmap reads).
 *
 * @param font_id ID returned by font_manager_load
 * @return        Path string, or NULL if not loaded
 */
const char *font_manager_get_path(int font_id);

/** Unload all fonts and free all memory. */
void font_manager_unload_all(void);
