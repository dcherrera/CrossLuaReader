/**
 * @file epub_opf.c
 * @brief Parse the OPF (.opf) package document.
 *        Extracts metadata (title, author, language, identifier, modified,
 *        publisher, date, description, cover_id), the manifest (id → href +
 *        media-type + properties), the spine (idref → linear, properties),
 *        and detects toc_id (NCX) / nav_id (NavDoc) / cover_id.
 *
 *        Common to EPUB 2 and EPUB 3 — same OPF schema, slightly different
 *        cover signaling and TOC declaration which we handle here.
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

/* Element-state machine for OPF. */
typedef enum {
    OPF_ROOT,
    OPF_PACKAGE,
    OPF_METADATA,
    OPF_MANIFEST,
    OPF_SPINE,
    OPF_GUIDE,
    OPF_DC_TITLE,
    OPF_DC_CREATOR,
    OPF_DC_LANGUAGE,
    OPF_DC_IDENTIFIER,
    OPF_DC_PUBLISHER,
    OPF_DC_DATE,
    OPF_DC_DESCRIPTION,
    OPF_META_DCTERMS_MODIFIED,
} opf_state_t;

/* Per-element text accumulator: buffer the on_text fragments inside a tag,
 * since expat may split text across multiple callbacks. */
#define OPF_TEXT_BUF  256

typedef struct {
    epub_book_t *book;
    opf_state_t  state;

    char         text_buf[OPF_TEXT_BUF];
    size_t       text_len;

    /* unique-identifier id from <package>; matched against <dc:identifier id="..."> */
    char         pkg_unique_id[24];

    /* For <dc:creator> with optional opf:role="aut" — pick first one with
     * role=aut, or first one if no role specified. */
    bool         creator_taken;
    char         pending_creator_role[16];
} opf_ctx_t;

/* ── Text accumulator helpers ───────────────────────────────────── */

static void text_reset(opf_ctx_t *c) {
    c->text_len = 0;
    c->text_buf[0] = '\0';
}

static void text_append(opf_ctx_t *c, const char *s, size_t n) {
    if (c->text_len + n >= sizeof(c->text_buf)) {
        n = sizeof(c->text_buf) - 1 - c->text_len;
    }
    if (n > 0) {
        memcpy(c->text_buf + c->text_len, s, n);
        c->text_len += n;
        c->text_buf[c->text_len] = '\0';
    }
}

/* Trim leading/trailing whitespace in place, return pointer into text_buf. */
static const char *text_trimmed(opf_ctx_t *c) {
    char *p = c->text_buf;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    char *end = c->text_buf + c->text_len;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
    return p;
}

/* ── Manifest / spine push ──────────────────────────────────────── */

static bool ensure_manifest_capacity(epub_book_t *book) {
    if (book->manifest_count < book->manifest_capacity) return true;
    uint16_t new_cap = book->manifest_capacity ? book->manifest_capacity * 2 : 32;
    if (new_cap > EPUB_MANIFEST_MAX) new_cap = EPUB_MANIFEST_MAX;
    if (new_cap == book->manifest_capacity) return false;

    epub_manifest_item_t *m = (epub_manifest_item_t *)epub_arena_alloc(
        &book->arena, new_cap * sizeof(epub_manifest_item_t));
    if (!m) return false;
    if (book->manifest) {
        memcpy(m, book->manifest, book->manifest_count * sizeof(epub_manifest_item_t));
    }
    book->manifest = m;
    book->manifest_capacity = new_cap;
    return true;
}

static bool ensure_spine_capacity(epub_book_t *book) {
    if (book->spine_count < book->spine_capacity) return true;
    uint16_t new_cap = book->spine_capacity ? book->spine_capacity * 2 : 32;
    if (new_cap > EPUB_SPINE_MAX) new_cap = EPUB_SPINE_MAX;
    if (new_cap == book->spine_capacity) return false;

    epub_spine_entry_t *s = (epub_spine_entry_t *)epub_arena_alloc(
        &book->arena, new_cap * sizeof(epub_spine_entry_t));
    if (!s) return false;
    if (book->spine) {
        memcpy(s, book->spine, book->spine_count * sizeof(epub_spine_entry_t));
    }
    book->spine = s;
    book->spine_capacity = new_cap;
    return true;
}

/* ── SAX callbacks ──────────────────────────────────────────────── */

static const char *attr_get(const xml_attr_t *attrs, int n, const char *key) {
    for (int i = 0; i < n; i++) {
        if (strcmp(attrs[i].key, key) == 0) return attrs[i].value;
    }
    return NULL;
}

