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
 * Load a firmware-embedded .cfont (bytes in flash) and assign it an ID.
 * Uses the same slot pool as font_manager_load. Subsequent on-demand glyph
 * and bitmap reads come from the supplied buffer instead of opening an SD
 * file. The buffer must remain valid for the lifetime of the font slot —
 * .rodata arrays from the build pipeline satisfy this.
 *
 * @param data Pointer to .cfont bytes in flash
 * @param len  Size of the buffer in bytes
 * @return     Font ID (0+), or -1 on failure
 */
int font_manager_load_buffer(const uint8_t *data, uint32_t len);

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

/**
 * Set a fallback font for a slot. When glyph lookup fails on the primary
 * font, the renderer will try the fallback font before giving up.
 *
 * @param font_id     Primary font slot ID
 * @param fallback_id Fallback font slot ID (must be loaded, != font_id)
 * @return            true on success
 */
bool font_manager_set_fallback(int font_id, int fallback_id);

/**
 * Clear the fallback for a font slot.
 *
 * @param font_id Font slot ID
 */
void font_manager_clear_fallback(int font_id);

/**
 * Get the fallback font data and path for a slot.
 *
 * @param font_id  Primary font slot ID
 * @param out_path Output: fallback font's SD path (can be NULL if not needed)
 * @return         Fallback font data, or NULL if no fallback set
 */
const font_data_t *font_manager_get_fallback(int font_id, const char **out_path);
