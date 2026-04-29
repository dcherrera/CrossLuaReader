# EPUB Reader — Lua API Requirements Spec

**Status:** Draft. Pre-implementation. Source-of-truth for what C must expose so a Lua-based EPUB reader plugin can be built without RAM or speed surprises.

**Audience:** Whoever writes the phased Phase-9 build plan after this spec is agreed. The build plan will sequence what gets implemented when; this spec only enumerates *what* must exist.

**Context:** ESP32-C3 (RISC-V, 380KB usable RAM, no PSRAM, single 48KB framebuffer, monochrome 800x480 e-ink). Runtime is pure C; plugins are Lua 5.4. Reference implementation for proven-on-this-hardware feasibility: `crosspoint-reader/lib/Epub/`.

---

## 1. Goals and non-goals

### Goals (v1)

- Open EPUB 2.0.1 and EPUB 3.x reflowable books from SD card.
- Read text with proper word wrap, paragraph spacing, headings, basic emphasis (bold, italic, code).
- Honor reading order (spine), hierarchical TOC, and intra-book anchor links.
- Render inline images (JPEG, PNG) sized to fit the column.
- Persist reading position across reboots; resume on the same chapter and offset.
- Cache pagination tables to SD so re-opens are fast.
- Honor `page-progression-direction` (LTR/RTL) and `dir="rtl"` for Hebrew/Arabic.
- Recognize and reject DRM-protected books cleanly (no crash, clear message).
- Distinguish font obfuscation (IDPF/Adobe) from real DRM so font-obfuscated books still open.

### Non-goals (deferred to v2 or out-of-scope)

- Embedded font rendering (`@font-face`) — render with on-device `.cfont` set, map `font-family` family-hint only.
- CFI (Canonical Fragment Identifier) generation/parse — use `{spine_index, char_offset}` instead.
- Hyphenation (Liang/dictionary-based) — greedy first-fit line break is sufficient.
- Knuth-Plass total-fit line breaking — greedy is fine on e-ink.
- Fixed-layout EPUBs (`rendition:layout="pre-paginated"`) — detect and warn, don't render.
- Scripted content (`properties="scripted"`) — render text statically, ignore JS.
- Media overlays (audio sync), MathML, video, audio, forms, EPUB 3.1+ scripted EPUBs.
- Adobe ADEPT / Apple FairPlay / Amazon KFX / B&N DRM — recognize and reject.
- Encryption beyond IDPF/Adobe font obfuscation.
- WebP, AVIF, SVG with arbitrary filters.
- ZIP64.
- True bidi-isolate / first-letter / first-line CSS pseudo-elements (drop caps deferred).
- Search-in-book.
- Annotation/highlight/note storage.

### Provisional v1.5 list (decide during phasing)

- Cover thumbnail extraction for the file browser.
- Footnote popup-back navigation.
- Page-list (EPUB 3 print-page mapping).
- Justified text (word-spacing slack).

---

## 2. Architecture principle

**C owns parsers and decoders. Lua owns orchestration.** This is the same split CrossPoint uses, and the only one that fits the RAM budget. Concretely:

| C implements | Lua implements |
|---|---|
| ZIP entry I/O (streaming inflate) | "Which book is open" lifecycle |
| XML / HTML SAX parser | TOC tree walk, link resolution heuristics |
| CSS parser + selector matcher | Settings (font size, margins, paragraph alignment override) |
| Computed-style cache | Footnote return-stack |
| Image decoders (JPEG, PNG → 1-bit framebuffer) | Page-turn input handling, refresh strategy |
| Styled-span line breaker + word-position emitter | TOC/menu/percent-jump/footnote sub-activities (UI) |
| Variable-height block flow | Reading-position persistence (file format) |
| Pagination cache writer/reader | "What to do when DRM detected" UX |
| HTML entity decoder | "Should embedded styles be honored or overridden" policy |

Lua does not parse XML, CSS, ZIP, JPEG, PNG, or run the line breaker. Lua does call `chapter:next_event()` to consume parsed XHTML, gets back already-styled-and-decoded text, and tells the layout engine which block to push next.

---

## 3. Data model

Vocabulary used throughout the API specs below.

- **Book** — one EPUB file. Owns the ZIP handle, parsed OPF, TOC, manifest, spine, merged stylesheet.
- **Spine item** — a chapter-shaped resource (XHTML) in linear reading order. Indexed 0..N-1.
- **Chapter** — open spine item, currently being parsed/laid out. Holds the SAX parser state.
- **Block** — a layout-level unit emitted by Lua to the layout engine: paragraph, heading, image, blockquote, list-item, hr. Has variable height.
- **Span** — a styled run of text within a block. `{text, font_id, style_flags, color}`.
- **Style flags** — bitfield: BOLD, ITALIC, UNDERLINE, STRIKE, CODE, SUB, SUP, LINK.
- **Page** — the rendered output for one screen. A list of draw-list ops: text words at positions, images at positions, lines/rules.
- **Position** — `{spine_index, char_offset}` plus optional `fragment` for anchor links. Persisted across reboots.
- **Style context** — the CSS computed-style stack for the current element ancestry. Held by C, queried by tag/class/id.

---

## 4. Required new Lua APIs

The following modules need new bindings or extensions. Existing modules (`display`, `input`, `system`, `font`, `layout`, `text`, `storage`) are referenced where extensions are required.

### 4.1 `zip.*` — archive access (new module)

