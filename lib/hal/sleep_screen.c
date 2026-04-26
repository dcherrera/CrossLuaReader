/**
 * @file sleep_screen.c
 * @brief Sleep screen rendering: wallpaper decode, cycle/random selection,
 *        "SLEEP" overlay for clear mode. Uses boot font for text.
 *
 * @status Phase 8
 * @issues None
 * @todo None
 */

#include "sleep_screen.h"
#include "boot_font.h"
#include "bmp_decoder.h"
#include "hal_storage.h"
#include "hal_display.h"
#include "hal_power.h"
#include "renderer.h"
#include "font_manager.h"
#include "font_render.h"
#include "logging.h"

#include "lua.h"
#include "lauxlib.h"

#include "esp_random.h"

#include <string.h>
#include <stdio.h>

/* ── State ──────────────────────────────────────────────────────── */

static sleep_mode_t current_mode = SLEEP_MODE_BLANK;
static char wallpaper_name[64] = "";

/* Lua sleep hook (optional, set by plugins for custom sleep screen content) */
static lua_State *hook_state = NULL;
static int hook_ref = LUA_NOREF;

#define WALLPAPER_DIR   "/wallpapers"
#define MAX_WALLPAPERS  32
#define WP_NAME_MAX     64
#define CYCLE_IDX_FILE  "/crosslua_sleep_idx.txt"

static char wp_list[MAX_WALLPAPERS][WP_NAME_MAX];
static int  wp_count = 0;
static bool wp_scanned = false;

/* ── Wallpaper directory scan ───────────────────────────────────── */

static bool ends_with_bmp(const char *name) {
    int len = (int)strlen(name);
    if (len < 5) return false;
    const char *ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'b' || ext[1] == 'B') &&
            (ext[2] == 'm' || ext[2] == 'M') &&
            (ext[3] == 'p' || ext[3] == 'P'));
}

static void scan_wallpapers(void) {
    if (wp_scanned) return;
    wp_scanned = true;
    wp_count = 0;

    hal_dir_t dir = hal_storage_dir_open(WALLPAPER_DIR);
    if (!dir) {
        LOG_INF("SLEEP", "No %s directory", WALLPAPER_DIR);
        return;
    }

    char name_buf[128];
    bool is_dir;
    while (hal_storage_dir_next(dir, name_buf, sizeof(name_buf), &is_dir)) {
        if (is_dir) continue;
        if (!ends_with_bmp(name_buf)) continue;
        if (wp_count >= MAX_WALLPAPERS) break;

        strncpy(wp_list[wp_count], name_buf, WP_NAME_MAX - 1);
        wp_list[wp_count][WP_NAME_MAX - 1] = '\0';
        wp_count++;
    }

    hal_storage_dir_close(dir);
    LOG_INF("SLEEP", "Found %d wallpaper(s)", wp_count);
}

/* ── Cycle index persistence ────────────────────────────────────── */

static int read_cycle_index(void) {
    hal_file_t f = hal_storage_open(CYCLE_IDX_FILE, HAL_FILE_READ);
    if (!f) return 0;

    char buf[8];
    int n = hal_storage_file_read(f, buf, sizeof(buf) - 1);
    hal_storage_file_close(f);

    if (n <= 0) return 0;
    buf[n] = '\0';
    int idx = 0;
    for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; i++) {
        idx = idx * 10 + (buf[i] - '0');
    }
    return idx;
}

static void write_cycle_index(int idx) {
    hal_file_t f = hal_storage_open(CYCLE_IDX_FILE, HAL_FILE_WRITE);
    if (!f) return;
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", idx);
    hal_storage_file_write(f, buf, n);
    hal_storage_file_close(f);
}

/* ── Render helpers ─────────────────────────────────────────────── */

static bool render_wallpaper(const char *name) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", WALLPAPER_DIR, name);
    if (!bmp_decode_to_framebuffer(path, NULL)) {
        LOG_ERR("SLEEP", "Failed to decode: %s", path);
        return false;
    }
    return true;
}

static void render_blank(void) {
    renderer_clear_screen(0xFF);
}

static void render_single(void) {
    if (wallpaper_name[0] == '\0' || !render_wallpaper(wallpaper_name)) {
        render_blank();
    }
}

static void render_cycle(void) {
    scan_wallpapers();
    if (wp_count == 0) {
        render_blank();
        return;
    }

    int idx = read_cycle_index() % wp_count;
    if (!render_wallpaper(wp_list[idx])) {
        render_blank();
    }
    write_cycle_index((idx + 1) % wp_count);
}

