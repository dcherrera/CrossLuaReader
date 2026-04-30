/**
 * @file epub_toc.c
 * @brief Parse the EPUB table of contents.
 *
 *        Two forms are supported and normalized to the same tree:
 *          - NCX (EPUB 2): toc.ncx file with <navMap><navPoint>...
 *          - NavDoc (EPUB 3): nav.xhtml with <nav epub:type="toc"><ol><li><a>
 *
 *        Selection: NavDoc preferred when book->nav_id is set (EPUB 3),
 *        otherwise NCX via book->toc_id. If neither is present the book
 *        opens with no TOC tree (toc_root = NULL) — readers fall back to
 *        spine-only navigation.
 *
 * @status Phase 9.A.2
 * @issues None
 * @todo None
 */

#include "epub_internal.h"
#include "xml.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

#define TOC_MAX_DEPTH       8
#define TOC_TEXT_BUF      256
#define TOC_HREF_BUF      128

/* ── Tree-builder utilities (shared by NCX + NavDoc) ────────────── */

typedef struct {
    epub_book_t *book;

    /* Stack of current parents at each depth. parents[0] is a synthetic
     * root holder — its .next chain becomes book->toc_root. */
    epub_toc_node_t *parents[TOC_MAX_DEPTH];

    /* Node currently being built (between begin_node and commit). */
    epub_toc_node_t *current;
    uint8_t          current_depth;

    /* Per-element text accumulator (XML text may arrive in fragments). */
    char    text_buf[TOC_TEXT_BUF];
    size_t  text_len;

    /* href captured from the last <content src=...> (NCX) or <a href=...> (NavDoc). */
    char    href_buf[TOC_HREF_BUF];
    bool    have_href;
} toc_builder_t;

static void text_reset(toc_builder_t *b) {
    b->text_len = 0;
    b->text_buf[0] = '\0';
}

static void text_append(toc_builder_t *b, const char *s, size_t n) {
    if (b->text_len + n >= sizeof(b->text_buf)) {
        n = sizeof(b->text_buf) - 1 - b->text_len;
    }
    if (n > 0) {
        memcpy(b->text_buf + b->text_len, s, n);
        b->text_len += n;
        b->text_buf[b->text_len] = '\0';
    }
}

static const char *attr_get(const xml_attr_t *attrs, int n, const char *key) {
    for (int i = 0; i < n; i++) {
        if (strcmp(attrs[i].key, key) == 0) return attrs[i].value;
    }
    return NULL;
}

static epub_toc_node_t *new_node(epub_book_t *book) {
    epub_toc_node_t *n = (epub_toc_node_t *)epub_arena_alloc(&book->arena, sizeof(epub_toc_node_t));
    if (!n) return NULL;
    n->label = NULL;
    n->href = NULL;
    n->children = NULL;
    n->next = NULL;
    n->depth = 0;
    return n;
}

static void begin_node(toc_builder_t *b, uint8_t depth) {
    epub_toc_node_t *n = new_node(b->book);
    if (!n) return;

    n->depth = depth;
    b->current = n;
    b->current_depth = depth;
    text_reset(b);
    b->href_buf[0] = '\0';
    b->have_href = false;

    /* Attach as last child of parents[depth-1]. */
    if (depth == 0 || depth >= TOC_MAX_DEPTH) return;
    epub_toc_node_t *parent = b->parents[depth - 1];
    if (!parent) return;

    if (!parent->children) {
        parent->children = n;
    } else {
        epub_toc_node_t *tail = parent->children;
        while (tail->next) tail = tail->next;
        tail->next = n;
    }
    b->parents[depth] = n;
}

static void commit_node(toc_builder_t *b) {
    if (!b->current) return;

    /* Trim accumulated label text. */
    char *p = b->text_buf;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    char *end = b->text_buf + b->text_len;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';

    if (*p) b->current->label = epub_arena_strdup(&b->book->arena, p);
    if (b->have_href && b->href_buf[0]) {
        b->current->href = epub_arena_strdup(&b->book->arena, b->href_buf);
    }

    b->current = NULL;
    text_reset(b);
}

