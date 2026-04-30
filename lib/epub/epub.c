/**
 * @file epub.c
 * @brief Top-level EPUB book object — open, close, accessors. The parser
 *        work lives in the per-section files (epub_container.c, epub_opf.c,
 *        epub_toc.c) which all share epub_internal.h.
 *
 * @status Phase 9.A.2
 * @issues None
 * @todo None
 */

#include "epub.h"
#include "epub_internal.h"

#include "logging.h"

#include <stdlib.h>
#include <string.h>

#define EPUB_ARENA_CHUNK_DEFAULT  4096

/* ── Arena ──────────────────────────────────────────────────────── */

void *epub_arena_alloc(epub_arena_t *a, size_t n) {
    /* Round up to 4-byte alignment for safe pointer-typed slots. */
    n = (n + 3u) & ~(size_t)3u;

    epub_arena_chunk_t *c = a->head;
    if (!c || c->used + n > c->capacity) {
        size_t cap = (n > EPUB_ARENA_CHUNK_DEFAULT) ? n : EPUB_ARENA_CHUNK_DEFAULT;
        c = (epub_arena_chunk_t *)malloc(sizeof(epub_arena_chunk_t) + cap);
        if (!c) return NULL;
        c->next = a->head;
        c->used = 0;
        c->capacity = cap;
        a->head = c;
    }
    void *p = c->data + c->used;
    c->used += n;
    a->total_used += n;
    return p;
}

const char *epub_arena_strdup(epub_arena_t *a, const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *dst = (char *)epub_arena_alloc(a, len + 1);
    if (!dst) return NULL;
    memcpy(dst, src, len + 1);
    return dst;
}

const char *epub_arena_strndup(epub_arena_t *a, const char *src, size_t n) {
    if (!src) return NULL;
    char *dst = (char *)epub_arena_alloc(a, n + 1);
    if (!dst) return NULL;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return dst;
}

void epub_arena_free(epub_arena_t *a) {
    epub_arena_chunk_t *c = a->head;
    while (c) {
        epub_arena_chunk_t *next = c->next;
        free(c);
        c = next;
    }
    a->head = NULL;
    a->total_used = 0;
}

/* ── href helper ────────────────────────────────────────────────── */

void epub_resolve_archive_path(const char *opf_base, const char *href,
                               char *out_buf, size_t out_size) {
    if (out_size == 0) return;

    /* Absolute paths in OPF are relative to the OCF root. */
    if (href && href[0] == '/') {
        href++;  /* skip the leading slash */
        if (out_size > 0) {
            strncpy(out_buf, href, out_size - 1);
            out_buf[out_size - 1] = '\0';
        }
        return;
    }

    size_t base_len = opf_base ? strlen(opf_base) : 0;
    size_t href_len = href ? strlen(href) : 0;

    if (base_len + href_len + 1 > out_size) {
        /* Truncate gracefully — the caller should bound both inputs. */
        out_buf[0] = '\0';
        return;
    }

    if (base_len > 0) memcpy(out_buf, opf_base, base_len);
    if (href_len > 0) memcpy(out_buf + base_len, href, href_len);
    out_buf[base_len + href_len] = '\0';
}

/* ── Book lifecycle ─────────────────────────────────────────────── */

epub_book_t *epub_open(const char *path, const char *cache_dir, epub_err_t *out_err) {
    epub_err_t local_err;
    if (!out_err) out_err = &local_err;
    *out_err = EPUB_OK;

    if (!path) {
        *out_err = EPUB_ERR_IO;
        return NULL;
    }

    epub_book_t *book = (epub_book_t *)calloc(1, sizeof(epub_book_t));
    if (!book) {
        *out_err = EPUB_ERR_OOM;
        return NULL;
    }

    strncpy(book->path, path, sizeof(book->path) - 1);
    book->path[sizeof(book->path) - 1] = '\0';
    if (cache_dir) {
        strncpy(book->cache_dir, cache_dir, sizeof(book->cache_dir) - 1);
    }

    /* Open the archive with EPUB mimetype validation. */
    book->zip = zip_open(path, /*validate_epub_mimetype=*/true);
    if (!book->zip) {
        *out_err = EPUB_ERR_NOT_EPUB;  /* could be IO or wrong magic; conservative */
        epub_close(book);
        return NULL;
    }

    /* DRM check before any further work — reject early. */
    zip_drm_t drm = zip_drm_state(book->zip);
    if (drm == ZIP_DRM_PROTECTED) {
        LOG_ERR("EPUB", "DRM-protected book: %s", path);
        *out_err = EPUB_ERR_DRM;
        epub_close(book);
        return NULL;
    }
    /* OBFUSCATION is acceptable — book opens, fonts may be obfuscated. */

    if (!epub_parse_container(book)) {
        *out_err = EPUB_ERR_MALFORMED_CONTAINER;
        epub_close(book);
        return NULL;
    }

    if (!epub_parse_opf(book)) {
        *out_err = EPUB_ERR_MALFORMED_OPF;
        epub_close(book);
        return NULL;
    }

    if (book->spine_count == 0) {
        *out_err = EPUB_ERR_NO_SPINE;
        epub_close(book);
        return NULL;
    }

    /* TOC is optional — parse failures here only log; the book still opens. */
    epub_parse_toc(book);

    /* Compute cumulative spine sizes for percent-progress reporting. */
    book->cumulative_sizes = (uint32_t *)calloc(book->spine_count, sizeof(uint32_t));
    if (book->cumulative_sizes) {
        uint32_t cum = 0;
        for (uint16_t i = 0; i < book->spine_count; i++) {
            char fullpath[80];
            epub_resolve_archive_path(book->opf_base, book->spine[i].href,
                                       fullpath, sizeof(fullpath));
            cum += zip_entry_size(book->zip, fullpath);
            book->cumulative_sizes[i] = cum;
        }
    }

    LOG_INF("EPUB", "Opened %s: spine=%u, manifest=%u, toc_root=%s",
            path, book->spine_count, book->manifest_count,
            book->toc_root ? "yes" : "no");
    return book;
}

