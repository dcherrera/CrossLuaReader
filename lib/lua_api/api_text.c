/**
 * @file api_text.c
 * @brief Lua text.* module: C-side text indexing and page layout.
 *        Streams files from SD in 8KB chunks, never loads full file.
 *        Word wrapping uses font_render measurement directly — no Lua
 *        boundary crossing, glyph cache stays warm.
 *
 * @status Phase 9
 * @issues None
 * @todo None
 */

#include "api_text.h"
#include "lua.h"
#include "lauxlib.h"

#include "hal_storage.h"
#include "hal_system.h"
#include "font_manager.h"
#include "font_render.h"
#include "utf8.h"
#include "logging.h"

#include <string.h>
#include <stdlib.h>

#define CHUNK_SIZE      4096
#define MAX_LINE_BYTES  1024   /* max source line we'll handle */
#define MAX_PAGES       10000  /* safety limit */

/* ── UTF-8 helpers ─────────────────────────────────────────────── */

/**
 * Find the largest byte count that doesn't split a UTF-8 sequence.
 */
static int utf8_safe_len(const char *buf, int len) {
    if (len <= 0) return 0;
    int i = len;
    while (i > 0 && (buf[i - 1] & 0xC0) == 0x80) {
        i--;
    }
    if (i > 0) {
        uint8_t b = (uint8_t)buf[i - 1];
        int seq = 1;
        if (b >= 0xC0 && b <= 0xDF) seq = 2;
        else if (b >= 0xE0 && b <= 0xEF) seq = 3;
        else if (b >= 0xF0 && b <= 0xF7) seq = 4;
        if (i - 1 + seq > len) return i - 1;
    }
    return len;
}

/**
 * Advance past one UTF-8 codepoint, return its byte length.
 */
static int utf8_char_len(uint8_t b) {
    if (b < 0x80) return 1;
    if (b < 0xC0) return 1; /* continuation, shouldn't happen at start */
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    return 4;
}

/* ── Word wrap engine ──────────────────────────────────────────── */

/** Callback for each wrapped line during indexing (just counts lines). */
typedef void (*line_callback_t)(const char *line, int len, void *ctx);

/**
 * Word-wrap a single source line into the viewport width.
 * Calls cb() for each wrapped display line.
 * Uses font measurement for accurate wrapping.
 *
 * @param font_id    Font slot ID
 * @param font_path  Font file path for glyph reads
 * @param text       Source line (no newlines)
 * @param text_len   Length in bytes
 * @param vp_width   Viewport width in pixels
 * @param cb         Callback per wrapped line (can be NULL for counting)
 * @param ctx        Context for callback
 * @return           Number of wrapped display lines
 */
/**
 * Word wrap using CrossPoint's approach: measure actual substring width,
 * not accumulated individual word widths. More accurate with kerning.
 *
 * Algorithm:
 * 1. Check if full line fits → output as-is
 * 2. If too wide, find last space that keeps text within viewport
 * 3. If no space, break at UTF-8 character boundary
 * 4. Repeat for remaining text
 */
static int wrap_line(int font_id, const char *text, int text_len,
                     int vp_width, line_callback_t cb, void *ctx) {
    if (text_len == 0) {
        if (cb) cb("", 0, ctx);
        return 1;
    }

    if (!font_manager_get(font_id) || !font_manager_get_path(font_id)) {
        LOG_ERR("TEXT", "wrap_line: font %d not loaded!", font_id);
        if (cb) cb(text, text_len, ctx);
        return 1;
    }

    int lines = 0;
    const char *remaining = text;
    int remaining_len = text_len;

    /* Reusable buffer for measuring substrings */
    char measure_buf[MAX_LINE_BYTES];

    while (remaining_len > 0) {
        /* Measure the full remaining text */
        int copy = remaining_len < MAX_LINE_BYTES - 1 ? remaining_len : MAX_LINE_BYTES - 1;
        memcpy(measure_buf, remaining, copy);
        measure_buf[copy] = '\0';

        int full_w = font_render_get_advance_fb(font_id, measure_buf);

        if (full_w <= vp_width) {
            /* Entire remaining text fits on one line */
            /* Trim trailing spaces */
            int trim = copy;
            while (trim > 0 && remaining[trim - 1] == ' ') trim--;
            if (cb) cb(remaining, trim, ctx);
            lines++;
            break;
        }

        /* Text too wide — find break point.
         * Search for the last space where text before it fits. */
        int break_pos = copy;
        int last_space = -1;

        /* Scan for spaces and find the rightmost one that fits */
        for (int i = 0; i < copy; i++) {
            if (remaining[i] == ' ') {
                /* Measure text up to this space */
                memcpy(measure_buf, remaining, i);
                measure_buf[i] = '\0';
                int w = font_render_get_advance_fb(font_id, measure_buf);
                if (w <= vp_width) {
                    last_space = i;
                } else {
                    break;  /* past the limit, use last good space */
                }
            }
        }

        if (last_space > 0) {
            /* Break at the last space that fits */
            break_pos = last_space;
        } else {
            /* No space found — break at character boundary */
            /* Binary search: find how many chars fit */
            break_pos = 0;
            int i = 0;
            while (i < copy) {
                int clen = utf8_char_len((uint8_t)remaining[i]);
                int next = i + clen;
                if (next > copy) break;

                memcpy(measure_buf, remaining, next);
                measure_buf[next] = '\0';
                int w = font_render_get_advance_fb(font_id, measure_buf);
                if (w > vp_width) break;

                break_pos = next;
                i = next;
            }
            if (break_pos == 0 && copy > 0) {
                /* At least one character */
                break_pos = utf8_char_len((uint8_t)remaining[0]);
            }
        }

        /* Output the line up to break point */
        int out_len = break_pos;
        while (out_len > 0 && remaining[out_len - 1] == ' ') out_len--;
        if (cb) cb(remaining, out_len, ctx);
        lines++;

        /* Skip past break point + any trailing space */
        remaining += break_pos;
        remaining_len -= break_pos;
        while (remaining_len > 0 && *remaining == ' ') {
            remaining++;
            remaining_len--;
        }
    }

    return lines;
}

