/**
 * @file hal_system.h
 * @brief System-level API: boot, restart, heap stats, uptime.
 *        Uses ESP-IDF calls directly (no bridge needed).
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize system-level hardware (watchdog, serial).
 * Must be called first in the boot sequence.
 *
 * @return true on success
 */
bool hal_system_init(void);

/** Restart the device immediately. Does not return. */
void hal_system_restart(void);

/** @return Current free heap in bytes. */
uint32_t hal_system_free_heap(void);

/** @return Total heap size in bytes. */
uint32_t hal_system_total_heap(void);

/** @return Minimum free heap since boot (high-water mark). */
uint32_t hal_system_min_free_heap(void);

/** @return Milliseconds since boot. */
uint32_t hal_system_uptime_ms(void);

/** @return Firmware version string. */
const char *hal_system_version(void);
