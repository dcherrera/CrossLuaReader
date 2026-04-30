/**
 * @file api_epub.c
 * @brief Lua epub.* module bindings. Exposes the high-level book object
 *        from lib/epub. Book handles are GC-collected userdata.
 *
 *        Module-level: epub.open(path, cache_dir) → book | nil, errcode, errmsg
 *
 *        Book methods (all on the userdata returned by epub.open):
 *          b:close()
 *          b:metadata()           → table
 *          b:manifest_count()     → int
 *          b:manifest_at(i)       → {id, href, media_type, properties}
 *          b:manifest_lookup(id)  → table | nil
 *          b:spine_count()        → int
 *          b:spine_at(i)          → {idref, href, media_type, linear}
 *          b:toc()                → tree of {label, href, depth, children = {...}}
 *          b:resolve_href(base, href) → {spine_index, fragment} | nil
 *          b:read_item(href)      → string | nil, errmsg
 *          b:item_size(href)      → int
 *          b:cumulative_size(spine_index) → int
 *          b:cache_dir()          → string
 *          b:path()               → string
 *
 * @status Phase 9.A.3
 * @issues None
 * @todo Phase 9.B will add b:open_chapter(spine_index) returning a SAX
 *       iterator for chapter content.
 */

#include "api_epub.h"
#include "lauxlib.h"

#include "epub.h"
#include "logging.h"

#include <string.h>

#define EPUB_BOOK_MT  "crosslua.epub.book"

typedef struct {
    epub_book_t *book;  /* NULL after close */
} book_ud_t;

/* ── Helpers ────────────────────────────────────────────────────── */

static book_ud_t *check_book_ud(lua_State *L, int idx) {
    return (book_ud_t *)luaL_checkudata(L, idx, EPUB_BOOK_MT);
}

static epub_book_t *check_book(lua_State *L, int idx) {
    book_ud_t *u = check_book_ud(L, idx);
    if (!u->book) luaL_error(L, "epub book is closed");
    return u->book;
}

static const char *err_to_string(epub_err_t e) {
    switch (e) {
        case EPUB_OK:                       return "ok";
        case EPUB_ERR_IO:                   return "io_error";
        case EPUB_ERR_NOT_EPUB:             return "not_epub";
        case EPUB_ERR_DRM:                  return "drm";
        case EPUB_ERR_MALFORMED_CONTAINER:  return "malformed_container";
        case EPUB_ERR_MALFORMED_OPF:        return "malformed_opf";
        case EPUB_ERR_NO_SPINE:             return "no_spine";
        case EPUB_ERR_FIXED_LAYOUT:         return "fixed_layout";
        case EPUB_ERR_OOM:                  return "oom";
        default:                            return "unknown";
    }
}

static void push_str_or_nil(lua_State *L, const char *s) {
    if (s) lua_pushstring(L, s);
    else   lua_pushnil(L);
}

/* ── Module functions ───────────────────────────────────────────── */

/* epub.open(path, cache_dir = nil) → book | nil, errcode, errmsg */
static int l_epub_open(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    const char *cache_dir = luaL_optstring(L, 2, "/cache/epub");

    epub_err_t err = EPUB_OK;
    epub_book_t *book = epub_open(path, cache_dir, &err);
    if (!book) {
        lua_pushnil(L);
        lua_pushstring(L, err_to_string(err));
        lua_pushfstring(L, "epub.open failed: %s", err_to_string(err));
        return 3;
    }

    book_ud_t *u = (book_ud_t *)lua_newuserdata(L, sizeof(book_ud_t));
    u->book = book;
    luaL_setmetatable(L, EPUB_BOOK_MT);
    return 1;
}

/* b:close() — idempotent */
static int l_book_close(lua_State *L) {
    book_ud_t *u = check_book_ud(L, 1);
    if (u->book) {
        epub_close(u->book);
        u->book = NULL;
    }
    return 0;
}

static int l_book_gc(lua_State *L) {
    book_ud_t *u = (book_ud_t *)lua_touserdata(L, 1);
    if (u && u->book) {
        epub_close(u->book);
        u->book = NULL;
    }
    return 0;
}

/* b:metadata() → table */
static int l_book_metadata(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const epub_metadata_t *m = epub_metadata(book);

    lua_createtable(L, 0, 12);

#define SET_STR(key, val) do { push_str_or_nil(L, val); lua_setfield(L, -2, key); } while (0)
    SET_STR("title",          m->title);
    SET_STR("author",         m->author);
    SET_STR("language",       m->language);
    SET_STR("identifier",     m->identifier);
    SET_STR("modified",       m->modified);
    SET_STR("publisher",      m->publisher);
    SET_STR("date_published", m->date_published);
    SET_STR("description",    m->description);
    SET_STR("cover_id",       m->cover_id);
#undef SET_STR

    lua_pushinteger(L, m->version);
    lua_setfield(L, -2, "epub_version");

    lua_pushstring(L, m->page_progression_direction == EPUB_PPD_RTL ? "rtl" : "ltr");
    lua_setfield(L, -2, "page_progression_direction");

    return 1;
}

/* b:manifest_count() → int */
static int l_book_manifest_count(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    lua_pushinteger(L, epub_manifest_count(book));
    return 1;
}

static void push_manifest_item(lua_State *L, const epub_manifest_item_t *m) {
    lua_createtable(L, 0, 4);
    push_str_or_nil(L, m->id);          lua_setfield(L, -2, "id");
    push_str_or_nil(L, m->href);        lua_setfield(L, -2, "href");
    push_str_or_nil(L, m->media_type);  lua_setfield(L, -2, "media_type");
    push_str_or_nil(L, m->properties);  lua_setfield(L, -2, "properties");
}