/*
 * Buffers allocated on demand — only when text.indexPages or
 * text.getPageLines is called. Freed immediately after.
 * Avoids 10KB permanent static BSS cost.
 */
static char *s_chunk_buf = NULL;
static char *s_line_buf = NULL;
static char *s_leftover = NULL;
static char *s_stripped = NULL;   /* for markdown stripping */

static bool alloc_buffers(void) {
    if (!s_chunk_buf) s_chunk_buf = (char *)malloc(CHUNK_SIZE + 1);
    if (!s_line_buf)  s_line_buf  = (char *)malloc(MAX_LINE_BYTES);
    if (!s_leftover)  s_leftover  = (char *)malloc(MAX_LINE_BYTES);
    if (!s_stripped)  s_stripped  = (char *)malloc(MAX_LINE_BYTES);
    if (!s_chunk_buf || !s_line_buf || !s_leftover || !s_stripped) {
        LOG_ERR("TEXT", "Buffer alloc failed (need %d bytes, free %u)",
                CHUNK_SIZE + 1 + MAX_LINE_BYTES * 2, hal_system_free_heap());
        free(s_chunk_buf); s_chunk_buf = NULL;
        free(s_line_buf);  s_line_buf  = NULL;
        free(s_leftover);  s_leftover  = NULL;
        free(s_stripped);  s_stripped  = NULL;
        return false;
    }
    return true;
}

static void free_buffers(void) {
    free(s_chunk_buf); s_chunk_buf = NULL;
    free(s_line_buf);  s_line_buf  = NULL;
    free(s_leftover);  s_leftover  = NULL;
    free(s_stripped);  s_stripped  = NULL;
}

/* ── text.indexPages ───────────────────────────────────────────── */

/*
 * text.indexPages(fontId, path, viewportWidth, linesPerPage) → table
 *
 * Streams the file in 8KB chunks, word-wraps each line with font
 * measurement, builds an array of byte offsets (one per page).
 * Returns a Lua table of integers. Never loads the full file.
 */