ZIP entries read as streams. No full-archive extraction. Backed by `uzlib` (already vendored) for DEFLATE; STORE entries copy directly.

```
zip.open(path) → handle | nil, errmsg
    Open an archive on SD. Validates the EPUB magic (mimetype entry first,
    STORE method, exact bytes "application/epub+zip") and rejects non-EPUB
    ZIPs at this layer. Returns an opaque userdata handle.

zip.close(handle)
    Free the archive handle.

zip.list(handle) → array of {name, compressed_size, uncompressed_size, method}
    Enumerate central directory entries. Used once at book-open to find OPF,
    TOC, etc.; not on hot path.

zip.has(handle, name) → bool
    Fast existence check by name.

zip.read(handle, name) → string | nil, errmsg
    Read entire entry into a Lua string. Use only for small entries (OPF, TOC,
    container.xml, CSS files) — typically < 32 KB. Errors on entries > a configured
    max (default 256 KB).

zip.read_chunked(handle, name, callback) → bool, errmsg
    Stream an entry through a Lua callback. callback(chunk_string) called with
    successive ~4 KB chunks; return false from callback to abort. For large
    entries (chapters, images) so we never hold the whole entry in RAM.

zip.entry_size(handle, name) → int | nil
    Uncompressed size of a named entry without reading it.

zip.is_drm_encrypted(handle) → bool, "drm" | "obfuscation" | "none"
    Inspect META-INF/encryption.xml. Returns:
      false, "none"          — no encryption.xml or only unsupported empty
      true,  "obfuscation"   — only IDPF/Adobe font-obfuscation entries (book is openable)
      true,  "drm"           — anything else (book must be rejected)
```

**Rationale for `is_drm_encrypted` returning two booleans:** font obfuscation must not be confused with DRM; the reader still opens font-obfuscated books fine.

### 4.2 `xml.*` — SAX parser (new module)

Streaming, namespace-aware, lenient (HTML5-flavored when configured for HTML; strict-XML otherwise). Backed by expat (~80 KB flash) per `build_spec.md`. Custom decoder also acceptable if it's smaller; expat is the safe choice.

```
xml.parse(input, opts, callbacks) → bool, errmsg
    input:      string OR { read = function() return chunk_string|nil end }
                  — accepts inline strings or pull-source closures, so chapters
                  can stream from zip.read_chunked.
    opts:       { mode = "html"|"xml",
                  decode_entities = true,
                  expand_named_entities = true,   -- handle &nbsp; etc.
                  allow_unclosed = true,          -- HTML5 forgiveness
                  strip_namespaces = true }       -- pass tags as plain "p", not "{http://...}p"
    callbacks:  { on_start = function(tag, attrs) end,
                  on_end   = function(tag) end,
                  on_text  = function(text) end,        -- already entity-decoded
                  on_pi    = function(target, data) end -- optional, processing instructions
                  on_doctype = function(name) end       -- optional, DOCTYPE noop in most cases
                }

    Returns true on full parse, false + errmsg on fatal error. Continues past
    recoverable errors when allow_unclosed=true.
```

`attrs` is a Lua table `{ key = value, ... }`. Order is not guaranteed. `epub:type` and `xml:lang` come through as `["epub:type"]` and `["xml:lang"]` keys (only after `strip_namespaces` is interpreted as: namespace URIs in tag names are stripped; attribute prefixes are preserved as plain string keys).

**Memory note:** the parser must use bounded buffers (suggest 4 KB scratch + 1 KB attribute-value buffer). Whole-entry buffering is not acceptable for chapter parses.

### 4.3 `epub.*` — high-level book operations (new module)

The C side offers two layers: a thin layer that wraps `zip.*` + `xml.*` to parse OPF/container/TOC, and a higher-level book object that holds the parsed result. Lua mostly uses the higher-level form.

```
epub.open(path, cache_dir) → book | nil, errmsg, errcode
    Opens the archive, validates mimetype, parses container.xml and OPF, parses
    the TOC (NCX or NavDoc). cache_dir is where this book's pagination cache,
    thumbnails, and parsed-OPF cache live (e.g. /cache/epub_reader/{hash}/).
    errcode values:
      "not_epub"           — bad mimetype
      "drm"                — encrypted with non-obfuscation algorithm
      "malformed_container"
      "malformed_opf"
      "no_spine"
      "fixed_layout"       — pre-paginated rendition, refused at v1
      "io_error"

epub.close(book)
    Release all parser state and the zip handle.

book:metadata() → {
    title, author, language, identifier,
    publisher, date_published, modified,
    description, cover_id,
    page_progression_direction = "ltr"|"rtl",
    epub_version = "2"|"3"
}

book:cover() → string|nil
    Returns a path on SD where a 1-bit BMP of the cover has been generated
    (cached on first call). nil if no cover. Used by file browser / library
    thumbnails.

book:thumb(height) → string|nil
    Same as cover() but at a specific height (e.g. 120). Cached separately.

book:manifest() → { [id] = {href, media_type, properties = {prop1=true, ...}} }
    Full manifest as a table keyed by manifest id.

book:spine() → array of {idref, linear, properties, href, media_type}
    Ordered by spine. Each entry pre-resolved so href is the in-archive path
    rooted at the OPF directory.

book:spine_size() → int
    Convenience.

book:toc() → tree of {label, href, children, depth}
    Hierarchical; children may be empty. EPUB 2 (NCX) and EPUB 3 (NavDoc)
    normalized to the same shape.

book:landmarks() → array of {type, label, href}        (EPUB 3, may be empty)
book:page_list() → array of {label, href}              (optional, may be nil)

book:resolve_href(href, base_href) → {spine_index, fragment}|nil
    Resolve an internal link (e.g. "chapter5.xhtml#footnote_42") against the
    spine. base_href is the chapter the link came from (so relative paths work).
    Returns nil if the link target is missing.

book:read_item(href) → string|nil, errmsg
    Read a manifest item to a Lua string. For small items (CSS, OPF, etc.).
    Wraps zip.read with the OPF-base-path resolved.

book:read_item_chunked(href, callback) → bool
    Stream an item. For chapters and images.

book:item_size(href) → int

book:cumulative_size(spine_index) → int
    Sum of bytes through this spine item; used for {percent} progress estimates.

book:cache_dir() → string
    Where this book's cache lives on SD.

book:clear_cache() → bool
    Wipe pagination cache for this book (e.g. when font size changes
    invalidates layout). Useful for debugging.
```

