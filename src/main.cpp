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
#include "font_manager.h"
#include "font_render.h"
#include "font_cache.h"
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

    LOG_INF("MAIN", "Free heap before font load: %u bytes", hal_system_free_heap());

    /* Step 9: Load a font from SD (if available) */
    int font_id = font_manager_load("/fonts/NotoSans-14-Regular.cfont");

    LOG_INF("MAIN", "Free heap after font load: %u bytes", hal_system_free_heap());

    /* Render test */
    renderer_clear_screen(0xFF);

    if (font_id >= 0) {
        const font_data_t *font = font_manager_get(font_id);
        const char *font_path = font_manager_get_path(font_id);

        font_render_draw_text(font, font_path, 20, 20, "Hello, CrossLua Reader!", true);
        font_render_draw_text(font, font_path, 20, 60, "Phase 2: Font System", true);

        /* Geometric test shapes */
        renderer_draw_line(20, 100, 440, 100, true);
        renderer_draw_rect(20, 110, 200, 50, true);
    } else {
        /* No font available — fallback to geometric test */
        LOG_INF("MAIN", "No font on SD — drawing test pattern only");
        renderer_fill_rect(20, 20, 200, 80, true);
        renderer_draw_rect(20, 120, 200, 80, true);
        renderer_draw_line(20, 220, 440, 220, true);
    }

    hal_display_refresh(REFRESH_FAST);

    LOG_INF("MAIN", "Init complete");
    LOG_INF("MAIN", "Free heap: %u bytes", hal_system_free_heap());
    LOG_INF("MAIN", "Screen: %dx%d",
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