static int l_text_index_pages(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    int vp_width = (int)lua_tointeger(L, 3);
    int lines_per_page = (int)lua_tointeger(L, 4);

    if (lines_per_page < 1) lines_per_page = 1;
    if (vp_width < 10) vp_width = 10;

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    if (!alloc_buffers()) {
        lua_pushnil(L);
        lua_pushstring(L, "out of memory for text buffers");
        return 2;
    }

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        free_buffers();
        lua_pushnil(L);
        lua_pushstring(L, "cannot open file");
        return 2;
    }

    size_t file_size = hal_storage_file_size(f);

    /* Growable page offset array */
    int page_cap = 256;
    uint32_t *pages = (uint32_t *)malloc(page_cap * sizeof(uint32_t));
    if (!pages) {
        hal_storage_file_close(f);
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    int page_count = 0;
    pages[page_count++] = 0; /* first page at offset 0 */

    /* Use static buffers to avoid stack overflow */
    char *buf = s_chunk_buf;
    char *leftover = s_leftover;
    int leftover_len = 0;

    uint32_t file_offset = 0;
    int display_lines = 0;
    /* Track byte offset at start of current source line in the file */
    uint32_t line_file_offset = 0;

    while (file_offset < file_size) {
        int read_len = (int)(file_size - file_offset);
        if (read_len > CHUNK_SIZE) read_len = CHUNK_SIZE;

        int got = hal_storage_file_read(f, buf, read_len);
        if (got <= 0) break;

        int safe = utf8_safe_len(buf, got);
        buf[safe] = '\0';

        /* Process combined leftover + chunk */
        const char *p = buf;
        const char *chunk_end = buf + safe;

        while (p < chunk_end) {
            /* Find newline */
            const char *nl = (const char *)memchr(p, '\n', chunk_end - p);

            const char *line;
            int line_len;
            bool complete_line;

            if (nl) {
                line_len = (int)(nl - p);
                /* Strip \r */
                if (line_len > 0 && p[line_len - 1] == '\r') line_len--;
                line = p;
                complete_line = true;
            } else {
                /* No newline — save as leftover for next chunk */
                int remain = (int)(chunk_end - p);
                if (remain < MAX_LINE_BYTES) {
                    memcpy(leftover, p, remain);
                    leftover_len = remain;
                }
                break;
            }

            /* Build full line (leftover + current) */
            char *line_buf = s_line_buf;
            int full_len;

            if (leftover_len > 0) {
                full_len = leftover_len + line_len;
                if (full_len >= MAX_LINE_BYTES) full_len = MAX_LINE_BYTES - 1;
                memcpy(line_buf, leftover, leftover_len);
                int copy = full_len - leftover_len;
                if (copy > 0) memcpy(line_buf + leftover_len, line, copy);
                line_buf[full_len] = '\0';
                leftover_len = 0;
            } else {
                full_len = line_len < MAX_LINE_BYTES - 1 ? line_len : MAX_LINE_BYTES - 1;
                memcpy(line_buf, line, full_len);
                line_buf[full_len] = '\0';
            }

            /* Count display lines this source line produces */
            int wrapped = wrap_line(font_id, line_buf, full_len, vp_width, NULL, NULL);
            display_lines += wrapped;

            /* Advance past newline */
            p = nl + 1;
            line_file_offset = file_offset + (uint32_t)(p - buf);

            /* Page break? */
            if (display_lines >= lines_per_page) {
                if (line_file_offset < file_size && page_count < MAX_PAGES) {
                    /* Grow array if needed */
                    if (page_count >= page_cap) {
                        page_cap *= 2;
                        uint32_t *new_pages = (uint32_t *)realloc(pages, page_cap * sizeof(uint32_t));
                        if (!new_pages) {
                            LOG_ERR("TEXT", "realloc failed at %d pages", page_count);
                            break;
                        }
                        pages = new_pages;
                    }
                    pages[page_count++] = line_file_offset;
                }
                display_lines = 0;
            }
        }

        /* Handle leftover at end of chunk (no final newline) */
        if (p >= chunk_end && leftover_len == 0) {
            /* Entire chunk consumed, no leftover */
        }

        file_offset += safe;

        /* Skip bytes we couldn't use (partial UTF-8 at end) */
        if (safe < got) {
            hal_storage_file_seek(f, file_offset);
        }
    }

    hal_storage_file_close(f);

    /* Handle any final leftover (last line without newline) */
    if (leftover_len > 0 && display_lines > 0) {
        /* Already counted in current page */
    }

    LOG_INF("TEXT", "Indexed %d pages for %s (%u bytes)", page_count, path, (unsigned)file_size);

    /* Push result as Lua table */
    lua_createtable(L, page_count, 0);
    for (int i = 0; i < page_count; i++) {
        lua_pushinteger(L, (lua_Integer)pages[i]);
        lua_rawseti(L, -2, i + 1);
    }

    free(pages);
    free_buffers();
    return 1;
}

/* ── text.getPageLines ─────────────────────────────────────────── */

/* Callback context for collecting wrapped lines into a Lua table */
typedef struct {
    lua_State *L;
    int table_idx;
    int count;
    int max_lines;
} collect_ctx_t;

static void collect_line(const char *line, int len, void *ctx) {
    collect_ctx_t *c = (collect_ctx_t *)ctx;
    if (c->count >= c->max_lines) return;
    c->count++;
    lua_pushlstring(c->L, line, len);
    lua_rawseti(c->L, c->table_idx, c->count);
}

/*
 * text.getPageLines(fontId, path, offset, viewportWidth, linesPerPage) → table
 *
 * Reads one page worth of text starting at byte offset, word-wraps
 * with font measurement, returns a table of display line strings.
 */
static int l_text_get_page_lines(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    uint32_t offset = (uint32_t)lua_tointeger(L, 3);
    int vp_width = (int)lua_tointeger(L, 4);
    int lines_per_page = (int)lua_tointeger(L, 5);

    if (lines_per_page < 1) lines_per_page = 1;

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    if (!alloc_buffers()) {
        lua_pushnil(L);
        return 1;
    }

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        free_buffers();
        lua_pushnil(L);
        return 1;
    }

    hal_storage_file_seek(f, offset);

    char *buf = s_chunk_buf;
    int got = hal_storage_file_read(f, buf, CHUNK_SIZE);
    hal_storage_file_close(f);

    if (got <= 0) {
        free_buffers();
        lua_newtable(L);
        return 1;
    }

    int safe = utf8_safe_len(buf, got);
    buf[safe] = '\0';

    /* Create result table */
    lua_createtable(L, lines_per_page, 0);
    int table_idx = lua_gettop(L);

    collect_ctx_t ctx;
    ctx.L = L;
    ctx.table_idx = table_idx;
    ctx.count = 0;
    ctx.max_lines = lines_per_page;

    /* Process source lines */
    const char *p = buf;
    const char *end = buf + safe;

    while (p < end && ctx.count < lines_per_page) {
        const char *nl = (const char *)memchr(p, '\n', end - p);
        int line_len;

        if (nl) {
            line_len = (int)(nl - p);
            if (line_len > 0 && p[line_len - 1] == '\r') line_len--;
        } else {
            line_len = (int)(end - p);
        }

        if (line_len >= MAX_LINE_BYTES) line_len = MAX_LINE_BYTES - 1;

        /* Null-terminate for wrap_line */
        char *line_buf = s_line_buf;
        memcpy(line_buf, p, line_len);
        line_buf[line_len] = '\0';

        wrap_line(font_id, line_buf, line_len, vp_width, collect_line, &ctx);

        p = nl ? (nl + 1) : end;
    }

    free_buffers();
    return 1;
}

