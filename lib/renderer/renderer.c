/**
 * @file renderer.c
 * @brief Framebuffer rendering: pixel, line, rect with orientation transforms.
 *        Ported from CrossPoint's GfxRenderer.cpp pixel-level operations.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "renderer.h"
#include "hal_display.h"
#include "logging.h"

#include <string.h>
#include <stdlib.h>

/* Physical bezel margins (pixels) — same as CrossPoint */
#define MARGIN_TOP    9
#define MARGIN_RIGHT  3
#define MARGIN_BOTTOM 3
#define MARGIN_LEFT   3

/* Module state */
static uint8_t *framebuffer = NULL;
static uint16_t panel_width = 0;
static uint16_t panel_height = 0;
static uint16_t panel_width_bytes = 0;
static uint32_t buffer_size = 0;
static orientation_t current_orient = ORIENT_PORTRAIT;

bool renderer_init(void) {
    framebuffer = hal_display_get_framebuffer();
    if (!framebuffer) {
        LOG_ERR("RND", "No framebuffer from display");
        return false;
    }

    panel_width = (uint16_t)hal_display_width();
    panel_height = (uint16_t)hal_display_height();
    panel_width_bytes = (uint16_t)hal_display_width_bytes();
    buffer_size = hal_display_buffer_size();

    LOG_INF("RND", "Renderer init: panel %dx%d, buffer %u bytes",
            panel_width, panel_height, buffer_size);
    return true;
}

void renderer_set_orientation(orientation_t orient) {
    current_orient = orient;
}

orientation_t renderer_get_orientation(void) {
    return current_orient;
}

int renderer_screen_width(void) {
    switch (current_orient) {
        case ORIENT_PORTRAIT:
        case ORIENT_PORTRAIT_INV:
            return panel_height;
        case ORIENT_LANDSCAPE_CW:
        case ORIENT_LANDSCAPE_CCW:
        default:
            return panel_width;
    }
}

int renderer_screen_height(void) {
    switch (current_orient) {
        case ORIENT_PORTRAIT:
        case ORIENT_PORTRAIT_INV:
            return panel_width;
        case ORIENT_LANDSCAPE_CW:
        case ORIENT_LANDSCAPE_CCW:
        default:
            return panel_height;
    }
}

/**
 * Transform logical coordinates to physical framebuffer coordinates.
 * Ported from GfxRenderer::rotateCoordinates (lines 52-82).
 */
static void rotate_coords(int lx, int ly, int *phyX, int *phyY) {
    switch (current_orient) {
        case ORIENT_PORTRAIT:
            *phyX = ly;
            *phyY = panel_height - 1 - lx;
            break;
        case ORIENT_LANDSCAPE_CW:
            *phyX = panel_width - 1 - lx;
            *phyY = panel_height - 1 - ly;
            break;
        case ORIENT_PORTRAIT_INV:
            *phyX = panel_width - 1 - ly;
            *phyY = lx;
            break;
        case ORIENT_LANDSCAPE_CCW:
        default:
            *phyX = lx;
            *phyY = ly;
            break;
    }
}

void renderer_draw_pixel(int x, int y, bool black) {
    int phyX, phyY;
    rotate_coords(x, y, &phyX, &phyY);

    /* Bounds check against physical panel */
    if (phyX < 0 || phyX >= panel_width || phyY < 0 || phyY >= panel_height) {
        return;
    }

    uint32_t byte_index = (uint32_t)phyY * panel_width_bytes + (phyX / 8);
    uint8_t bit_pos = 7 - (phyX % 8);

    /*
     * E-ink convention: bit=1 is white, bit=0 is black.
     * "black=true" means draw a black pixel → clear the bit.
     */
    if (black) {
        framebuffer[byte_index] &= ~(1 << bit_pos);
    } else {
        framebuffer[byte_index] |= (1 << bit_pos);
    }
}

void renderer_draw_line(int x1, int y1, int x2, int y2, bool black) {
    /* Horizontal line optimization */
    if (y1 == y2) {
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        for (int x = x1; x <= x2; x++) {
            renderer_draw_pixel(x, y1, black);
        }
        return;
    }

    /* Vertical line optimization */
    if (x1 == x2) {
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
        for (int y = y1; y <= y2; y++) {
            renderer_draw_pixel(x1, y, black);
        }
        return;
    }

    /* Bresenham's line algorithm */
    int dx = abs(x2 - x1);
    int dy = -abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        renderer_draw_pixel(x1, y1, black);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void renderer_draw_rect(int x, int y, int w, int h, bool black) {
    renderer_draw_line(x, y, x + w - 1, y, black);             /* top */
    renderer_draw_line(x, y + h - 1, x + w - 1, y + h - 1, black); /* bottom */
    renderer_draw_line(x, y, x, y + h - 1, black);             /* left */
    renderer_draw_line(x + w - 1, y, x + w - 1, y + h - 1, black); /* right */
}

void renderer_fill_rect_gray(int x, int y, int w, int h) {
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            if ((row + col) % 2 == 0) {
                renderer_draw_pixel(col, row, true);
            }
        }
    }
}