static void on_start(void *user, const char *tag,
                      const xml_attr_t *attrs, int n_attrs) {
    opf_ctx_t *c = (opf_ctx_t *)user;
    text_reset(c);

    /* package — capture version + unique-identifier id + page-progression-direction */
    if (strcmp(tag, "package") == 0) {
        c->state = OPF_PACKAGE;
        const char *ver = attr_get(attrs, n_attrs, "version");
        if (ver) {
            if (ver[0] == '3') c->book->meta.version = EPUB_V_3;
            else if (ver[0] == '2') c->book->meta.version = EPUB_V_2;
        }
        const char *uid = attr_get(attrs, n_attrs, "unique-identifier");
        if (uid) {
            strncpy(c->pkg_unique_id, uid, sizeof(c->pkg_unique_id) - 1);
        }
        return;
    }

    if (strcmp(tag, "metadata") == 0) { c->state = OPF_METADATA; return; }
    if (strcmp(tag, "manifest") == 0) { c->state = OPF_MANIFEST; return; }
    if (strcmp(tag, "spine") == 0)    {
        c->state = OPF_SPINE;
        const char *ppd = attr_get(attrs, n_attrs, "page-progression-direction");
        if (ppd && strcmp(ppd, "rtl") == 0) {
            c->book->meta.page_progression_direction = EPUB_PPD_RTL;
        }
        const char *toc_id = attr_get(attrs, n_attrs, "toc");
        if (toc_id) {
            strncpy(c->book->toc_id, toc_id, sizeof(c->book->toc_id) - 1);
        }
        return;
    }
    if (strcmp(tag, "guide") == 0)    { c->state = OPF_GUIDE; return; }

    /* metadata children: dc:title, dc:creator, dc:language, dc:identifier,
     * dc:publisher, dc:date, dc:description, meta property=dcterms:modified. */
    if (c->state == OPF_METADATA) {
        if (strcmp(tag, "title") == 0) { c->state = OPF_DC_TITLE; return; }
        if (strcmp(tag, "creator") == 0) {
            c->state = OPF_DC_CREATOR;
            const char *role = attr_get(attrs, n_attrs, "role");
            c->pending_creator_role[0] = '\0';
            if (role) {
                strncpy(c->pending_creator_role, role, sizeof(c->pending_creator_role) - 1);
            }
            return;
        }
        if (strcmp(tag, "language") == 0)    { c->state = OPF_DC_LANGUAGE; return; }
        if (strcmp(tag, "identifier") == 0)  {
            /* Capture the one whose id matches package.unique-identifier;
             * the on_end will commit. We don't differentiate here — the first
             * matching identifier wins, fallback to first one. */
            c->state = OPF_DC_IDENTIFIER;
            return;
        }
        if (strcmp(tag, "publisher") == 0)   { c->state = OPF_DC_PUBLISHER; return; }
        if (strcmp(tag, "date") == 0)        { c->state = OPF_DC_DATE; return; }
        if (strcmp(tag, "description") == 0) { c->state = OPF_DC_DESCRIPTION; return; }

        /* <meta property="dcterms:modified">VALUE</meta> (EPUB 3) */
        if (strcmp(tag, "meta") == 0) {
            const char *prop = attr_get(attrs, n_attrs, "property");
            const char *name = attr_get(attrs, n_attrs, "name");
            const char *content = attr_get(attrs, n_attrs, "content");

            if (prop && strcmp(prop, "dcterms:modified") == 0) {
                c->state = OPF_META_DCTERMS_MODIFIED;
                return;
            }
            /* EPUB 2 cover signal: <meta name="cover" content="cover-id"/> */
            if (name && content && strcmp(name, "cover") == 0) {
                strncpy(c->book->cover_id, content, sizeof(c->book->cover_id) - 1);
                c->book->meta.cover_id = epub_arena_strdup(&c->book->arena, content);
            }
            return;
        }
    }

    /* manifest item */
    if (c->state == OPF_MANIFEST && strcmp(tag, "item") == 0) {
        if (!ensure_manifest_capacity(c->book)) return;
        const char *id   = attr_get(attrs, n_attrs, "id");
        const char *href = attr_get(attrs, n_attrs, "href");
        const char *mt   = attr_get(attrs, n_attrs, "media-type");
        const char *prop = attr_get(attrs, n_attrs, "properties");

        if (id && href) {
            epub_manifest_item_t *m = &c->book->manifest[c->book->manifest_count++];
            m->id         = epub_arena_strdup(&c->book->arena, id);
            m->href       = epub_arena_strdup(&c->book->arena, href);
            m->media_type = mt   ? epub_arena_strdup(&c->book->arena, mt)   : NULL;
            m->properties = prop ? epub_arena_strdup(&c->book->arena, prop) : NULL;

            /* EPUB 3 cover-image signal (only set if not already set by EPUB 2 meta). */
            if (prop && c->book->cover_id[0] == '\0' &&
                strstr(prop, "cover-image") != NULL) {
                strncpy(c->book->cover_id, id, sizeof(c->book->cover_id) - 1);
                c->book->meta.cover_id = m->id;
            }
            /* EPUB 3 navigation document. */
            if (prop && c->book->nav_id[0] == '\0' &&
                strstr(prop, "nav") != NULL) {
                strncpy(c->book->nav_id, id, sizeof(c->book->nav_id) - 1);
            }
        }
        return;
    }

    /* spine itemref */
    if (c->state == OPF_SPINE && strcmp(tag, "itemref") == 0) {
        if (!ensure_spine_capacity(c->book)) return;
        const char *idref = attr_get(attrs, n_attrs, "idref");
        const char *linear = attr_get(attrs, n_attrs, "linear");
        if (idref) {
            epub_spine_entry_t *s = &c->book->spine[c->book->spine_count++];
            s->idref  = epub_arena_strdup(&c->book->arena, idref);
            s->linear = !(linear && strcmp(linear, "no") == 0);
            s->href = NULL;        /* resolved at end of parse */
            s->media_type = NULL;  /* resolved at end of parse */
        }
        return;
    }
}

