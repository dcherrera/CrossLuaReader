/**
 * @file utf8.h
 * @brief UTF-8 decoding, encoding, and combining mark detection.
 *        Pure C port of CrossPoint's Utf8.h/cpp.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define REPLACEMENT_GLYPH 0xFFFD

/**
 * Decode the next UTF-8 codepoint from a byte stream.
 * Advances *str past the consumed bytes.
 *
 * @param str Pointer to pointer into UTF-8 byte stream
 * @return    Codepoint, or 0 at end-of-string, or REPLACEMENT_GLYPH on error
 */
uint32_t utf8_next_codepoint(const uint8_t **str);

/**
 * Check if a codepoint is a Unicode combining mark that should
 * not advance the cursor. Includes Latin diacriticals, Hebrew
 * nikkud/cantillation, and other combining ranges.
 *
 * @param cp Unicode codepoint
 * @return   true if combining mark
 */
bool utf8_is_combining_mark(uint32_t cp);

/**
 * Truncate a buffer to the last complete UTF-8 codepoint boundary.
 *
 * @param buf Raw char buffer
 * @param len Buffer length
 * @return    Safe length (<= len) ending on a codepoint boundary
 */
int utf8_safe_truncate(const char *buf, int len);

/**
 * Encode a single Unicode codepoint as UTF-8.
 *
 * @param cp       Codepoint to encode
 * @param buf      Output buffer
 * @param buf_size Size of output buffer
 * @return         Bytes written (1-4), or 0 if buffer too small
 */
int utf8_encode(uint32_t cp, char *buf, int buf_size);
