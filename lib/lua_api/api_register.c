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

#include "logging.h"

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
    luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
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
    api_register_all(L);

    LOG_INF("LUA", "Lua state created with CrossLua API");
    return L;
}
