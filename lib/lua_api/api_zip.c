/**
 * @file api_zip.c
 * @brief Lua zip.* module bindings. Wraps lib/zip with a userdata-backed
 *        archive handle so unclosed archives are reclaimed by the Lua GC.
 *
 *        Surface (all methods on the userdata returned by zip.open):
 *          h:close()                    explicit close (idempotent)
 *          h:list()                     → array of names
 *          h:has(name)                  → bool
 *          h:size(name)                 → int  (uncompressed)
 *          h:read(name)                 → string | nil, errmsg
 *          h:drm_state()                → "none" | "obfuscation" | "drm"
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo None
 */

#include "api_zip.h"
#include "lauxlib.h"

#include "zip.h"
#include "logging.h"

#include <string.h>

#define ZIP_HANDLE_MT  "crosslua.zip.handle"

typedef struct {
    zip_handle_t *zh;  /* NULL after close; userdata kept until GC */
} zip_ud_t;

/* ── Helpers ────────────────────────────────────────────────────── */

static zip_ud_t *check_zip_ud(lua_State *L, int idx) {
    return (zip_ud_t *)luaL_checkudata(L, idx, ZIP_HANDLE_MT);
}

static zip_handle_t *check_zip_open(lua_State *L, int idx) {
    zip_ud_t *u = check_zip_ud(L, idx);
    if (!u->zh) {
        luaL_error(L, "zip handle is closed");
    }
    return u->zh;
}

/* ── Module functions ───────────────────────────────────────────── */

/* zip.open(path, validate_epub_mimetype = false) → handle | nil, errmsg */
static int l_zip_open(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    bool validate = lua_toboolean(L, 2);

    zip_handle_t *zh = zip_open(path, validate);
    if (!zh) {
        lua_pushnil(L);
        lua_pushstring(L, "zip_open failed");
        return 2;
    }

    zip_ud_t *u = (zip_ud_t *)lua_newuserdata(L, sizeof(zip_ud_t));
    u->zh = zh;
    luaL_setmetatable(L, ZIP_HANDLE_MT);
    return 1;
}

/* h:close() — idempotent */
static int l_zip_close(lua_State *L) {
    zip_ud_t *u = check_zip_ud(L, 1);
    if (u->zh) {
        zip_close(u->zh);
        u->zh = NULL;
    }
    return 0;
}

/* __gc: ensure native handle is freed if user forgets to close */
static int l_zip_gc(lua_State *L) {
    zip_ud_t *u = (zip_ud_t *)lua_touserdata(L, 1);
    if (u && u->zh) {
        zip_close(u->zh);
        u->zh = NULL;
    }
    return 0;
}

/* h:list() → array of names */
static int l_zip_list(lua_State *L) {
    zip_handle_t *zh = check_zip_open(L, 1);
    uint16_t n = zip_entry_count(zh);
    lua_createtable(L, n, 0);
    for (uint16_t i = 0; i < n; i++) {
        lua_pushstring(L, zip_entry_name(zh, i));
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/* h:has(name) → bool */
static int l_zip_has(lua_State *L) {
    zip_handle_t *zh = check_zip_open(L, 1);
    const char *name = luaL_checkstring(L, 2);
    lua_pushboolean(L, zip_has(zh, name));
    return 1;
}

/* h:size(name) → int */
static int l_zip_size(lua_State *L) {
    zip_handle_t *zh = check_zip_open(L, 1);
    const char *name = luaL_checkstring(L, 2);
    lua_pushinteger(L, (lua_Integer)zip_entry_size(zh, name));
    return 1;
}

/* h:read(name) → string | nil, errmsg */
static int l_zip_read(lua_State *L) {
    zip_handle_t *zh = check_zip_open(L, 1);
    const char *name = luaL_checkstring(L, 2);

    uint32_t sz = zip_entry_size(zh, name);
    if (sz == 0 && !zip_has(zh, name)) {
        lua_pushnil(L);
        lua_pushfstring(L, "no such entry: %s", name);
        return 2;
    }

    /* Cap read size to keep Lua-side strings sane. Plugins should use the
     * (future) chunked API for chapters and images. */
    if (sz > 256 * 1024) {
        lua_pushnil(L);
        lua_pushfstring(L, "entry too large for zip:read (%u bytes)", sz);
        return 2;
    }

    char *buf = (char *)lua_newuserdata(L, sz > 0 ? sz : 1);  /* GC-tracked scratch */
    int n = zip_read(zh, name, buf, sz);
    if (n < 0) {
        lua_pop(L, 1);  /* drop the scratch */
        lua_pushnil(L);
        lua_pushstring(L, "zip_read error");
        return 2;
    }
    lua_pushlstring(L, buf, (size_t)n);
    lua_remove(L, -2);  /* drop the scratch, leaving only the string */
    return 1;
}

/* h:drm_state() → "none" | "obfuscation" | "drm" */
static int l_zip_drm_state(lua_State *L) {
    zip_handle_t *zh = check_zip_open(L, 1);
    zip_drm_t s = zip_drm_state(zh);
    switch (s) {
        case ZIP_DRM_NONE:        lua_pushstring(L, "none"); break;
        case ZIP_DRM_OBFUSCATION: lua_pushstring(L, "obfuscation"); break;
        case ZIP_DRM_PROTECTED:
        default:                  lua_pushstring(L, "drm"); break;
    }
    return 1;
}

/* ── Registration ───────────────────────────────────────────────── */

static const luaL_Reg zip_handle_methods[] = {
    {"close",     l_zip_close},
    {"list",      l_zip_list},
    {"has",       l_zip_has},
    {"size",      l_zip_size},
    {"read",      l_zip_read},
    {"drm_state", l_zip_drm_state},
    {NULL, NULL},
};

static const luaL_Reg zip_module[] = {
    {"open", l_zip_open},
    {NULL, NULL},
};

void api_zip_register(lua_State *L) {
    /* Userdata metatable — once per state. */
    if (luaL_newmetatable(L, ZIP_HANDLE_MT)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");  /* mt.__index = mt */
        luaL_setfuncs(L, zip_handle_methods, 0);
        lua_pushcfunction(L, l_zip_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    luaL_newlib(L, zip_module);
    lua_setglobal(L, "zip");
}
