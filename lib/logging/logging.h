/**
 * @file logging.h
 * @brief Logging macros for CrossLua Reader runtime.
 *        Provides LOG_ERR, LOG_INF, LOG_DBG gated by build flags.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdint.h>

/**
 * Print a formatted log message with level and origin tag.
 *
 * @param level  Log level string ("ERR", "INF", "DBG")
 * @param origin Module tag for filtering (e.g. "HAL", "RND")
 * @param format printf-style format string
 */
void cl_log_printf(const char *level, const char *origin, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#ifdef ENABLE_SERIAL_LOG
  #if LOG_LEVEL >= 0
    #define LOG_ERR(origin, fmt, ...) cl_log_printf("ERR", origin, fmt, ##__VA_ARGS__)
  #else
    #define LOG_ERR(origin, fmt, ...)
  #endif
  #if LOG_LEVEL >= 1
    #define LOG_INF(origin, fmt, ...) cl_log_printf("INF", origin, fmt, ##__VA_ARGS__)
  #else
    #define LOG_INF(origin, fmt, ...)
  #endif
  #if LOG_LEVEL >= 2
    #define LOG_DBG(origin, fmt, ...) cl_log_printf("DBG", origin, fmt, ##__VA_ARGS__)
  #else
    #define LOG_DBG(origin, fmt, ...)
  #endif
#else
  #define LOG_ERR(origin, fmt, ...)
  #define LOG_INF(origin, fmt, ...)
  #define LOG_DBG(origin, fmt, ...)
#endif
