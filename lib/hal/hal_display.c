/**
 * @file hal_display.c
 * @brief E-ink display implementation. Wraps bridge with X3 detection
 *        and dimension caching for fast access.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "hal_display.h"
#include "hal_gpio.h"
#include "logging.h"

#include <stddef.h>

/* Bridge function declarations */
extern void bridge_display_set_x3(void);
extern void bridge_display_init(void);
extern void bridge_display_clear(uint8_t color);
extern void bridge_display_refresh(int mode, bool turn_off);
extern void bridge_display_deep_sleep(void);
extern uint8_t *bridge_display_get_framebuffer(void);
extern uint16_t bridge_display_get_width(void);
extern uint16_t bridge_display_get_height(void);
extern uint16_t bridge_display_get_width_bytes(void);
extern uint32_t bridge_display_get_buffer_size(void);

/* Cached display properties for fast access */
static uint8_t *cached_framebuffer = NULL;
static uint16_t cached_width = 0;
static uint16_t cached_height = 0;
static uint16_t cached_width_bytes = 0;
static uint32_t cached_buffer_size = 0;
static bool initialized = false;

bool hal_display_init(void) {
    if (initialized) return true;

    if (hal_gpio_is_x3()) {
        LOG_INF("DISP", "Configuring for X3 panel");
        bridge_display_set_x3();
    }

    LOG_INF("DISP", "Initializing display");
    bridge_display_init();

    cached_framebuffer = bridge_display_get_framebuffer();
    cached_width = bridge_display_get_width();
    cached_height = bridge_display_get_height();
    cached_width_bytes = bridge_display_get_width_bytes();
    cached_buffer_size = bridge_display_get_buffer_size();

    if (!cached_framebuffer) {
        LOG_ERR("DISP", "Failed to get framebuffer");
        return false;
    }

    LOG_INF("DISP", "Display: %dx%d, buffer: %u bytes",
            cached_width, cached_height, cached_buffer_size);

    initialized = true;
    return true;
}

void hal_display_clear(uint8_t color) {
    bridge_display_clear(color);
}

void hal_display_refresh(refresh_mode_t mode) {
    bridge_display_refresh((int)mode, false);
}

void hal_display_deep_sleep(void) {
    bridge_display_deep_sleep();
}

uint8_t *hal_display_get_framebuffer(void) {
    return cached_framebuffer;
}

int hal_display_width(void) {
    return cached_width;
}

int hal_display_height(void) {
    return cached_height;
}

int hal_display_width_bytes(void) {
    return cached_width_bytes;
}

uint32_t hal_display_buffer_size(void) {
    return cached_buffer_size;
}
