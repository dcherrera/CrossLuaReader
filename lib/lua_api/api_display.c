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
#include "hal_gpio.h"
#include "api_input.h"
#include "renderer.h"
#include "font_manager.h"
#include "font_render.h"
#include "layout_engine.h"
#include "logging.h"

/* display.clear(color) — color: 0xFF=white, 0x00=black. Default white. */
static int l_display_clear(lua_State *L) {
    uint8_t color = (uint8_t)luaL_optinteger(L, 1, 0xFF);
    renderer_clear_screen(color);
    return 0;
}

/* display.refresh(mode) — 0=full, 1=half, 2=fast. Default fast.
 * Re-polls buttons after the blocking refresh so presses during
 * the refresh aren't lost (enables fast menu scrolling). */
static int l_display_refresh(lua_State *L) {
    int mode = (int)luaL_optinteger(L, 1, 2);
    hal_display_refresh((refresh_mode_t)mode);
    /* Button polling handled by background input task — presses during
     * refresh are automatically queued and won't be lost */
    return 0;
}

/* display.refreshFull() */
static int l_display_refresh_full(lua_State *L) {
    (void)L;
    hal_display_refresh(REFRESH_FULL);
    return 0;
}

/* display.drawText(fontId, x, y, text) — fallback-aware */
static int l_display_draw_text(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    int x = (int)lua_tonumber(L, 2);
    int y = (int)lua_tonumber(L, 3);
    const char *text = luaL_checkstring(L, 4);

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    font_render_draw_text_fb(font_id, x, y, text, true);
    return 0;
}

/* display.drawLine(x1, y1, x2, y2) */
static int l_display_draw_line(lua_State *L) {
    int x1 = (int)lua_tonumber(L, 1);
    int y1 = (int)lua_tonumber(L, 2);
    int x2 = (int)lua_tonumber(L, 3);
    int y2 = (int)lua_tonumber(L, 4);
    renderer_draw_line(x1, y1, x2, y2, true);
    return 0;
}

/* display.drawRect(x, y, w, h) */
static int l_display_draw_rect(lua_State *L) {
    int x = (int)lua_tonumber(L, 1);
    int y = (int)lua_tonumber(L, 2);
    int w = (int)lua_tonumber(L, 3);
    int h = (int)lua_tonumber(L, 4);
    renderer_draw_rect(x, y, w, h, true);
    return 0;
}

/* display.fillRect(x, y, w, h) */
static int l_display_fill_rect(lua_State *L) {
    int x = (int)lua_tonumber(L, 1);
    int y = (int)lua_tonumber(L, 2);
    int w = (int)lua_tonumber(L, 3);
    int h = (int)lua_tonumber(L, 4);
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

/* display.getTextWidth(fontId, text) → int — fallback-aware */
static int l_display_get_text_width(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const char *text = luaL_checkstring(L, 2);

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    lua_pushinteger(L, font_render_get_advance_fb(font_id, text));
    return 1;
}

/* display.getLineHeight(fontId) → int */
static int l_display_get_line_height(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const font_data_t *font = font_manager_get(font_id);
    if (!font) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }
    lua_pushinteger(L, font_render_get_line_height(font));
    return 1;
}

/* display.fillRoundedRect(x, y, w, h, radius) */
static int l_display_fill_rounded_rect(lua_State *L) {
    int x = (int)lua_tonumber(L, 1);
    int y = (int)lua_tonumber(L, 2);
    int w = (int)lua_tonumber(L, 3);
    int h = (int)lua_tonumber(L, 4);
    int r = lua_isnoneornil(L, 5) ? 6 : (int)lua_tonumber(L, 5);
    renderer_fill_rounded_rect(x, y, w, h, r, true);
    return 0;
}

/* display.fillRoundedRectGray(x, y, w, h, radius) — dithered gray selection */
static int l_display_fill_rounded_rect_gray(lua_State *L) {
    int x = (int)lua_tonumber(L, 1);
    int y = (int)lua_tonumber(L, 2);
    int w = (int)lua_tonumber(L, 3);
    int h = (int)lua_tonumber(L, 4);
    int r = lua_isnoneornil(L, 5) ? 6 : (int)lua_tonumber(L, 5);
    renderer_fill_rounded_rect_gray(x, y, w, h, r);
    return 0;
}

/* display.drawTextInverted(fontId, x, y, text) — white text, fallback-aware */
static int l_display_draw_text_inverted(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    int x = (int)lua_tonumber(L, 2);
    int y = (int)lua_tonumber(L, 3);
    const char *text = luaL_checkstring(L, 4);

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    font_render_draw_text_fb(font_id, x, y, text, false);
    return 0;
}

/* display.setOrientation(n) — 0=portrait, 1=landscape_cw, 2=inverted, 3=landscape_ccw */
static int l_display_set_orientation(lua_State *L) {
    int orient = (int)lua_tonumber(L, 1);
    renderer_set_orientation((orientation_t)orient);
    /* Keep layout engine in sync so body/footer regions match new dimensions */
    layout_set_orientation((orientation_t)orient);
    return 0;
}

/* display.getOrientation() → int */
static int l_display_get_orientation(lua_State *L) {
    lua_pushinteger(L, (int)renderer_get_orientation());
    return 1;
}

/* display.drawLinePhysical(x1, y1, x2, y2) — physical coords, no rotation */
static int l_display_draw_line_physical(lua_State *L) {
    int x1 = (int)lua_tonumber(L, 1);
    int y1 = (int)lua_tonumber(L, 2);
    int x2 = (int)lua_tonumber(L, 3);
    int y2 = (int)lua_tonumber(L, 4);
    renderer_draw_line_physical(x1, y1, x2, y2, true);
    return 0;
}

/* display.drawRectPhysical(x, y, w, h) — physical coords, no rotation */
static int l_display_draw_rect_physical(lua_State *L) {
    int x = (int)lua_tonumber(L, 1);
    int y = (int)lua_tonumber(L, 2);
    int w = (int)lua_tonumber(L, 3);
    int h = (int)lua_tonumber(L, 4);
    renderer_draw_rect_physical(x, y, w, h, true);
    return 0;
}

/* display.drawTextPhysical(fontId, x, y, text) — physical coords, fallback-aware */
static int l_display_draw_text_physical(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    int x = (int)lua_tonumber(L, 2);
    int y = (int)lua_tonumber(L, 3);
    const char *text = luaL_checkstring(L, 4);

    if (!font_manager_get(font_id)) return 0;

    /* Temporarily set portrait orientation for physical rendering */
    orientation_t saved = renderer_get_orientation();
    renderer_set_orientation(ORIENT_PORTRAIT);
    font_render_draw_text_fb(font_id, x, y, text, true);
    renderer_set_orientation(saved);
    return 0;
}

/* display.contentArea() → x, y, w, h — usable content area excluding button bar */
static int l_display_content_area(lua_State *L) {
    int x, y, w, h;
    renderer_get_content_area(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
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
        {"getTextWidth",      l_display_get_text_width},
        {"getLineHeight",     l_display_get_line_height},
        {"fillRoundedRect",      l_display_fill_rounded_rect},
        {"fillRoundedRectGray", l_display_fill_rounded_rect_gray},
        {"drawTextInverted",  l_display_draw_text_inverted},
        {"setOrientation",       l_display_set_orientation},
        {"getOrientation",       l_display_get_orientation},
        {"drawLinePhysical",     l_display_draw_line_physical},
        {"drawRectPhysical",     l_display_draw_rect_physical},
        {"drawTextPhysical",     l_display_draw_text_physical},
        {"contentArea",          l_display_content_area},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "display");
}
