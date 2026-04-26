/**
 * @file main.cpp
 * @brief CrossLua Reader entry point — Arduino setup()/loop() shim.
 *        This is the ONLY non-bridge .cpp file. All logic lives in pure C
 *        HAL, renderer, font, and plugin manager modules.
 *
 * @status Phase 4 — plugin manager integration
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
#include "font_cache.h"
#include "plugin_manager.h"
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

    /* Step 8: Font cache */
    font_cache_init();

    /* Step 9: Discover plugins */
    LOG_INF("MAIN", "Free heap before plugins: %u bytes", hal_system_free_heap());

    if (plugin_manager_init()) {
        LOG_INF("MAIN", "Found %d plugin(s)", plugin_manager_count());
    } else {
        LOG_INF("MAIN", "No plugins found");
    }

    /* Step 10: Start plugin (restore last or first available) */
    if (!plugin_manager_start(NULL, NULL)) {
        LOG_ERR("MAIN", "No plugin to start — device idle");
        renderer_clear_screen(0xFF);
        renderer_fill_rect(20, 20, 200, 40, true);
        hal_display_refresh(REFRESH_FAST);
    }

    LOG_INF("MAIN", "Init complete. Free heap: %u bytes", hal_system_free_heap());
}

void loop() {
    hal_gpio_poll();
    plugin_manager_dispatch_loop();
    hal_power_check_sleep();
    vTaskDelay(1);
}