### 4.4 `css.*` — stylesheet parsing and selector matching (new module)

CSS lives partially in `<style>` blocks within content, partially in linked stylesheet files, partially in inline `style=""`. The C side merges them into a single rule list and does selector matching when asked.

```
css.parse(text, source_kind) → stylesheet
    source_kind: "inline" | "embedded" | "external"
    Returns an opaque handle. Subsequent calls to css.merge build up the active
    stylesheet for a chapter/book.

css.merge(...stylesheets) → merged_stylesheet
    Merge multiple stylesheets in cascade order. Caller is responsible for
    passing them in the correct order (UA defaults → external → embedded → inline).

book:stylesheet() → merged_stylesheet
    Convenience: book parses all manifest CSS files at open and exposes the
    merged result. Lua usually doesn't need css.parse directly.

ss:computed(element_descriptor) → computed_style
    element_descriptor: {tag, id, classes, ancestors = {{tag, id, classes}, ...}}
    Returns a flat table of resolved property values:
      {
        font_size_px, font_weight, font_style, text_decoration,
        text_align, text_indent_px, line_height_px,
        margin_top_px, margin_bottom_px, margin_left_px, margin_right_px,
        padding_top_px, padding_bottom_px, padding_left_px, padding_right_px,
        display = "block"|"inline"|"inline-block"|"none",
        direction = "ltr"|"rtl",
        vertical_align = "baseline"|"sub"|"super",
        list_style_type, color
      }
    Length values are pre-resolved to pixels. em/rem use the inherited font
    size; % uses the supplied viewport_width (passed to the stylesheet at
    creation, so the matcher can resolve them).

ss:apply_inline(element_descriptor, inline_style_string) → computed_style
    Same as computed() but layered with the element's `style=""` attribute.

ss:set_user_overrides(table)
    User-side overrides: e.g. paragraph_alignment = "justify"|"left"|"book"
    when "book", element CSS wins; otherwise user wins.
```

**Selector subset implemented:** type, class, id, descendant, child (`>`), adjacent (`+`), attribute presence/equals, pseudo-classes (`:first-child`, `:last-child`, `:first-of-type`). Pseudo-elements (`::first-letter`, `::first-line`) **deferred**. Specificity follows standard rules; `!important` honored.

**Properties parsed:** `font-family, font-size, font-weight, font-style, text-align, text-indent, line-height, text-decoration, color, margin-*, padding-*, display, direction, vertical-align, list-style-type, page-break-{before,after,inside}, white-space, word-spacing, letter-spacing`. Anything else is parsed-and-ignored (no error).

### 4.5 `image.*` — decoders (new module)

JPEG and PNG decoded directly to a 1-bit framebuffer at a target size. No full-resolution decode.

```
image.open(book, href) → image_handle | nil, errmsg
    Find the manifest item, identify format from extension+magic, return a
    handle. Holds a streaming decoder.

image.size(handle) → {width, height}
    Intrinsic dimensions from the file header (no full decode).

image.format(handle) → "jpeg" | "png" | "gif" | "unknown"

image.decode(handle, opts) → bool, errmsg
    opts: {
      target_width, target_height,    -- fit dimensions (aspect preserved)
      dither = "bayer4"|"floyd"|"none",
      x, y                            -- top-left in framebuffer (logical coords)
    }
    Decodes-and-blits straight into the active framebuffer at (x,y), scaled
    to fit target_{width,height} preserving aspect ratio. Centered within the
    target box. No intermediate image buffer.

image.close(handle)
```

Decoders: JPEG via a small JPEG decoder (CrossPoint uses `bitbank2/JPEGDEC`; we can either vendor it or write a minimal subset — TBD during phase planning). PNG via a small PNG decoder (CrossPoint uses `bitbank2/PNGdec`). GIF deferred.

**Cover thumbnails:** `book:cover()` and `book:thumb(height)` are convenience wrappers that decode-and-write a 1-bit BMP to the book's cache dir. They must work without an active framebuffer; a separate small render path is needed (or a temp 48 KB buffer; budget OK at startup).

### 4.6 `text.*` — extensions for styled spans, justification, blocks

Existing `text.*` (TXT/MD pagination + wrap) stays. New entry points needed for EPUB.