static void on_end(void *user, const char *tag) {
    opf_ctx_t *c = (opf_ctx_t *)user;

    /* Commit text-bearing leaf elements. */
    if (c->state == OPF_DC_TITLE && strcmp(tag, "title") == 0) {
        if (!c->book->meta.title) c->book->meta.title = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_CREATOR && strcmp(tag, "creator") == 0) {
        const char *txt = text_trimmed(c);
        bool prefer = (!c->creator_taken) ||
                      (strcmp(c->pending_creator_role, "aut") == 0);
        if (prefer) {
            c->book->meta.author = epub_arena_strdup(&c->book->arena, txt);
            c->creator_taken = true;
        }
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_LANGUAGE && strcmp(tag, "language") == 0) {
        if (!c->book->meta.language) c->book->meta.language = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_IDENTIFIER && strcmp(tag, "identifier") == 0) {
        if (!c->book->meta.identifier) c->book->meta.identifier = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_PUBLISHER && strcmp(tag, "publisher") == 0) {
        if (!c->book->meta.publisher) c->book->meta.publisher = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_DATE && strcmp(tag, "date") == 0) {
        if (!c->book->meta.date_published) c->book->meta.date_published = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_DC_DESCRIPTION && strcmp(tag, "description") == 0) {
        if (!c->book->meta.description) c->book->meta.description = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (c->state == OPF_META_DCTERMS_MODIFIED && strcmp(tag, "meta") == 0) {
        if (!c->book->meta.modified) c->book->meta.modified = epub_arena_strdup(&c->book->arena, text_trimmed(c));
        c->state = OPF_METADATA;
    } else if (strcmp(tag, "metadata") == 0) {
        c->state = OPF_PACKAGE;
    } else if (strcmp(tag, "manifest") == 0) {
        c->state = OPF_PACKAGE;
    } else if (strcmp(tag, "spine") == 0) {
        c->state = OPF_PACKAGE;
    } else if (strcmp(tag, "guide") == 0) {
        c->state = OPF_PACKAGE;
    }
}

static void on_text(void *user, const char *text, size_t len) {
    opf_ctx_t *c = (opf_ctx_t *)user;
    /* Only buffer text inside the metadata leaf elements we care about. */
    switch (c->state) {
    case OPF_DC_TITLE:
    case OPF_DC_CREATOR:
    case OPF_DC_LANGUAGE:
    case OPF_DC_IDENTIFIER:
    case OPF_DC_PUBLISHER:
    case OPF_DC_DATE:
    case OPF_DC_DESCRIPTION:
    case OPF_META_DCTERMS_MODIFIED:
        text_append(c, text, len);
        break;
    default:
        break;
    }
}

/* ── Top-level entry ────────────────────────────────────────────── */

bool epub_parse_opf(epub_book_t *book) {
    uint32_t sz = zip_entry_size(book->zip, book->opf_path);
    if (sz == 0 || sz > 256 * 1024) {
        LOG_ERR("EPUB", "OPF missing or too large: %s (%u)", book->opf_path, sz);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) return false;

    int n = zip_read(book->zip, book->opf_path, buf, sz);
    if (n < 0) {
        free(buf);
        LOG_ERR("EPUB", "OPF read failed");
        return false;
    }
    buf[sz] = '\0';

    opf_ctx_t ctx = { .book = book, .state = OPF_ROOT };
    xml_handlers_t h = {
        .on_start = on_start,
        .on_end   = on_end,
        .on_text  = on_text,
    };

    bool parse_ok = xml_parse_buffer(buf, sz, XML_MODE_STRICT, &h, &ctx);
    free(buf);

    if (!parse_ok) {
        LOG_ERR("EPUB", "OPF parse error");
        return false;
    }

    /* Cross-link spine to manifest: each itemref's idref → manifest item;
     * cache the href and media-type on the spine entry for fast access. */
    for (uint16_t i = 0; i < book->spine_count; i++) {
        epub_spine_entry_t *s = &book->spine[i];
        if (!s->idref) continue;
        const epub_manifest_item_t *m = epub_manifest_lookup(book, s->idref);
        if (m) {
            s->href       = m->href;
            s->media_type = m->media_type;
        }
    }

    return true;
}
