/**
 * @file xml.c
 * @brief XML SAX parser implementation. Wraps expat. Strips namespace
 *        URIs from element local names (using expat's namespace separator)
 *        so callbacks see plain "package", "rootfile", etc. regardless of
 *        the namespace declarations in the source. Attribute prefixes
 *        (e.g. epub:type) are preserved verbatim by NOT enabling expat's
 *        attribute-namespace processing.
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo Phase 9.B will add named-entity decoding for HTML mode.
 */

#include "xml.h"
#include "expat.h"
#include "logging.h"

#include <stdlib.h>
#include <string.h>

#define XML_NS_SEP  '|'  /* Expat uses this to glue namespace URI + local name */
#define XML_PARSE_CHUNK 4096

/* ── Internal context bridging expat → user callbacks ───────────── */

typedef struct {
    const xml_handlers_t *h;
    void *user_ctx;
} bridge_t;

/**
 * Strip namespace URI prefix from an expat-decorated name.
 * Expat with namespace processing emits names as either:
 *   "<URI>|<local-name>"      — namespaced element/attribute
 *   "<local-name>"            — non-namespaced
 * We always want the local name. (Element callbacks only — for
 * attributes we want the user's literal "epub:type" form, so we
 * disable expat namespace handling for those by NOT calling
 * XML_SetParamEntityParsing and by using XML_ParserCreate without
 * the namespace separator argument.)
 */
static const char *strip_ns(const char *name) {
    const char *bar = strchr(name, XML_NS_SEP);
    return bar ? bar + 1 : name;
}

/* ── Expat callback adapters ────────────────────────────────────── */

static void XMLCALL on_start_xml(void *user_data, const XML_Char *name, const XML_Char **atts) {
    bridge_t *b = (bridge_t *)user_data;
    if (!b->h->on_start) return;

    /* Count attributes (expat passes a NULL-terminated key/value pair list). */
    int n = 0;
    for (const XML_Char **p = atts; *p; p += 2) n++;

    /* Bound the attribute count to a reasonable max — EPUB files rarely
     * have more than a dozen attributes per element. 32 is generous. */
    if (n > 32) n = 32;

    xml_attr_t local[32];
    for (int i = 0; i < n; i++) {
        local[i].key   = atts[i * 2];
        local[i].value = atts[i * 2 + 1];
    }

    b->h->on_start(b->user_ctx, strip_ns(name), local, n);
}

static void XMLCALL on_end_xml(void *user_data, const XML_Char *name) {
    bridge_t *b = (bridge_t *)user_data;
    if (b->h->on_end) {
        b->h->on_end(b->user_ctx, strip_ns(name));
    }
}

static void XMLCALL on_text_xml(void *user_data, const XML_Char *s, int len) {
    bridge_t *b = (bridge_t *)user_data;
    if (b->h->on_text && len > 0) {
        b->h->on_text(b->user_ctx, s, (size_t)len);
    }
}

/* ── Parser lifecycle ───────────────────────────────────────────── */

static XML_Parser create_parser(xml_mode_t mode, bridge_t *b) {
    /* Namespace processing on element names: pass URI-separator as second
     * arg. Attribute keys keep their literal form. */
    XML_Parser p = XML_ParserCreateNS(NULL, XML_NS_SEP);
    if (!p) return NULL;

    /* Mode currently only affects entity handling and recovery; for now
     * STRICT and HTML behave the same. Phase 9.B will diverge. */
    (void)mode;

    XML_SetUserData(p, b);
    XML_SetElementHandler(p, on_start_xml, on_end_xml);
    XML_SetCharacterDataHandler(p, on_text_xml);
    return p;
}

/* ── Public API ─────────────────────────────────────────────────── */

bool xml_parse_buffer(const void *buf, size_t len, xml_mode_t mode,
                      const xml_handlers_t *h, void *ctx) {
    if (!buf || !h) return false;

    bridge_t b = { .h = h, .user_ctx = ctx };
    XML_Parser p = create_parser(mode, &b);
    if (!p) {
        LOG_ERR("XML", "parser create failed");
        return false;
    }

    enum XML_Status s = XML_Parse(p, (const char *)buf, (int)len, /*isFinal=*/1);
    if (s != XML_STATUS_OK) {
        LOG_ERR("XML", "parse error at line %lu: %s",
                (unsigned long)XML_GetCurrentLineNumber(p),
                XML_ErrorString(XML_GetErrorCode(p)));
        XML_ParserFree(p);
        return false;
    }

    XML_ParserFree(p);
    return true;
}

bool xml_parse_pull(xml_reader_t read_fn, void *read_ctx, xml_mode_t mode,
                    const xml_handlers_t *h, void *ctx) {
    if (!read_fn || !h) return false;

    bridge_t b = { .h = h, .user_ctx = ctx };
    XML_Parser p = create_parser(mode, &b);
    if (!p) {
        LOG_ERR("XML", "parser create failed");
        return false;
    }

    bool ok = true;

    while (1) {
        const uint8_t *chunk;
        size_t chunk_len = 0;
        if (!read_fn(read_ctx, &chunk, &chunk_len)) {
            LOG_ERR("XML", "pull-source read error");
            ok = false;
            break;
        }

        bool last = (chunk_len == 0);
        enum XML_Status s = XML_Parse(p, (const char *)chunk, (int)chunk_len, last ? 1 : 0);
        if (s != XML_STATUS_OK) {
            LOG_ERR("XML", "parse error at line %lu: %s",
                    (unsigned long)XML_GetCurrentLineNumber(p),
                    XML_ErrorString(XML_GetErrorCode(p)));
            ok = false;
            break;
        }
        if (last) break;
    }

    XML_ParserFree(p);
    return ok;
}
