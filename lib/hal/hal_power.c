/**
 * @file hal_power.c
 * @brief Power management implementation. Battery caching, auto-sleep.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "hal_power.h"
#include "hal_gpio.h"
#include "hal_display.h"
#include "sleep_screen.h"
#include "logging.h"

#include "esp_timer.h"
#include "hal/usb_serial_jtag_ll.h"

/* Bridge function declarations */
extern void bridge_battery_init(uint8_t adc_pin, float divider);
extern uint16_t bridge_battery_read_percentage(void);
extern uint16_t bridge_battery_read_millivolts(void);

/* X4 battery ADC config */
#define BAT_ADC_PIN      0
#define BAT_DIVIDER      2.0f

/* Battery cache */
#define BATTERY_CACHE_MS 30000
static uint16_t cached_percent = 0;
static uint32_t last_battery_read_ms = 0;

/* Auto-sleep */
static uint32_t sleep_timeout_ms = 600000;  /* 10 minutes default, configurable */
static uint32_t last_activity_ms = 0;
static bool sleep_suppressed = false;

bool hal_power_init(void) {
    LOG_INF("PWR", "Initializing power management");

    if (hal_gpio_is_x4()) {
        bridge_battery_init(BAT_ADC_PIN, BAT_DIVIDER);
        cached_percent = bridge_battery_read_percentage();
        LOG_INF("PWR", "Battery: %d%%", cached_percent);
    } else {
        LOG_INF("PWR", "X3 fuel gauge (I2C) — not yet implemented");
    }

    last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
    last_battery_read_ms = last_activity_ms;
    return true;
}

uint16_t hal_power_battery_percent(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (now - last_battery_read_ms > BATTERY_CACHE_MS) {
        if (hal_gpio_is_x4()) {
            cached_percent = bridge_battery_read_percentage();
        }
        last_battery_read_ms = now;
    }

    return cached_percent;
}

uint16_t hal_power_battery_millivolts(void) {
    if (hal_gpio_is_x4()) {
        return bridge_battery_read_millivolts();
    }
    return 0;
}

void hal_power_set_sleep_timeout(uint32_t minutes) {
    if (minutes == 0) {
        sleep_timeout_ms = 0;  /* disable auto-sleep */
        LOG_INF("PWR", "Auto-sleep disabled");
    } else {
        sleep_timeout_ms = minutes * 60 * 1000;
        LOG_INF("PWR", "Sleep timeout: %u min", minutes);
    }
}

void hal_power_suppress_sleep(bool suppress) {
    sleep_suppressed = suppress;
    LOG_INF("PWR", "Sleep %s", suppress ? "suppressed" : "restored");
}

void hal_power_check_sleep(void) {
    if (sleep_suppressed || sleep_timeout_ms == 0) return;

    if (hal_gpio_was_any_pressed() || hal_gpio_was_any_released()) {
        last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        return;
    }

    /* Suppress sleep while USB is connected */
    if (hal_power_is_usb_connected()) {
        last_activity_ms = (uint32_t)(esp_timer_get_time() / 1000);
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_activity_ms > sleep_timeout_ms) {
        LOG_INF("PWR", "Auto-sleep after %u ms idle", sleep_timeout_ms);
        hal_power_enter_sleep();
    }
}

bool hal_power_is_usb_connected(void) {
    return usb_serial_jtag_ll_txfifo_writable();
}

void hal_power_enter_sleep(void) {
    LOG_INF("PWR", "Entering deep sleep");
    sleep_screen_render();
    hal_display_deep_sleep();
    hal_gpio_start_deep_sleep();
}
