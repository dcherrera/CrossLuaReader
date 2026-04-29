/**
 * @file layout_engine.h
 * @brief Centralized display layout engine. Owns all spatial calculations
 *        for Header/Body/Footer regions. Single source of truth for
 *        "how many lines fit" and "where does content go."
 *
 * @status Phase 1 — core engine
 * @issues None
 * @todo None
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "renderer.h"  /* orientation_t */

/**
 * Layout state — singleton, all spatial calculations.
 * Fields ordered by size descending (int32 enum first, then int16).
 * Zero heap allocation — lives as a single static struct.
 */
typedef struct {
    /* 4-byte field first (enum is int-sized) */
    orientation_t orientation;

    /* Configurable — set from Lua, trigger recalculate */
    int16_t header_height;    /**< Header region height. 0 = hidden. */
    int16_t footer_height;    /**< Footer region height. 0 = hidden. Default 40. */
    int16_t margin_top;       /**< Body top margin. Default 10. */
    int16_t margin_right;     /**< Body right margin. Default 10. */
    int16_t margin_bottom;    /**< Body bottom margin. Default 10. */
    int16_t margin_left;      /**< Body left margin. Default 10. */
    int16_t line_spacing;     /**< Extra pixels between lines. Default 0. */
    int16_t line_height;      /**< Pixels per text line. 0 = derive from font. */
    int16_t font_id;          /**< Font to derive line_height from. -1 = none. */
    int16_t button_bar;       /**< Physical button bar height. 48 = UI, 0 = reader. */

    /* Computed — read-only, recalculated automatically */
    int16_t header_x, header_y, header_w, header_h;
    int16_t body_x, body_y, body_w, body_h;                 /**< With margins */
    int16_t body_raw_x, body_raw_y, body_raw_w, body_raw_h; /**< Without margins */
    int16_t footer_x, footer_y, footer_w, footer_h;
    int16_t lines_per_page;        /**< Uniform lines fitting in body */
    int16_t effective_line_height;  /**< line_height + line_spacing */

    /* Physical display info */
    int16_t display_w, display_h;
    int16_t bezel_top, bezel_right, bezel_bottom, bezel_left;
} layout_state_t;

/**
 * Initialize the layout engine. Reads physical display dimensions
 * and bezel margins from the renderer. Sets defaults.
 * Call once during boot, after renderer_init().
 */
void layout_init(void);

/**
 * Reset configurable values to defaults. Called by plugin manager
 * before each plugin's onEnter() so every plugin starts clean.
 */
void layout_reset_defaults(void);

/**
 * Recalculate all computed values from current configurable state.
 * Called automatically by all setters — rarely needed directly.
 */
void layout_recalculate(void);

/* ── Setters (each triggers recalculate) ───────────────────────── */

void layout_set_header_height(int16_t height);
void layout_set_footer_height(int16_t height);
void layout_set_margins(int16_t top, int16_t right, int16_t bottom, int16_t left);
void layout_set_line_spacing(int16_t spacing);
void layout_set_line_height(int16_t height);

/**
 * Reserve space for the physical button bar in landscape modes.
 * @param height 48 for UI plugins (button hints visible), 0 for readers.
 */
void layout_set_button_bar(int16_t height);

/**
 * Set the font used to derive line_height.
 * Reads advance_y from the font's metadata.
 * @param font_id Font slot ID (0-2), or -1 to clear.
 */
void layout_set_font(int16_t font_id);

/**
 * Set orientation. Updates display dimensions and bezel rotation.
 * @param orient One of ORIENT_PORTRAIT..ORIENT_LANDSCAPE_CCW
 */
void layout_set_orientation(orientation_t orient);

/* ── Getters ───────────────────────────────────────────────────── */

/** @return Lines fitting in body area at current line height. */
int16_t layout_lines_per_page(void);

/** @return Effective line height (line_height + line_spacing). */
int16_t layout_line_height(void);

/** Header region bounds. All zero if header_height == 0. */
void layout_header_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h);

/** Body region bounds WITH margins applied. */
void layout_body_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h);

/** Body region bounds WITHOUT margins (for free drawing). */
void layout_body_area_raw(int16_t *x, int16_t *y, int16_t *w, int16_t *h);

/** Footer region bounds. All zero if footer_height == 0. */
void layout_footer_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h);

/** Convenience: body width with margins. */
int16_t layout_body_width(void);

/** Convenience: body height with margins. */
int16_t layout_body_height(void);