/* ── Markdown helpers ──────────────────────────────────────────── */

/**
 * Strip inline markdown syntax for width measurement.
 * Removes **, *, __, _, `, [text](url) markers.
 * Returns length of stripped text in dst.
 */
static int strip_markdown(const char *src, int src_len, char *dst, int dst_size) {
    int si = 0, di = 0;

    while (si < src_len && di < dst_size - 1) {
        /* Code span: `text` */
        if (src[si] == '`') {
            si++;
            while (si < src_len && src[si] != '`' && di < dst_size - 1) {
                dst[di++] = src[si++];
            }
            if (si < src_len) si++;  /* skip closing ` */
            continue;
        }

        /* Bold: ** or __ */
        if (si + 1 < src_len &&
            ((src[si] == '*' && src[si+1] == '*') ||
             (src[si] == '_' && src[si+1] == '_'))) {
            si += 2;
            continue;
        }

        /* Italic: * or _ (single, not at word boundary for _ in middle) */
        if (src[si] == '*' || src[si] == '_') {
            si++;
            continue;
        }

        /* Link: [text](url) — keep text, skip url */
        if (src[si] == '[') {
            si++;  /* skip [ */
            while (si < src_len && src[si] != ']' && di < dst_size - 1) {
                dst[di++] = src[si++];
            }
            if (si < src_len) si++;  /* skip ] */
            /* Skip (url) if present */
            if (si < src_len && src[si] == '(') {
                si++;
                while (si < src_len && src[si] != ')') si++;
                if (si < src_len) si++;  /* skip ) */
            }
            continue;
        }

        dst[di++] = src[si++];
    }

    dst[di] = '\0';
    return di;
}

/**
 * Detect markdown block type from a source line.
 * Returns: 0=paragraph, 1=h1, 2=h2, 3=h3, 4=code_fence, 5=hr,
 *          6=list, 7=blockquote, 8=blank
 * Sets *content_start to index where actual text starts.
 * Sets *indent to list nesting depth (for lists).
 */
static int detect_block_type(const char *line, int len, int *content_start, int *indent) {
    *content_start = 0;
    *indent = 0;

    /* Blank line */
    if (len == 0) return 8;
    {
        int i = 0;
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= len) return 8;
    }

    /* Code fence */
    if (len >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') return 4;

    /* HR */
    if (len >= 3) {
        int dashes = 0, stars = 0, underscores = 0;
        for (int i = 0; i < len; i++) {
            if (line[i] == '-') dashes++;
            else if (line[i] == '*') stars++;
            else if (line[i] == '_') underscores++;
            else if (line[i] != ' ') break;
        }
        if (dashes >= 3 || stars >= 3 || underscores >= 3) return 5;
    }

    /* Headers — check longest match first */
    if (line[0] == '#') {
        if (len >= 6 && line[1] == '#' && line[2] == '#' && line[3] == '#' && line[4] == '#' && line[5] == ' ') {
            *content_start = 6;
            return 10;  /* h5 */
        }
        if (len >= 5 && line[1] == '#' && line[2] == '#' && line[3] == '#' && line[4] == ' ') {
            *content_start = 5;
            return 9;  /* h4 */
        }
        if (len >= 4 && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
            *content_start = 4;
            return 3;  /* h3 */
        }
        if (len >= 3 && line[1] == '#' && line[2] == ' ') {
            *content_start = 3;
            return 2;  /* h2 */
        }
        if (len >= 2 && line[1] == ' ') {
            *content_start = 2;
            return 1;  /* h1 */
        }
    }

    /* Blockquote */
    if (line[0] == '>') {
        *content_start = (len > 1 && line[1] == ' ') ? 2 : 1;
        return 7;
    }

    /* List: count leading spaces for indent */
    {
        int i = 0;
        while (i < len && line[i] == ' ') i++;
        *indent = i / 2;

        if (i < len && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
            i + 1 < len && line[i+1] == ' ') {
            *content_start = i + 2;
            return 6;
        }
        /* Ordered list */
        if (i < len && line[i] >= '0' && line[i] <= '9') {
            int j = i;
            while (j < len && line[j] >= '0' && line[j] <= '9') j++;
            if (j < len && line[j] == '.' && j + 1 < len && line[j+1] == ' ') {
                *content_start = j + 2;
                return 6;
            }
        }
    }

    return 0;  /* paragraph */
}

/* ── text.indexMarkdownPages ──────────────────────────────────── */

