/**
 * @file api_display.c
 * @brief Lua display.* module: screen rendering and measurement.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "api_display.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_display.h"
#include "renderer.h"
#include "font_manager.h"
#include "font_render.h"

/* display.clear(color) — color: 0xFF=white, 0x00=black. Default white. */
static int l_display_clear(lua_State *L) {
    uint8_t color = (uint8_t)luaL_optinteger(L, 1, 0xFF);
    renderer_clear_screen(color);
    return 0;
}

/* display.refresh(mode) — 0=full, 1=half, 2=fast. Default fast. */
static int l_display_refresh(lua_State *L) {
    int mode = (int)luaL_optinteger(L, 1, 2);
    hal_display_refresh((refresh_mode_t)mode);
    return 0;
}

/* display.refreshFull() */
static int l_display_refresh_full(lua_State *L) {
    (void)L;
    hal_display_refresh(REFRESH_FULL);
    return 0;
}

/* display.drawText(fontId, x, y, text) */
static int l_display_draw_text(lua_State *L) {
    int font_id = (int)luaL_checkinteger(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    const char *text = luaL_checkstring(L, 4);

    const font_data_t *font = font_manager_get(font_id);
    const char *path = font_manager_get_path(font_id);
    if (!font || !path) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    font_render_draw_text(font, path, x, y, text, true);
    return 0;
}

/* display.drawLine(x1, y1, x2, y2) */
static int l_display_draw_line(lua_State *L) {
    int x1 = (int)luaL_checkinteger(L, 1);
    int y1 = (int)luaL_checkinteger(L, 2);
    int x2 = (int)luaL_checkinteger(L, 3);
    int y2 = (int)luaL_checkinteger(L, 4);
    renderer_draw_line(x1, y1, x2, y2, true);
    return 0;
}

/* display.drawRect(x, y, w, h) */
static int l_display_draw_rect(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    renderer_draw_rect(x, y, w, h, true);
    return 0;
}

/* display.fillRect(x, y, w, h) */
static int l_display_fill_rect(lua_State *L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    renderer_fill_rect(x, y, w, h, true);
    return 0;
}

/* display.width() → int */
static int l_display_width(lua_State *L) {
    lua_pushinteger(L, renderer_screen_width());
    return 1;
}

/* display.height() → int */
static int l_display_height(lua_State *L) {
    lua_pushinteger(L, renderer_screen_height());
    return 1;
}

/* display.getTextWidth(fontId, text) → int */
static int l_display_get_text_width(lua_State *L) {
    int font_id = (int)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);

    const font_data_t *font = font_manager_get(font_id);
    if (!font) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    lua_pushinteger(L, font_render_get_text_advance(font, text));
    return 1;
}

/* display.getLineHeight(fontId) → int */
static int l_display_get_line_height(lua_State *L) {
    int font_id = (int)luaL_checkinteger(L, 1);
    const font_data_t *font = font_manager_get(font_id);
    if (!font) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }
    lua_pushinteger(L, font_render_get_line_height(font));
    return 1;
}

void api_display_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"clear",          l_display_clear},
        {"refresh",        l_display_refresh},
        {"refreshFull",    l_display_refresh_full},
        {"drawText",       l_display_draw_text},
        {"drawLine",       l_display_draw_line},
        {"drawRect",       l_display_draw_rect},
        {"fillRect",       l_display_fill_rect},
        {"width",          l_display_width},
        {"height",         l_display_height},
        {"getTextWidth",   l_display_get_text_width},
        {"getLineHeight",  l_display_get_line_height},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "display");
}
