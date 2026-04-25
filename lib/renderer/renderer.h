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

/** Invert all pixels in the framebuffer. */
void renderer_invert_screen(void);

/**
 * Get the viewable area margins (bezel compensation), rotated for
 * current orientation.
 *
 * @param top, right, bottom, left  Output margin values in pixels
 */
void renderer_get_viewable_margins(int *top, int *right, int *bottom, int *left);
