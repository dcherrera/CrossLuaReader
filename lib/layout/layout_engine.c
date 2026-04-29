/**
 * @file layout_engine.c
 * @brief Centralized layout engine implementation. Computes Header/Body/Footer
 *        regions from configurable inputs. Single static struct, zero heap.
 *
 * @status Phase 1 — core engine
 * @issues None
 * @todo None
 */

#include "layout_engine.h"
#include "font_manager.h"
#include "font_render.h"
#include "logging.h"

/* Physical button bar: 40px bar + 8px padding at device bottom.
 * In landscape, this maps to a side edge and must be excluded. */
#define BUTTON_BAR_PHYSICAL 48

static layout_state_t state;

/* ── Helpers ───────────────────────────────────────────────────── */

/** Clamp a value to [min, max]. */
static int16_t clamp16(int16_t val, int16_t min, int16_t max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* ── Init ──────────────────────────────────────────────────────── */

void layout_init(void) {
    /* Query physical display from renderer */
    state.display_w = (int16_t)renderer_screen_width();
    state.display_h = (int16_t)renderer_screen_height();
    state.orientation = renderer_get_orientation();

    /* Fetch bezel margins (already rotated for current orientation) */
    int top, right, bottom, left;
    renderer_get_viewable_margins(&top, &right, &bottom, &left);
    state.bezel_top    = (int16_t)top;
    state.bezel_right  = (int16_t)right;
    state.bezel_bottom = (int16_t)bottom;
    state.bezel_left   = (int16_t)left;

    /* Defaults */
    state.header_height = 0;
    state.footer_height = 40;
    state.button_bar    = BUTTON_BAR_PHYSICAL;
    state.margin_top    = 10;
    state.margin_right  = 10;
    state.margin_bottom = 10;
    state.margin_left   = 10;
    state.line_spacing  = 0;
    state.line_height   = 0;  /* derive from font when set */
    state.font_id       = -1;

    layout_recalculate();

    LOG_INF("LAYOUT", "Init: %dx%d, bezel t=%d r=%d b=%d l=%d",
            state.display_w, state.display_h,
            state.bezel_top, state.bezel_right,
            state.bezel_bottom, state.bezel_left);
}

void layout_reset_defaults(void) {
    state.header_height = 0;
    state.footer_height = 40;
    state.button_bar    = BUTTON_BAR_PHYSICAL;
    state.margin_top    = 10;
    state.margin_right  = 10;
    state.margin_bottom = 10;
    state.margin_left   = 10;
    state.line_spacing  = 0;
    state.line_height   = 0;
    state.font_id       = -1;
    layout_recalculate();
}

/* ── Recalculate ───────────────────────────────────────────────── */

void layout_recalculate(void) {
    /* Effective margins = bezel + button bar on the physical-bottom edge */
    int16_t eff_top    = state.bezel_top;
    int16_t eff_right  = state.bezel_right;
    int16_t eff_bottom = state.bezel_bottom;
    int16_t eff_left   = state.bezel_left;

    switch (state.orientation) {
        case ORIENT_LANDSCAPE_CW:   eff_left   += state.button_bar; break;
        case ORIENT_LANDSCAPE_CCW:  eff_right  += state.button_bar; break;
        default: break; /* portrait: footer covers button bar zone */
    }

    int16_t usable_w = state.display_w - eff_left - eff_right;
    int16_t usable_h = state.display_h - eff_top - eff_bottom;
    int16_t origin_x = eff_left;
    int16_t origin_y = eff_top;

    /* Header: top of usable area */
    state.header_x = origin_x;
    state.header_y = origin_y;
    state.header_w = usable_w;
    state.header_h = state.header_height;

    /* Footer: bottom of usable area */
    state.footer_w = usable_w;
    state.footer_h = state.footer_height;
    state.footer_x = origin_x;
    state.footer_y = origin_y + usable_h - state.footer_height;

    /* Body raw: everything between header and footer */
    state.body_raw_x = origin_x;
    state.body_raw_y = origin_y + state.header_height;
    state.body_raw_w = usable_w;
    state.body_raw_h = usable_h - state.header_height - state.footer_height;

    /* Clamp body_raw_h to non-negative */
    if (state.body_raw_h < 0) state.body_raw_h = 0;

    /* Body with margins */
    state.body_x = state.body_raw_x + state.margin_left;
    state.body_y = state.body_raw_y + state.margin_top;
    state.body_w = state.body_raw_w - state.margin_left - state.margin_right;
    state.body_h = state.body_raw_h - state.margin_top - state.margin_bottom;

    if (state.body_w < 0) state.body_w = 0;
    if (state.body_h < 0) state.body_h = 0;

    /* Effective line height */
    state.effective_line_height = state.line_height + state.line_spacing;
    if (state.effective_line_height < 1) state.effective_line_height = 1;

    /* Lines per page */
    state.lines_per_page = state.body_h / state.effective_line_height;
    if (state.lines_per_page < 0) state.lines_per_page = 0;
}

/* ── Setters ───────────────────────────────────────────────────── */

void layout_set_header_height(int16_t height) {
    state.header_height = clamp16(height, 0, state.display_h);
    layout_recalculate();
}

void layout_set_footer_height(int16_t height) {
    state.footer_height = clamp16(height, 0, state.display_h);
    layout_recalculate();
}

void layout_set_margins(int16_t top, int16_t right, int16_t bottom, int16_t left) {
    state.margin_top    = clamp16(top, 0, 100);
    state.margin_right  = clamp16(right, 0, 100);
    state.margin_bottom = clamp16(bottom, 0, 100);
    state.margin_left   = clamp16(left, 0, 100);
    layout_recalculate();
}

void layout_set_button_bar(int16_t height) {
    state.button_bar = clamp16(height, 0, 100);
    layout_recalculate();
}

void layout_set_line_spacing(int16_t spacing) {
    state.line_spacing = clamp16(spacing, 0, 50);
    layout_recalculate();
}

void layout_set_line_height(int16_t height) {
    state.line_height = clamp16(height, 0, 200);
    state.font_id = -1;  /* manual override clears font derivation */
    layout_recalculate();
}

void layout_set_font(int16_t font_id) {
    const font_data_t *font = font_manager_get(font_id);
    if (font) {
        state.font_id = font_id;
        state.line_height = (int16_t)font_render_get_line_height(font);
        layout_recalculate();
        LOG_INF("LAYOUT", "Font %d → line_height=%d, lpp=%d",
                font_id, state.line_height, state.lines_per_page);
    }
}

void layout_set_orientation(orientation_t orient) {
    state.orientation = orient;
    state.display_w = (int16_t)renderer_screen_width();
    state.display_h = (int16_t)renderer_screen_height();

    /* Re-fetch rotated bezel margins */
    int top, right, bottom, left;
    renderer_get_viewable_margins(&top, &right, &bottom, &left);
    state.bezel_top    = (int16_t)top;
    state.bezel_right  = (int16_t)right;
    state.bezel_bottom = (int16_t)bottom;
    state.bezel_left   = (int16_t)left;

    layout_recalculate();
}

/* ── Getters ───────────────────────────────────────────────────── */

int16_t layout_lines_per_page(void) {
    return state.lines_per_page;
}

int16_t layout_line_height(void) {
    return state.effective_line_height;
}

void layout_header_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
    *x = state.header_x; *y = state.header_y;
    *w = state.header_w; *h = state.header_h;
}

void layout_body_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
    *x = state.body_x; *y = state.body_y;
    *w = state.body_w; *h = state.body_h;
}

void layout_body_area_raw(int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
    *x = state.body_raw_x; *y = state.body_raw_y;
    *w = state.body_raw_w; *h = state.body_raw_h;
}

void layout_footer_area(int16_t *x, int16_t *y, int16_t *w, int16_t *h) {
    *x = state.footer_x; *y = state.footer_y;
    *w = state.footer_w; *h = state.footer_h;
}

int16_t layout_body_width(void) {
    return state.body_w;
}

int16_t layout_body_height(void) {
    return state.body_h;
}