void renderer_fill_rounded_rect_gray(int x, int y, int w, int h, int radius) {
    if (radius <= 0) {
        renderer_fill_rect_gray(x, y, w, h);
        return;
    }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    renderer_fill_rect_gray(x + radius, y, w - 2 * radius, h);
    renderer_fill_rect_gray(x, y + radius, radius, h - 2 * radius);
    renderer_fill_rect_gray(x + w - radius, y + radius, radius, h - 2 * radius);

    int cx_tl = x + radius, cy_tl = y + radius;
    int cx_tr = x + w - 1 - radius, cy_tr = y + radius;
    int cx_bl = x + radius, cy_bl = y + h - 1 - radius;
    int cx_br = x + w - 1 - radius, cy_br = y + h - 1 - radius;
    int r = radius, px = 0, py = r, d = 1 - r;

    while (px <= py) {
        for (int i = cx_tl - py; i <= cx_tl; i++)
            if ((cy_tl - px + i) % 2 == 0) renderer_draw_pixel(i, cy_tl - px, true);
        for (int i = cx_tl - px; i <= cx_tl; i++)
            if ((cy_tl - py + i) % 2 == 0) renderer_draw_pixel(i, cy_tl - py, true);
        for (int i = cx_tr; i <= cx_tr + py; i++)
            if ((cy_tr - px + i) % 2 == 0) renderer_draw_pixel(i, cy_tr - px, true);
        for (int i = cx_tr; i <= cx_tr + px; i++)
            if ((cy_tr - py + i) % 2 == 0) renderer_draw_pixel(i, cy_tr - py, true);
        for (int i = cx_bl - py; i <= cx_bl; i++)
            if ((cy_bl + px + i) % 2 == 0) renderer_draw_pixel(i, cy_bl + px, true);
        for (int i = cx_bl - px; i <= cx_bl; i++)
            if ((cy_bl + py + i) % 2 == 0) renderer_draw_pixel(i, cy_bl + py, true);
        for (int i = cx_br; i <= cx_br + py; i++)
            if ((cy_br + px + i) % 2 == 0) renderer_draw_pixel(i, cy_br + px, true);
        for (int i = cx_br; i <= cx_br + px; i++)
            if ((cy_br + py + i) % 2 == 0) renderer_draw_pixel(i, cy_br + py, true);
        px++;
        if (d < 0) { d += 2 * px + 1; } else { py--; d += 2 * (px - py) + 1; }
    }
}

void renderer_fill_rect(int x, int y, int w, int h, bool black) {
    for (int row = y; row < y + h; row++) {
        renderer_draw_line(x, row, x + w - 1, row, black);
    }
}

void renderer_fill_rounded_rect(int x, int y, int w, int h, int radius, bool black) {
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) {
        renderer_fill_rect(x, y, w, h, black);
        return;
    }

    /* Clamp radius to half the smallest dimension */
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* Fill center rectangle (between the rounded corners) */
    renderer_fill_rect(x + radius, y, w - 2 * radius, h, black);

    /* Fill left and right side strips (excluding corner zones) */
    renderer_fill_rect(x, y + radius, radius, h - 2 * radius, black);
    renderer_fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, black);

    /* Fill rounded corners using midpoint circle algorithm */
    int cx_tl = x + radius;         /* top-left center */
    int cy_tl = y + radius;
    int cx_tr = x + w - 1 - radius; /* top-right center */
    int cy_tr = y + radius;
    int cx_bl = x + radius;         /* bottom-left center */
    int cy_bl = y + h - 1 - radius;
    int cx_br = x + w - 1 - radius; /* bottom-right center */
    int cy_br = y + h - 1 - radius;

    int r = radius;
    int px = 0, py = r;
    int d = 1 - r;

    while (px <= py) {
        /* Fill horizontal spans for each corner quadrant */
        /* Top-left: fill from (cx_tl - py) to (cx_tl) at (cy_tl - px) */
        for (int i = cx_tl - py; i <= cx_tl; i++)
            renderer_draw_pixel(i, cy_tl - px, black);
        for (int i = cx_tl - px; i <= cx_tl; i++)
            renderer_draw_pixel(i, cy_tl - py, black);

        /* Top-right */
        for (int i = cx_tr; i <= cx_tr + py; i++)
            renderer_draw_pixel(i, cy_tr - px, black);
        for (int i = cx_tr; i <= cx_tr + px; i++)
            renderer_draw_pixel(i, cy_tr - py, black);

        /* Bottom-left */
        for (int i = cx_bl - py; i <= cx_bl; i++)
            renderer_draw_pixel(i, cy_bl + px, black);
        for (int i = cx_bl - px; i <= cx_bl; i++)
            renderer_draw_pixel(i, cy_bl + py, black);

        /* Bottom-right */
        for (int i = cx_br; i <= cx_br + py; i++)
            renderer_draw_pixel(i, cy_br + px, black);
        for (int i = cx_br; i <= cx_br + px; i++)
            renderer_draw_pixel(i, cy_br + py, black);

        px++;
        if (d < 0) {
            d += 2 * px + 1;
        } else {
            py--;
            d += 2 * (px - py) + 1;
        }
    }
}