/*
 * text.indexMarkdownPages(fontId, path, viewportWidth, linesPerPage)
 *   → page_offsets_table, code_state_table
 *
 * Streams markdown file, strips syntax for measurement, word-wraps
 * with real font metrics. Tracks code block state across pages.
 */
static int l_text_index_markdown_pages(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    int vp_width = (int)lua_tointeger(L, 3);
    int lines_per_page = (int)lua_tointeger(L, 4);

    if (lines_per_page < 1) lines_per_page = 1;
    if (vp_width < 10) vp_width = 10;

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    if (!alloc_buffers()) {
        lua_pushnil(L);
        lua_pushstring(L, "out of memory for text buffers");
        return 2;
    }

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        free_buffers();
        lua_pushnil(L);
        lua_pushstring(L, "cannot open file");
        return 2;
    }

    size_t file_size = hal_storage_file_size(f);

    int page_cap = 256;
    uint32_t *pages = (uint32_t *)malloc(page_cap * sizeof(uint32_t));
    uint8_t *code_states = (uint8_t *)malloc(page_cap);
    if (!pages || !code_states) {
        free(pages);
        free(code_states);
        hal_storage_file_close(f);
        free_buffers();
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }

    int page_count = 0;
    pages[page_count] = 0;
    code_states[page_count] = 0;
    page_count++;

    char *buf = s_chunk_buf;
    char *leftover = s_leftover;
    int leftover_len = 0;
    char *stripped = s_stripped;

    uint32_t file_offset = 0;
    int display_lines = 0;
    bool in_code = false;

    while (file_offset < file_size) {
        int read_len = (int)(file_size - file_offset);
        if (read_len > CHUNK_SIZE) read_len = CHUNK_SIZE;

        int got = hal_storage_file_read(f, buf, read_len);
        if (got <= 0) break;

        int safe = utf8_safe_len(buf, got);
        buf[safe] = '\0';

        const char *p = buf;
        const char *chunk_end = buf + safe;

        while (p < chunk_end) {
            const char *nl = (const char *)memchr(p, '\n', chunk_end - p);
            const char *line;
            int line_len;

            if (nl) {
                line_len = (int)(nl - p);
                if (line_len > 0 && p[line_len - 1] == '\r') line_len--;
                line = p;
            } else {
                int remain = (int)(chunk_end - p);
                if (remain < MAX_LINE_BYTES) {
                    memcpy(leftover, p, remain);
                    leftover_len = remain;
                }
                break;
            }

            /* Build full line with leftover */
            char *line_buf = s_line_buf;
            int full_len;
            if (leftover_len > 0) {
                full_len = leftover_len + line_len;
                if (full_len >= MAX_LINE_BYTES) full_len = MAX_LINE_BYTES - 1;
                memcpy(line_buf, leftover, leftover_len);
                int copy = full_len - leftover_len;
                if (copy > 0) memcpy(line_buf + leftover_len, line, copy);
                line_buf[full_len] = '\0';
                leftover_len = 0;
            } else {
                full_len = line_len < MAX_LINE_BYTES - 1 ? line_len : MAX_LINE_BYTES - 1;
                memcpy(line_buf, line, full_len);
                line_buf[full_len] = '\0';
            }

            /* Check code fence toggle first */
            if (full_len >= 3 && line_buf[0] == '`' && line_buf[1] == '`' && line_buf[2] == '`') {
                in_code = !in_code;
                p = nl + 1;
                continue;
            }

            /* Count display lines */
            int block_lines = 0;
            int avail_w = vp_width;

            if (in_code) {
                /* code block line — no block detection, no stripping */
                block_lines = wrap_line(font_id, line_buf, full_len, vp_width - 16, NULL, NULL);
            } else {

            /* Detect block type (only outside code blocks) */
            int content_start = 0, indent = 0;
            int block_type = detect_block_type(line_buf, full_len, &content_start, &indent);

            if (block_type == 8) {
                /* blank */
                block_lines = 1;
            } else if (block_type == 5) {
                /* hr */
                block_lines = 1;
            } else {
                /* H1/H2 take 2 display lines, everything else takes 1 per wrapped line */
                if (block_type == 1 || block_type == 2) {
                    block_lines += 1;  /* extra line for header height */
                }

                /* Reduce width for indented blocks */
                if (block_type == 6) avail_w -= (indent + 1) * 20;  /* list */
                if (block_type == 7) avail_w -= 20;  /* blockquote */
                if (avail_w < 50) avail_w = 50;

                /* Strip markdown for measurement */
                const char *content = line_buf + content_start;
                int content_len = full_len - content_start;
                int stripped_len = strip_markdown(content, content_len, stripped, MAX_LINE_BYTES);
                block_lines += wrap_line(font_id, stripped, stripped_len, avail_w, NULL, NULL);
            }
            } /* end non-code block */

            display_lines += block_lines;

            p = nl + 1;
            uint32_t line_file_offset = file_offset + (uint32_t)(p - buf);

            /* Page break */
            if (display_lines >= lines_per_page) {
                if (line_file_offset < file_size && page_count < MAX_PAGES) {
                    if (page_count >= page_cap) {
                        page_cap *= 2;
                        uint32_t *np = (uint32_t *)realloc(pages, page_cap * sizeof(uint32_t));
                        uint8_t *nc = (uint8_t *)realloc(code_states, page_cap);
                        if (!np || !nc) { free(np ? np : pages); free(nc ? nc : code_states); break; }
                        pages = np;
                        code_states = nc;
                    }
                    pages[page_count] = line_file_offset;
                    code_states[page_count] = in_code ? 1 : 0;
                    page_count++;
                }
                display_lines = 0;
            }
        }

        file_offset += safe;
        if (safe < got) hal_storage_file_seek(f, file_offset);
    }

    hal_storage_file_close(f);
    free_buffers();

    LOG_INF("TEXT", "MD indexed %d pages for %s (%u bytes)", page_count, path, (unsigned)file_size);

    /* Return two tables: offsets and code states */
    lua_createtable(L, page_count, 0);
    for (int i = 0; i < page_count; i++) {
        lua_pushinteger(L, (lua_Integer)pages[i]);
        lua_rawseti(L, -2, i + 1);
    }

    lua_createtable(L, page_count, 0);
    for (int i = 0; i < page_count; i++) {
        lua_pushboolean(L, code_states[i]);
        lua_rawseti(L, -2, i + 1);
    }

    free(pages);
    free(code_states);
    return 2;
}