/* b:manifest_at(i) — 1-based index for Lua convention */
static int l_book_manifest_at(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    int i = (int)luaL_checkinteger(L, 2) - 1;
    const epub_manifest_item_t *m = epub_manifest_at(book, (uint16_t)i);
    if (!m) { lua_pushnil(L); return 1; }
    push_manifest_item(L, m);
    return 1;
}

/* b:manifest_lookup(id) */
static int l_book_manifest_lookup(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const char *id = luaL_checkstring(L, 2);
    const epub_manifest_item_t *m = epub_manifest_lookup(book, id);
    if (!m) { lua_pushnil(L); return 1; }
    push_manifest_item(L, m);
    return 1;
}

/* b:spine_count() */
static int l_book_spine_count(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    lua_pushinteger(L, epub_spine_count(book));
    return 1;
}

/* b:spine_at(i) — 1-based */
static int l_book_spine_at(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    int i = (int)luaL_checkinteger(L, 2) - 1;
    const epub_spine_entry_t *s = epub_spine_at(book, (uint16_t)i);
    if (!s) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 4);
    push_str_or_nil(L, s->idref);       lua_setfield(L, -2, "idref");
    push_str_or_nil(L, s->href);        lua_setfield(L, -2, "href");
    push_str_or_nil(L, s->media_type);  lua_setfield(L, -2, "media_type");
    lua_pushboolean(L, s->linear);      lua_setfield(L, -2, "linear");
    return 1;
}

/* Recursively push a TOC subtree as a Lua table. */
static void push_toc_node(lua_State *L, const epub_toc_node_t *n) {
    lua_createtable(L, 0, 4);
    push_str_or_nil(L, n->label);  lua_setfield(L, -2, "label");
    push_str_or_nil(L, n->href);   lua_setfield(L, -2, "href");
    lua_pushinteger(L, n->depth);  lua_setfield(L, -2, "depth");

    if (n->children) {
        lua_newtable(L);
        int idx = 1;
        for (const epub_toc_node_t *c = n->children; c; c = c->next) {
            push_toc_node(L, c);
            lua_rawseti(L, -2, idx++);
        }
        lua_setfield(L, -2, "children");
    }
}

/* b:toc() — array of top-level nodes */
static int l_book_toc(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const epub_toc_node_t *root = epub_toc_root(book);
    lua_newtable(L);
    int idx = 1;
    for (const epub_toc_node_t *c = root; c; c = c->next) {
        push_toc_node(L, c);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

/* b:resolve_href(base_href, href) → spine_index (1-based), fragment | nil */
static int l_book_resolve_href(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const char *base = luaL_optstring(L, 2, NULL);
    const char *href = luaL_checkstring(L, 3);

    char fragment[64];
    int idx = epub_resolve_href(book, base, href, fragment, sizeof(fragment));
    if (idx < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, idx + 1);  /* 1-based for Lua */
    if (fragment[0]) lua_pushstring(L, fragment);
    else             lua_pushnil(L);
    return 2;
}

/* b:read_item(href) → string | nil, errmsg */
static int l_book_read_item(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const char *href = luaL_checkstring(L, 2);
    uint32_t sz = epub_item_size(book, href);

    if (sz == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "item not found or empty");
        return 2;
    }
    if (sz > 256 * 1024) {
        lua_pushnil(L);
        lua_pushfstring(L, "item too large for read_item (%u bytes)", sz);
        return 2;
    }

    char *buf = (char *)lua_newuserdata(L, sz);
    int n = epub_read_item(book, href, buf, sz);
    if (n < 0) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "read error");
        return 2;
    }
    lua_pushlstring(L, buf, (size_t)n);
    lua_remove(L, -2);
    return 1;
}

/* b:item_size(href) */
static int l_book_item_size(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    const char *href = luaL_checkstring(L, 2);
    lua_pushinteger(L, (lua_Integer)epub_item_size(book, href));
    return 1;
}

/* b:cumulative_size(spine_index) — 1-based */
static int l_book_cumulative_size(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    int i = (int)luaL_checkinteger(L, 2) - 1;
    if (i < 0) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, (lua_Integer)epub_cumulative_size(book, (uint16_t)i));
    return 1;
}

/* b:cache_dir() / b:path() */
static int l_book_cache_dir(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    push_str_or_nil(L, epub_cache_dir(book));
    return 1;
}
static int l_book_path(lua_State *L) {
    epub_book_t *book = check_book(L, 1);
    push_str_or_nil(L, epub_path(book));
    return 1;
}

/* ── Registration ───────────────────────────────────────────────── */

static const luaL_Reg book_methods[] = {
    {"close",            l_book_close},
    {"metadata",         l_book_metadata},
    {"manifest_count",   l_book_manifest_count},
    {"manifest_at",      l_book_manifest_at},
    {"manifest_lookup",  l_book_manifest_lookup},
    {"spine_count",      l_book_spine_count},
    {"spine_at",         l_book_spine_at},
    {"toc",              l_book_toc},
    {"resolve_href",     l_book_resolve_href},
    {"read_item",        l_book_read_item},
    {"item_size",        l_book_item_size},
    {"cumulative_size",  l_book_cumulative_size},
    {"cache_dir",        l_book_cache_dir},
    {"path",             l_book_path},
    {NULL, NULL},
};

static const luaL_Reg epub_module[] = {
    {"open", l_epub_open},
    {NULL, NULL},
};

void api_epub_register(lua_State *L) {
    if (luaL_newmetatable(L, EPUB_BOOK_MT)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, book_methods, 0);
        lua_pushcfunction(L, l_book_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);

    luaL_newlib(L, epub_module);
    lua_setglobal(L, "epub");
}
