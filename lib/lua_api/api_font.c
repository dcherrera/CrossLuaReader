/**
 * @file api_font.c
 * @brief Lua font.* module: load and unload .cfont files from SD.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_font.h"
#include "lua.h"
#include "lauxlib.h"

#include "font_manager.h"

/* font.load(path) → fontId or nil, errmsg */
static int l_font_load(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int id = font_manager_load(path);
    if (id < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to load font");
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

/* font.unload(fontId) */
static int l_font_unload(lua_State *L) {
    int id = (int)lua_tointeger(L, 1);
    font_manager_unload(id);
    return 0;
}

void api_font_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"load",   l_font_load},
        {"unload", l_font_unload},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "font");
}