/* ── text.wrapString ──────────────────────────────────────────── */

/*
 * text.wrapString(fontId, str, viewportWidth) → table of strings
 *
 * Word-wraps a string with real font measurement.
 * No file I/O — works on in-memory strings.
 */
static int l_text_wrap_string(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    size_t str_len;
    const char *str = luaL_checklstring(L, 2, &str_len);
    int vp_width = (int)lua_tointeger(L, 3);

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    lua_createtable(L, 4, 0);
    int table_idx = lua_gettop(L);

    collect_ctx_t ctx;
    ctx.L = L;
    ctx.table_idx = table_idx;
    ctx.count = 0;
    ctx.max_lines = 1000;  /* no practical limit */

    wrap_line(font_id, str, (int)str_len, vp_width, collect_line, &ctx);

    return 1;
}

/* ── Inline span parser ────────────────────────────────────────── */

/* Style IDs matching Lua side */
#define STYLE_NORMAL  0
#define STYLE_BOLD    1
#define STYLE_ITALIC  2
#define STYLE_CODE    3
#define STYLE_LINK    4

typedef struct {
    const char *text;
    int         len;
    int         style;
} span_t;

#define MAX_SPANS 64

/**
 * Parse inline markdown spans from a line.
 * Produces an array of span_t with text pointers into src.
 * Returns number of spans.
 */
static int parse_inline_spans(const char *src, int src_len,
                               span_t *spans, int max_spans) {
    int count = 0;
    int i = 0;

    while (i < src_len && count < max_spans) {
        /* Code: `text` */
        if (src[i] == '`') {
            int close = i + 1;
            while (close < src_len && src[close] != '`') close++;
            if (close < src_len) {
                spans[count].text = src + i + 1;
                spans[count].len = close - i - 1;
                spans[count].style = STYLE_CODE;
                count++;
                i = close + 1;
                continue;
            }
        }

        /* Bold: ** */
        if (i + 1 < src_len && src[i] == '*' && src[i+1] == '*') {
            int close = i + 2;
            while (close + 1 < src_len && !(src[close] == '*' && src[close+1] == '*')) close++;
            if (close + 1 < src_len) {
                spans[count].text = src + i + 2;
                spans[count].len = close - i - 2;
                spans[count].style = STYLE_BOLD;
                count++;
                i = close + 2;
                continue;
            }
        }

        /* Bold: __ */
        if (i + 1 < src_len && src[i] == '_' && src[i+1] == '_') {
            int close = i + 2;
            while (close + 1 < src_len && !(src[close] == '_' && src[close+1] == '_')) close++;
            if (close + 1 < src_len) {
                spans[count].text = src + i + 2;
                spans[count].len = close - i - 2;
                spans[count].style = STYLE_BOLD;
                count++;
                i = close + 2;
                continue;
            }
        }

        /* Italic: * (single) */
        if (src[i] == '*' && (i + 1 >= src_len || src[i+1] != '*')) {
            int close = i + 1;
            while (close < src_len && src[close] != '*') close++;
            if (close < src_len) {
                spans[count].text = src + i + 1;
                spans[count].len = close - i - 1;
                spans[count].style = STYLE_ITALIC;
                count++;
                i = close + 1;
                continue;
            }
        }

        /* Italic: _ (single) */
        if (src[i] == '_' && (i + 1 >= src_len || src[i+1] != '_')) {
            int close = i + 1;
            while (close < src_len && src[close] != '_') close++;
            if (close < src_len) {
                spans[count].text = src + i + 1;
                spans[count].len = close - i - 1;
                spans[count].style = STYLE_ITALIC;
                count++;
                i = close + 1;
                continue;
            }
        }

        /* Link: [text](url) — keep text only */
        if (src[i] == '[') {
            int cb = i + 1;
            while (cb < src_len && src[cb] != ']') cb++;
            if (cb < src_len && cb + 1 < src_len && src[cb+1] == '(') {
                int cp = cb + 2;
                while (cp < src_len && src[cp] != ')') cp++;
                if (cp < src_len) {
                    spans[count].text = src + i + 1;
                    spans[count].len = cb - i - 1;
                    spans[count].style = STYLE_LINK;
                    count++;
                    i = cp + 1;
                    continue;
                }
            }
        }

        /* Normal text: collect until next special char */
        int start = i;
        i++;
        while (i < src_len && src[i] != '`' && src[i] != '*' &&
               src[i] != '_' && src[i] != '[') {
            i++;
        }
        spans[count].text = src + start;
        spans[count].len = i - start;
        spans[count].style = STYLE_NORMAL;
        count++;
    }

    return count;
}

