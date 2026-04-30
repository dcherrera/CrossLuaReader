/**
 * @file zip.h
 * @brief ZIP archive reader for SD-resident archives. Streams DEFLATE
 *        through uzlib; STORE entries copy directly. Used by the EPUB
 *        reader pipeline (containers are ZIPs).
 *
 *        Only PKZIP archives in the EPUB / OCF subset are supported:
 *        STORE (method 0) and DEFLATE (method 8). No ZIP64, no
 *        encryption (other than EPUB font obfuscation, which is
 *        recognized but not deobfuscated here).
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Opaque archive handle. */
typedef struct zip_handle_s zip_handle_t;

/** Encryption classification reported by zip_drm_state(). */
typedef enum {
    ZIP_DRM_NONE         = 0,  /**< No encryption.xml or only empty entries. */
    ZIP_DRM_OBFUSCATION  = 1,  /**< Only IDPF/Adobe font-obfuscation entries — book is openable. */
    ZIP_DRM_PROTECTED    = 2,  /**< Real DRM (Adobe ADEPT, etc.) — book must be rejected. */
} zip_drm_t;

/**
 * Open a ZIP archive on the SD card. Reads the End-of-Central-Directory
 * record, parses the Central Directory into a single allocation, and
 * keeps the file handle open for streaming entry reads.
 *
 * If validate_epub_mimetype is true, ALSO verifies the OCF magic:
 * the first central-directory entry must be `mimetype`, STORE method,
 * uncompressed contents `application/epub+zip` (no trailing whitespace).
 *
 * @param path                 SD path to the .zip / .epub
 * @param validate_epub_mimetype  true to require the EPUB OCF magic
 * @return handle on success, NULL on failure (file open / parse / magic)
 */
zip_handle_t *zip_open(const char *path, bool validate_epub_mimetype);

/** Close handle and free all associated state. Safe to call with NULL. */
void zip_close(zip_handle_t *zh);

/** @return number of entries in the central directory. */
uint16_t zip_entry_count(const zip_handle_t *zh);

/**
 * @return 1-based: the i'th entry name (NUL-terminated, points into the
 *         handle's internal name pool — do not free, do not modify).
 *         NULL if i is out of range.
 */
const char *zip_entry_name(const zip_handle_t *zh, uint16_t i);

/** @return true if any central-directory entry matches name exactly. */
bool zip_has(const zip_handle_t *zh, const char *name);

/**
 * @return uncompressed size of the named entry, or 0 if not found
 *         or if the entry is genuinely empty.
 */
uint32_t zip_entry_size(const zip_handle_t *zh, const char *name);

/**
 * Read an entry's full content into a caller-supplied buffer.
 *
 * @param zh      Archive handle
 * @param name    Entry name (must match exactly)
 * @param dst     Destination buffer
 * @param dst_max Capacity of dst in bytes
 * @return        Bytes written on success.
 *                -1 on entry-not-found, decompression error, or I/O error.
 *                -2 if the entry's uncompressed size exceeds dst_max
 *                   (no partial data is written in that case).
 */
int zip_read(zip_handle_t *zh, const char *name, void *dst, size_t dst_max);

/**
 * Stream an entry through a callback in ~4 KB chunks. For chapters and
 * images that are too large to hold in RAM. The callback receives raw
 * uncompressed bytes; return false to abort.
 *
 * @param zh        Archive handle
 * @param name      Entry name
 * @param callback  Called with each chunk (data, len, ctx); return false to stop
 * @param ctx       Opaque pointer passed to callback
 * @return true on full read, false on abort / not-found / error
 */
typedef bool (*zip_chunk_cb_t)(const void *data, size_t len, void *ctx);
bool zip_read_chunked(zip_handle_t *zh, const char *name,
                      zip_chunk_cb_t callback, void *ctx);

/**
 * Inspect META-INF/encryption.xml (if present) and classify whether this
 * archive is DRM-protected, font-obfuscated only, or unencrypted.
 *
 * IDPF font obfuscation algorithms recognized:
 *   http://www.idpf.org/2008/embedding
 * Adobe font obfuscation algorithm recognized:
 *   http://ns.adobe.com/pdf/enc#RC
 *
 * Anything else with a non-empty <EncryptionMethod> is treated as DRM.
 */
zip_drm_t zip_drm_state(zip_handle_t *zh);
