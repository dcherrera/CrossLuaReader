/**
 * @file bidi.c
 * @brief Bidirectional text support implementation.
 *        Ported from CrossPoint's ScriptDetector and GfxRenderer BiDi logic.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "bidi.h"
#include "utf8.h"

#include <string.h>

bool bidi_is_rtl_codepoint(uint32_t cp) {
    return cp >= 0x05D0 && cp <= 0x05EA;
}

bool bidi_is_letter_codepoint(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return true;
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 0x00C0 && cp <= 0x024F) return true;  /* Latin Extended */
    if (cp >= 0x05D0 && cp <= 0x05EA) return true;  /* Hebrew */
    if (cp >= 0x0600 && cp <= 0x06FF) return true;  /* Arabic */
    return false;
}

bool bidi_starts_with_rtl(const char *text, int max_letters) {
    if (!text || max_letters <= 0) return false;
    const uint8_t *p = (const uint8_t *)text;
    int letter_count = 0;

    while (*p && letter_count < max_letters) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (cp == 0 || cp == REPLACEMENT_GLYPH) break;
        if (cp <= 0x20) continue;
        if (utf8_is_combining_mark(cp)) continue;
        if (!bidi_is_letter_codepoint(cp)) continue;
        letter_count++;
        return bidi_is_rtl_codepoint(cp);
    }
    return false;
}

bool bidi_has_rtl(const char *text) {
    if (!text) return false;

    /* Fast path: all ASCII means no RTL */
    const uint8_t *q = (const uint8_t *)text;
    bool has_non_ascii = false;
    while (*q) {
        if (*q & 0x80) { has_non_ascii = true; break; }
        q++;
    }
    if (!has_non_ascii) return false;

    const uint8_t *p = (const uint8_t *)text;
    while (*p) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (cp == 0 || cp == REPLACEMENT_GLYPH) break;
        if (bidi_is_rtl_codepoint(cp)) return true;
    }
    return false;
}

/**
 * Mirror a bracket codepoint for RTL display.
 * Returns the mirrored codepoint, or the original if not a bracket.
 */
static uint32_t mirror_bracket(uint32_t cp) {
    switch (cp) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        default:  return cp;
    }
}

/**
 * Reverse RTL word codepoints in-place, preserving grapheme clusters
 * (base + combining marks stay together). Mirrors brackets.
 *
 * @param codepoints Array of codepoints
 * @param count      Number of codepoints
 * @param reversed   Output array (same size as codepoints)
 * @return           Number of codepoints written to reversed
 */
static int reverse_grapheme_clusters(const uint32_t *codepoints, int count,
                                     uint32_t *reversed) {
    /* Identify clusters: each cluster starts with a non-combining codepoint */
    typedef struct { int start; int len; } cluster_t;
    cluster_t clusters[64];
    int cluster_count = 0;
    int i = 0;

    while (i < count && cluster_count < 64) {
        int start = i;
        i++;
        while (i < count && utf8_is_combining_mark(codepoints[i])) i++;
        clusters[cluster_count].start = start;
        clusters[cluster_count].len = i - start;
        cluster_count++;
    }

    /* Write clusters in reverse order */
    int out = 0;
    for (int c = cluster_count - 1; c >= 0; c--) {
        for (int j = 0; j < clusters[c].len; j++) {
            uint32_t cp = codepoints[clusters[c].start + j];
            reversed[out++] = mirror_bracket(cp);
        }
    }
    return out;
}

/**
 * Check if character is whitespace.
 */