```
text.wrap_spans(spans, viewport_width, opts) → array of lines
    spans: { {text, font_id, style_flags}, ... } — input spans for ONE block.
    opts: {
      align = "left"|"right"|"center"|"justify",
      first_line_indent_px = 0,
      hanging_indent_px = 0,
      direction = "ltr"|"rtl",
    }
    Returns:
      { { words = { {text, font_id, style_flags, x_px, width_px}, ... },
          line_height_px,
          natural_width_px, slack_px } , ... }
    Greedy first-fit line break. Word boundaries are at U+0020 and other
    whitespace; non-breaking space (U+00A0) does not break. CJK lines break
    on character boundaries (no inter-word spacing — basic level).
    Justification when align="justify": each non-last line has slack distributed
    across word gaps; words[].x_px reflects final positions.

display.draw_words(line, x_offset_px, y_px)
    line: one entry from text.wrap_spans output.
    Iterates words and calls font_render with each word's resolved x and style.
    Implemented C-side so the per-word drawText boundary crossings stay out of
    Lua. (May ultimately go in display.* — venue is a v1 implementation detail,
    spec-wise the API exists.)

text.measure_string(font_id, str) → width_px
    Already exists implicitly via display.getTextWidth — re-confirm API name.

text.line_height(font_id, line_height_px_override) → int
    Resolve "auto" line-height to a pixel value given a font.
```

**No DOM here.** `text.wrap_spans` operates on *one* paragraph/block at a time. Lua builds the spans array as it consumes XML events; when it hits a block boundary, it calls `wrap_spans`, gets back lines, and pushes them to the layout cursor. The C side never holds the whole chapter's parsed structure.

### 4.7 `layout.*` — extensions for variable-height block flow

Existing layout engine stays. New helpers needed for EPUB-style flow.

```
layout.body_cursor() → cursor handle
    Allocates a flow cursor positioned at body top-left. Tracks current y as
    blocks are pushed.

cursor:remaining_height() → int
    Pixels available below current cursor before footer/button-bar.

cursor:can_fit(height_px) → bool
    Pure check; doesn't advance.

cursor:advance(height_px)
    Move the cursor down by height_px. Returns true if still within body, false
    if cursor crossed into footer area (page is full).

cursor:y() → int
cursor:reset()
    Reset to top of body.

layout.page_break_class(reason) → none
    For diagnostics only — Lua tells the engine why it broke (block-overflow,
    page-break-before, etc.). Useful in cache invalidation.
```

**Why expose this when Lua could track y itself?** Centralizing the cursor in C lets future features (footer area shrinking when no footer text, multi-column layout for landscape) ship without rewriting every reader. It also means the EPUB reader and a hypothetical PDF/CBZ reader share the same flow primitives.

### 4.8 `font.*` — additions for style mapping

Existing `font.load`, `font.boot`, `font.setFallback` stay. New helpers:

```
font.resolve_family(family_hint, weight, style) → font_id
    Map a CSS font-family + weight + style to a loaded font slot. Implementation
    consults a small registry (registered at Lua startup):
      family_hint: "serif"|"sans-serif"|"monospace"|"book"|specific-family-name
      weight: "normal"|"bold"
      style: "normal"|"italic"
    Returns the best-matching loaded font slot, or the reader default if no
    match. Used to pick fonts when consuming styled spans.

font.register_family(family_name, weight, style, font_id)
    Add a mapping. Called once per loaded font during reader init.

font.list_loaded() → { {id, path, family_hint?}, ... }
    For diagnostics + the family registry.
```

This is small and Lua-side (could live in a new `lib/fonts.lua` extension), but the API needs a thin C-side hook for `resolve_family` if we want to keep it out of every drawText. Open question — see §11.

### 4.9 `storage.*` — additions

Existing `storage.*` mostly suffices. One new helper:

```
storage.atomic_write(path, content) → bool, errmsg
    Write to a temp file in the same directory, fsync, rename over the target.
    Used for reading-position persistence and pagination cache writes so a
    crash mid-write doesn't corrupt a book's cache. Implementation lives
    C-side because Lua has no fsync/rename primitives today.

storage.dir_size(path) → int|nil
    Sum of file sizes recursively. Used to enforce per-book cache size caps
    if we add that policy.
```

### 4.10 `system.*` — already covered

`system.freeHeap`, `system.batteryPercent`, `system.millis`, `system.log`, `system.reload` — all sufficient for the reader. No new system bindings needed for EPUB itself.

---

## 5. Persistence

### Reading-position file

Path: `/cache/epub_reader/{book_hash}/progress.json`

```json
{
  "version": 1,
  "spine_index": 4,
  "char_offset": 18432,
  "fragment": null,
  "saved_at": 1740000000,
  "settings_hash": "a1b2c3d4"
}
```

`book_hash` = `hash(absolute_path)` (consistent with CrossPoint). `settings_hash` is a digest of `{font_id, font_size, line_spacing, paragraph_alignment, screen_margin}` — when settings change, position remains valid (offset is character-based, not pixel/page-based) but the pagination cache invalidates.

Writes are debounced to chapter exit + every 16 page turns + activity exit, never on every page turn (SD wear). Use `storage.atomic_write`.

### Pagination cache

Path: `/cache/epub_reader/{book_hash}/sections/{spine_index}.bin`

Binary format (proposed; finalize during impl):

