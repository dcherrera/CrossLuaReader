/**
 * @file zip.c
 * @brief ZIP archive reader implementation. Parses the central directory
 *        once at zip_open() into a single arena allocation; subsequent
 *        reads seek into the SD file and decompress on demand. Memory
 *        cost per archive is bounded by the central directory size
 *        (~64 bytes per entry name + fixed metadata).
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo None
 */

#include "zip.h"
#include "hal_storage.h"
#include "logging.h"
#include "uzlib.h"

#include <stdlib.h>
#include <string.h>

#define ZIP_LOCAL_SIG       0x04034b50u
#define ZIP_CD_SIG          0x02014b50u
#define ZIP_EOCD_SIG        0x06054b50u

#define ZIP_METHOD_STORE    0u
#define ZIP_METHOD_DEFLATE  8u

#define ZIP_EOCD_MIN_SIZE   22
#define ZIP_EOCD_SCAN_MAX   (ZIP_EOCD_MIN_SIZE + 65535)  /* + max comment length */

#define ZIP_INFLATE_BUF     2048u  /* compressed-side window */
#define ZIP_OUTPUT_CHUNK    1024u  /* uncompressed chunk for streaming callbacks */

/* ── Types ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
    uint32_t name_offset;       /**< into name_pool */
    uint16_t method;
    uint16_t name_len;
} zip_entry_t;

struct zip_handle_s {
    hal_file_t   fh;
    char         path[64];      /* SD path of the .zip — used to reopen on read errors */
    zip_entry_t *entries;
    char        *name_pool;
    uint32_t     name_pool_size;
    uint16_t     entry_count;
};

/* ── Little-endian readers (RISC-V is LE but we don't assume host order) ── */

static inline uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── EOCD scan ──────────────────────────────────────────────────── */

/**
 * Scan backward from end of file for the EOCD signature. ZIP allows up
 * to 65535 bytes of comment after EOCD, so we scan up to that range.
 *
 * @param fh         open file
 * @param file_size  total file size
 * @param out_offset receives EOCD absolute offset on success
 * @param out_buf    receives a memcpy of the 22-byte EOCD record on success
 * @return true on found
 */
static bool find_eocd(hal_file_t fh, uint32_t file_size,
                      uint32_t *out_offset, uint8_t out_buf[ZIP_EOCD_MIN_SIZE]) {
    if (file_size < ZIP_EOCD_MIN_SIZE) return false;

    /* Scan a window at the tail. Most EPUBs have no archive comment, so
     * the EOCD sits exactly 22 bytes from the end — we'd find it on the
     * first read. The 64KB+22 fallback handles archives with comments. */
    uint32_t scan_size = (file_size > ZIP_EOCD_SCAN_MAX) ? ZIP_EOCD_SCAN_MAX : file_size;
    uint32_t scan_start = file_size - scan_size;

    uint8_t *buf = (uint8_t *)malloc(scan_size);
    if (!buf) {
        LOG_ERR("ZIP", "EOCD scan malloc failed: %u bytes", scan_size);
        return false;
    }

    if (!hal_storage_file_seek(fh, scan_start)) {
        free(buf);
        return false;
    }
    int n = hal_storage_file_read(fh, buf, scan_size);
    if (n != (int)scan_size) {
        free(buf);
        return false;
    }

    /* Walk backward looking for EOCD signature. */
    for (int32_t i = (int32_t)scan_size - ZIP_EOCD_MIN_SIZE; i >= 0; i--) {
        if (rd_le32(&buf[i]) == ZIP_EOCD_SIG) {
            memcpy(out_buf, &buf[i], ZIP_EOCD_MIN_SIZE);
            *out_offset = scan_start + (uint32_t)i;
            free(buf);
            return true;
        }
    }

    free(buf);
    return false;
}

/* ── Central directory parsing ──────────────────────────────────── */

/**
 * Read and parse the central directory. Allocates `entries` and
 * `name_pool` on the heap; caller takes ownership.
 *
 * @return true on success
 */
