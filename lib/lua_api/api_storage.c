/**
 * @file api_storage.c
 * @brief Lua storage.* module: SD card file and directory operations.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_storage.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_storage.h"

#include <stdlib.h>
#include <string.h>

/* storage.read(path) → string or nil, errmsg */
static int l_storage_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to open file");
        return 2;
    }

    size_t size = hal_storage_file_size(f);
    if (size > 256 * 1024) {  /* 256KB safety limit for Lua strings */
        hal_storage_file_close(f);
        lua_pushnil(L);
        lua_pushstring(L, "file too large");
        return 2;
    }

    char *buf = (char *)malloc(size);
    if (!buf) {
        hal_storage_file_close(f);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    int read = hal_storage_file_read(f, buf, size);
    hal_storage_file_close(f);

    if (read < 0) {
        free(buf);
        lua_pushnil(L);
        lua_pushstring(L, "read error");
        return 2;
    }

    lua_pushlstring(L, buf, (size_t)read);
    free(buf);
    return 1;
}

/* storage.readBytes(path, offset, length) → string or nil, errmsg */
static int l_storage_read_bytes(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t offset = (size_t)lua_tointeger(L, 2);
    size_t length = (size_t)lua_tointeger(L, 3);

    if (length > 64 * 1024) {
        lua_pushnil(L);
        lua_pushstring(L, "length too large (max 64KB)");
        return 2;
    }

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to open file");
        return 2;
    }

    hal_storage_file_seek(f, offset);
    char *buf = (char *)malloc(length);
    if (!buf) {
        hal_storage_file_close(f);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    int read = hal_storage_file_read(f, buf, length);
    hal_storage_file_close(f);

    if (read < 0) {
        free(buf);
        lua_pushnil(L);
        lua_pushstring(L, "read error");
        return 2;
    }

    lua_pushlstring(L, buf, (size_t)read);
    free(buf);
    return 1;
}

/* storage.write(path, content) → bool */
static int l_storage_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t len;
    const char *content = luaL_checklstring(L, 2, &len);

    hal_file_t f = hal_storage_open(path, HAL_FILE_WRITE);
    if (!f) {
        lua_pushboolean(L, 0);
        return 1;
    }

    size_t written = hal_storage_file_write(f, content, len);
    hal_storage_file_close(f);

    lua_pushboolean(L, written == len);
    return 1;
}

/* storage.exists(path) → bool */
static int l_storage_exists(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, hal_storage_exists(path));
    return 1;
}

/* storage.mkdir(path) → bool */
static int l_storage_mkdir(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, hal_storage_mkdir(path));
    return 1;
}

/* storage.remove(path) → bool */
static int l_storage_remove(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, hal_storage_remove(path));
    return 1;
}

/* storage.fileSize(path) → int or nil */
static int l_storage_file_size(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        lua_pushnil(L);
        return 1;
    }
    size_t size = hal_storage_file_size(f);
    hal_storage_file_close(f);
    lua_pushinteger(L, (lua_Integer)size);
    return 1;
}

/* storage.list(path) → table of {name=string, isDir=bool} or nil */
static int l_storage_list(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);

    hal_dir_t dir = hal_storage_dir_open(path);
    if (!dir) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    int idx = 1;
    char name_buf[128];
    bool is_dir;

    while (hal_storage_dir_next(dir, name_buf, sizeof(name_buf), &is_dir)) {
        lua_newtable(L);
        lua_pushstring(L, name_buf);
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, is_dir);
        lua_setfield(L, -2, "isDir");
        lua_rawseti(L, -2, idx++);
    }

    hal_storage_dir_close(dir);
    return 1;
}

void api_storage_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"read",      l_storage_read},
        {"readBytes", l_storage_read_bytes},
        {"write",     l_storage_write},
        {"exists",    l_storage_exists},
        {"mkdir",     l_storage_mkdir},
        {"remove",    l_storage_remove},
        {"fileSize",  l_storage_file_size},
        {"list",      l_storage_list},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "storage");
}
