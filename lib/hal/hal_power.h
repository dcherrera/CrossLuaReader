/**
 * @file hal_power.h
 * @brief Power management API: battery level, sleep control.
 *
 * @status Phase 6 — configurable sleep timeout + suppress
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize power management and battery monitor.
 * Must be called after hal_gpio_init() (needs device type for X3/X4).
 *
 * @return true on success
 */
bool hal_power_init(void);

/** @return Battery percentage (0-100). Cached, polled every 30 seconds. */
uint16_t hal_power_battery_percent(void);

/** @return Battery voltage in millivolts. */
uint16_t hal_power_battery_millivolts(void);

/**
 * Check if device should auto-sleep due to inactivity.
 * Call once per loop iteration.
 */
void hal_power_check_sleep(void);

/**
 * Set the auto-sleep timeout.
 *
 * @param minutes Minutes of inactivity before sleep (1-30). 0 = disable.
 */
void hal_power_set_sleep_timeout(uint32_t minutes);

/**
 * Suppress or restore auto-sleep.
 * Use when WiFi server is active, USB connected, or downloading.
 *
 * @param suppress true to prevent sleep, false to restore normal behavior
 */
void hal_power_suppress_sleep(bool suppress);

/** @return true if USB is connected (suppresses auto-sleep). */
bool hal_power_is_usb_connected(void);

/** Enter deep sleep immediately. Does not return. */
void hal_power_enter_sleep(void);
