/**
 * @file hal_system.c
 * @brief System API implementation using ESP-IDF functions.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "hal_system.h"
#include "logging.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#ifndef CROSSLUA_READER_VERSION
#define CROSSLUA_READER_VERSION "0.0.0-dev"
#endif

bool hal_system_init(void) {
    LOG_INF("SYS", "System init");
    return true;
}

void hal_system_restart(void) {
    LOG_INF("SYS", "Restarting...");
    esp_restart();
}

uint32_t hal_system_free_heap(void) {
    return esp_get_free_heap_size();
}

uint32_t hal_system_total_heap(void) {
    return heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
}

uint32_t hal_system_min_free_heap(void) {
    return esp_get_minimum_free_heap_size();
}

uint32_t hal_system_uptime_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

const char *hal_system_version(void) {
    return CROSSLUA_READER_VERSION;
}