static bool parse_central_directory(zip_handle_t *zh,
                                     uint32_t cd_offset, uint32_t cd_size,
                                     uint16_t entry_count) {
    if (entry_count == 0) {
        zh->entries = NULL;
        zh->name_pool = NULL;
        zh->name_pool_size = 0;
        zh->entry_count = 0;
        return true;
    }

    uint8_t *cd_buf = (uint8_t *)malloc(cd_size);
    if (!cd_buf) {
        LOG_ERR("ZIP", "CD malloc failed: %u bytes", cd_size);
        return false;
    }

    if (!hal_storage_file_seek(zh->fh, cd_offset) ||
        hal_storage_file_read(zh->fh, cd_buf, cd_size) != (int)cd_size) {
        LOG_ERR("ZIP", "CD read failed");
        free(cd_buf);
        return false;
    }

    zh->entries = (zip_entry_t *)calloc(entry_count, sizeof(zip_entry_t));
    if (!zh->entries) {
        LOG_ERR("ZIP", "Entry array malloc failed");
        free(cd_buf);
        return false;
    }

    /* First pass: validate signatures, accumulate name pool size. */
    const uint8_t *p = cd_buf;
    const uint8_t *end = cd_buf + cd_size;
    uint32_t name_pool_size = 0;

    for (uint16_t i = 0; i < entry_count; i++) {
        if ((size_t)(end - p) < 46 || rd_le32(p) != ZIP_CD_SIG) {
            LOG_ERR("ZIP", "Bad CD entry %u (sig)", i);
            free(cd_buf);
            free(zh->entries);
            zh->entries = NULL;
            return false;
        }
        uint16_t nlen = rd_le16(p + 28);
        uint16_t xlen = rd_le16(p + 30);
        uint16_t clen = rd_le16(p + 32);
        name_pool_size += nlen + 1;  /* +1 for NUL */
        if ((size_t)(end - p) < 46u + nlen + xlen + clen) {
            LOG_ERR("ZIP", "Bad CD entry %u (truncated)", i);
            free(cd_buf);
            free(zh->entries);
            zh->entries = NULL;
            return false;
        }
        p += 46u + nlen + xlen + clen;
    }

    zh->name_pool = (char *)malloc(name_pool_size);
    if (!zh->name_pool) {
        LOG_ERR("ZIP", "Name pool malloc failed: %u bytes", name_pool_size);
        free(cd_buf);
        free(zh->entries);
        zh->entries = NULL;
        return false;
    }
    zh->name_pool_size = name_pool_size;

    /* Second pass: fill entries. */
    p = cd_buf;
    uint32_t name_off = 0;

    for (uint16_t i = 0; i < entry_count; i++) {
        zip_entry_t *e = &zh->entries[i];
        e->method              = rd_le16(p + 10);
        e->crc32               = rd_le32(p + 16);
        e->compressed_size     = rd_le32(p + 20);
        e->uncompressed_size   = rd_le32(p + 24);
        e->name_len            = rd_le16(p + 28);
        e->local_header_offset = rd_le32(p + 42);
        e->name_offset         = name_off;

        memcpy(zh->name_pool + name_off, p + 46, e->name_len);
        zh->name_pool[name_off + e->name_len] = '\0';
        name_off += e->name_len + 1;

        uint16_t xlen = rd_le16(p + 30);
        uint16_t clen = rd_le16(p + 32);
        p += 46u + e->name_len + xlen + clen;
    }

    zh->entry_count = entry_count;
    free(cd_buf);
    return true;
}

/* ── Lookup ─────────────────────────────────────────────────────── */

static const zip_entry_t *find_entry(const zip_handle_t *zh, const char *name) {
    if (!zh || !name) return NULL;
    size_t nlen = strlen(name);
    for (uint16_t i = 0; i < zh->entry_count; i++) {
        const zip_entry_t *e = &zh->entries[i];
        if (e->name_len == nlen &&
            memcmp(zh->name_pool + e->name_offset, name, nlen) == 0) {
            return e;
        }
    }
    return NULL;
}

/* ── Local file header ──────────────────────────────────────────── */

/**
 * Seek to the start of compressed data for a given entry. Local file
 * headers can have different name/extra lengths than central-directory
 * entries (rare but legal), so we read the local header to compute the
 * exact data offset.
 */
