/**
 * @file utf8.c
 * @brief UTF-8 decoding, encoding, and combining mark detection.
 *        Ported from CrossPoint's Utf8.cpp and ScriptDetector.cpp.
 *
 * @status Complete
 * @issues None
 * @todo None
 */

#include "utf8.h"

static int utf8_codepoint_len(uint8_t c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

uint32_t utf8_next_codepoint(const uint8_t **str) {
    if (**str == 0) return 0;

    uint8_t lead = **str;
    int bytes = utf8_codepoint_len(lead);
    const uint8_t *chr = *str;

    /* Invalid lead byte (stray continuation 0x80-0xBF, or 0xFE/0xFF) */
    if (bytes == 1 && lead >= 0x80) {
        (*str)++;
        return REPLACEMENT_GLYPH;
    }

    if (bytes == 1) {
        (*str)++;
        return chr[0];
    }

    /* Validate continuation bytes */
    for (int i = 1; i < bytes; i++) {
        if ((chr[i] & 0xC0) != 0x80) {
            *str += i;
            return REPLACEMENT_GLYPH;
        }
    }

    uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);
    for (int i = 1; i < bytes; i++) {
        cp = (cp << 6) | (chr[i] & 0x3F);
    }

    /* Reject overlong encodings, surrogates, out-of-range */
    bool overlong = (bytes == 2 && cp < 0x80) ||
                    (bytes == 3 && cp < 0x800) ||
                    (bytes == 4 && cp < 0x10000);
    bool surrogate = (cp >= 0xD800 && cp <= 0xDFFF);

    if (overlong || surrogate || cp > 0x10FFFF) {
        (*str)++;
        return REPLACEMENT_GLYPH;
    }

    *str += bytes;
    return cp;
}

bool utf8_is_combining_mark(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F)      /* Combining Diacritical Marks */
        || (cp >= 0x0591 && cp <= 0x05BD)      /* Hebrew cantillation + nikkud */
        || (cp == 0x05BF)                      /* Hebrew point rafe */
        || (cp >= 0x05C1 && cp <= 0x05C2)      /* Hebrew shin/sin dot */
        || (cp >= 0x05C4 && cp <= 0x05C5)      /* Hebrew upper/lower dot */
        || (cp == 0x05C7)                      /* Hebrew qamats qatan */
        || (cp >= 0x1DC0 && cp <= 0x1DFF)      /* Combining Diacriticals Supplement */
        || (cp >= 0x20D0 && cp <= 0x20FF)      /* Combining for Symbols */
        || (cp >= 0xFE20 && cp <= 0xFE2F);     /* Combining Half Marks */
}

int utf8_safe_truncate(const char *buf, int len) {
    if (len <= 0) return 0;

    int lead_pos = len - 1;
    while (lead_pos > 0 && ((uint8_t)buf[lead_pos] & 0xC0) == 0x80) {
        lead_pos--;
    }

    int expected = utf8_codepoint_len((uint8_t)buf[lead_pos]);
    int actual = len - lead_pos;

    if (actual < expected && lead_pos > 0) {
        return lead_pos;
    }
    return len;
}

int utf8_encode(uint32_t cp, char *buf, int buf_size) {
    if (cp < 0x80) {
        if (buf_size < 1) return 0;
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (buf_size < 2) return 0;
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (buf_size < 3) return 0;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (buf_size < 4) return 0;
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