void epub_close(epub_book_t *book) {
    if (!book) return;
    if (book->zip) zip_close(book->zip);
    free(book->cumulative_sizes);
    epub_arena_free(&book->arena);
    free(book);
}

/* ── Accessors ──────────────────────────────────────────────────── */

const epub_metadata_t *epub_metadata(const epub_book_t *book) {
    return book ? &book->meta : NULL;
}

uint16_t epub_manifest_count(const epub_book_t *book) {
    return book ? book->manifest_count : 0;
}

const epub_manifest_item_t *epub_manifest_at(const epub_book_t *book, uint16_t i) {
    if (!book || i >= book->manifest_count) return NULL;
    return &book->manifest[i];
}

const epub_manifest_item_t *epub_manifest_lookup(const epub_book_t *book, const char *id) {
    if (!book || !id) return NULL;
    for (uint16_t i = 0; i < book->manifest_count; i++) {
        if (book->manifest[i].id && strcmp(book->manifest[i].id, id) == 0) {
            return &book->manifest[i];
        }
    }
    return NULL;
}

uint16_t epub_spine_count(const epub_book_t *book) {
    return book ? book->spine_count : 0;
}

const epub_spine_entry_t *epub_spine_at(const epub_book_t *book, uint16_t i) {
    if (!book || i >= book->spine_count) return NULL;
    return &book->spine[i];
}

const epub_toc_node_t *epub_toc_root(const epub_book_t *book) {
    return book ? book->toc_root : NULL;
}

const char *epub_cache_dir(const epub_book_t *book) {
    return book ? book->cache_dir : NULL;
}

const char *epub_path(const epub_book_t *book) {
    return book ? book->path : NULL;
}

zip_handle_t *epub_zip(epub_book_t *book) {
    return book ? book->zip : NULL;
}

uint32_t epub_item_size(const epub_book_t *book, const char *href) {
    if (!book || !href) return 0;
    char fullpath[80];
    epub_resolve_archive_path(book->opf_base, href, fullpath, sizeof(fullpath));
    return zip_entry_size(book->zip, fullpath);
}

uint32_t epub_cumulative_size(const epub_book_t *book, uint16_t spine_index) {
    if (!book || !book->cumulative_sizes || spine_index >= book->spine_count) return 0;
    return book->cumulative_sizes[spine_index];
}

int epub_read_item(epub_book_t *book, const char *href, void *dst, size_t dst_max) {
    if (!book || !href) return -1;
    char fullpath[80];
    epub_resolve_archive_path(book->opf_base, href, fullpath, sizeof(fullpath));
    return zip_read(book->zip, fullpath, dst, dst_max);
}

/* ── href resolution ────────────────────────────────────────────── */

int epub_resolve_href(const epub_book_t *book, const char *base_href,
                      const char *href, char *out_fragment, size_t out_fragment_size) {
    if (out_fragment && out_fragment_size > 0) out_fragment[0] = '\0';
    if (!book || !href) return -1;

    /* Split fragment. */
    const char *hash = strchr(href, '#');
    size_t path_len = hash ? (size_t)(hash - href) : strlen(href);
    if (hash && out_fragment && out_fragment_size > 0) {
        size_t flen = strlen(hash + 1);
        if (flen >= out_fragment_size) flen = out_fragment_size - 1;
        memcpy(out_fragment, hash + 1, flen);
        out_fragment[flen] = '\0';
    }

    /* Resolve relative to base_href's directory.
     * Simplistic for v1: only support same-dir or absolute (rooted)
     * references; no .. traversal. EPUB 3 best practice keeps
     * everything in one OPS/ directory anyway. */
    char target[80];
    target[0] = '\0';

    if (path_len == 0) {
        /* fragment-only — same document; we don't know its spine index here */
        return -1;
    }

    if (href[0] == '/') {
        /* Absolute (rooted at OCF). */
        size_t copy = (path_len < sizeof(target)) ? path_len : sizeof(target) - 1;
        memcpy(target, href + 1, copy - 1);
        target[copy - 1] = '\0';
    } else if (base_href) {
        /* Relative: take base_href's directory and append. */
        const char *slash = strrchr(base_href, '/');
        size_t dir_len = slash ? (size_t)(slash - base_href + 1) : 0;
        if (dir_len + path_len + 1 > sizeof(target)) return -1;
        memcpy(target, base_href, dir_len);
        memcpy(target + dir_len, href, path_len);
        target[dir_len + path_len] = '\0';
    } else {
        /* No base — assume relative to OPF dir. */
        size_t copy = (path_len < sizeof(target)) ? path_len : sizeof(target) - 1;
        memcpy(target, href, copy);
        target[copy] = '\0';
    }

    /* Linear scan over spine; cheap on typical book sizes (<200 entries). */
    for (uint16_t i = 0; i < book->spine_count; i++) {
        if (book->spine[i].href && strcmp(book->spine[i].href, target) == 0) {
            return (int)i;
        }
    }
    return -1;
}
