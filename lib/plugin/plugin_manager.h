/**
 * @file plugin_manager.h
 * @brief Plugin discovery, lifecycle management, and switching.
 *        Scans /plugins/ for .lua files, manages Lua states,
 *        dispatches onEnter/loop/onExit lifecycle calls.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PLUGIN_MAX_DISCOVERED  16
#define PLUGIN_NAME_MAX        32
#define PLUGIN_ID_MAX          24
#define PLUGIN_PATH_MAX        64
#define PLUGIN_EXT_MAX         8
#define PLUGIN_EXT_LEN         8

/** Metadata for a discovered plugin, parsed from its plugin table. */
typedef struct {
    char name[PLUGIN_NAME_MAX];
    char id[PLUGIN_ID_MAX];
    char path[PLUGIN_PATH_MAX];
    char menu_entry[PLUGIN_NAME_MAX];
    char type[12];
    char file_extensions[PLUGIN_EXT_MAX][PLUGIN_EXT_LEN];
    int  ext_count;
    bool valid;
    bool system;   /**< true = stock plugin, shares Lua state with other stock plugins */
} plugin_info_t;

/**
 * Scan /plugins/ on SD card and parse plugin manifests.
 * Call once during boot, after HAL and font cache are initialized.
 *
 * @return true if at least one plugin was discovered
 */
bool plugin_manager_init(void);

/** @return Number of discovered plugins. */
int plugin_manager_count(void);

/**
 * Get info for a discovered plugin by index.
 *
 * @param index 0-based index (0 to plugin_manager_count()-1)
 * @return      Plugin info, or NULL if index out of range
 */
const plugin_info_t *plugin_manager_get_info(int index);

/**
 * Activate a plugin by ID. Stops the current plugin if one is active.
 * If plugin_id is NULL, restores the last active plugin from SD,
 * or falls back to "home" if no state is saved.
 *
 * @param plugin_id Plugin ID to start, or NULL for auto-restore
 * @param arg       Optional argument passed to onEnter (e.g., file path)
 * @return          true if plugin started successfully
 */
bool plugin_manager_start(const char *plugin_id, const char *arg);

/**
 * Dispatch one frame to the active plugin's loop() function.
 * Also handles pending navigation requests (finish/switch/goHome).
 * Call once per main loop iteration.
 */
void plugin_manager_dispatch_loop(void);

/**
 * Switch to another plugin. Stops current, starts target.
 *
 * @param plugin_id Target plugin ID
 * @param arg       Optional argument for onEnter
 * @return          true if switch succeeded
 */
bool plugin_manager_switch(const char *plugin_id, const char *arg);

/** Stop the current plugin (calls onExit, closes Lua state). */
void plugin_manager_stop(void);

/** @return ID of the currently active plugin, or NULL if none. */
const char *plugin_manager_active_id(void);

/**
 * Find a reader plugin that handles a given file extension.
 *
 * @param extension File extension without dot (e.g., "epub")
 * @return          Index of the reader plugin, or -1 if none found
 */
int plugin_manager_find_reader(const char *extension);

/**
 * Re-scan plugins from SD card. Stops active plugin, clears list,
 * re-discovers from /plugins/. Use after SD card hot-swap.
 */
void plugin_manager_reinit(void);