static bool seek_to_data(zip_handle_t *zh, const zip_entry_t *e) {
    uint8_t lh[30];
    if (!hal_storage_file_seek(zh->fh, e->local_header_offset) ||
        hal_storage_file_read(zh->fh, lh, sizeof(lh)) != (int)sizeof(lh)) {
        return false;
    }
    if (rd_le32(lh) != ZIP_LOCAL_SIG) {
        LOG_ERR("ZIP", "Bad local header sig");
        return false;
    }
    uint16_t nlen = rd_le16(lh + 26);
    uint16_t xlen = rd_le16(lh + 28);
    return hal_storage_file_seek(zh->fh, e->local_header_offset + 30u + nlen + xlen);
}

/* ── EPUB mimetype validation ───────────────────────────────────── */

static bool validate_epub_magic(zip_handle_t *zh) {
    if (zh->entry_count == 0) return false;
    const zip_entry_t *first = &zh->entries[0];
    const char *first_name = zh->name_pool + first->name_offset;

    if (strcmp(first_name, "mimetype") != 0) {
        LOG_ERR("ZIP", "First entry is not mimetype: '%s'", first_name);
        return false;
    }
    if (first->method != ZIP_METHOD_STORE) {
        LOG_ERR("ZIP", "mimetype is compressed (method %u)", first->method);
        return false;
    }

    static const char EXPECTED[] = "application/epub+zip";
    if (first->uncompressed_size != sizeof(EXPECTED) - 1) {
        LOG_ERR("ZIP", "mimetype length %u != %u",
                first->uncompressed_size, (unsigned)(sizeof(EXPECTED) - 1));
        return false;
    }

    char buf[24];
    if (!seek_to_data(zh, first) ||
        hal_storage_file_read(zh->fh, buf, sizeof(EXPECTED) - 1) != (int)(sizeof(EXPECTED) - 1)) {
        LOG_ERR("ZIP", "mimetype read failed");
        return false;
    }
    if (memcmp(buf, EXPECTED, sizeof(EXPECTED) - 1) != 0) {
        LOG_ERR("ZIP", "mimetype contents mismatch");
        return false;
    }
    return true;
}

/* ── Public API ─────────────────────────────────────────────────── */

zip_handle_t *zip_open(const char *path, bool validate_epub_mimetype) {
    if (!path) return NULL;

    hal_file_t fh = hal_storage_open(path, HAL_FILE_READ);
    if (!fh) {
        LOG_ERR("ZIP", "open failed: %s", path);
        return NULL;
    }

    uint32_t file_size = hal_storage_file_size(fh);

    uint32_t eocd_offset;
    uint8_t eocd[ZIP_EOCD_MIN_SIZE];
    if (!find_eocd(fh, file_size, &eocd_offset, eocd)) {
        LOG_ERR("ZIP", "EOCD not found in %s", path);
        hal_storage_file_close(fh);
        return NULL;
    }

    uint16_t entry_count = rd_le16(eocd + 10);
    uint32_t cd_size     = rd_le32(eocd + 12);
    uint32_t cd_offset   = rd_le32(eocd + 16);

    zip_handle_t *zh = (zip_handle_t *)calloc(1, sizeof(zip_handle_t));
    if (!zh) {
        LOG_ERR("ZIP", "handle alloc failed");
        hal_storage_file_close(fh);
        return NULL;
    }
    zh->fh = fh;
    strncpy(zh->path, path, sizeof(zh->path) - 1);
    zh->path[sizeof(zh->path) - 1] = '\0';

    if (!parse_central_directory(zh, cd_offset, cd_size, entry_count)) {
        zip_close(zh);
        return NULL;
    }

    if (validate_epub_mimetype && !validate_epub_magic(zh)) {
        zip_close(zh);
        return NULL;
    }

    LOG_INF("ZIP", "Opened %s: %u entries", path, entry_count);
    return zh;
}

void zip_close(zip_handle_t *zh) {
    if (!zh) return;
    if (zh->fh) hal_storage_file_close(zh->fh);
    free(zh->entries);
    free(zh->name_pool);
    free(zh);
}

uint16_t zip_entry_count(const zip_handle_t *zh) {
    return zh ? zh->entry_count : 0;
}

