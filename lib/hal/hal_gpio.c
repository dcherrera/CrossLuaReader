/**
 * @file hal_gpio.c
 * @brief Input/GPIO implementation. Initializes shared SPI bus,
 *        wraps InputManager bridge, detects X3/X4 hardware.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "hal_gpio.h"
#include "logging.h"

/* Bridge function declarations */
extern void bridge_spi_init(void);
extern void bridge_input_init(void);
extern void bridge_input_update(void);
extern bool bridge_input_is_pressed(uint8_t button);
extern bool bridge_input_was_pressed(uint8_t button);
extern bool bridge_input_was_any_pressed(void);
extern bool bridge_input_was_released(uint8_t button);
extern bool bridge_input_was_any_released(void);
extern unsigned long bridge_input_get_held_time(void);
extern bool bridge_input_is_power_pressed(void);
extern int bridge_detect_device_type(void);
extern void bridge_configure_deep_sleep(void);
extern void bridge_enter_deep_sleep(void);

static device_type_t detected_device = DEVICE_X4;
static bool initialized = false;

bool hal_gpio_init(void) {
    if (initialized) return true;

    LOG_INF("GPIO", "Initializing SPI bus");
    bridge_spi_init();

    LOG_INF("GPIO", "Detecting device type");
    int dt = bridge_detect_device_type();
    detected_device = (dt == 1) ? DEVICE_X3 : DEVICE_X4;
    LOG_INF("GPIO", "Device: %s", detected_device == DEVICE_X3 ? "X3" : "X4");

    LOG_INF("GPIO", "Initializing input manager");
    bridge_input_init();

    initialized = true;
    return true;
}

void hal_gpio_poll(void) {
    bridge_input_update();
}

bool hal_gpio_is_pressed(uint8_t button) {
    return bridge_input_is_pressed(button);
}

bool hal_gpio_was_pressed(uint8_t button) {
    return bridge_input_was_pressed(button);
}

bool hal_gpio_was_any_pressed(void) {
    return bridge_input_was_any_pressed();
}

bool hal_gpio_was_released(uint8_t button) {
    return bridge_input_was_released(button);
}

bool hal_gpio_was_any_released(void) {
    return bridge_input_was_any_released();
}

unsigned long hal_gpio_get_held_time(void) {
    return bridge_input_get_held_time();
}

device_type_t hal_gpio_get_device_type(void) {
    return detected_device;
}

bool hal_gpio_is_x3(void) {
    return detected_device == DEVICE_X3;
}

bool hal_gpio_is_x4(void) {
    return detected_device == DEVICE_X4;
}

void hal_gpio_start_deep_sleep(void) {
    LOG_INF("GPIO", "Entering deep sleep");
    bridge_configure_deep_sleep();
    bridge_enter_deep_sleep();
}
