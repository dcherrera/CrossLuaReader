/**
 * @file renderer.h
 * @brief Framebuffer rendering API: pixel, line, rect drawing with
 *        orientation transforms. No text rendering (that's Phase 2).
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Screen orientation modes. */
typedef enum {
    ORIENT_PORTRAIT       = 0,  /**< 480x800 logical (default) */
    ORIENT_LANDSCAPE_CW   = 1,  /**< 800x480 logical, rotated 180 */
    ORIENT_PORTRAIT_INV   = 2,  /**< 480x800 logical, inverted */
    ORIENT_LANDSCAPE_CCW  = 3   /**< 800x480 logical, native panel */
} orientation_t;

/**
 * Initialize the renderer. Gets framebuffer pointer and dimensions
 * from the display HAL. Must be called after hal_display_init().
 *
 * @return true on success
 */
bool renderer_init(void);

/**
 * Set the logical screen orientation.
 * Affects coordinate transforms for all draw calls.
 *
 * @param orient One of the ORIENT_* values
 */
void renderer_set_orientation(orientation_t orient);

/** @return Current orientation. */
orientation_t renderer_get_orientation(void);

/** @return Logical screen width for the current orientation. */
int renderer_screen_width(void);

/** @return Logical screen height for the current orientation. */
int renderer_screen_height(void);

/**
 * Draw a single pixel in logical coordinates.
 *
 * @param x     Logical x coordinate
 * @param y     Logical y coordinate
 * @param black true = draw black pixel, false = draw white pixel
 */
void renderer_draw_pixel(int x, int y, bool black);

/**
 * Draw a line between two points.
 * Uses horizontal/vertical optimizations and Bresenham's for diagonals.
 *
 * @param x1, y1  Start point (logical)
 * @param x2, y2  End point (logical)
 * @param black   true = black, false = white
 */
void renderer_draw_line(int x1, int y1, int x2, int y2, bool black);

/**
 * Draw a rectangle outline (1px border).
 *
 * @param x, y   Top-left corner (logical)
 * @param w, h   Width and height
 * @param black  true = black, false = white
 */
void renderer_draw_rect(int x, int y, int w, int h, bool black);

/**
 * Draw a filled rectangle.
 *
 * @param x, y   Top-left corner (logical)
 * @param w, h   Width and height
 * @param black  true = black, false = white
 */
void renderer_fill_rect(int x, int y, int w, int h, bool black);

/**
 * Fill entire framebuffer with a byte value.
 *
 * @param color 0xFF = white, 0x00 = black
 */
void renderer_clear_screen(uint8_t color);

/**
 * Draw a filled rectangle with a dithered gray pattern.
 * Uses checkerboard pattern — every other pixel is black.
 *
 * @param x, y   Top-left corner (logical)
 * @param w, h   Width and height
 */
void renderer_fill_rect_gray(int x, int y, int w, int h);

/**
 * Draw a filled rectangle with rounded corners.
 *
 * @param x, y   Top-left corner (logical)
 * @param w, h   Width and height
 * @param radius Corner radius in pixels
 * @param black  true = black, false = white
 */
void renderer_fill_rounded_rect(int x, int y, int w, int h, int radius, bool black);

/**
 * Draw a filled rounded rectangle with dithered gray fill.
 *
 * @param x, y   Top-left corner (logical)
 * @param w, h   Width and height
 * @param radius Corner radius in pixels
 */
void renderer_fill_rounded_rect_gray(int x, int y, int w, int h, int radius);

/**
 * Draw a pixel in physical (portrait) coordinates, bypassing orientation.
 * Used for UI elements that must stay at fixed physical positions
 * (e.g., button hints at the physical bottom of the device).
 *
 * @param x, y   Physical coordinates (portrait: 480x800)
 * @param black  true = black, false = white
 */
void renderer_draw_pixel_physical(int x, int y, bool black);

/**
 * Draw a line in physical coordinates, bypassing orientation.
 */
void renderer_draw_line_physical(int x1, int y1, int x2, int y2, bool black);

/**
 * Draw a rectangle outline in physical coordinates.
 */
void renderer_draw_rect_physical(int x, int y, int w, int h, bool black);

/**
 * Get the usable content area, excluding the physical button bar.
 * The button bar occupies 40px at the physical bottom. Depending on
 * orientation, this maps to a different logical edge.
 *
 * @param out_x, out_y  Top-left of content area (logical)
 * @param out_w, out_h  Width and height of content area
 */
void renderer_get_content_area(int *out_x, int *out_y, int *out_w, int *out_h);

/** Invert all pixels in the framebuffer. */
void renderer_invert_screen(void);

/**
 * Get the viewable area margins (bezel compensation), rotated for
 * current orientation.
 *
 * @param top, right, bottom, left  Output margin values in pixels
 */
void renderer_get_viewable_margins(int *top, int *right, int *bottom, int *left);
