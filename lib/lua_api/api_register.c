/**
 * @file api_register.c
 * @brief Lua state creation and API module registration.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_register.h"
#include "api_display.h"
#include "api_input.h"
#include "api_storage.h"
#include "api_system.h"
#include "api_font.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "hal_storage.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

/**
 * Open safe standard Lua libraries.
 * Excludes io and os (we provide our own via HAL).
 * Excludes debug (unnecessary for plugins, saves memory).
 */
static void open_safe_libs(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1);       lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1);  lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);     lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);       lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);       lua_pop(L, 1);
    /* coroutine lib excluded — saves ~2-3KB per Lua state, unused by plugins */
}

/**
 * Custom package searcher that loads Lua modules from SD card via HAL.
 * Handles require("lib.theme") → loads "/plugins/lib/theme.lua"
 */
static int sd_searcher(lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);

    /* Convert module name dots to path separators: "lib.theme" → "/plugins/lib/theme.lua" */
    char modpath[96];
    strncpy(modpath, modname, sizeof(modpath) - 1);
    modpath[sizeof(modpath) - 1] = '\0';
    for (char *p = modpath; *p; p++) {
        if (*p == '.') *p = '/';
    }

    char path[128];
    snprintf(path, sizeof(path), "/plugins/%s.lua", modpath);

    /* Try to load via HAL storage */
    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        lua_pushfstring(L, "\n\tno file '%s' on SD", path);
        return 1;
    }

    size_t size = hal_storage_file_size(f);
    char *buf = (char *)malloc(size);
    if (!buf) {
        hal_storage_file_close(f);
        lua_pushstring(L, "\n\tout of memory");
        return 1;
    }

    int read = hal_storage_file_read(f, buf, size);
    hal_storage_file_close(f);

    if (read != (int)size) {
        free(buf);
        lua_pushfstring(L, "\n\tread error on '%s'", path);
        return 1;
    }

    int err = luaL_loadbuffer(L, buf, size, path);
    free(buf);

    if (err != 0) {
        return lua_error(L);
    }

    return 1;  /* return the loaded chunk */
}

/**
 * Install our SD card searcher into package.searchers.
 */
static void install_sd_searcher(lua_State *L) {
    luaL_requiref(L, "package", luaopen_package, 1);
    lua_getfield(L, -1, "searchers");

    /* Add our searcher at position 2 (after preload searcher) */
    int len = (int)lua_rawlen(L, -1);
    lua_pushcfunction(L, sd_searcher);
    lua_rawseti(L, -2, len + 1);

    lua_pop(L, 2);  /* pop searchers table and package table */
}

void api_register_all(lua_State *L) {
    api_display_register(L);
    api_input_register(L);
    api_storage_register(L);
    api_system_register(L);
    api_font_register(L);
}

lua_State *api_create_state(void) {
    lua_State *L = luaL_newstate();
    if (!L) {
        LOG_ERR("LUA", "Failed to create Lua state");
        return NULL;
    }

    open_safe_libs(L);
    install_sd_searcher(L);
    api_register_all(L);

    LOG_INF("LUA", "Lua state created with CrossLua API");
    return L;
}
