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
#include "api_text.h"
#include "api_layout.h"
#include "api_zip.h"
#include "api_xml.h"
#include "api_epub.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "hal_storage.h"
#include "hal_system.h"
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
/**
 * Lua bytecode writer callback for lua_dump().
 * Writes compiled bytecode to an SD file.
 */
static int bytecode_writer(lua_State *L, const void *p, size_t sz, void *ud) {
    (void)L;
    hal_file_t f = (hal_file_t)ud;
    return (hal_storage_file_write(f, p, sz) == (int)sz) ? 0 : 1;
}

/**
 * Try to load a cached .luac bytecode file.
 * Returns true if cache was valid and loaded onto the Lua stack.
 * Cache is invalid if .lua is newer (different size).
 */
static bool load_cached_bytecode(lua_State *L, const char *luac_path,
                                  const char *lua_path, size_t lua_size) {
    hal_file_t f = hal_storage_open(luac_path, HAL_FILE_READ);
    if (!f) return false;

    size_t cache_size = hal_storage_file_size(f);

    /* Read 4-byte header: original .lua file size for invalidation */
    uint32_t stored_lua_size = 0;
    if (hal_storage_file_read(f, &stored_lua_size, 4) != 4 ||
        stored_lua_size != (uint32_t)lua_size) {
        hal_storage_file_close(f);
        return false;  /* cache stale */
    }

    size_t bc_size = cache_size - 4;
    char *buf = (char *)malloc(bc_size);
    if (!buf) {
        hal_storage_file_close(f);
        return false;
    }

    int read = hal_storage_file_read(f, buf, bc_size);
    hal_storage_file_close(f);

    if (read != (int)bc_size) {
        free(buf);
        return false;
    }

    int err = luaL_loadbuffer(L, buf, bc_size, lua_path);
    free(buf);

    if (err != 0) {
        lua_pop(L, 1);  /* pop error message */
        return false;
    }

    return true;
}

/**
 * Save compiled bytecode to .luac cache file.
 * Prepends 4-byte .lua file size for cache invalidation.
 */
static void save_bytecode_cache(lua_State *L, const char *luac_path,
                                 size_t lua_size) {
    hal_file_t f = hal_storage_open(luac_path, HAL_FILE_WRITE);
    if (!f) return;

    /* Write .lua file size as invalidation key */
    uint32_t size32 = (uint32_t)lua_size;
    hal_storage_file_write(f, &size32, 4);

    /* Dump the compiled chunk (top of Lua stack) */
    lua_pushvalue(L, -1);  /* copy the chunk */
    lua_dump(L, bytecode_writer, f, 0);
    lua_pop(L, 1);  /* pop the copy */

    hal_storage_file_close(f);
}

static int sd_searcher(lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);

    /* Convert module name dots to path separators */
    char modpath[96];
    strncpy(modpath, modname, sizeof(modpath) - 1);
    modpath[sizeof(modpath) - 1] = '\0';
    for (char *p = modpath; *p; p++) {
        if (*p == '.') *p = '/';
    }

    char lua_path[128];
    snprintf(lua_path, sizeof(lua_path), "/plugins/%s.lua", modpath);

    /* Check if .lua file exists */
    hal_file_t f = hal_storage_open(lua_path, HAL_FILE_READ);
    if (!f) {
        lua_pushfstring(L, "\n\tno file '%s' on SD", lua_path);
        return 1;
    }
    size_t lua_size = hal_storage_file_size(f);
    hal_storage_file_close(f);

    /* Try cached bytecode first */
    char luac_path[128];
    snprintf(luac_path, sizeof(luac_path), "/plugins/%s.luac", modpath);

    if (load_cached_bytecode(L, luac_path, lua_path, lua_size)) {
        LOG_DBG("LUA", "require '%s': cached bytecode", modname);
        return 1;
    }

    /* Cache miss — load and compile from source */
    f = hal_storage_open(lua_path, HAL_FILE_READ);
    if (!f) {
        lua_pushfstring(L, "\n\tcannot reopen '%s'", lua_path);
        return 1;
    }

    char *buf = (char *)malloc(lua_size);
    if (!buf) {
        hal_storage_file_close(f);
        lua_pushstring(L, "\n\tout of memory");
        return 1;
    }

    int read = hal_storage_file_read(f, buf, lua_size);
    hal_storage_file_close(f);

    if (read != (int)lua_size) {
        free(buf);
        lua_pushfstring(L, "\n\tread error on '%s'", lua_path);
        return 1;
    }

    int err = luaL_loadbuffer(L, buf, lua_size, lua_path);
    free(buf);

    if (err != 0) {
        return lua_error(L);
    }

    /* Cache the compiled bytecode for next time */
    save_bytecode_cache(L, luac_path, lua_size);
    LOG_INF("LUA", "require '%s': compiled + cached", modname);

    return 1;
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

void api_register_core(lua_State *L) {
    api_display_register(L);
    api_input_register(L);
    api_storage_register(L);
    api_system_register(L);
    api_font_register(L);
    api_layout_register(L);
}

void api_register_capability(lua_State *L, const char *cap) {
    if (!cap || !cap[0]) return;

    if (strcmp(cap, "text") == 0) {
        api_text_register(L);
        return;
    }
    if (strcmp(cap, "zip") == 0) {
        api_zip_register(L);
        return;
    }
    if (strcmp(cap, "xml") == 0) {
        api_xml_register(L);
        return;
    }
    if (strcmp(cap, "epub") == 0) {
        api_epub_register(L);
        return;
    }

    /* Unknown capability — log and ignore (forward compatibility:
     * older firmware can boot a plugin that declares a future capability). */
    LOG_INF("LUA", "Unknown capability '%s' — skipping registration", cap);
}

lua_State *api_create_state(void) {
    uint32_t h0 = hal_system_free_heap();

    lua_State *L = luaL_newstate();
    if (!L) {
        LOG_ERR("LUA", "Failed to create Lua state");
        return NULL;
    }
    uint32_t h1 = hal_system_free_heap();

    open_safe_libs(L);
    uint32_t h2 = hal_system_free_heap();

    install_sd_searcher(L);
    api_register_core(L);
    uint32_t h3 = hal_system_free_heap();

    LOG_INF("LUA", "Lua state: bare=%uB, +libs=%uB, +core=%uB (total %uB)",
            h0 - h1, h1 - h2, h2 - h3, h0 - h3);
    return L;
}
