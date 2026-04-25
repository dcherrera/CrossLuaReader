/**
 * @file plugin_manager.c
 * @brief Plugin manager implementation: discovery, lifecycle, switching.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "plugin_manager.h"
#include "api_register.h"
#include "hal_storage.h"
#include "renderer.h"
#include "logging.h"

#include "lua.h"
#include "lauxlib.h"

#include <string.h>
#include <stdlib.h>

/* ── State ──────────────────────────────────────────────────────────── */

static plugin_info_t plugins[PLUGIN_MAX_DISCOVERED];
static int plugin_count = 0;

static lua_State *active_state = NULL;
static int active_index = -1;

/* Navigation flags (set by Lua-side plugin.finish/navigate/goHome) */
static bool nav_pending = false;
static bool nav_go_home = false;
static char nav_target_id[PLUGIN_ID_MAX];
static char nav_arg[PLUGIN_PATH_MAX];

#define STATE_FILE "/crosslua_state.txt"

/* ── Lua file loading via HAL storage ───────────────────────────────── */

/**
 * Load and execute a Lua file from SD card using hal_storage.
 * Standard luaL_dofile uses C fopen which doesn't work with SdFat.
 *
 * @return 0 on success, non-zero on error (error message on Lua stack)
 */
static int load_lua_file(lua_State *L, const char *path) {
    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        lua_pushfstring(L, "cannot open %s", path);
        return 1;
    }

    size_t size = hal_storage_file_size(f);
    if (size == 0 || size > 256 * 1024) {
        hal_storage_file_close(f);
        lua_pushfstring(L, "file too large or empty: %s (%d bytes)", path, (int)size);
        return 1;
    }

    char *buf = (char *)malloc(size);
    if (!buf) {
        hal_storage_file_close(f);
        lua_pushfstring(L, "out of memory loading %s", path);
        return 1;
    }

    int read = hal_storage_file_read(f, buf, size);
    hal_storage_file_close(f);

    if (read != (int)size) {
        free(buf);
        lua_pushfstring(L, "read error on %s: got %d, expected %d", path, read, (int)size);
        return 1;
    }

    int err = luaL_loadbuffer(L, buf, size, path);
    free(buf);

    if (err != 0) {
        return err;  /* error message already on stack */
    }

    /* Execute the loaded chunk */
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

/* ── Helpers ────────────────────────────────────────────────────────── */

/**
 * Read a string field from a Lua table on top of stack.
 * Pops nothing, leaves stack unchanged.
 */
