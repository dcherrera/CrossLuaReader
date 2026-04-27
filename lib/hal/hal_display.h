/**
 * @file hal_display.h
 * @brief E-ink display API: init, clear, refresh, framebuffer access.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** E-ink refresh modes. */
typedef enum {
    REFRESH_FULL = 0,  /**< Full waveform refresh (best quality, slowest) */
    REFRESH_HALF = 1,  /**< Half refresh (balanced) */
    REFRESH_FAST = 2   /**< Fast refresh via custom LUT (fastest, some ghosting) */
} refresh_mode_t;

/**
 * Initialize the e-ink display.
 * Must be called after hal_gpio_init() (shared SPI bus).
 * Automatically configures for X3 if detected.
 *
 * @return true on success
 */
bool hal_display_init(void);

/**
 * Fill the framebuffer with a solid color.
 *
 * @param color Byte value: 0xFF = white, 0x00 = black
 */
void hal_display_clear(uint8_t color);

/**
 * Push framebuffer to the e-ink display.
 *
 * @param mode Refresh quality/speed tradeoff
 */
void hal_display_refresh(refresh_mode_t mode);

/**
 * Wait for any in-progress e-ink refresh to complete.
 * Call before writing new data to the framebuffer if a previous
 * refresh may still be in progress.
 */
void hal_display_wait_busy(void);

/** Enter display deep sleep (low power). */
void hal_display_deep_sleep(void);

/** @return Pointer to the framebuffer (1bpp, MSB first, row-major). */
uint8_t *hal_display_get_framebuffer(void);

/** @return Display width in pixels. */
int hal_display_width(void);

/** @return Display height in pixels. */
int hal_display_height(void);

/** @return Display width in bytes (width / 8). */
int hal_display_width_bytes(void);

/** @return Framebuffer size in bytes. */
uint32_t hal_display_buffer_size(void);