/* ── NCX parser ─────────────────────────────────────────────────── */

typedef struct {
    toc_builder_t builder;

    /* navPoint depth tracker (NCX nests navPoint inside navPoint). */
    uint8_t       nav_point_depth;

    /* True while we are inside <navLabel><text>. */
    bool          in_nav_label_text;
} ncx_ctx_t;

static void ncx_on_start(void *user, const char *tag,
                          const xml_attr_t *attrs, int n_attrs) {
    ncx_ctx_t *c = (ncx_ctx_t *)user;

    if (strcmp(tag, "navPoint") == 0) {
        c->nav_point_depth++;
        begin_node(&c->builder, c->nav_point_depth);
        return;
    }

    if (c->builder.current && strcmp(tag, "navLabel") == 0) return;

    if (c->builder.current && strcmp(tag, "text") == 0) {
        c->in_nav_label_text = true;
        return;
    }

    if (c->builder.current && strcmp(tag, "content") == 0) {
        const char *src = attr_get(attrs, n_attrs, "src");
        if (src) {
            strncpy(c->builder.href_buf, src, sizeof(c->builder.href_buf) - 1);
            c->builder.href_buf[sizeof(c->builder.href_buf) - 1] = '\0';
            c->builder.have_href = true;
        }
        return;
    }
}

static void ncx_on_end(void *user, const char *tag) {
    ncx_ctx_t *c = (ncx_ctx_t *)user;

    if (strcmp(tag, "text") == 0) {
        c->in_nav_label_text = false;
        return;
    }

    if (strcmp(tag, "navPoint") == 0) {
        commit_node(&c->builder);
        if (c->nav_point_depth > 0) c->nav_point_depth--;
        return;
    }
}

static void ncx_on_text(void *user, const char *text, size_t len) {
    ncx_ctx_t *c = (ncx_ctx_t *)user;
    if (c->in_nav_label_text) text_append(&c->builder, text, len);
}

/* ── NavDoc parser ──────────────────────────────────────────────── */

/* Track <ol> nesting depth and current <a>; tags <li> open a new node
 * at the current depth, <a> contributes href + text. */
typedef struct {
    toc_builder_t builder;

    /* Parsing only the first <nav epub:type="toc"> in the document. */
    bool          in_nav_toc;
    uint8_t       ol_depth;     /* 0 outside any <ol>, 1 = top-level, etc. */
    bool          in_anchor;    /* between <a> and </a> inside the TOC */
} nav_ctx_t;

static void nav_on_start(void *user, const char *tag,
                          const xml_attr_t *attrs, int n_attrs) {
    nav_ctx_t *c = (nav_ctx_t *)user;

    if (!c->in_nav_toc && strcmp(tag, "nav") == 0) {
        const char *etype = attr_get(attrs, n_attrs, "epub:type");
        if (etype && strcmp(etype, "toc") == 0) c->in_nav_toc = true;
        return;
    }

    if (!c->in_nav_toc) return;

    if (strcmp(tag, "ol") == 0) {
        c->ol_depth++;
        return;
    }

    if (strcmp(tag, "li") == 0) {
        begin_node(&c->builder, c->ol_depth);
        return;
    }

    if (strcmp(tag, "a") == 0 && c->builder.current) {
        const char *href = attr_get(attrs, n_attrs, "href");
        if (href) {
            strncpy(c->builder.href_buf, href, sizeof(c->builder.href_buf) - 1);
            c->builder.href_buf[sizeof(c->builder.href_buf) - 1] = '\0';
            c->builder.have_href = true;
        }
        c->in_anchor = true;
        return;
    }
}