const char *zip_entry_name(const zip_handle_t *zh, uint16_t i) {
    if (!zh || i >= zh->entry_count) return NULL;
    return zh->name_pool + zh->entries[i].name_offset;
}

bool zip_has(const zip_handle_t *zh, const char *name) {
    return find_entry(zh, name) != NULL;
}

uint32_t zip_entry_size(const zip_handle_t *zh, const char *name) {
    const zip_entry_t *e = find_entry(zh, name);
    return e ? e->uncompressed_size : 0;
}

/* ── Read paths ─────────────────────────────────────────────────── */

static int read_store(zip_handle_t *zh, const zip_entry_t *e,
                       void *dst, size_t dst_max) {
    if (e->uncompressed_size > dst_max) return -2;
    if (!seek_to_data(zh, e)) return -1;
    int n = hal_storage_file_read(zh->fh, dst, e->uncompressed_size);
    return (n == (int)e->uncompressed_size) ? n : -1;
}

static int read_deflate(zip_handle_t *zh, const zip_entry_t *e,
                         void *dst, size_t dst_max) {
    if (e->uncompressed_size > dst_max) return -2;
    if (!seek_to_data(zh, e)) return -1;

    uint8_t *comp = (uint8_t *)malloc(e->compressed_size);
    if (!comp) return -1;

    int rd = hal_storage_file_read(zh->fh, comp, e->compressed_size);
    if (rd != (int)e->compressed_size) {
        free(comp);
        return -1;
    }

    struct uzlib_uncomp d;
    uzlib_uncompress_init(&d, NULL, 0);
    d.source       = comp;
    d.source_limit = comp + e->compressed_size;
    d.dest_start   = (unsigned char *)dst;
    d.dest         = (unsigned char *)dst;
    d.dest_limit   = (unsigned char *)dst + e->uncompressed_size;

    int res;
    while ((res = uzlib_uncompress(&d)) == TINF_OK) { /* keep going */ }
    free(comp);

    if (res != TINF_DONE) {
        LOG_ERR("ZIP", "deflate failed: %d", res);
        return -1;
    }
    return (int)e->uncompressed_size;
}

int zip_read(zip_handle_t *zh, const char *name, void *dst, size_t dst_max) {
    const zip_entry_t *e = find_entry(zh, name);
    if (!e) return -1;
    if (e->method == ZIP_METHOD_STORE)   return read_store(zh, e, dst, dst_max);
    if (e->method == ZIP_METHOD_DEFLATE) return read_deflate(zh, e, dst, dst_max);
    LOG_ERR("ZIP", "Unsupported method %u for '%s'", e->method, name);
    return -1;
}

/* ── Streaming read ─────────────────────────────────────────────── */

static bool stream_store(zip_handle_t *zh, const zip_entry_t *e,
                          zip_chunk_cb_t cb, void *ctx) {
    if (!seek_to_data(zh, e)) return false;

    uint8_t *buf = (uint8_t *)malloc(ZIP_OUTPUT_CHUNK);
    if (!buf) return false;

    uint32_t remaining = e->uncompressed_size;
    while (remaining > 0) {
        size_t want = (remaining < ZIP_OUTPUT_CHUNK) ? remaining : ZIP_OUTPUT_CHUNK;
        int n = hal_storage_file_read(zh->fh, buf, want);
        if (n != (int)want) {
            free(buf);
            return false;
        }
        if (!cb(buf, want, ctx)) {
            free(buf);
            return false;
        }
        remaining -= (uint32_t)want;
    }
    free(buf);
    return true;
}