/* ── text.renderMarkdownPage helpers ───────────────────────────── */

/* Collected wrapped lines for one source line (before calling Lua).
 * A single source line rarely wraps to more than 8 display lines.
 * Each line is max 128 chars (viewport is ~60 chars wide at size 14). */
#define MAX_WRAPPED_LINES 8
#define MAX_WRAPPED_LEN   256
typedef struct {
    char   lines[MAX_WRAPPED_LINES][MAX_WRAPPED_LEN];
    int    lens[MAX_WRAPPED_LINES];
    int    count;
} wrapped_block_t;

static wrapped_block_t s_wrapped;

static void collect_wrapped(const char *line, int len, void *ctx) {
    (void)ctx;
    if (s_wrapped.count >= MAX_WRAPPED_LINES) return;
    int copy = len < MAX_WRAPPED_LEN - 1 ? len : MAX_WRAPPED_LEN - 1;
    memcpy(s_wrapped.lines[s_wrapped.count], line, copy);
    s_wrapped.lines[s_wrapped.count][copy] = '\0';
    s_wrapped.lens[s_wrapped.count] = copy;
    s_wrapped.count++;
}

static const char *type_names[] = {
    "paragraph", "h1", "h2", "h3", "code_fence", "hr",
    "list", "blockquote", "blank", "h4", "h5"
};

/**
 * Call Lua callback for one wrapped line with parsed spans.
 * Called AFTER wrap_line returns (not inside it) to avoid deep stack.
 */
static void emit_to_lua(lua_State *L, int cb_ref, int block_type,
                          const char *line, int len, int indent, bool is_code) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);

    /* Arg 1: block type */
    int bt = is_code ? 4 : block_type;
    lua_pushstring(L, (bt >= 0 && bt <= 10) ? type_names[bt] : "paragraph");

    /* Arg 2: line text */
    lua_pushlstring(L, line, len);

    /* Arg 3: spans table */
    span_t line_spans[MAX_SPANS];
    int ls_count;

    if (is_code) {
        line_spans[0].text = line;
        line_spans[0].len = len;
        line_spans[0].style = STYLE_CODE;
        ls_count = 1;
    } else {
        ls_count = parse_inline_spans(line, len, line_spans, MAX_SPANS);
    }

    lua_createtable(L, ls_count, 0);
    for (int i = 0; i < ls_count; i++) {
        lua_createtable(L, 0, 2);
        lua_pushlstring(L, line_spans[i].text, line_spans[i].len);
        lua_setfield(L, -2, "text");
        lua_pushinteger(L, line_spans[i].style);
        lua_setfield(L, -2, "style");
        lua_rawseti(L, -2, i + 1);
    }

    /* Arg 4: indent */
    lua_pushinteger(L, indent);

    if (lua_pcall(L, 4, 0, 0) != 0) {
        const char *err = lua_tostring(L, -1);
        LOG_ERR("TEXT", "MD render cb: %s", err ? err : "?");
        lua_pop(L, 1);
    }
}

/*
 * text.renderMarkdownPage(fontId, path, offset, viewportWidth,
 *                          linesPerPage, inCodeBlock, callback)
 *
 * Reads one page of markdown, parses blocks, strips and wraps text
 * with real font metrics, calls callback per wrapped line with
 * block type and styled spans. Zero intermediate tables.
 *
 * Callback: function(block_type, line_text, spans, indent)
 *   block_type: string
 *   line_text: string (plain wrapped text)
 *   spans: table of {text=string, style=int}
 *   indent: int
 *
 * Returns: bool (true if was in code block at end of page)
 */
