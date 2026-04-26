/**
 * @file sleep_screen.h
 * @brief Sleep screen rendering. Supports wallpapers (single/cycle/random),
 *        blank, and clear (keep page) modes. Renders before deep sleep.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>

/* Forward declare lua_State to avoid pulling in lua.h */
typedef struct lua_State lua_State;

/** Sleep screen rendering modes. */
typedef enum {
    SLEEP_MODE_BLANK  = 0,  /**< Clear screen to white */
    SLEEP_MODE_SINGLE = 1,  /**< Show specific wallpaper */
    SLEEP_MODE_CYCLE  = 2,  /**< Cycle through /wallpapers/ top-to-bottom */
    SLEEP_MODE_RANDOM = 3,  /**< Random pick from /wallpapers/ */
    SLEEP_MODE_CLEAR  = 4,  /**< Keep current page, overlay "SLEEP" text */
} sleep_mode_t;

/** Set the sleep screen mode (called from Lua at boot). */
void sleep_screen_set_mode(sleep_mode_t mode);

/** Set the wallpaper filename for SINGLE mode (e.g., "sunset.bmp"). */
void sleep_screen_set_wallpaper(const char *filename);

/**
 * Register a Lua callback to be called after the base sleep screen renders
 * but before the display refresh. The callback can draw text, shapes, etc.
 * using normal display.* API calls (they write to the framebuffer).
 *
 * @param L   Lua state that owns the callback
 * @param ref Lua registry reference to the callback function (from luaL_ref)
 */
void sleep_screen_set_hook(lua_State *L, int ref);

/**
 * Clear the sleep hook. Call when the plugin that set it exits.
 */
void sleep_screen_clear_hook(void);

/**
 * Render the sleep screen to the framebuffer and refresh the display.
 * Called from hal_power_enter_sleep() before entering deep sleep.
 * If a Lua hook is registered, it is called after the base screen renders.
 */
void sleep_screen_render(void);