static bool is_whitespace(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int bidi_reorder_line(const char *text, char *out_buf, int out_size) {
    if (!text || !out_buf || out_size < 1) return 0;

    int text_len = (int)strlen(text);
    if (text_len == 0) {
        out_buf[0] = '\0';
        return 0;
    }

    bool line_is_rtl = bidi_starts_with_rtl(text, 5);
    int out_pos = 0;

    if (line_is_rtl) {
        /* RTL base: walk word runs from end to start */
        const char *run_end = text + text_len;

        while (run_end > text && out_pos < out_size - 1) {
            bool is_space = is_whitespace((uint8_t)*(run_end - 1));
            const char *run_start = run_end - 1;

            while (run_start > text &&
                   is_whitespace((uint8_t)*(run_start - 1)) == is_space) {
                run_start--;
            }

            int run_len = (int)(run_end - run_start);

            if (is_space) {
                /* Copy whitespace as-is */
                int copy = (run_len < out_size - 1 - out_pos) ? run_len : out_size - 1 - out_pos;
                memcpy(out_buf + out_pos, run_start, copy);
                out_pos += copy;
            } else {
                /* Decode word into codepoints */
                uint32_t cps[64];
                int cp_count = 0;
                const uint8_t *p = (const uint8_t *)run_start;
                const uint8_t *end = (const uint8_t *)run_end;

                while (p < end && cp_count < 64) {
                    uint32_t cp = utf8_next_codepoint(&p);
                    if (cp == 0) break;
                    cps[cp_count++] = cp;
                }

                /* Check if word has RTL — if so, reverse clusters */
                bool word_has_rtl = false;
                for (int j = 0; j < cp_count; j++) {
                    if (bidi_is_rtl_codepoint(cps[j])) {
                        word_has_rtl = true;
                        break;
                    }
                }

                uint32_t output_cps[64];
                int output_count;

                if (word_has_rtl) {
                    output_count = reverse_grapheme_clusters(cps, cp_count, output_cps);
                } else {
                    memcpy(output_cps, cps, cp_count * sizeof(uint32_t));
                    output_count = cp_count;
                }

                /* Encode back to UTF-8 */
                for (int j = 0; j < output_count && out_pos < out_size - 4; j++) {
                    int written = utf8_encode(output_cps[j], out_buf + out_pos, out_size - out_pos);
                    out_pos += written;
                }
            }

            run_end = run_start;
        }
    } else {
        /* LTR base: keep word order, only reverse RTL words */
        const char *run_start = text;

        while (*run_start && out_pos < out_size - 1) {
            bool is_space = is_whitespace((uint8_t)*run_start);
            const char *run_end_ptr = run_start + 1;

            while (*run_end_ptr && is_whitespace((uint8_t)*run_end_ptr) == is_space) {
                run_end_ptr++;
            }

            int run_len = (int)(run_end_ptr - run_start);

            if (is_space) {
                int copy = (run_len < out_size - 1 - out_pos) ? run_len : out_size - 1 - out_pos;
                memcpy(out_buf + out_pos, run_start, copy);
                out_pos += copy;
            } else {
                uint32_t cps[64];
                int cp_count = 0;
                const uint8_t *p = (const uint8_t *)run_start;
                const uint8_t *end = (const uint8_t *)run_end_ptr;

                while (p < end && cp_count < 64) {
                    uint32_t cp = utf8_next_codepoint(&p);
                    if (cp == 0) break;
                    cps[cp_count++] = cp;
                }

                bool word_has_rtl = false;
                for (int j = 0; j < cp_count; j++) {
                    if (bidi_is_rtl_codepoint(cps[j])) {
                        word_has_rtl = true;
                        break;
                    }
                }

                uint32_t output_cps[64];
                int output_count;

                if (word_has_rtl) {
                    output_count = reverse_grapheme_clusters(cps, cp_count, output_cps);
                } else {
                    memcpy(output_cps, cps, cp_count * sizeof(uint32_t));
                    output_count = cp_count;
                }

                for (int j = 0; j < output_count && out_pos < out_size - 4; j++) {
                    int written = utf8_encode(output_cps[j], out_buf + out_pos, out_size - out_pos);
                    out_pos += written;
                }
            }

            run_start = run_end_ptr;
        }
    }

    out_buf[out_pos] = '\0';
    return out_pos;
}
