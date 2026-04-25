/**
 * @file bidi.h
 * @brief Bidirectional text support: RTL detection, grapheme-cluster-aware
 *        reversal, bracket mirroring. C port of CrossPoint's ScriptDetector.
 *
 * @status Complete
 * @issues None
 * @todo None
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @return true if codepoint is Hebrew consonant (U+05D0-U+05EA). */
bool bidi_is_rtl_codepoint(uint32_t cp);

/** @return true if codepoint is a letter (Latin, Hebrew, Arabic). */
bool bidi_is_letter_codepoint(uint32_t cp);

/**
 * Scan text for the first strong directional letter.
 * Skips whitespace, combining marks, digits, and punctuation.
 *
 * @param text        UTF-8 text to scan
 * @param max_letters Max letter codepoints to inspect before giving up
 * @return            true if first letter found is RTL
 */
bool bidi_starts_with_rtl(const char *text, int max_letters);

/**
 * Check if text contains any RTL codepoints.
 * Fast path: scans for non-ASCII bytes first.
 *
 * @param text UTF-8 text
 * @return     true if any RTL codepoint found
 */
bool bidi_has_rtl(const char *text);

/**
 * Reorder a line of text from logical to visual order.
 * Handles RTL-base and LTR-base lines, grapheme cluster preservation,
 * and bracket mirroring.
 *
 * @param text     Input UTF-8 text (logical order)
 * @param out_buf  Output buffer for visual-order text
 * @param out_size Size of output buffer
 * @return         Bytes written to out_buf (excluding NUL terminator)
 */
int bidi_reorder_line(const char *text, char *out_buf, int out_size);
