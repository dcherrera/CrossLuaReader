/**
 * @file api_xml.c
 * @brief Lua xml.* module bindings. Wraps lib/xml SAX parsing with a
 *        single entry point (xml.parse) that takes the input, options,
 *        and Lua callbacks.
 *
 *        Surface:
 *          xml.parse(input, opts, handlers) → bool, errmsg
 *            input:    string (whole document) — pull-source via Lua
 *                      callback deferred to Phase 9.B.
 *            opts:     { mode = "strict" | "html" }   (defaults to "strict")
 *            handlers: { on_start = function(tag, attrs) ... end,
 *                        on_end   = function(tag) ... end,
 *                        on_text  = function(text) ... end }
 *                      attrs is a Lua table { [key] = value, ... }
 *
 *        The on_start / on_end / on_text Lua functions are stored in the
 *        Lua registry for the lifetime of the parse call, then released.
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo Phase 9.B: pull-source variant + named-entity decoding.
 */

#include "api_xml.h"
#include "lauxlib.h"

#include "xml.h"
#include "logging.h"

#include <string.h>

/* Refs into the registry for the user's three callbacks during a parse. */
typedef struct {
    lua_State *L;
    int ref_start;   /* LUA_NOREF if not provided */
    int ref_end;
    int ref_text;
    bool error;      /* set by callback if it raises */
    char errmsg[128];
} lua_xml_ctx_t;

static void capture_error(lua_xml_ctx_t *c) {
    if (!c->error) {
        c->error = true;
        const char *msg = lua_tostring(c->L, -1);
        if (msg) {
            strncpy(c->errmsg, msg, sizeof(c->errmsg) - 1);
            c->errmsg[sizeof(c->errmsg) - 1] = '\0';
        } else {
            strncpy(c->errmsg, "lua callback error", sizeof(c->errmsg) - 1);
        }
    }
    lua_pop(c->L, 1);  /* drop the error */
}

static void br_on_start(void *user, const char *tag,
                         const xml_attr_t *attrs, int n_attrs) {
    lua_xml_ctx_t *c = (lua_xml_ctx_t *)user;
    if (c->error || c->ref_start == LUA_NOREF) return;

    lua_rawgeti(c->L, LUA_REGISTRYINDEX, c->ref_start);
    lua_pushstring(c->L, tag);

    /* Build attrs table { [key] = value, ... }. */
    lua_createtable(c->L, 0, n_attrs);
    for (int i = 0; i < n_attrs; i++) {
        lua_pushstring(c->L, attrs[i].value);
        lua_setfield(c->L, -2, attrs[i].key);
    }

    if (lua_pcall(c->L, 2, 0, 0) != LUA_OK) capture_error(c);
}

static void br_on_end(void *user, const char *tag) {
    lua_xml_ctx_t *c = (lua_xml_ctx_t *)user;
    if (c->error || c->ref_end == LUA_NOREF) return;

    lua_rawgeti(c->L, LUA_REGISTRYINDEX, c->ref_end);
    lua_pushstring(c->L, tag);
    if (lua_pcall(c->L, 1, 0, 0) != LUA_OK) capture_error(c);
}

static void br_on_text(void *user, const char *text, size_t len) {
    lua_xml_ctx_t *c = (lua_xml_ctx_t *)user;
    if (c->error || c->ref_text == LUA_NOREF) return;

    lua_rawgeti(c->L, LUA_REGISTRYINDEX, c->ref_text);
    lua_pushlstring(c->L, text, len);
    if (lua_pcall(c->L, 1, 0, 0) != LUA_OK) capture_error(c);
}

/* xml.parse(input, opts, handlers) → bool, errmsg */
static int l_xml_parse(lua_State *L) {
    luaL_checktype(L, 1, LUA_TSTRING);  /* input string (pull-source TBD in 9.B) */
    luaL_checktype(L, 3, LUA_TTABLE);   /* handlers */

    /* opts.mode */
    xml_mode_t mode = XML_MODE_STRICT;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "mode");
        if (lua_isstring(L, -1)) {
            const char *m = lua_tostring(L, -1);
            if (strcmp(m, "html") == 0) mode = XML_MODE_HTML;
        }
        lua_pop(L, 1);
    }

    /* Stash handlers in the registry. */
    lua_xml_ctx_t c = { .L = L, .error = false };
    c.errmsg[0] = '\0';

    lua_getfield(L, 3, "on_start");
    c.ref_start = lua_isfunction(L, -1) ? luaL_ref(L, LUA_REGISTRYINDEX) : (lua_pop(L, 1), LUA_NOREF);

    lua_getfield(L, 3, "on_end");
    c.ref_end   = lua_isfunction(L, -1) ? luaL_ref(L, LUA_REGISTRYINDEX) : (lua_pop(L, 1), LUA_NOREF);

    lua_getfield(L, 3, "on_text");
    c.ref_text  = lua_isfunction(L, -1) ? luaL_ref(L, LUA_REGISTRYINDEX) : (lua_pop(L, 1), LUA_NOREF);

    size_t buf_len = 0;
    const char *buf = lua_tolstring(L, 1, &buf_len);

    xml_handlers_t h = {
        .on_start = br_on_start,
        .on_end   = br_on_end,
        .on_text  = br_on_text,
    };
    bool ok = xml_parse_buffer(buf, buf_len, mode, &h, &c);

    /* Release registry refs. */
    if (c.ref_start != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, c.ref_start);
    if (c.ref_end   != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, c.ref_end);
    if (c.ref_text  != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, c.ref_text);

    if (c.error) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, c.errmsg);
        return 2;
    }
    if (!ok) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "xml parse error");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

void api_xml_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"parse", l_xml_parse},
        {NULL, NULL},
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "xml");
}