/* ── Physical coordinate drawing (bypasses orientation) ──────────── */

#define BUTTON_BAR_HEIGHT 40

void renderer_draw_pixel_physical(int x, int y, bool black) {
    /* Portrait transform: same as ORIENT_PORTRAIT */
    int phyX = y;
    int phyY = panel_height - 1 - x;

    if (phyX < 0 || phyX >= panel_width || phyY < 0 || phyY >= panel_height) return;

    uint32_t byte_index = (uint32_t)phyY * panel_width_bytes + (phyX / 8);
    uint8_t bit_pos = 7 - (phyX % 8);

    if (black) {
        framebuffer[byte_index] &= ~(1 << bit_pos);
    } else {
        framebuffer[byte_index] |= (1 << bit_pos);
    }
}

void renderer_draw_line_physical(int x1, int y1, int x2, int y2, bool black) {
    if (y1 == y2) {
        if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
        for (int x = x1; x <= x2; x++) renderer_draw_pixel_physical(x, y1, black);
        return;
    }
    if (x1 == x2) {
        if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
        for (int y = y1; y <= y2; y++) renderer_draw_pixel_physical(x1, y, black);
        return;
    }
    int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    while (1) {
        renderer_draw_pixel_physical(x1, y1, black);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void renderer_draw_rect_physical(int x, int y, int w, int h, bool black) {
    renderer_draw_line_physical(x, y, x + w - 1, y, black);
    renderer_draw_line_physical(x, y + h - 1, x + w - 1, y + h - 1, black);
    renderer_draw_line_physical(x, y, x, y + h - 1, black);
    renderer_draw_line_physical(x + w - 1, y, x + w - 1, y + h - 1, black);
}

void renderer_get_content_area(int *out_x, int *out_y, int *out_w, int *out_h) {
    int w = renderer_screen_width();
    int h = renderer_screen_height();
    int bar = BUTTON_BAR_HEIGHT + 8;

    switch (current_orient) {
        case ORIENT_PORTRAIT:
            /* physical bottom = logical bottom */
            *out_x = 0; *out_y = 0; *out_w = w; *out_h = h - bar;
            break;
        case ORIENT_LANDSCAPE_CW:
            /* physical bottom = logical left */
            *out_x = bar; *out_y = 0; *out_w = w - bar; *out_h = h;
            break;
        case ORIENT_PORTRAIT_INV:
            /* physical bottom = logical top */
            *out_x = 0; *out_y = bar; *out_w = w; *out_h = h - bar;
            break;
        case ORIENT_LANDSCAPE_CCW:
            /* physical bottom = logical right */
            *out_x = 0; *out_y = 0; *out_w = w - bar; *out_h = h;
            break;
        default:
            *out_x = 0; *out_y = 0; *out_w = w; *out_h = h - bar;
            break;
    }
}

void renderer_clear_screen(uint8_t color) {
    if (framebuffer) {
        memset(framebuffer, color, buffer_size);
    }
}

void renderer_invert_screen(void) {
    if (!framebuffer) return;
    for (uint32_t i = 0; i < buffer_size; i++) {
        framebuffer[i] = ~framebuffer[i];
    }
}

void renderer_get_viewable_margins(int *top, int *right, int *bottom, int *left) {
    /*
     * Physical margins rotated to match current orientation.
     * Ported from GfxRenderer::getOrientedViewableTRBL.
     */
    switch (current_orient) {
        case ORIENT_PORTRAIT:
            *top = MARGIN_LEFT;
            *right = MARGIN_TOP;
            *bottom = MARGIN_RIGHT;
            *left = MARGIN_BOTTOM;
            break;
        case ORIENT_LANDSCAPE_CW:
            *top = MARGIN_TOP;
            *right = MARGIN_LEFT;
            *bottom = MARGIN_BOTTOM;
            *left = MARGIN_RIGHT;
            break;
        case ORIENT_PORTRAIT_INV:
            *top = MARGIN_RIGHT;
            *right = MARGIN_BOTTOM;
            *bottom = MARGIN_LEFT;
            *left = MARGIN_TOP;
            break;
        case ORIENT_LANDSCAPE_CCW:
        default:
            *top = MARGIN_BOTTOM;
            *right = MARGIN_RIGHT;
            *bottom = MARGIN_TOP;
            *left = MARGIN_LEFT;
            break;
    }
}
