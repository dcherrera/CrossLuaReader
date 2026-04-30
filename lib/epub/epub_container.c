/**
 * @file epub_container.c
 * @brief Parse META-INF/container.xml. The OCF spec mandates this file
 *        as the entry point that points at the OPF package document.
 *        Picks the first <rootfile media-type="application/oebps-package+xml">.
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

#define CONTAINER_PATH  "META-INF/container.xml"

/* SAX state — we only care about the first matching <rootfile>. */
typedef struct {
    epub_book_t *book;
    bool         found;
} container_ctx_t;

static void on_start(void *user, const char *tag,
                      const xml_attr_t *attrs, int n_attrs) {
    container_ctx_t *c = (container_ctx_t *)user;
    if (c->found) return;
    if (strcmp(tag, "rootfile") != 0) return;

    const char *full_path = NULL;
    const char *media_type = NULL;

    for (int i = 0; i < n_attrs; i++) {
        if (strcmp(attrs[i].key, "full-path") == 0) {
            full_path = attrs[i].value;
        } else if (strcmp(attrs[i].key, "media-type") == 0) {
            media_type = attrs[i].value;
        }
    }

    /* Accept either: media-type explicitly set to OPF, or no media-type
     * declared (legacy/buggy producers). The first rootfile wins. */
    if (full_path &&
        (!media_type || strcmp(media_type, "application/oebps-package+xml") == 0)) {
        strncpy(c->book->opf_path, full_path, sizeof(c->book->opf_path) - 1);
        c->book->opf_path[sizeof(c->book->opf_path) - 1] = '\0';

        /* Derive the OPF base directory: everything up to and including
         * the last '/'. If there's no slash, base is empty. */
        const char *slash = strrchr(c->book->opf_path, '/');
        if (slash) {
            size_t base_len = (size_t)(slash - c->book->opf_path) + 1;
            if (base_len >= sizeof(c->book->opf_base)) base_len = sizeof(c->book->opf_base) - 1;
            memcpy(c->book->opf_base, c->book->opf_path, base_len);
            c->book->opf_base[base_len] = '\0';
        } else {
            c->book->opf_base[0] = '\0';
        }

        c->found = true;
    }
}

bool epub_parse_container(epub_book_t *book) {
    uint32_t sz = zip_entry_size(book->zip, CONTAINER_PATH);
    if (sz == 0 || sz > 8192) {
        LOG_ERR("EPUB", "%s missing or unreasonably large (%u)", CONTAINER_PATH, sz);
        return false;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) return false;

    int n = zip_read(book->zip, CONTAINER_PATH, buf, sz);
    if (n < 0) {
        free(buf);
        LOG_ERR("EPUB", "%s read failed", CONTAINER_PATH);
        return false;
    }
    buf[sz] = '\0';

    container_ctx_t ctx = { .book = book, .found = false };
    xml_handlers_t h = { .on_start = on_start };

    bool parse_ok = xml_parse_buffer(buf, sz, XML_MODE_STRICT, &h, &ctx);
    free(buf);

    if (!parse_ok) {
        LOG_ERR("EPUB", "container.xml parse error");
        return false;
    }
    if (!ctx.found) {
        LOG_ERR("EPUB", "container.xml has no rootfile");
        return false;
    }

    LOG_INF("EPUB", "OPF path: %s (base: '%s')", book->opf_path, book->opf_base);
    return true;
}
