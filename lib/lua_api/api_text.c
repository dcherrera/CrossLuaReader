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
static int wrap_line(int font_id, const char *text, int text_len,
                     int vp_width, line_callback_t cb, void *ctx) {
    if (text_len == 0) {
        if (cb) cb("", 0, ctx);
        return 1;
    }

    const font_data_t *font = font_manager_get(font_id);
    const char *font_path = font_manager_get_path(font_id);
    if (!font || !font_path) {
        if (cb) cb(text, text_len, ctx);
        return 1;
    }

    /* Measure space width once */
    int space_w = font_render_get_advance_fb(font_id, " ");
    int lines = 0;

    const char *p = text;
    const char *end = text + text_len;

    /* Current line tracking */
    const char *line_start = p;
    int line_w = 0;
    bool has_content = false;

    while (p < end) {
        /* Find next word */
        const char *word_start = p;

        /* Skip leading spaces */
        while (p < end && *p == ' ') p++;
        if (p >= end && !has_content) {
            /* Trailing spaces only */
            break;
        }
        if (p > word_start && has_content) {
            /* There were spaces — they act as potential break points */
        }

        /* Scan word */
        const char *ws = p;
        while (p < end && *p != ' ') {
            p += utf8_char_len((uint8_t)*p);
            if (p > end) p = end;
        }
        int word_len = (int)(p - ws);
        if (word_len == 0) continue;

        /* Measure the word */
        char word_buf[MAX_LINE_BYTES];
        int copy_len = word_len < MAX_LINE_BYTES - 1 ? word_len : MAX_LINE_BYTES - 1;
        memcpy(word_buf, ws, copy_len);
        word_buf[copy_len] = '\0';

        int word_w = font_render_get_advance_fb(font_id, word_buf);

        /* Would this word fit on the current line? */
        int needed = has_content ? (space_w + word_w) : word_w;

        if (has_content && line_w + needed > vp_width) {
            /* Flush current line */
            int flush_len = (int)(ws - line_start);
            /* Trim trailing spaces */
            while (flush_len > 0 && line_start[flush_len - 1] == ' ') flush_len--;

            if (cb) cb(line_start, flush_len, ctx);
            lines++;

            line_start = ws;
            line_w = word_w;
            has_content = true;

            /* Single word wider than viewport — break it */
            if (word_w > vp_width) {
                const char *wp = ws;
                const char *wp_end = ws + word_len;
                const char *break_start = wp;
                int bw = 0;

                while (wp < wp_end) {
                    int clen = utf8_char_len((uint8_t)*wp);
                    char ch_buf[8];
                    int cl = clen < 7 ? clen : 7;
                    memcpy(ch_buf, wp, cl);
                    ch_buf[cl] = '\0';
                    int ch_w = font_render_get_advance_fb(font_id, ch_buf);

                    if (bw + ch_w > vp_width && wp > break_start) {
                        if (cb) cb(break_start, (int)(wp - break_start), ctx);
                        lines++;
                        break_start = wp;
                        bw = 0;
                    }
                    bw += ch_w;
                    wp += clen;
                }
                /* Remainder becomes the new current line */
                line_start = break_start;
                line_w = bw;
            }
        } else {
            line_w += needed;
            has_content = true;
        }
    }

    /* Flush remaining */
    if (has_content || line_start == text) {
        int flush_len = (int)(end - line_start);
        while (flush_len > 0 && line_start[flush_len - 1] == ' ') flush_len--;
        if (cb) cb(line_start, flush_len, ctx);
        lines++;
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

    /* Headers */
    if (line[0] == '#') {
        if (len >= 4 && line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
            *content_start = 4;
            return 3;
        }
        if (len >= 3 && line[0] == '#' && line[1] == '#' && line[2] == ' ') {
            *content_start = 3;
            return 2;
        }
        if (len >= 2 && line[0] == '#' && line[1] == ' ') {
            *content_start = 2;
            return 1;
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

            /* Detect block type */
            int content_start = 0, indent = 0;
            int block_type = detect_block_type(line_buf, full_len, &content_start, &indent);

            /* Handle code fence toggle */
            if (block_type == 4) {
                in_code = !in_code;
                p = nl + 1;
                continue;
            }

            /* Count display lines */
            int block_lines = 0;
            int avail_w = vp_width;

            if (block_type == 8) {
                /* blank */
                block_lines = 1;
            } else if (block_type == 5) {
                /* hr */
                block_lines = 1;
            } else if (in_code) {
                /* code block line — no stripping, indent reduces width */
                block_lines = wrap_line(font_id, line_buf, full_len, vp_width - 16, NULL, NULL);
            } else {
                /* Reduce width for indented blocks */
                if (block_type == 6) avail_w -= (indent + 1) * 20;  /* list */
                if (block_type == 7) avail_w -= 20;  /* blockquote */
                if (avail_w < 50) avail_w = 50;

                /* Strip markdown for measurement */
                const char *content = line_buf + content_start;
                int content_len = full_len - content_start;
                int stripped_len = strip_markdown(content, content_len, stripped, MAX_LINE_BYTES);
                block_lines = wrap_line(font_id, stripped, stripped_len, avail_w, NULL, NULL);

                /* Headers get extra spacing */
                if (block_type == 1 || block_type == 2) block_lines += 1;
            }

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

/* ── Registration ──────────────────────────────────────────────── */

void api_text_register(lua_State *L) {
    static const luaL_Reg funcs[] = {
        {"indexPages",          l_text_index_pages},
        {"getPageLines",        l_text_get_page_lines},
        {"indexMarkdownPages",  l_text_index_markdown_pages},
        {"wrapString",          l_text_wrap_string},
        {NULL, NULL}
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "text");
}