```
HEADER (16 bytes)
  magic[4]            = "CLPG"
  version             u8 (current = 1)
  flags               u8  (bit0=is_rtl, bit1=has_images)
  font_id             u8
  reserved            u8
  settings_hash       u32
  page_count          u16
  body_height_px      u16

PAGE OFFSETS (4 * page_count bytes)
  offset_into_xhtml[]  u32
  -- byte offset within the chapter XHTML where each page starts

DRAW LISTS (variable, page_count entries)
  page_size           u16
  page_data[page_size] -- compact draw-list ops (TBD format)
```

Draw-list ops are minimal:

```
op     fields                                size
0x01   text_word(x, y, font_id, style, len, bytes)
0x02   image(x, y, w, h, href_offset_in_chunk)
0x03   line(x1, y1, x2, y2)
0x04   rect(x, y, w, h, fill)
0x05   page_anchor(name_offset, name_len)   -- for fragment-anchor lookup
```

Cache validation on open: `header.settings_hash == current_settings_hash` and `header.font_id == current_font_id`. Otherwise discard and re-paginate.

### Cache size policy

`/cache/epub_reader/` capped at 50 MB total (configurable). LRU eviction at the per-book level on overflow.

---

## 6. Edge cases the API must handle

Catalog of cases the C-side parsers and the Lua-side reader must handle gracefully. Each row should be addressable through the APIs above without panic / crash / silent data loss.

| Case | API handling |
|---|---|
| `mimetype` not first / compressed / wrong content | `epub.open` returns `errcode="not_epub"` |
| No `META-INF/container.xml` | `errcode="malformed_container"` |
| Multiple rootfiles in container | Pick first `application/oebps-package+xml` (silent) |
| OPF unparseable | `errcode="malformed_opf"` |
| Spine empty or `toc` attr missing | `errcode="no_spine"` if spine empty; missing toc just means no NCX, look for NavDoc |
| Manifest entry referenced by spine but missing | Skip with diagnostic event |
| TOC entry href to missing file | `book:resolve_href` returns nil; UI skips |
| Self-closing tags written as `<br>` (HTML5) | `xml.parse(mode="html")` accepts |
| Mismatched tag close (e.g. `<p>...<span>...</p>...</span>`) | `mode="html"` recovers; `mode="xml"` returns error |
| Numeric entity `&#NNNN;` / `&#xNNNN;` | Decoded by parser (mandatory) |
| Named entity `&nbsp;` | Decoded; full HTML named-entity table |
| `<img>` href to nonexistent item | Lua renders alt text or placeholder rect |
| `<img>` larger than column | `image.decode` shrinks to fit (aspect preserved) |
| JPEG/PNG decoder error | `image.decode` returns `false`; reader renders alt text |
| GIF / SVG / WebP `<img>` | Reader detects unsupported format, renders alt text |
| `direction="rtl"` on `<html>` or `<body>` | `css:computed` returns `direction="rtl"` for root and inherited; layout flips |
| `page-progression-direction="rtl"` | `book:metadata().page_progression_direction = "rtl"`; reader inverts page-turn input mapping |
| `<style>` block in chapter | Parsed by `css.parse(source="embedded")`; merged into chapter stylesheet at chapter open |
| External `<link rel="stylesheet">` | Parsed at book open via `book:stylesheet()` |
| Inline `style=""` | `ss:apply_inline(element, style_string)` per element |
| `META-INF/encryption.xml` with IDPF/Adobe font obfuscation | `zip.is_drm_encrypted` returns `(true, "obfuscation")`; book opens; obfuscated fonts rendered with on-device fallback (not deobfuscated) |
| `META-INF/encryption.xml` with anything else | `(true, "drm")`; `epub.open` returns `errcode="drm"` |
| EPUB 3 with `properties="scripted"` | Render text statically; no JS executed |
| Fixed-layout EPUB (`rendition:layout="pre-paginated"`) | `errcode="fixed_layout"` |
| Remote-resource manifest item | Render placeholder; never fetch |
| `epub:type="footnote"` aside | Layout skips block in normal flow; reader exposes via footnote overlay |
| `epub:type="noteref"` link | Layout adds tap region; activation pushes footnote-return position and navigates |
| Chapter > 256 KB (rare badly-split EPUBs) | `book:read_item_chunked` streams without RAM impact; `xml.parse` accepts pull source |
| Text in non-UTF-8 encoding | Best-effort: assume UTF-8; if BOM detected for UTF-16, the C parser converts; otherwise display garbled and emit a diagnostic |
| `<table>` | v1: render as block, no column layout (each `<td>` becomes its own paragraph). Document this as known limitation. |
| `<ruby>` / `<rt>` | v1: drop ruby annotations, render base text only |
| `<sub>` / `<sup>` | Span style flags SUB / SUP shift baseline by ~30% of font height, rendered at smaller size |
| `<pre>` / `<code>` | Span style flag CODE; whitespace not collapsed inside pre |
| Whitespace runs in normal flow | Collapsed to single space by `xml.parse` text-event normalization (configurable) |
| Empty paragraphs (`<p></p>`) | Layout cursor advances by margin-top + margin-bottom but no line height |

---

## 7. Diagnostic / error channel

Events the C side surfaces so the UI can show a "this book has issues" state without crashing:

```
on_diagnostic(severity, code, detail)
    severity: "info"|"warning"|"error"
    code: "missing_manifest_item"|"broken_link"|"bad_image"|"malformed_html"|...
    detail: free-form string
```

Lua-side reader collects these per book and exposes a "diagnostics" sub-screen. Doesn't gate reading — the reader keeps going.

