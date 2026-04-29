/**
 * @file api_layout.c
 * @brief Lua bindings for the layout engine (layout.* module).
 *        Exposes region setters/getters for Header/Body/Footer.
 *
 * @status Phase 1 — core bindings
 * @issues None
 * @todo None
 */

#include "api_layout.h"
#include "lua.h"
#include "lauxlib.h"
#include "layout_engine.h"

/* ── Setters ───────────────────────────────────────────────────── */

static int l_layout_set_header_height(lua_State *L) {
    layout_set_header_height((int16_t)lua_tointeger(L, 1));
    return 0;
}

static int l_layout_set_footer_height(lua_State *L) {
    layout_set_footer_height((int16_t)lua_tointeger(L, 1));
    return 0;
}

static int l_layout_set_margins(lua_State *L) {
    int16_t top   = (int16_t)lua_tointeger(L, 1);
    int16_t right = (int16_t)lua_tointeger(L, 2);
    int16_t bot   = (int16_t)lua_tointeger(L, 3);
    int16_t left  = (int16_t)lua_tointeger(L, 4);
    layout_set_margins(top, right, bot, left);
    return 0;
}

/* layout.setMargin(m) — uniform margin all sides */
static int l_layout_set_margin(lua_State *L) {
    int16_t m = (int16_t)lua_tointeger(L, 1);
    layout_set_margins(m, m, m, m);
    return 0;
}

static int l_layout_set_line_spacing(lua_State *L) {
    layout_set_line_spacing((int16_t)lua_tointeger(L, 1));
    return 0;
}

static int l_layout_set_line_height(lua_State *L) {
    layout_set_line_height((int16_t)lua_tointeger(L, 1));
    return 0;
}

static int l_layout_set_font(lua_State *L) {
    layout_set_font((int16_t)lua_tointeger(L, 1));
    return 0;
}

static int l_layout_set_orientation(lua_State *L) {
    layout_set_orientation((orientation_t)lua_tointeger(L, 1));
    return 0;
}

/* ── Getters ───────────────────────────────────────────────────── */

static int l_layout_lines_per_page(lua_State *L) {
    lua_pushinteger(L, layout_lines_per_page());
    return 1;
}

static int l_layout_line_height(lua_State *L) {
    lua_pushinteger(L, layout_line_height());
    return 1;
}

static int l_layout_header_area(lua_State *L) {
    int16_t x, y, w, h;
    layout_header_area(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

static int l_layout_body_area(lua_State *L) {
    int16_t x, y, w, h;
    layout_body_area(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

static int l_layout_body_area_raw(lua_State *L) {
    int16_t x, y, w, h;
    layout_body_area_raw(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

static int l_layout_footer_area(lua_State *L) {
    int16_t x, y, w, h;
    layout_footer_area(&x, &y, &w, &h);
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 4;
}

static int l_layout_body_width(lua_State *L) {
    lua_pushinteger(L, layout_body_width());
    return 1;
}

static int l_layout_body_height(lua_State *L) {
    lua_pushinteger(L, layout_body_height());
    return 1;
}

/* ── Registration ──────────────────────────────────────────────── */

void api_layout_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        /* Setters */
        {"setHeaderHeight", l_layout_set_header_height},
        {"setFooterHeight", l_layout_set_footer_height},
        {"setMargins",      l_layout_set_margins},
        {"setMargin",       l_layout_set_margin},
        {"setLineSpacing",  l_layout_set_line_spacing},
        {"setLineHeight",   l_layout_set_line_height},
        {"setFont",         l_layout_set_font},
        {"setOrientation",  l_layout_set_orientation},
        /* Getters */
        {"linesPerPage",    l_layout_lines_per_page},
        {"lineHeight",      l_layout_line_height},
        {"headerArea",      l_layout_header_area},
        {"bodyArea",        l_layout_body_area},
        {"bodyAreaRaw",     l_layout_body_area_raw},
        {"footerArea",      l_layout_footer_area},
        {"bodyWidth",       l_layout_body_width},
        {"bodyHeight",      l_layout_body_height},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "layout");
}
