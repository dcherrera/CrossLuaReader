/**
 * @file boot_font.h
 * @brief Boot font accessor. A .cfont is loaded at C startup (before Lua)
 *        for crash screens and sleep screen text overlays.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */
#pragma once

/** Set the boot font ID (called once during setup). */
void boot_font_set_id(int id);

/** @return Boot font slot ID, or -1 if not loaded. */
int boot_font_get_id(void);
