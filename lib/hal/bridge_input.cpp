/**
 * @file bridge_input.cpp
 * @brief C++ bridge wrapping InputManager SDK class and Arduino SPI/Wire
 *        for C HAL access. Also handles X3/X4 device type detection.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include <Arduino.h>
#include <InputManager.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include "esp_sleep.h"

extern "C" {

/* SPI pins (shared between display and SD card) */
#define SPI_SCLK 8
#define SPI_MISO 7
#define SPI_MOSI 10
#define SPI_CS   21

/* I2C for X3 detection */
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

/* X3 I2C device addresses */
#define BQ27220_ADDR 0x55
#define DS3231_ADDR  0x68

/* Power button */
#define POWER_BUTTON_PIN 3

static InputManager input_mgr;
static int cached_device_type = -1;  /* -1 = not yet detected */

void bridge_spi_init(void) {
    SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, SPI_CS);
}

void bridge_input_init(void) {
    input_mgr.begin();
}

void bridge_input_update(void) {
    input_mgr.update();
}

bool bridge_input_is_pressed(uint8_t button) {
    return input_mgr.isPressed(button);
}

bool bridge_input_was_pressed(uint8_t button) {
    return input_mgr.wasPressed(button);
}

bool bridge_input_was_any_pressed(void) {
    return input_mgr.wasAnyPressed();
}

bool bridge_input_was_released(uint8_t button) {
    return input_mgr.wasReleased(button);
}

bool bridge_input_was_any_released(void) {
    return input_mgr.wasAnyReleased();
}

unsigned long bridge_input_get_held_time(void) {
    return input_mgr.getHeldTime();
}

bool bridge_input_is_power_pressed(void) {
    return input_mgr.isPowerButtonPressed();
}

/**
 * Detect device type by probing I2C for X3-specific chips.
 * Caches result in NVS for fast subsequent boots.
 *
 * @return 0 = X4, 1 = X3
 */
int bridge_detect_device_type(void) {
    if (cached_device_type >= 0) {
        return cached_device_type;
    }

    /* Check NVS cache first */
    Preferences prefs;
    prefs.begin("crosslua", true);
    int stored = prefs.getInt("device_type", -1);
    prefs.end();

    if (stored >= 0) {
        cached_device_type = stored;
        return cached_device_type;
    }

    /* Probe I2C for X3 fuel gauge (BQ27220 at 0x55) */
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    Wire.beginTransmission(BQ27220_ADDR);
    bool has_fuel_gauge = (Wire.endTransmission() == 0);

    cached_device_type = has_fuel_gauge ? 1 : 0;

    /* Store in NVS for next boot */
    prefs.begin("crosslua", false);
    prefs.putInt("device_type", cached_device_type);
    prefs.end();

    Wire.end();
    return cached_device_type;
}

/**
 * Configure power button GPIO for deep sleep wakeup.
 */
void bridge_configure_deep_sleep(void) {
    /* ESP32-C3 uses GPIO wakeup, not ext0 */
    gpio_num_t pin = (gpio_num_t)POWER_BUTTON_PIN;
    esp_deep_sleep_enable_gpio_wakeup(1ULL << pin, ESP_GPIO_WAKEUP_GPIO_LOW);
}

/**
 * Enter ESP32 deep sleep.
 */
void bridge_enter_deep_sleep(void) {
    esp_deep_sleep_start();
}

}  /* extern "C" */