---

## 8. Memory budget (estimates)

Rough estimates for the new code footprint. Real numbers measured during impl. Budgets are flash-in-firmware unless noted.

| Component | Flash | Notes |
|---|---|---|
| zip module (uzlib already in flash) | ~3 KB | Mostly entry iteration + central directory parse |
| xml module (expat) | ~80 KB | Big but proven on this hardware in CrossPoint |
| epub module (high-level) | ~10 KB | OPF + container + TOC parsers, book object |
| css module (parser + matcher) | ~15 KB | Selector matcher is the bulk |
| image module (JPEG + PNG) | ~50 KB | Both decoders combined |
| text wrap_spans + draw_words | ~6 KB | Builds on existing text.* infra |
| layout cursor | ~1 KB | Trivial |
| **Subtotal new code** | **~165 KB** | |
| Current flash usage (post-Phase 8.5) | 636 KB (9.7%) | |
| **Projected after EPUB code** | **~800 KB (~12%)** | Still tiny against 6.5 MB partition |

RAM at runtime per chapter (estimates):

| | Bytes |
|---|---|
| Stylesheet (parsed, computed) | ~4-8 KB |
| SAX parser scratch | ~5 KB |
| Span buffer for current block | ~2 KB |
| Layout cursor | ~64 B |
| Image decode (active, peak) | ~16 KB |
| **Per-chapter peak** | **~30-35 KB** |

This fits within the existing ~89 KB plugin RAM budget with margin.

---

## 9. EPUB 2 vs EPUB 3 — what surfaces at the API level

| Concern | EPUB 2 | EPUB 3 | API behavior |
|---|---|---|---|
| OPF version | `<package version="2.0">` | `<package version="3.0">` | `book:metadata().epub_version` |
| TOC | NCX file (`toc.ncx`, manifest `media-type="application/x-dtbncx+xml"`, spine `toc=` attr) | Nav document (`nav.xhtml`, manifest `properties="nav"`) | `book:toc()` normalizes both into one tree shape |
| Cover signal | `<meta name="cover" content="cover-id"/>` | manifest `properties="cover-image"` | `book:metadata().cover_id` checks both |
| Footnotes | Convention: `<sup><a href="#fn1">1</a></sup>` + target | `<a href="..." epub:type="noteref">` + `<aside epub:type="footnote">` | EPUB 3 idiomatic detection via `epub:type`; EPUB 2 detected heuristically (small `<a>` near sup, target on same/related page) |
| Page list | Optional in NCX `<pageList>` | `<nav epub:type="page-list">` | `book:page_list()` normalizes both, returns nil if absent |
| Landmarks | `<guide>` with `<reference type=...>` | `<nav epub:type="landmarks">` | `book:landmarks()` normalizes |
| HTML | XHTML 1.1 (strict) | (X)HTML5 (lenient ok) | `xml.parse(mode="html")` is the safe default for both — EPUB 2 XHTML parses fine in HTML mode |
| Encryption | rare | standardized `META-INF/encryption.xml` | Same `zip.is_drm_encrypted` for both |

The Lua reader does *not* branch on EPUB version for content rendering. The C parsers handle both transparently and present a unified API.

---

## 10. Resolved decisions

The following questions were raised during spec review and resolved. They are binding for v1.

1. **Justification in v1?** ✅ Yes. Greedy line break emits per-word x positions; non-last lines distribute slack across word gaps when `align="justify"`. ~1 KB code, no RAM cost.

2. **Cover thumbnail format.** ✅ 1-bit BMP. Reuses the existing sleep-screen `bmp_decoder.c` path. Cached in the book's cache dir.

3. **`font.resolve_family` venue.** ✅ C-side, exposed via `font.*` namespace. Reasoning: consistency with `font.boot()` / `font.load()`, ~3× more compact registry data than Lua tables, and any plugin (not just EPUB) can use it. ~200 bytes of static state at runtime; opt-in capability not required since `font.*` is a core module.

4. **CSS computed-style cache.** ✅ Ship with a small LRU cache (~16 entries). Adds <1 KB of plugin RAM during the EPUB reader's lifetime; freed at plugin exit.

5. **Pre-paginate at chapter open vs lazy?** ✅ Lazy. Paginate forward from the current position synchronously; spawn a background FreeRTOS task to paginate the rest of the chapter so the progress percentage settles within ~1s. Page byte-offsets and full draw-lists persist to SD per §5 (Option A heavy cache) so revisits are sub-500ms.

6. **Image rendering at top-of-chapter.** ✅ Shrink to viewport height (`max-height = body_h - 2*line_height`), aspect preserved, centered.

7. **EPUB 2 footnote heuristic.** ✅ Detect `<a href="...">` wrapped in `<sup>`. Class-name heuristics rejected — too noisy.

8. **OPF / TOC re-parse on every open vs cached?** ✅ Re-parse on every open. No cache file. Parse cost is ~50-200ms; against ~1-2s e-ink boot/render it's invisible. Re-evaluate only if open time exceeds 500ms.

9. **Maximum chapter size.** ✅ No hard cap. Streaming parser doesn't care. SAX scratch buffer bounded to 4 KB internally. Reject manifest entries > 5 MB as not-a-chapter.

10. **Embedded fonts: hard-no or family-name-hint?** ✅ Hard-no on `@font-face` rendering. `font-family` from CSS is mapped via the `font.resolve_family` registry to an on-device font slot. Documented as a deliberate divergence from desktop EPUB readers in user-facing docs.

