/**
 * @file main.cpp
 * @brief CrossLua Reader entry point — Arduino setup()/loop() shim.
 *        This is the ONLY non-bridge .cpp file. All logic lives in pure C
 *        HAL, renderer, font, and plugin manager modules.
 *
 * @status Phase 8 — sleep screen, crash recovery, SD reload
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
#include "sleep_screen.h"
#include "renderer.h"
#include "font_cache.h"
#include "font_manager.h"
#include "font_render.h"
#include "boot_font.h"
#include "api_input.h"
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

    /* Step 3.5: Start background input polling task */
    api_input_start_task();

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

    /* Step 8.5: Boot font — loaded before Lua for crash/sleep screens */
    {
        int bf = font_manager_load("/fonts/Ubuntu/Ubuntu-12-Regular.cfont");
        if (bf >= 0) {
            boot_font_set_id(bf);
            LOG_INF("MAIN", "Boot font loaded: slot %d", bf);
        } else {
            LOG_ERR("MAIN", "Boot font load failed — crash screens will be textless");
        }
    }

    /* Step 9: Discover plugins */
    LOG_INF("MAIN", "Free heap before plugins: %u bytes", hal_system_free_heap());

    if (plugin_manager_init()) {
        LOG_INF("MAIN", "Found %d plugin(s)", plugin_manager_count());
    } else {
        LOG_INF("MAIN", "No plugins found");
    }

    /* Step 10: Start plugin */
    if (!plugin_manager_start("home", NULL)) {
        LOG_ERR("MAIN", "No plugin to start — device idle");
        renderer_clear_screen(0xFF);
        renderer_fill_rect(20, 20, 200, 40, true);
        hal_display_refresh(REFRESH_FAST);
    }

    LOG_INF("MAIN", "Init complete. Free heap: %u bytes", hal_system_free_heap());
}

#define POWER_RELOAD_MS  2000   /* >2s = SD reload */
#define POWER_SLEEP_MS    500   /* >0.5s = manual sleep */

static bool power_action_taken = false;

void loop() {
    /* Button polling handled by background input task */

    /* Power button handling: long-press = reload, short-press = sleep */
    if (hal_gpio_is_pressed(BTN_POWER)) {
        unsigned long held = hal_gpio_get_held_time();
        if (held >= POWER_RELOAD_MS && !power_action_taken) {
            power_action_taken = true;
            LOG_INF("MAIN", "Power long-press: SD reload");

            /* Show reload feedback */
            int fid = boot_font_get_id();
            renderer_set_orientation(ORIENT_PORTRAIT);
            renderer_clear_screen(0xFF);
            if (fid >= 0) {
                font_render_draw_text_fb(fid, 30, 100, "Reloading SD card...", true);
            }
            hal_display_refresh(REFRESH_FAST);

            /* Stop current plugin (closes Lua state + files) */
            plugin_manager_stop();

            /* Reinit SD card */
            vTaskDelay(50 / portTICK_PERIOD_MS);
            hal_storage_reinit();
            vTaskDelay(50 / portTICK_PERIOD_MS);

            /* Re-discover plugins and start home */
            plugin_manager_reinit();
            plugin_manager_start("home", NULL);
        }
    } else if (hal_gpio_was_released(BTN_POWER)) {
        if (!power_action_taken) {
            unsigned long held = hal_gpio_get_held_time();
            if (held >= POWER_SLEEP_MS) {
                LOG_INF("MAIN", "Power short-press: sleep");
                hal_power_enter_sleep();
            }
        }
        power_action_taken = false;
    }

    plugin_manager_dispatch_loop();
    hal_power_check_sleep();
    vTaskDelay(1);
}
