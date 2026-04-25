/**
 * @file hal_gpio.h
 * @brief Input/GPIO API: button polling, device type detection, deep sleep.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Device type (X3 or X4 hardware). */
typedef enum {
    DEVICE_X4 = 0,
    DEVICE_X3 = 1
} device_type_t;

/** Button indices matching SDK InputManager. */
#define BTN_BACK    0
#define BTN_CONFIRM 1
#define BTN_LEFT    2
#define BTN_RIGHT   3
#define BTN_UP      4
#define BTN_DOWN    5
#define BTN_POWER   6
#define BTN_COUNT   7

/**
 * Initialize SPI bus, input manager, and detect device type.
 * Must be called before hal_display_init() and hal_storage_init()
 * because SPI is shared.
 *
 * @return true on success
 */
bool hal_gpio_init(void);

/** Poll button states. Call once per loop iteration. */
void hal_gpio_poll(void);

/** @return true if button is currently held down. */
bool hal_gpio_is_pressed(uint8_t button);

/** @return true if button was pressed since last poll. */
bool hal_gpio_was_pressed(uint8_t button);

/** @return true if any button was pressed since last poll. */
bool hal_gpio_was_any_pressed(void);

/** @return true if button was released since last poll. */
bool hal_gpio_was_released(uint8_t button);

/** @return true if any button was released since last poll. */
bool hal_gpio_was_any_released(void);

/** @return How long the current button(s) have been held (ms). */
unsigned long hal_gpio_get_held_time(void);

/** @return Detected device type (X3 or X4). */
device_type_t hal_gpio_get_device_type(void);

/** @return true if device is X3. */
bool hal_gpio_is_x3(void);

/** @return true if device is X4. */
bool hal_gpio_is_x4(void);

/** Enter deep sleep with power button wakeup. Does not return. */
void hal_gpio_start_deep_sleep(void);
