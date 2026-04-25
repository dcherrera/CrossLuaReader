/**
 * @file logging.c
 * @brief Logging implementation using ESP-IDF's esp_log facility.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

void cl_log_printf(const char *level, const char *origin, const char *format, ...) {
    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);

    printf("[%u] [%s] [%s] ", uptime_ms, level, origin);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("\n");
}
