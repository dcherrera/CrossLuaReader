/**
 * @file epub.h
 * @brief High-level EPUB book object. Opens a .epub container, parses
 *        META-INF/container.xml + the OPF package + NCX/NavDoc TOC, and
 *        exposes navigation primitives. Builds on lib/zip and lib/xml.
 *
 *        Per spec §10 decision #8: no parsed-OPF cache — every open
 *        re-parses. Parse cost is 50-200ms; against e-ink redraw it's
 *        invisible.
 *
 * @status Phase 9.A.2
 * @issues None
 * @todo Phase 9.B: chapter parsing + content reads. Phase 9.E: pagination
 *       cache. This module exposes raw item reads (epub_read_item) but
 *       does not interpret content.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zip.h"

typedef struct epub_book_s epub_book_t;

/** Open-time error codes. Returned via epub_open's out_err parameter. */
typedef enum {
    EPUB_OK                  = 0,
    EPUB_ERR_IO              = 1,  /**< file not found, read failure */
    EPUB_ERR_NOT_EPUB        = 2,  /**< missing/wrong mimetype */
    EPUB_ERR_DRM             = 3,  /**< DRM-protected (real, not font obfuscation) */
    EPUB_ERR_MALFORMED_CONTAINER = 4,
    EPUB_ERR_MALFORMED_OPF   = 5,
    EPUB_ERR_NO_SPINE        = 6,
    EPUB_ERR_FIXED_LAYOUT    = 7,  /**< pre-paginated rendition — refused at v1 */
    EPUB_ERR_OOM             = 8,
} epub_err_t;

/** EPUB version detected from <package version="..."> in the OPF. */
typedef enum {
    EPUB_V_UNKNOWN = 0,
    EPUB_V_2       = 2,
    EPUB_V_3       = 3,
} epub_version_t;

/** Page-progression-direction from spine attribute (default LTR). */
typedef enum {
    EPUB_PPD_LTR = 0,
    EPUB_PPD_RTL = 1,
} epub_ppd_t;

/** Top-level book metadata. All string pointers are NUL-terminated and
 *  owned by the book — valid until epub_close(). NULL if not present. */
typedef struct {
    const char *title;
    const char *author;        /**< first dc:creator with role="aut" if present */
    const char *language;      /**< BCP-47, e.g. "en", "he" */
    const char *identifier;    /**< unique-identifier from package */
    const char *modified;      /**< EPUB 3 dcterms:modified, ISO 8601 */
    const char *publisher;
    const char *date_published;
    const char *description;
    const char *cover_id;      /**< manifest id of the cover image, NULL if absent */

    epub_ppd_t      page_progression_direction;
    epub_version_t  version;
} epub_metadata_t;

/** Manifest item. */
typedef struct {
    const char *id;
    const char *href;          /**< relative to OPF dir; resolves via epub_read_item */
    const char *media_type;
    const char *properties;    /**< raw "cover-image nav scripted ..." string, NULL if none */
} epub_manifest_item_t;

/** Spine entry, in linear reading order. */
typedef struct {
    const char *idref;
    const char *href;          /**< pre-resolved manifest href for convenience */
    const char *media_type;    /**< pre-resolved */
    bool        linear;        /**< false = auxiliary item (footnote pages, etc.) */
} epub_spine_entry_t;

/** TOC node — hierarchical tree. NCX and NavDoc normalized to this shape. */
typedef struct epub_toc_node_s {
    const char                  *label;
    const char                  *href;        /**< may include #fragment */
    struct epub_toc_node_s      *children;    /**< NULL if leaf */
    struct epub_toc_node_s      *next;        /**< sibling chain */
    uint8_t                      depth;
} epub_toc_node_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * Open a .epub on the SD card and parse its metadata.
 *
 * @param path       SD path to the .epub
 * @param cache_dir  Where this book's pagination cache will live (Phase 9.E)
 * @param out_err    Receives a specific error code on failure
 * @return book handle on success, NULL on failure (out_err is set)
 */
epub_book_t *epub_open(const char *path, const char *cache_dir, epub_err_t *out_err);

/** Close the book and free all parsed state. Safe to call with NULL. */
void epub_close(epub_book_t *book);

/* ── Accessors (all pointers owned by book; valid until epub_close) ── */

const epub_metadata_t       *epub_metadata(const epub_book_t *book);
uint16_t                     epub_manifest_count(const epub_book_t *book);
const epub_manifest_item_t  *epub_manifest_at(const epub_book_t *book, uint16_t i);
const epub_manifest_item_t  *epub_manifest_lookup(const epub_book_t *book, const char *id);
uint16_t                     epub_spine_count(const epub_book_t *book);
const epub_spine_entry_t    *epub_spine_at(const epub_book_t *book, uint16_t i);
const epub_toc_node_t       *epub_toc_root(const epub_book_t *book);

const char                  *epub_cache_dir(const epub_book_t *book);
const char                  *epub_path(const epub_book_t *book);
zip_handle_t                *epub_zip(epub_book_t *book);  /**< for low-level item reads */

/**
 * Resolve an internal href (e.g. "chapter5.xhtml#footnote_42") relative
 * to a base href to a {spine_index, fragment} pair. Returns -1 if the
 * target doesn't match a spine entry. Fragment is written to out_fragment
 * (NUL-terminated) if present, otherwise out_fragment[0] = '\0'.
 *
 * @param base_href base href of the document containing the link
 *                  (relative to OPF dir, may be NULL for OPF-relative)
 * @param href      link target as written in the source
 * @param out_fragment buffer of at least 64 bytes
 * @return spine index (>= 0) on hit, -1 on miss
 */
int epub_resolve_href(const epub_book_t *book, const char *base_href,
                      const char *href, char *out_fragment, size_t out_fragment_size);

/**
 * Read a manifest item into a caller-supplied buffer. href is resolved
 * relative to the OPF base path. For chapter-sized items use
 * epub_read_item_chunked instead.
 *
 * @return bytes read, or negative on error (matching zip_read).
 */
int epub_read_item(epub_book_t *book, const char *href, void *dst, size_t dst_max);

/** @return uncompressed size of the manifest item, or 0 if not found. */
uint32_t epub_item_size(const epub_book_t *book, const char *href);

/** Cumulative bytes through spine_index (sum of uncompressed sizes).
 *  Used for percent-progress estimates. */
uint32_t epub_cumulative_size(const epub_book_t *book, uint16_t spine_index);