### Hard non-functional requirement

11. **Zero RAM cost when the reader is not active.** All EPUB-specific bindings (`zip.*`, `xml.*`, `epub.*`, `css.*`, `image.*`) AND the existing `text.*` module are **opt-in per plugin manifest** — they are NOT registered into Lua states for plugins that don't declare them. `text.*` is included because it's currently registered into every plugin's state but only used by readers; rolling it into the same opt-in mechanism in Phase 9.0 is a one-line cleanup that recovers a few hundred bytes from non-reader plugin states and keeps the architecture consistent. See §14 for the full RAM discipline rules and §11 Phase 9.0 for the implementation.

---

## 11. Recommended phasing (informs the build_plan, not the build_plan itself)

Buckets ordered for "when does each become useful." This is a sketch — the actual build_plan will refine it.

**Phase 9.0 — Capability registration (prerequisite).**
Add opt-in capability declarations to the plugin manifest so EPUB-specific bindings only register into the Lua states of plugins that need them. Affects:
- `plugin_info_t` gains `requires[8][16]` + `requires_count`.
- `parse_plugin_manifest` extends to extract `requires = {"zip","xml",...}` from the plugin table (same string-table parser used for `fileExtensions`).
- `api_register.c` splits into `api_register_core(L)` (display, input, storage, font, system, layout, text — what every plugin gets) and per-capability registrars (`api_zip_register`, `api_xml_register`, `api_epub_register`, `api_css_register`, `api_image_register`).
- `plugin_manager_start` calls `api_register_core(active_state)` immediately after `api_create_state()`, then iterates `plugins[idx].requires` and dispatches to the matching capability registrar.
- Stock plugins (home, file_browser, settings, txt_reader, md_reader) declare no `requires`. Firmware home declares no `requires`. Only the EPUB reader (in Phase A onward) declares the EPUB-specific capabilities.
- Verifiable: build with all EPUB binding modules present but no plugin declaring them. Confirm the bindings live in flash but Lua state size is unchanged from current baseline (~89 KB plugin RAM).

**Phase A — Read the container, see metadata.**
- `zip.*`, `xml.*` (HTML/XML SAX, entities), `epub.open`, `book:metadata`, `book:manifest`, `book:spine`, `book:toc`. No content rendering yet.
- Verifiable: CLI-style `epub.open(path)` and `print(book:metadata())` from Lua.

**Phase B — Render styled text without images, no CSS.**
- `text.wrap_spans`, `display.draw_words`, `layout.body_cursor`. Hardcode minimal style mapping (h1/h2/h3 = bigger, `<em>`/`<i>` = italic flag, `<strong>`/`<b>` = bold flag).
- Verifiable: open a simple EPUB, scroll through chapter as paginated text. Headings visually distinct.

**Phase C — CSS subset.**
- `css.parse`, `css.merge`, `ss:computed`, `ss:apply_inline`. Replace hardcoded styling.
- Verifiable: same EPUB now respects margin-top, text-indent, text-align variations.

**Phase D — Images.**
- `image.*`, JPEG + PNG decoders. `book:cover()` thumbnail generation.
- Verifiable: illustrations render inline; cover appears in file browser.

**Phase E — Persistence + pagination cache.**
- `storage.atomic_write`, pagination cache file format, reading-position file, settings-hash invalidation.
- Verifiable: page turns are <500ms after first visit; reboot resumes on the same page.

**Phase F — Navigation polish.**
- TOC sub-activity, percent-jump, footnote-return stack, anchor-link resolution.
- Verifiable: tap noteref → jump to footnote → back returns. TOC opens to a valid chapter list.

**Phase G — Edge cases + hardening.**
- DRM rejection UX, fixed-layout rejection UX, broken-link diagnostics, malformed-EPUB recovery.
- Justification (v1 inclusion).
- Verifiable: a deliberately-broken EPUB opens with diagnostics shown, doesn't crash.

**Deferred (v1.5 / v2):**
- Hyphenation, embedded font rendering, CFI export/import, KOReader sync hooks, search, annotations.

---

## 12. Existing assets reused

What the current codebase already provides that the EPUB reader will consume:

- **Layout engine** (`lib/layout/`): regions, margins, font-derived line height, orientation-aware. EPUB extends with body cursor (§4.7) but the foundations are there.
- **Font system** (`lib/font/`): SD-loaded `.cfont` with on-demand glyph reads, fallback chains, RTL/BiDi reordering already in `lib/bidi/`. EPUB only adds `font.resolve_family` (§4.8).
- **Existing `text.*` C-side word wrap**: `text.indexPages`, `text.getPageLines`, `text.wrapString` for plain text. EPUB needs `text.wrap_spans` (§4.6) for styled spans, but the wrap loop and font-measurement glue are already in `lib/lua_api/api_text.c`.
- **BMP decoder** (`lib/renderer/bmp_decoder.c`): used for sleep wallpapers; reused for `book:cover()` thumbnail format.
- **Reader utility lib** (`/plugins/lib/reader_utils.lua`): page-turn handling, status bar, refresh strategy. EPUB plugin uses it the same as TXT/MD readers.
- **Settings + progress libraries** (`/plugins/lib/settings.lua`, `progress.lua`): font size, line spacing, paragraph alignment all already configurable. EPUB just respects the same settings.