static bool stream_deflate(zip_handle_t *zh, const zip_entry_t *e,
                            zip_chunk_cb_t cb, void *ctx) {
    if (!seek_to_data(zh, e)) return false;

    /* Whole-input buffering: simpler than chunk-feed, costs compressed
     * size in heap. Acceptable for chapters (~5-50 KB compressed). */
    uint8_t *comp = (uint8_t *)malloc(e->compressed_size);
    uint8_t *out  = (uint8_t *)malloc(ZIP_OUTPUT_CHUNK);
    if (!comp || !out) {
        free(comp); free(out);
        return false;
    }

    if (hal_storage_file_read(zh->fh, comp, e->compressed_size) != (int)e->compressed_size) {
        free(comp); free(out);
        return false;
    }

    struct uzlib_uncomp d;
    uzlib_uncompress_init(&d, NULL, 0);
    d.source       = comp;
    d.source_limit = comp + e->compressed_size;

    uint32_t remaining = e->uncompressed_size;
    bool ok = true;

    while (remaining > 0) {
        size_t chunk = (remaining < ZIP_OUTPUT_CHUNK) ? remaining : ZIP_OUTPUT_CHUNK;
        d.dest_start = out;
        d.dest       = out;
        d.dest_limit = out + chunk;

        int res;
        while ((res = uzlib_uncompress(&d)) == TINF_OK && d.dest < d.dest_limit) { }
        if (res != TINF_OK && res != TINF_DONE) {
            LOG_ERR("ZIP", "stream deflate failed: %d", res);
            ok = false;
            break;
        }
        if (!cb(out, chunk, ctx)) { ok = false; break; }
        remaining -= (uint32_t)chunk;
    }

    free(comp);
    free(out);
    return ok;
}

bool zip_read_chunked(zip_handle_t *zh, const char *name,
                      zip_chunk_cb_t cb, void *ctx) {
    if (!zh || !name || !cb) return false;
    const zip_entry_t *e = find_entry(zh, name);
    if (!e) return false;
    if (e->uncompressed_size == 0) return true;

    if (e->method == ZIP_METHOD_STORE)   return stream_store(zh, e, cb, ctx);
    if (e->method == ZIP_METHOD_DEFLATE) return stream_deflate(zh, e, cb, ctx);
    LOG_ERR("ZIP", "Unsupported method %u for streaming '%s'", e->method, name);
    return false;
}

/* ── DRM classification ─────────────────────────────────────────── */

/**
 * EPUB encryption.xml is small (typically < 4 KB even for many obfuscated
 * fonts). We read it whole, scan for known font-obfuscation algorithm URIs.
 * Anything else with EncryptionMethod elements means real DRM.
 */
zip_drm_t zip_drm_state(zip_handle_t *zh) {
    if (!zip_has(zh, "META-INF/encryption.xml")) {
        return ZIP_DRM_NONE;
    }

    uint32_t sz = zip_entry_size(zh, "META-INF/encryption.xml");
    if (sz == 0)   return ZIP_DRM_NONE;
    if (sz > 16 * 1024) return ZIP_DRM_PROTECTED;  /* unreasonably large = suspicious */

    char *buf = (char *)malloc(sz + 1);
    if (!buf) return ZIP_DRM_PROTECTED;  /* fail-safe */

    int n = zip_read(zh, "META-INF/encryption.xml", buf, sz);
    if (n < 0) {
        free(buf);
        return ZIP_DRM_PROTECTED;
    }
    buf[sz] = '\0';

    /* Cheap substring scan — we don't need real XML parsing for this.
     * Each <EncryptionMethod Algorithm="..."> appears once per encrypted
     * resource. If every algorithm is in the obfuscation list we're fine. */
    static const char *OBFUSCATION_URIS[] = {
        "http://www.idpf.org/2008/embedding",
        "http://ns.adobe.com/pdf/enc#RC",
        NULL,
    };

    bool any_method = false;
    bool any_drm    = false;
    const char *p = buf;
    while ((p = strstr(p, "Algorithm=")) != NULL) {
        any_method = true;
        const char *q = strchr(p, '"');
        if (!q) break;
        q++;
        const char *qe = strchr(q, '"');
        if (!qe) break;

        bool matched = false;
        for (int i = 0; OBFUSCATION_URIS[i]; i++) {
            size_t ulen = strlen(OBFUSCATION_URIS[i]);
            if ((size_t)(qe - q) == ulen &&
                memcmp(q, OBFUSCATION_URIS[i], ulen) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            any_drm = true;
            break;
        }
        p = qe + 1;
    }

    free(buf);
    if (any_drm)    return ZIP_DRM_PROTECTED;
    if (any_method) return ZIP_DRM_OBFUSCATION;
    return ZIP_DRM_NONE;  /* encryption.xml exists but had no methods */
}