static int l_text_render_markdown_page(lua_State *L) {
    int font_id = (int)lua_tointeger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    uint32_t offset = (uint32_t)lua_tointeger(L, 3);
    int vp_width = (int)lua_tointeger(L, 4);
    int lines_per_page = (int)lua_tointeger(L, 5);
    bool in_code = lua_toboolean(L, 6);
    luaL_checktype(L, 7, LUA_TFUNCTION);

    if (!font_manager_get(font_id)) {
        return luaL_error(L, "invalid font id: %d", font_id);
    }

    if (!alloc_buffers()) {
        lua_pushboolean(L, in_code);
        return 1;
    }

    /* Store callback in registry */
    lua_pushvalue(L, 7);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    hal_file_t f = hal_storage_open(path, HAL_FILE_READ);
    if (!f) {
        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
        free_buffers();
        lua_pushboolean(L, in_code);
        return 1;
    }

    size_t file_size = hal_storage_file_size(f);
    hal_storage_file_seek(f, offset);

    int read_len = (int)(file_size - offset);
    if (read_len > CHUNK_SIZE) read_len = CHUNK_SIZE;

    char *buf = s_chunk_buf;
    int got = hal_storage_file_read(f, buf, read_len);
    hal_storage_file_close(f);

    if (got <= 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
        free_buffers();
        lua_pushboolean(L, in_code);
        return 1;
    }

    int safe = utf8_safe_len(buf, got);
    buf[safe] = '\0';

    int lines_emitted = 0;
    bool is_code = in_code;

    char *stripped = s_stripped;
    char *line_buf = s_line_buf;

    const char *p = buf;
    const char *end = buf + safe;

    while (p < end && lines_emitted < lines_per_page) {
        /* Extract one source line */
        const char *nl = (const char *)memchr(p, '\n', end - p);
        int line_len;
        if (nl) {
            line_len = (int)(nl - p);
            if (line_len > 0 && p[line_len - 1] == '\r') line_len--;
        } else {
            line_len = (int)(end - p);
        }
        if (line_len >= MAX_LINE_BYTES) line_len = MAX_LINE_BYTES - 1;
        memcpy(line_buf, p, line_len);
        line_buf[line_len] = '\0';

        /* Check for code fence toggle first */
        if (line_len >= 3 && line_buf[0] == '`' && line_buf[1] == '`' && line_buf[2] == '`') {
            is_code = !is_code;
            p = nl ? (nl + 1) : end;
            continue;
        }

        /* Inside code block: skip block detection, use raw line */
        if (is_code) {
            int avail_w_code = vp_width - 16;
            if (avail_w_code < 50) avail_w_code = 50;

            s_wrapped.count = 0;
            wrap_line(font_id, line_buf, line_len, avail_w_code, collect_wrapped, NULL);

            for (int i = 0; i < s_wrapped.count && lines_emitted < lines_per_page; i++) {
                emit_to_lua(L, cb_ref, 4, /* code_fence type */
                            s_wrapped.lines[i], s_wrapped.lens[i],
                            0, true);
                lines_emitted++;
            }

            p = nl ? (nl + 1) : end;
            continue;
        }

        /* Detect block type (only for non-code lines) */
        int content_start = 0, indent = 0;
        int block_type = detect_block_type(line_buf, line_len, &content_start, &indent);

        /* Blank line */
        if (block_type == 8) {
            emit_to_lua(L, cb_ref, 8, "", 0, 0, false);
            lines_emitted++;
            p = nl ? (nl + 1) : end;
            continue;
        }

        /* HR */
        if (block_type == 5) {
            emit_to_lua(L, cb_ref, 5, "", 0, 0, false);
            lines_emitted++;
            p = nl ? (nl + 1) : end;
            continue;
        }

        /* No artificial header spacing — headers use their 2-line height
         * for visual separation. Blank lines in source provide spacing. */

        /* Get content text */
        const char *content = line_buf + content_start;
        int content_len = line_len - content_start;

        /* Calculate available width */
        int avail_w = vp_width;
        if (block_type == 6) avail_w -= (indent + 1) * 20;
        if (block_type == 7) avail_w -= 20;
        if (is_code) avail_w -= 16;
        if (avail_w < 50) avail_w = 50;

        /* Word wrap into static buffer (NOT into Lua callback) */
        s_wrapped.count = 0;
        if (is_code) {
            wrap_line(font_id, content, content_len, avail_w, collect_wrapped, NULL);
        } else {
            int stripped_len = strip_markdown(content, content_len, stripped, MAX_LINE_BYTES);
            wrap_line(font_id, stripped, stripped_len, avail_w, collect_wrapped, NULL);
        }
        if (s_wrapped.count > 1) {
            LOG_INF("TEXT", "Wrapped %d lines (type=%d, len=%d, w=%d)",
                    s_wrapped.count, block_type, content_len, avail_w);
        }

        /* Now call Lua for each wrapped line (stack is clean here) */
        for (int i = 0; i < s_wrapped.count && lines_emitted < lines_per_page; i++) {
            emit_to_lua(L, cb_ref, block_type,
                        s_wrapped.lines[i], s_wrapped.lens[i],
                        indent, is_code);
            lines_emitted++;
        }

        p = nl ? (nl + 1) : end;
    }

    luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
    free_buffers();

    lua_pushboolean(L, is_code);
    return 1;
}

/* ── Registration ──────────────────────────────────────────────── */

void api_text_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"indexPages",          l_text_index_pages},
        {"getPageLines",        l_text_get_page_lines},
        {"indexMarkdownPages",  l_text_index_markdown_pages},
        {"wrapString",          l_text_wrap_string},
        {"renderMarkdownPage",  l_text_render_markdown_page},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "text");
}
