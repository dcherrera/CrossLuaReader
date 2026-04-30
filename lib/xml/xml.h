/**
 * @file xml.h
 * @brief Streaming XML/HTML SAX parser. Wraps expat with a CrossLua-flavored
 *        callback API and namespace stripping. Used for OCF container.xml,
 *        OPF package, NCX/NavDoc TOC (Phase 9.A) and chapter XHTML (Phase 9.B).
 *
 * @status Phase 9.A.1
 * @issues None
 * @todo HTML5-named-entity decoding deferred to Phase 9.B where chapter
 *       content needs it. EPUB control files (container, OPF, NCX, NavDoc)
 *       use only the XML predefined entities (&lt; &gt; &amp; &quot; &apos;)
 *       which expat handles natively.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Parser mode. STRICT for OPF/container/NCX (well-formed XML);
 *  HTML for NavDoc and chapter XHTML where some real-world files
 *  cut corners (unclosed tags, attribute values without quotes).
 *  HTML mode currently behaves identically to STRICT — Phase 9.B will
 *  loosen it (HTML5 fragment quirks). */
typedef enum {
    XML_MODE_STRICT = 0,
    XML_MODE_HTML   = 1,
} xml_mode_t;

/** Single attribute as visible to callbacks: NUL-terminated key/value.
 *  Keys retain any prefix verbatim (e.g. "epub:type", "xml:lang") so
 *  callers can pattern-match on them; element tag names get their
 *  namespace URI stripped to a plain local name. */
typedef struct {
    const char *key;
    const char *value;
} xml_attr_t;

/** Callback set. Any handler may be NULL. Return values not used. */
typedef struct {
    void (*on_start)(void *ctx, const char *tag, const xml_attr_t *attrs, int n_attrs);
    void (*on_end)(void *ctx, const char *tag);
    void (*on_text)(void *ctx, const char *text, size_t len);
} xml_handlers_t;

/**
 * Parse an in-memory XML/HTML buffer with SAX callbacks.
 *
 * @param buf       Source bytes (does not need NUL termination).
 * @param len       Number of bytes in buf.
 * @param mode      Parser mode (STRICT or HTML — see xml_mode_t).
 * @param h         Callbacks; any field may be NULL.
 * @param ctx       User pointer passed to every callback.
 * @return true on full successful parse, false on parse error.
 */
bool xml_parse_buffer(const void *buf, size_t len, xml_mode_t mode,
                      const xml_handlers_t *h, void *ctx);

/** Pull-source for streaming parses. Reader returns bytes available in
 *  *out_data and the count in *out_len; should set *out_len to 0 to signal
 *  EOF. Lifetime of the buffer is until the next read call. Return false
 *  to signal an unrecoverable read error (parse aborts). */
typedef bool (*xml_reader_t)(void *read_ctx, const uint8_t **out_data, size_t *out_len);

/**
 * Parse from a pull source (e.g. zip_read_chunked feeding a chapter into
 * the parser without buffering the whole entry).
 *
 * @param read_fn   Source callback.
 * @param read_ctx  Opaque context for read_fn.
 * @param mode      Parser mode.
 * @param h         Callbacks.
 * @param ctx       User pointer for callbacks.
 * @return true on full successful parse.
 */
bool xml_parse_pull(xml_reader_t read_fn, void *read_ctx, xml_mode_t mode,
                    const xml_handlers_t *h, void *ctx);