static void render_random(void) {
    scan_wallpapers();
    if (wp_count == 0) {
        render_blank();
        return;
    }

    int idx = (int)(esp_random() % (uint32_t)wp_count);
    if (!render_wallpaper(wp_list[idx])) {
        render_blank();
    }
}

static void render_clear(void) {
    /* Keep current framebuffer content (reader's current page).
     * Overlay a small "SLEEP" + battery indicator in bottom-right corner. */
    int fid = boot_font_get_id();
    if (fid < 0) return;

    const font_data_t *font = font_manager_get(fid);
    if (!font) return;

    int panel_w = hal_display_width();
    int panel_h = hal_display_height();
    int lh = font_render_get_line_height(font);

    /* White rectangle for text background */
    int box_w = 120;
    int box_h = lh + 8;
    int box_x = panel_w - box_w - 10;
    int box_y = panel_h - box_h - 10;

    /* Use portrait orientation for physical coordinates */
    orientation_t saved = renderer_get_orientation();
    renderer_set_orientation(ORIENT_PORTRAIT);

    renderer_fill_rect(box_x, box_y, box_w, box_h, false); /* white fill */
    renderer_draw_rect(box_x, box_y, box_w, box_h, true);  /* black border */

    char buf[32];
    snprintf(buf, sizeof(buf), "SLEEP %d%%", hal_power_battery_percent());
    font_render_draw_text_fb(fid, box_x + 8, box_y + 4, buf, true);

    renderer_set_orientation(saved);
}

/* ── Public API ─────────────────────────────────────────────────── */

void sleep_screen_set_mode(sleep_mode_t mode) {
    current_mode = mode;
    LOG_INF("SLEEP", "Mode set to %d", (int)mode);
}

void sleep_screen_set_wallpaper(const char *filename) {
    if (filename) {
        strncpy(wallpaper_name, filename, sizeof(wallpaper_name) - 1);
        wallpaper_name[sizeof(wallpaper_name) - 1] = '\0';
    } else {
        wallpaper_name[0] = '\0';
    }
}

void sleep_screen_set_hook(lua_State *L, int ref) {
    /* Clear any previous hook */
    if (hook_state && hook_ref != LUA_NOREF) {
        luaL_unref(hook_state, LUA_REGISTRYINDEX, hook_ref);
    }
    hook_state = L;
    hook_ref = ref;
    LOG_INF("SLEEP", "Sleep hook %s", ref != LUA_NOREF ? "set" : "cleared");
}

void sleep_screen_clear_hook(void) {
    if (hook_state && hook_ref != LUA_NOREF) {
        luaL_unref(hook_state, LUA_REGISTRYINDEX, hook_ref);
    }
    hook_state = NULL;
    hook_ref = LUA_NOREF;
}

/**
 * Call the Lua sleep hook if one is registered.
 * The hook can draw to the framebuffer using display.* APIs.
 * Errors are caught and logged — they don't prevent sleep.
 */
static void call_sleep_hook(void) {
    if (!hook_state || hook_ref == LUA_NOREF) return;

    lua_rawgeti(hook_state, LUA_REGISTRYINDEX, hook_ref);
    if (!lua_isfunction(hook_state, -1)) {
        lua_pop(hook_state, 1);
        return;
    }

    int err = lua_pcall(hook_state, 0, 0, 0);
    if (err) {
        const char *msg = lua_tostring(hook_state, -1);
        LOG_ERR("SLEEP", "Hook error: %s", msg ? msg : "?");
        lua_pop(hook_state, 1);
    }
}

void sleep_screen_render(void) {
    LOG_INF("SLEEP", "Rendering sleep screen (mode=%d)", (int)current_mode);

    switch (current_mode) {
        case SLEEP_MODE_BLANK:
            render_blank();
            break;
        case SLEEP_MODE_SINGLE:
            render_single();
            break;
        case SLEEP_MODE_CYCLE:
            render_cycle();
            break;
        case SLEEP_MODE_RANDOM:
            render_random();
            break;
        case SLEEP_MODE_CLEAR:
            render_clear();
            break;
        default:
            render_blank();
            break;
    }

    /* Call Lua hook after base screen renders, before refresh */
    call_sleep_hook();

    hal_display_refresh(REFRESH_FULL);
}
