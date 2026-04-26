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
#include "hal_storage.h"
#include "sleep_screen.h"
#include "plugin_manager.h"
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
    int ms = (int)lua_tointeger(L, 1);
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

/* system.setSleepTimeout(minutes) — set auto-sleep timeout. 0 = disable. */
static int l_system_set_sleep_timeout(lua_State *L) {
    int minutes = (int)lua_tonumber(L, 1);
    if (minutes < 0) minutes = 0;
    if (minutes > 60) minutes = 60;
    hal_power_set_sleep_timeout((uint32_t)minutes);
    return 0;
}

/* system.suppressSleep(bool) — suppress/restore auto-sleep */
static int l_system_suppress_sleep(lua_State *L) {
    bool suppress = lua_toboolean(L, 1);
    hal_power_suppress_sleep(suppress);
    return 0;
}

/* system.setSleepMode(mode) — 0=blank, 1=single, 2=cycle, 3=random, 4=clear */
static int l_system_set_sleep_mode(lua_State *L) {
    int mode = (int)lua_tointeger(L, 1);
    if (mode < 0 || mode > 4) mode = 0;
    sleep_screen_set_mode((sleep_mode_t)mode);
    return 0;
}

/* system.setSleepWallpaper(filename) — set wallpaper for single mode */
static int l_system_set_sleep_wallpaper(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    sleep_screen_set_wallpaper(name);
    return 0;
}

/* system.setSleepHook(func) — register a Lua callback for custom sleep screen content.
 * The callback is called after the base sleep screen renders but before display refresh.
 * It can use display.drawText, display.fillRect, etc. to draw on the sleep screen.
 * Pass nil to clear the hook. */
static int l_system_set_sleep_hook(lua_State *L) {
    if (lua_isnil(L, 1) || lua_isnone(L, 1)) {
        sleep_screen_clear_hook();
    } else {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);  /* push copy of function */
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        sleep_screen_set_hook(L, ref);
    }
    return 0;
}

/* system.reload() — reinit SD card and restart plugins from home */
static int l_system_reload(lua_State *L) {
    (void)L;
    hal_storage_reinit();
    plugin_manager_reinit();
    plugin_manager_start("home", NULL);
    return 0;
}

void api_system_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"freeHeap",            l_system_free_heap},
        {"totalHeap",           l_system_total_heap},
        {"batteryPercent",      l_system_battery},
        {"millis",              l_system_millis},
        {"delay",               l_system_delay},
        {"log",                 l_system_log},
        {"version",             l_system_version},
        {"restart",             l_system_restart},
        {"sleep",               l_system_sleep},
        {"setSleepTimeout",     l_system_set_sleep_timeout},
        {"suppressSleep",       l_system_suppress_sleep},
        {"setSleepMode",        l_system_set_sleep_mode},
        {"setSleepWallpaper",   l_system_set_sleep_wallpaper},
        {"setSleepHook",        l_system_set_sleep_hook},
        {"reload",              l_system_reload},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "system");
}
