/**
 * @file main.cpp
 * @brief CrossLua Reader entry point — Arduino setup()/loop() shim.
 *        This is the ONLY non-bridge .cpp file. All logic lives in pure C
 *        HAL and renderer modules called via extern "C".
 *
 * @status Phase 1 — hardware verification with test pattern
 * @issues None
 * @todo None
 */

#include <Arduino.h>

extern "C" {
#include "hal_system.h"
#include "hal_gpio.h"
#include "hal_display.h"
#include "hal_storage.h"
#include "hal_power.h"
#include "renderer.h"
#include "logging.h"
}

void setup() {
    /* Step 1: System init (watchdog, basic config) */
    hal_system_init();

    /* Step 2: Serial for logging */
    #ifdef ENABLE_SERIAL_LOG
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
        delay(10);
    }
    #endif

    LOG_INF("MAIN", "CrossLua Reader v%s", hal_system_version());

    /* Step 3: GPIO — SPI bus + buttons + device detect (must be before display/storage) */
    if (!hal_gpio_init()) {
        LOG_ERR("MAIN", "GPIO init failed");
        return;
    }
    LOG_INF("MAIN", "Device: %s", hal_gpio_is_x3() ? "X3" : "X4");

    /* Step 4: Display */
    if (!hal_display_init()) {
        LOG_ERR("MAIN", "Display init failed");
        return;
    }

    /* Step 5: SD card */
    if (!hal_storage_init()) {
        LOG_ERR("MAIN", "SD card init failed — continuing without storage");
    }

    /* Step 6: Power management + battery */
    hal_power_init();

    /* Step 7: Renderer */
    if (!renderer_init()) {
        LOG_ERR("MAIN", "Renderer init failed");
        return;
    }

    /* Phase 1 verification: draw test pattern */
    renderer_clear_screen(0xFF);

    /* Filled black rectangle */
    renderer_fill_rect(20, 20, 200, 80, true);

    /* Outlined rectangle */
    renderer_draw_rect(20, 120, 200, 80, true);

    /* Horizontal line */
    renderer_draw_line(20, 220, 440, 220, true);

    /* Vertical line */
    renderer_draw_line(240, 20, 240, 300, true);

    /* Diagonal line */
    renderer_draw_line(260, 20, 440, 200, true);

    /* Push to display */
    hal_display_refresh(REFRESH_FAST);

    LOG_INF("MAIN", "Phase 1 init complete");
    LOG_INF("MAIN", "Free heap: %u bytes", hal_system_free_heap());
    LOG_INF("MAIN", "Min free heap: %u bytes", hal_system_min_free_heap());
    LOG_INF("MAIN", "Screen: %dx%d (orient: portrait)",
            renderer_screen_width(), renderer_screen_height());
}

void loop() {
    hal_gpio_poll();

    /* Log button presses for verification */
    for (int i = 0; i < BTN_COUNT; i++) {
        if (hal_gpio_was_pressed(i)) {
            LOG_INF("MAIN", "Button pressed: %d (held %lu ms)",
                    i, hal_gpio_get_held_time());
        }
    }

    hal_power_check_sleep();
    vTaskDelay(1);
}