static void nav_on_end(void *user, const char *tag) {
    nav_ctx_t *c = (nav_ctx_t *)user;

    if (!c->in_nav_toc) return;

    if (strcmp(tag, "a") == 0) {
        c->in_anchor = false;
        return;
    }

    if (strcmp(tag, "li") == 0) {
        commit_node(&c->builder);
        return;
    }

    if (strcmp(tag, "ol") == 0) {
        if (c->ol_depth > 0) c->ol_depth--;
        return;
    }

    if (strcmp(tag, "nav") == 0) {
        c->in_nav_toc = false;
        return;
    }
}

static void nav_on_text(void *user, const char *text, size_t len) {
    nav_ctx_t *c = (nav_ctx_t *)user;
    if (c->in_anchor) text_append(&c->builder, text, len);
}

/* ── Top-level dispatcher ───────────────────────────────────────── */

static bool parse_ncx_file(epub_book_t *book, const char *href) {
    char fullpath[80];
    epub_resolve_archive_path(book->opf_base, href, fullpath, sizeof(fullpath));

    uint32_t sz = zip_entry_size(book->zip, fullpath);
    if (sz == 0 || sz > 256 * 1024) {
        LOG_ERR("EPUB", "NCX missing/too-large: %s (%u)", fullpath, sz);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) return false;
    int n = zip_read(book->zip, fullpath, buf, sz);
    if (n < 0) { free(buf); return false; }
    buf[sz] = '\0';

    ncx_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.builder.book = book;
    /* Synthesize root holder at depth 0. parents[0] holds the chain that
     * becomes book->toc_root. */
    epub_toc_node_t *root_holder = new_node(book);
    if (!root_holder) { free(buf); return false; }
    ctx.builder.parents[0] = root_holder;

    xml_handlers_t h = {
        .on_start = ncx_on_start,
        .on_end   = ncx_on_end,
        .on_text  = ncx_on_text,
    };

    bool ok = xml_parse_buffer(buf, sz, XML_MODE_STRICT, &h, &ctx);
    free(buf);
    if (!ok) {
        LOG_ERR("EPUB", "NCX parse error");
        return false;
    }

    book->toc_root = root_holder->children;
    return true;
}

static bool parse_navdoc_file(epub_book_t *book, const char *href) {
    char fullpath[80];
    epub_resolve_archive_path(book->opf_base, href, fullpath, sizeof(fullpath));

    uint32_t sz = zip_entry_size(book->zip, fullpath);
    if (sz == 0 || sz > 256 * 1024) {
        LOG_ERR("EPUB", "NavDoc missing/too-large: %s (%u)", fullpath, sz);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) return false;
    int n = zip_read(book->zip, fullpath, buf, sz);
    if (n < 0) { free(buf); return false; }
    buf[sz] = '\0';

    nav_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.builder.book = book;
    epub_toc_node_t *root_holder = new_node(book);
    if (!root_holder) { free(buf); return false; }
    ctx.builder.parents[0] = root_holder;

    xml_handlers_t h = {
        .on_start = nav_on_start,
        .on_end   = nav_on_end,
        .on_text  = nav_on_text,
    };

    bool ok = xml_parse_buffer(buf, sz, XML_MODE_HTML, &h, &ctx);
    free(buf);
    if (!ok) {
        LOG_ERR("EPUB", "NavDoc parse error");
        return false;
    }

    book->toc_root = root_holder->children;
    return true;
}

bool epub_parse_toc(epub_book_t *book) {
    /* Prefer NavDoc (EPUB 3) when available — it's the modern format. */
    if (book->nav_id[0]) {
        const epub_manifest_item_t *m = epub_manifest_lookup(book, book->nav_id);
        if (m && m->href) return parse_navdoc_file(book, m->href);
    }

    /* Fall back to NCX (EPUB 2 — also valid in EPUB 3 for back-compat). */
    if (book->toc_id[0]) {
        const epub_manifest_item_t *m = epub_manifest_lookup(book, book->toc_id);
        if (m && m->href) return parse_ncx_file(book, m->href);
    }

    /* No TOC available. Not an error. */
    return false;
}
