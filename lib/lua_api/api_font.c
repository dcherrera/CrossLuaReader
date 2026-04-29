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
#include "boot_font.h"

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

/* font.setFallback(primaryId, fallbackId) → bool */
static int l_font_set_fallback(lua_State *L) {
    int primary = (int)lua_tointeger(L, 1);
    int fallback = (int)lua_tointeger(L, 2);
    lua_pushboolean(L, font_manager_set_fallback(primary, fallback));
    return 1;
}

/* font.clearFallback(fontId) */
static int l_font_clear_fallback(lua_State *L) {
    int id = (int)lua_tointeger(L, 1);
    font_manager_clear_fallback(id);
    return 0;
}

/* font.boot() → fontId or -1 — firmware-resident boot font, available without SD */
static int l_font_boot(lua_State *L) {
    lua_pushinteger(L, boot_font_get_id());
    return 1;
}

void api_font_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"load",          l_font_load},
        {"unload",        l_font_unload},
        {"setFallback",   l_font_set_fallback},
        {"clearFallback", l_font_clear_fallback},
        {"boot",          l_font_boot},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "font");
}