---

## 13. Reference: CrossPoint EPUB module

Cross-reference points for proven implementations on this hardware (in `crosspoint-reader/lib/Epub/`):

- `Epub.h` — book object shape (matches `book:*` API in §4.3)
- `parsers/ContainerParser.cpp` — container.xml parse
- `parsers/ContentOpfParser.cpp` — OPF parse (manifest, spine, metadata)
- `parsers/TocNcxParser.cpp` — EPUB 2 NCX
- `parsers/TocNavParser.cpp` — EPUB 3 NavDoc
- `parsers/ChapterHtmlSlimParser.cpp` — chapter XHTML SAX (model for §4.2 in HTML mode)
- `css/CssParser.cpp` + `css/CssStyle.h` — CSS subset and properties (matches §4.4 enumeration)
- `blocks/BlockStyle.h` — block-level resolved style (matches `computed_style` shape in §4.4)
- `blocks/TextBlock.cpp` / `ImageBlock.cpp` — variable-height block layout (model for §4.7 cursor)
- `converters/JpegToFramebufferConverter.cpp` / `PngToFramebufferConverter.cpp` — direct-to-framebuffer image decode (matches §4.5 `image.decode`)
- `Section.cpp` — pagination + cache file layout (matches §5)
- `htmlEntities.cpp` — named entity table (reusable directly)
- `hyphenation/` — Liang hyphenation (deferred to v2 per §1)

CrossPoint is C++; CrossLuaReader is C. Algorithm logic transfers; STL-heavy code (std::string, std::vector, std::unique_ptr) needs translation to fixed buffers and explicit malloc/free per the project's `Resource Protocol` (CLAUDE.md).

---

## 14. RAM discipline (hard rules)

The EPUB reader implementation must satisfy: **zero RAM cost when the reader plugin is not active**. The following rules make that achievable; the build plan will gate each phase on them.

### 14.1 Opt-in Lua bindings

EPUB-specific binding modules (`zip.*`, `xml.*`, `epub.*`, `css.*`, `image.*`) are **never** registered into Lua states for plugins that don't declare them in their manifest's `requires = {...}` field. Phase 9.0 (§11) implements the manifest extension and the split registrars.

Verification: a plugin that does not declare any EPUB capability must have the same Lua-state byte size, measured via `lua_gc(L, LUA_GCCOUNT, 0)`, as it does today before any EPUB code lands. Tolerance: ±~200 bytes (Lua state churn).

### 14.2 No static `.bss` for sizable data

Anything > 256 bytes is heap-allocated on demand and freed when out of scope. Examples and rules:

| Buffer | Allocation | Freed at |
|---|---|---|
| SAX parser scratch (~4 KB) | malloc at `book:open_chapter()` | `chapter:close()` |
| Span buffer for current block (~2 KB) | malloc at chapter open | chapter close |
| CSS computed-style cache (~1 KB / 16 entries) | malloc at book open | `book:close()` (plugin exit) |
| Image decode workspace (~16 KB peak) | malloc per `image.decode()` call | end of `image.decode()` |
| Pagination cache per-page draw-list (variable, up to ~50 KB / chapter) | malloc when chapter opens, populated from SD if cached | chapter close |
| Compressed DEFLATE buffer (zip / image inflate) | malloc per inflate call | end of inflate |

Compile-time `static uint8_t buf[N]` is allowed only for tiny (< 256 byte) lookup tables and stack-saving scratch. All such buffers must be enumerated in code review.

### 14.3 Flash-only constants

All constant data — HTML entity names → codepoints table, default tag → CSS map, font-family registry seed entries, encryption-algorithm name → kind table, image-format magic bytes — declared `static const` so the compiler places them in `.rodata` (flash) instead of `.data` (initialized RAM).

### 14.4 No singletons across plugin invocations

The EPUB reader plugin's `onExit()` must teardown so completely that `system.freeHeap()` returns to within a few hundred bytes of its pre-`onEnter` value. No global "last book" cache, no persistent computed-style cache, no leaked file handles. Verification: `freeHeap` measured before plugin entry vs after plugin exit must match within 1 KB.

### 14.5 Per-chapter teardown

When the user navigates between chapters, the previous chapter's parser state, span buffers, current-page draw list, and image workspace are all freed before the next chapter's parser is opened. This is mostly a discipline rule for the implementation; the API in §4 enforces it via the `chapter` lifecycle (`book:open_chapter` / `chapter:close`).

### 14.6 Build-plan gates

Each phase in §11 carries a RAM gate:

- **Free heap before plugin entry vs after plugin exit:** must match within 1 KB. Measured via `system.freeHeap()` from a probe plugin that opens then exits the EPUB reader.
- **Free heap with EPUB plugin not declared in any active plugin:** must match the current baseline. Measured by booting the firmware (with EPUB code present but no plugin requiring it) and reading `system.freeHeap()` from home.
- **Working-set during reading:** peak RAM during a chapter visit must stay under ~40 KB above baseline (book object + parsed CSS + chapter parser state + ~16 KB image decode peak). Measured via `system.freeHeap()` mid-page-render.

If any gate fails, the responsible phase doesn't ship until the leak is found.

---

## End of spec

Once this is reviewed and the §10 decisions are stable, the next deliverable is a `build_plan.md`-style phased plan that sequences §11 (starting at Phase 9.0) with explicit build / test / verification gates per phase, including the RAM gates from §14.6.
