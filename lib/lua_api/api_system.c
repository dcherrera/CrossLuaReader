/**
 * @file api_system.c
 * @brief Lua system.* module: device info, timing, logging.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_system.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_system.h"
#include "hal_power.h"
#include "logging.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* system.freeHeap() → int */
static int l_system_free_heap(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)hal_system_free_heap());
    return 1;
}

/* system.totalHeap() → int */
static int l_system_total_heap(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)hal_system_total_heap());
    return 1;
}

/* system.batteryPercent() → int (0-100) */
static int l_system_battery(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)hal_power_battery_percent());
    return 1;
}

/* system.millis() → int */
static int l_system_millis(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)hal_system_uptime_ms());
    return 1;
}

/* system.delay(ms) — yield to FreeRTOS for N milliseconds */
static int l_system_delay(lua_State *L) {
    int ms = (int)luaL_checkinteger(L, 1);
    if (ms > 0) {
        vTaskDelay(ms / portTICK_PERIOD_MS);
    }
    return 0;
}

/* system.log(message) — log to serial output */
static int l_system_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    LOG_INF("LUA", "%s", msg);
    return 0;
}

/* system.version() → string */
static int l_system_version(lua_State *L) {
    lua_pushstring(L, hal_system_version());
    return 1;
}

/* system.restart() — reboot device */
static int l_system_restart(lua_State *L) {
    (void)L;
    hal_system_restart();
    return 0;  /* never reached */
}

/* system.sleep() — enter deep sleep */
static int l_system_sleep(lua_State *L) {
    (void)L;
    hal_power_enter_sleep();
    return 0;  /* never reached */
}

void api_system_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"freeHeap",       l_system_free_heap},
        {"totalHeap",      l_system_total_heap},
        {"batteryPercent", l_system_battery},
        {"millis",         l_system_millis},
        {"delay",          l_system_delay},
        {"log",            l_system_log},
        {"version",        l_system_version},
        {"restart",        l_system_restart},
        {"sleep",          l_system_sleep},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "system");
}
