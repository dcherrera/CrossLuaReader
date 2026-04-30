/**
 * @file epub_internal.h
 * @brief Private types shared between epub.c, epub_container.c, epub_opf.c,
 *        and epub_toc.c. Not exposed to consumers — they use epub.h only.
 *
 * @status Phase 9.A.2
 * @issues None
 * @todo None
 */
#pragma once

#include "epub.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── String arena ───────────────────────────────────────────────── */

/* Bump-allocated string pool: grows in 4 KB chunks. All metadata,
 * manifest item names, hrefs, etc. live in here. Single free()
 * per chunk at epub_close. No fragmentation. */
typedef struct epub_arena_chunk_s {
    struct epub_arena_chunk_s *next;
    size_t                     used;
    size_t                     capacity;
    char                       data[];
} epub_arena_chunk_t;

typedef struct {
    epub_arena_chunk_t *head;
    size_t              total_used;
} epub_arena_t;

/** Allocate `n` bytes from the arena. Returns NULL on OOM. */
void *epub_arena_alloc(epub_arena_t *a, size_t n);

/** Copy a NUL-terminated string into the arena and return a pointer to
 *  the copy. NULL src → returns NULL. Empty src → returns "". */
const char *epub_arena_strdup(epub_arena_t *a, const char *src);

/** Copy `n` bytes from src and append a NUL. Used for non-NUL-terminated
 *  XML text fragments. */
const char *epub_arena_strndup(epub_arena_t *a, const char *src, size_t n);

/** Free every chunk. */
void epub_arena_free(epub_arena_t *a);

/* ── Book object ────────────────────────────────────────────────── */

#define EPUB_MANIFEST_MAX  256   /**< sized for typical reflowable books */
#define EPUB_SPINE_MAX     256

struct epub_book_s {
    zip_handle_t            *zip;
    epub_arena_t             arena;

    char                     path[64];           /* SD path of the .epub */
    char                     cache_dir[64];
    char                     opf_base[32];       /* dir prefix inside the ZIP, e.g. "OEBPS/" */
    char                     opf_path[64];       /* full path inside ZIP, e.g. "OEBPS/content.opf" */

    epub_metadata_t          meta;

    epub_manifest_item_t    *manifest;           /* arena-resident array, length manifest_count */
    uint16_t                 manifest_count;
    uint16_t                 manifest_capacity;

    epub_spine_entry_t      *spine;              /* arena-resident array, length spine_count */
    uint16_t                 spine_count;
    uint16_t                 spine_capacity;

    char                     toc_id[24];         /* manifest id of NCX file (EPUB 2), if any */
    char                     nav_id[24];         /* manifest id of NavDoc (EPUB 3), if any */
    char                     cover_id[24];       /* manifest id of cover image, if any */

    epub_toc_node_t         *toc_root;           /* arena-resident sibling chain (top-level nav points) */

    uint32_t                *cumulative_sizes;   /* heap-resident, [spine_count]; indexed by spine pos */
};

/* ── Internal parser entry points ───────────────────────────────── */

/**
 * Parse META-INF/container.xml and write the discovered OPF path into
 * book->opf_path / book->opf_base.
 */
bool epub_parse_container(epub_book_t *book);

/**
 * Parse the OPF package document at book->opf_path. Populates metadata,
 * manifest, spine, toc_id, nav_id, cover_id.
 */
bool epub_parse_opf(epub_book_t *book);

/**
 * Parse the TOC (NCX or NavDoc, decided per book). Populates book->toc_root.
 */
bool epub_parse_toc(epub_book_t *book);

/* ── href helpers ───────────────────────────────────────────────── */

/**
 * Combine the OPF base directory with a manifest href to get the full
 * in-archive path. Writes NUL-terminated result to out_buf.
 *
 * Example: opf_base = "OEBPS/", href = "chapter1.xhtml" →
 *          out = "OEBPS/chapter1.xhtml"
 */
void epub_resolve_archive_path(const char *opf_base, const char *href,
                               char *out_buf, size_t out_size);