static void read_table_string(lua_State *L, const char *field,
                               char *buf, int buf_size) {
    lua_getfield(L, -1, field);
    if (lua_isstring(L, -1)) {
        const char *val = lua_tostring(L, -1);
        strncpy(buf, val, buf_size - 1);
        buf[buf_size - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
    lua_pop(L, 1);
}

/**
 * Read fileExtensions table from plugin table.
 */
static void read_file_extensions(lua_State *L, plugin_info_t *info) {
    lua_getfield(L, -1, "fileExtensions");
    if (lua_istable(L, -1)) {
        int len = (int)lua_rawlen(L, -1);
        if (len > PLUGIN_EXT_MAX) len = PLUGIN_EXT_MAX;
        for (int i = 1; i <= len; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_isstring(L, -1)) {
                const char *ext = lua_tostring(L, -1);
                strncpy(info->file_extensions[info->ext_count], ext, PLUGIN_EXT_LEN - 1);
                info->file_extensions[info->ext_count][PLUGIN_EXT_LEN - 1] = '\0';
                info->ext_count++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

/**
 * Check if a filename ends with .lua
 */
static bool ends_with_lua(const char *name) {
    int len = (int)strlen(name);
    if (len < 5) return false;
    return strcmp(name + len - 4, ".lua") == 0;
}

/**
 * Parse a plugin's manifest by running it in a temporary Lua state.
 * Only reads the plugin table — does not keep the state.
 */
static bool parse_plugin_manifest(const char *path, plugin_info_t *info) {
    memset(info, 0, sizeof(plugin_info_t));
    strncpy(info->path, path, PLUGIN_PATH_MAX - 1);

    lua_State *L = api_create_state();
    if (!L) return false;

    if (load_lua_file(L, path) != 0) {
        const char *err = lua_tostring(L, -1);
        LOG_ERR("PLUG", "Parse error in %s: %s", path, err ? err : "?");
        lua_close(L);
        return false;
    }

    /* Read plugin table */
    lua_getglobal(L, "plugin");
    if (!lua_istable(L, -1)) {
        LOG_ERR("PLUG", "No plugin table in %s", path);
        lua_close(L);
        return false;
    }

    read_table_string(L, "name", info->name, PLUGIN_NAME_MAX);
    read_table_string(L, "id", info->id, PLUGIN_ID_MAX);
    read_table_string(L, "type", info->type, sizeof(info->type));
    read_table_string(L, "menuEntry", info->menu_entry, PLUGIN_NAME_MAX);
    read_file_extensions(L, info);

    lua_close(L);

    if (info->id[0] == '\0') {
        LOG_ERR("PLUG", "Plugin in %s has no id", path);
        return false;
    }

    info->valid = true;
    return true;
}

/**
 * Find plugin index by ID.
 *
 * @return Index, or -1 if not found
 */
static int find_plugin_by_id(const char *plugin_id) {
    if (!plugin_id) return -1;
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].valid && strcmp(plugins[i].id, plugin_id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * Save the active plugin ID to SD for restore on next boot.
 */
static void save_state(const char *plugin_id) {
    hal_file_t f = hal_storage_open(STATE_FILE, HAL_FILE_WRITE);
    if (f) {
        hal_storage_file_write(f, plugin_id, strlen(plugin_id));
        hal_storage_file_close(f);
    }
}

/**
 * Read the saved plugin ID from SD.
 *
 * @return Plugin ID string in static buffer, or NULL
 */
static const char *load_saved_state(void) {
    static char buf[PLUGIN_ID_MAX];
    hal_file_t f = hal_storage_open(STATE_FILE, HAL_FILE_READ);
    if (!f) return NULL;

    int read = hal_storage_file_read(f, buf, PLUGIN_ID_MAX - 1);
    hal_storage_file_close(f);

    if (read <= 0) return NULL;
    buf[read] = '\0';
    return buf;
}

/* ── Navigation functions registered in Lua ─────────────────────────── */

static int l_plugin_finish(lua_State *L) {
    (void)L;
    nav_pending = true;
    nav_go_home = true;
    nav_target_id[0] = '\0';
    return 0;
}

static int l_plugin_navigate(lua_State *L) {
    const char *id = luaL_checkstring(L, 1);
    strncpy(nav_target_id, id, PLUGIN_ID_MAX - 1);
    nav_target_id[PLUGIN_ID_MAX - 1] = '\0';

    if (lua_isstring(L, 2)) {
        const char *arg = lua_tostring(L, 2);
        strncpy(nav_arg, arg, PLUGIN_PATH_MAX - 1);
        nav_arg[PLUGIN_PATH_MAX - 1] = '\0';
    } else {
        nav_arg[0] = '\0';
    }

    nav_pending = true;
    nav_go_home = false;
    return 0;
}

static int l_plugin_go_home(lua_State *L) {
    (void)L;
    nav_pending = true;
    nav_go_home = true;
    strncpy(nav_target_id, "home", PLUGIN_ID_MAX);
    return 0;
}

/**
 * Register navigation functions on the plugin table in Lua state.
 */
static void register_nav_functions(lua_State *L) {
    lua_getglobal(L, "plugin");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_pushcfunction(L, l_plugin_finish);
    lua_setfield(L, -2, "finish");

    lua_pushcfunction(L, l_plugin_navigate);
    lua_setfield(L, -2, "navigate");

    lua_pushcfunction(L, l_plugin_go_home);
    lua_setfield(L, -2, "goHome");

    lua_pop(L, 1);
}

/**
 * Call a plugin lifecycle function (onEnter, onExit) with pcall.
 */
static bool call_lifecycle(lua_State *L, const char *func_name, const char *arg) {
    lua_getglobal(L, "plugin");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return false;
    }

    lua_getfield(L, -1, func_name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return true;  /* optional function — not an error */
    }

    int nargs = 0;
    if (arg && arg[0]) {
        lua_pushstring(L, arg);
        nargs = 1;
    }

    int err = lua_pcall(L, nargs, 0, 0);
    lua_pop(L, 1);  /* pop plugin table */

    if (err) {
        const char *msg = lua_tostring(L, -1);
        LOG_ERR("PLUG", "%s() error: %s", func_name, msg ? msg : "?");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* ── Public API ─────────────────────────────────────────────────────── */

bool plugin_manager_init(void) {
    plugin_count = 0;
    memset(plugins, 0, sizeof(plugins));

    hal_dir_t dir = hal_storage_dir_open("/plugins");
    if (!dir) {
        LOG_INF("PLUG", "No /plugins directory on SD");
        return false;
    }

    char name_buf[128];
    bool is_dir;
    char path_buf[PLUGIN_PATH_MAX];

    while (hal_storage_dir_next(dir, name_buf, sizeof(name_buf), &is_dir)) {
        if (is_dir || !ends_with_lua(name_buf)) continue;
        if (plugin_count >= PLUGIN_MAX_DISCOVERED) break;

        snprintf(path_buf, sizeof(path_buf), "/plugins/%s", name_buf);

        if (parse_plugin_manifest(path_buf, &plugins[plugin_count])) {
            LOG_INF("PLUG", "Discovered: %s (%s) [%s]",
                    plugins[plugin_count].name,
                    plugins[plugin_count].id,
                    plugins[plugin_count].type);
            plugin_count++;
        }
    }

    hal_storage_dir_close(dir);
    LOG_INF("PLUG", "Discovered %d plugin(s)", plugin_count);
    return plugin_count > 0;
}

int plugin_manager_count(void) {
    return plugin_count;
}

const plugin_info_t *plugin_manager_get_info(int index) {
    if (index < 0 || index >= plugin_count) return NULL;
    return &plugins[index];
}

bool plugin_manager_start(const char *plugin_id, const char *arg) {
    /* Stop current plugin if active */
    if (active_state) {
        plugin_manager_stop();
    }

    /* Resolve plugin ID */
    const char *target_id = plugin_id;
    if (!target_id) {
        target_id = load_saved_state();
    }
    if (!target_id && plugin_count > 0) {
        /* Find "home" plugin, or use first available */
        int home_idx = find_plugin_by_id("home");
        target_id = (home_idx >= 0) ? "home" : plugins[0].id;
    }
    if (!target_id) {
        LOG_ERR("PLUG", "No plugins available");
        return false;
    }

    int idx = find_plugin_by_id(target_id);
    if (idx < 0) {
        LOG_ERR("PLUG", "Plugin not found: %s", target_id);
        /* Fall back to first plugin */
        if (plugin_count > 0) {
            idx = 0;
            target_id = plugins[0].id;
        } else {
            return false;
        }
    }

    /* Create fresh Lua state */
    active_state = api_create_state();
    if (!active_state) {
        LOG_ERR("PLUG", "Failed to create Lua state");
        return false;
    }

    /* Load and run the plugin file */
    if (load_lua_file(active_state, plugins[idx].path) != 0) {
        const char *err = lua_tostring(active_state, -1);
        LOG_ERR("PLUG", "Load error: %s", err ? err : "?");
        lua_close(active_state);
        active_state = NULL;
        return false;
    }

    /* Register navigation functions */
    register_nav_functions(active_state);

    /* Call onEnter */
    active_index = idx;
    nav_pending = false;

    call_lifecycle(active_state, "onEnter", arg);

    save_state(target_id);
    LOG_INF("PLUG", "Started: %s", plugins[idx].name);

    return true;
}

void plugin_manager_dispatch_loop(void) {
    if (!active_state) return;

    /* Handle pending navigation */
    if (nav_pending) {
        nav_pending = false;
        const char *target = nav_go_home ? "home" : nav_target_id;
        const char *arg = nav_arg[0] ? nav_arg : NULL;
        plugin_manager_switch(target, arg);
        return;
    }

    /* Call plugin.loop() */
    lua_getglobal(active_state, "plugin");
    if (!lua_istable(active_state, -1)) {
        lua_pop(active_state, 1);
        return;
    }

    lua_getfield(active_state, -1, "loop");
    if (!lua_isfunction(active_state, -1)) {
        lua_pop(active_state, 2);
        return;
    }

    int err = lua_pcall(active_state, 0, 0, 0);
    lua_pop(active_state, 1);  /* pop plugin table */

    if (err) {
        const char *msg = lua_tostring(active_state, -1);
        LOG_ERR("PLUG", "loop() error: %s", msg ? msg : "?");
        lua_pop(active_state, 1);

        /* Show error and stop plugin */
        renderer_clear_screen(0xFF);
        /* Can't render text without a font here — just log and stop */
        LOG_ERR("PLUG", "Plugin crashed, stopping");
        plugin_manager_stop();
    }
}

bool plugin_manager_switch(const char *plugin_id, const char *arg) {
    LOG_INF("PLUG", "Switching to: %s", plugin_id ? plugin_id : "(auto)");
    return plugin_manager_start(plugin_id, arg);
}

void plugin_manager_stop(void) {
    if (!active_state) return;

    call_lifecycle(active_state, "onExit", NULL);
    lua_close(active_state);
    active_state = NULL;
    active_index = -1;

    LOG_INF("PLUG", "Plugin stopped");
}

const char *plugin_manager_active_id(void) {
    if (active_index < 0 || active_index >= plugin_count) return NULL;
    return plugins[active_index].id;
}

int plugin_manager_find_reader(const char *extension) {
    if (!extension) return -1;
    for (int i = 0; i < plugin_count; i++) {
        if (!plugins[i].valid) continue;
        if (strcmp(plugins[i].type, "reader") != 0) continue;
        for (int e = 0; e < plugins[i].ext_count; e++) {
            if (strcmp(plugins[i].file_extensions[e], extension) == 0) {
                return i;
            }
        }
    }
    return -1;
}
