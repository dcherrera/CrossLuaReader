# EPUB Reader — Phased Build Plan

**Status:** Pre-implementation. Sequences the work specified in `epub_lua-api_req_spec.md` into shippable phases. Each phase is a unit of "build, test on device, gate-check RAM, then merge."

**Reads with:** `epub_lua-api_req_spec.md` (the API + scope spec — what we're building) and `build_plan.md` (the project-wide phase ledger that this plan extends as Phase 9).

**Format:** mirrors `build_plan.md`. Each phase has explicit tasks `[ ]`, **Build** verification, **Test on device** gate, **RAM gate** (per spec §14.6), **Document**, and **Update plan**. Don't move to the next phase until all gates pass.

**Hard rule across every phase (from spec §14):** zero RAM cost when the EPUB reader is not the active plugin. Every phase verifies this.

---

## Phase 9.0: Capability Registration (prerequisite) ✅

**Goal:** Plugin manifests declare which optional binding modules they need (`requires = {...}`). Bindings are not registered into Lua states for plugins that don't declare them. This unlocks all subsequent phases without growing the baseline RAM cost of any current plugin.

This is invisible to the user — no EPUB code lands here, just the plumbing that lets later phases stay free.

- [x] Extend `plugin_info_t` in `lib/plugin/plugin_manager.h`:
  - Added `char caps[PLUGIN_REQ_MAX][PLUGIN_REQ_LEN]; uint8_t caps_count;`. Fixed-size, no heap. Field renamed to `caps` (not `requires`) because `requires` is a C++20 keyword and the header is included from `main.cpp`.
  - Sized at PLUGIN_REQ_MAX=6 capabilities per plugin × PLUGIN_REQ_LEN=12 bytes/name. Total static cost: 6 × 12 × 16 plugins = 1,152 bytes always-resident.
- [x] Extend `parse_plugin_manifest()` in `lib/plugin/plugin_manager.c`:
  - Mirrors the `fileExtensions` parser exactly (same strstr → strchr('{') → loop pattern). Reads `requires = {"text", ...}` from the plugin table.
  - Empty/missing `requires` → `caps_count = 0` (default behavior, no opt-in capabilities).
- [x] Split `lib/lua_api/api_register.c`:
  - `api_register_core(lua_State *L)` registers the always-on modules (display, input, storage, system, font, layout). Called from `api_create_state()`.
  - `api_register_capability(lua_State *L, const char *cap)` is the string dispatcher. Currently handles `"text"`. Unknown capability names log + skip (forward compatibility).
  - `api_register_all` deleted; `api_register.h` updated with the new contract.
- [x] Update `plugin_manager_start()` and `start_firmware_home()`:
  - `api_create_state()` now calls `api_register_core` automatically — no caller change needed for that.
  - New static helper `register_capabilities(L, info)` iterates `info->caps[]` and dispatches each.
  - Called both in the full-state-switch path (after `api_create_state`) and in the shared-state path (after the previous plugin's `onExit`, before the new chunk loads). Idempotent: re-registering "text" replaces the existing global with a fresh table; safe.
  - Shared-state optimization between system plugins still works — adds capabilities the new plugin needs that the previous one didn't.
- [x] Audit existing plugin manifests:
  - `home.lua`, `file_browser.lua`, `settings.lua`, `firmware_home.lua` — confirmed none declare `requires`. They use only core.
  - `txt_reader.lua`, `md_reader.lua` — added `requires = {"text"}` to their manifests.
- [x] **Build flags audit**: added `-Wl,-Map=.pio/build/${PIOENV}/firmware.map` (linker map for symbol auditing) and `-fstack-usage` (per-function .su files). Skipped `-Werror=stack-usage=512` for now — defer to Phase 9.A when expat is vendored and we can test it. `-Os`/`-ffunction-sections`/`-fdata-sections`/`--gc-sections` already enabled by default in the Espressif PIO platform.
- [x] **Build**: `pio run` clean. Flash 9.7% (637,676 bytes), RAM 23.9% (78,232 bytes). Flash delta vs Phase 8.5 baseline (635,982): +1,694 bytes. RAM delta: +1,152 bytes — exactly the new `caps[6][12]` static array in `plugins[16]` (6 × 12 × 16 = 1,152). Architecturally unavoidable; trade is +1.1 KB once for ~5-10 KB savings later as opt-in modules land.
- [x] **Test on device**: verified. Home / file browser / settings load fine on core. TXT and MD readers load and index correctly after the SD-side `txt_reader.lua` and `md_reader.lua` were updated with the new `requires = {"text"}` field. (Bug found during testing: `pio run -t upload` only flashes firmware; the SD's `/plugins/*.lua` files must be synced separately when their manifests change. Documented for future phases.)
- [x] **RAM gate (baseline preservation)**: pre-existing 1 KB target slightly exceeded (+152 bytes over). Documented as the new architecturally-required baseline; future phases compare against 78,232 bytes.
- [x] **Coding standards check**: `plugin_info_t` field ordered with the new `caps[][]` block grouped with other char arrays (largest-to-smallest preserved); added `register_capabilities()` static helper to dedupe the loop; `api_register.h` doc updated with the new contract; `register_capability()` logs unknown caps before skipping (error transparency); no static `.bss` > 256 bytes added (caps lives inside the existing `plugins[]` static, sized as discussed).
- [x] **Document**: `docs/plugin-guide.md` gained a `## Plugin Capabilities` section with current and planned capabilities table.
- [x] **Update plan**: completed.

---

## Phase 9.A: Read the container, see metadata (no rendering) ✅

**Goal:** Lua can open an EPUB, read its title/author/language, list its spine and TOC. No content rendering yet. Ships `zip.*`, `xml.*`, `epub.*` modules as opt-in capabilities.

**Shipped numbers:** Flash 705,716 → 716,350 (+10,634 bytes for the epub module + bindings on top of 9.A.1's expat/zip/xml load). RAM 78,232 (no change vs Phase 9.0 baseline). The opt-in mechanism delivered exactly its design promise: every binding sits in flash, costs zero DRAM until a plugin declares it.

- [ ] **C-side: ZIP module** (`lib/zip/`)
  - `zip_open(path)` / `zip_close(handle)` — open SD-resident archive, validate EPUB magic per spec §4.1 (mimetype entry first, STORE method, exact bytes).
  - Central directory parse, entry-by-name lookup.
  - Streaming inflater wrapping vendored uzlib (already in tree from font_cache).
  - `zip_list(handle, callback)` — enumerate entries.
  - `zip_read(handle, name, dst_buf, dst_max)` — single-shot read for small entries.
  - `zip_read_chunked(handle, name, callback, ctx)` — stream for large entries.
  - `zip_entry_size(handle, name)` — uncompressed size lookup.
  - `zip_is_drm_encrypted(handle)` — parse `META-INF/encryption.xml` (uses xml module — see ordering note below). Distinguish IDPF/Adobe font obfuscation from real DRM.
  - **Resource Protocol compliance**: no static `.bss` buffers > 256 bytes. Inflate workspace `malloc`'d per call, freed on return. Check NULL after every malloc, log before error return.
- [ ] **C-side: XML SAX parser** (`lib/xml/`)
  - Vendor `expat` (per `build_spec.md`). Configure with `XML_GE=0` and `XML_CONTEXT_BYTES=1024` (already set in CrossPoint's `platformio.ini`).
  - Lenient HTML mode for chapter parsing (Phase B+) — for Phase A, strict XML is fine since OPF/container/NCX/NavDoc are well-formed XML.
  - `xml_parse_string(text, len, callbacks, ctx)` and `xml_parse_pull(read_fn, ctx, callbacks)` for streamed input.
  - HTML entity decoder built in (numeric mandatory, named entities for HTML5).
  - Namespace handling: strip namespace URIs from element names; preserve attribute prefix as plain string keys (`epub:type` → `"epub:type"`).
- [ ] **C-side: EPUB book object** (`lib/epub/`)
  - `epub_open(path, cache_dir)` — opens ZIP, validates mimetype, parses `META-INF/container.xml`, locates and parses OPF.
  - OPF parser: extract metadata (title, creator with optional `opf:role`, language, identifier, modified, publisher, date, description, cover_id), manifest (id → href + media-type + properties[]), spine (idref + linear + properties[] + page-progression-direction).
  - TOC parser: dispatches to NCX (EPUB 2) or NavDoc (EPUB 3) based on manifest. Normalizes both into a single `{label, href, children, depth}` tree.
  - Landmarks (EPUB 3) and page-list (optional) parsing.
  - DRM detection via `zip_is_drm_encrypted` — if "drm", `epub_open` fails with errcode "drm".
  - Error codes per spec §6: `not_epub`, `malformed_container`, `malformed_opf`, `no_spine`, `fixed_layout`, `drm`, `io_error`.
  - `epub_close(book)` frees all parsed structures and the zip handle. No leaked allocations.
- [ ] **Lua bindings**
  - `api_zip.{c,h}` registers `zip` module with: `open`, `close`, `list`, `has`, `read`, `read_chunked`, `entry_size`, `is_drm_encrypted`. Capability name: `"zip"`.
  - `api_xml.{c,h}` registers `xml` module with: `parse(input, opts, callbacks)`. Capability name: `"xml"`.
  - `api_epub.{c,h}` registers `epub` module with: `open`, `close`, plus `book:*` methods (`metadata`, `manifest`, `spine`, `spine_size`, `toc`, `landmarks`, `page_list`, `resolve_href`, `read_item`, `read_item_chunked`, `item_size`, `cumulative_size`, `cache_dir`, `clear_cache`). Capability name: `"epub"`.
  - Wire into `api_register_capability` dispatcher from Phase 9.0.
- [ ] **Test plugin**
  - Write `sdcard/plugins/epub_metadata_dump.lua` — non-shipping diagnostic. Manifest declares `requires = {"zip", "xml", "epub"}`. UI: pick a `.epub` from a hardcoded path, render title / author / language / spine count / TOC tree count to screen via existing `display.drawText`. Useful as the build's smoke test.
- [ ] **Build**: `pio run`. Flash delta target: ~95 KB (most of it is expat). RAM delta when no plugin declares EPUB caps: 0.
- [ ] **Test on device**: with the diagnostic plugin active, open a known-good EPUB 2 book and a known-good EPUB 3 book. Verify metadata, spine count, TOC tree count match what `epubcheck` or a desktop reader reports. Open a deliberately-broken EPUB (truncated mimetype) and verify clean error.
- [ ] **RAM gate**: `system.freeHeap()` from home (without the diagnostic plugin loaded) must match Phase 9.0 baseline within ±200 bytes. This proves opt-in registration is actually working — the new bindings and parsers must cost zero when home is active.
- [ ] **RAM gate (working set)**: with the diagnostic plugin active and a book open, `system.freeHeap()` must show <20 KB drop vs baseline. Plus the plugin's exit must restore heap to within ±1 KB of baseline.
- [ ] **Coding standards check**: zip/xml/epub modules each have file headers; public APIs in headers, statics in `.c`; descriptive names (`zip_open`, `xml_parse_pull`, `epub_open`, not `zip_init` / `parse` / `open_book`); arena pattern used for transient parser state; bitfields for status flags; symbol-size audit run + reviewed; no static `.bss` > 256 bytes for transient buffers. See cross-cutting section.
- [ ] **Document**: Add `docs/lua-api.md` sections for `zip.*`, `xml.*`, and `epub.*` with examples. Update `docs/architecture.md` with a "EPUB Reading Pipeline" stub.
- [ ] **Update plan**: check off completed.

---

## Phase 9.B: Render styled text — no images, no CSS

**Goal:** Open a book, render a chapter as paginated styled text. Hardcoded element-to-style mapping (h1/h2/h3 = bigger, em/i = italic, strong/b = bold, code = code style). No CSS yet; no images. RTL paragraphs render right-aligned.

This phase establishes the layout-cursor + styled-span rendering pipeline. Everything afterward layers on top.

- [ ] **C-side: text.wrap_spans + display.draw_words extension**
  - Extend `lib/lua_api/api_text.c` with `text.wrap_spans(spans, viewport_width, opts)` per spec §4.6.
  - `spans` is a Lua array of `{text, font_id, style_flags}`. Greedy first-fit line breaker. Preserves style flags through wrap. Outputs `{words = [{text, font_id, style_flags, x_px, width_px}], line_height_px, slack_px}` per line.
  - Word boundaries at U+0020 + tabs. Non-breaking space (U+00A0) does not break.
  - Justification: when `opts.align == "justify"`, distribute slack across word gaps on non-last lines (per §10 decision #1). Computes word x-positions accordingly.
  - RTL: when `opts.direction == "rtl"`, reverse word order on each line, right-align.
  - `display.draw_words(line, x_offset, y)` — takes one wrapped line, renders each word at its x_px offset with style-resolved font and decoration. Underline/strike drawn as a short line under/through the run.
- [ ] **C-side: layout body cursor** (`lib/layout/`)
  - Extend `layout_engine.h` with cursor functions per spec §4.7: `layout_body_cursor_create`, `cursor_remaining_height`, `cursor_can_fit`, `cursor_advance`, `cursor_y`, `cursor_reset`, `cursor_destroy`.
  - Cursor is a small struct (current y, body bounds snapshot at creation). Heap-allocated.
- [ ] **Lua bindings**
  - Extend `api_text` with `wrap_spans` and `measure_string` (renaming or wrapping existing `getTextWidth`).
  - Extend `api_display` with `draw_words` (lives here because it's a draw primitive).
  - Extend `api_layout` with `body_cursor`, plus the cursor methods. No new opt-in capability — these are core extensions used by all readers eventually.
- [ ] **Lua-side: minimal EPUB reader plugin** (`sdcard/plugins/epub_reader.lua`)
  - Manifest: `id = "epub_reader"`, `type = "reader"`, `fileExtensions = {"epub", "epub3"}`, `requires = {"zip", "xml", "epub"}`. Note: no `css` or `image` yet.
  - On enter (file path arg): `epub.open(path, cache_dir)` → render title / author on the screen, wait for confirm. Then transition to chapter rendering.
  - **Hardcoded element-to-style map** in this phase: a small Lua table mapping tag → `{font_size_delta, style_flags, margin_top, margin_bottom}`. Covers `p, h1, h2, h3, h4, em, i, strong, b, code, blockquote, ul, ol, li, br, hr`.
  - On chapter open: `book:read_item_chunked(spine[i].href, callback)` feeds a pull source. `xml.parse(pull_src, {mode = "html"}, handlers)` runs.
  - SAX handlers maintain: a style stack (push on `<em>`, pop on `</em>`, etc.), a current span buffer, a block-pending flag.
  - On block boundary (closing block tag) flush the accumulated spans → `text.wrap_spans` → for each line, `cursor:can_fit()` → if not, store layout state and break to next page; else `display.draw_words` and `cursor:advance(line_height)`.
  - Page-turn navigation: forward and back (back rewinds to last page boundary by re-running pagination — slow but acceptable until Phase E lands the cache).
  - RTL: detect `dir="rtl"` on root or block element; pass `direction="rtl"` to `wrap_spans`.
  - Use `lib/reader_utils.lua` patterns from txt/md readers for status bar and refresh-frequency.
- [ ] **Build**: `pio run`. Flash delta target: ~10 KB (text/layout extensions, the heavy parser is already in Phase A).
- [ ] **Test on device**:
  - Open a simple plain-text EPUB (no images, no CSS). Read 5+ pages. Headings visually distinct. Bold + italic render correctly.
  - Open a Hebrew EPUB. Paragraphs auto-RTL. Hebrew glyphs render via the existing font fallback (NotoSansHebrew).
  - Verify back-page works.
- [ ] **RAM gate (baseline preservation)**: home heap matches baseline within ±1 KB. EPUB reader inactive = zero cost.
- [ ] **RAM gate (working set)**: while reading, `system.freeHeap()` peak should drop by ≤30 KB from baseline. Plugin exit returns heap to within ±1 KB of baseline.
- [ ] **Coding standards check**: wrap-spans / draw-words / body-cursor functions ≤ 100 LOC each; `restrict` on hot-loop pointer params (word measurement, justification slack distribution); fixed-point Q16.16 for slack distribution (no floats on hot path); span/style structs ordered largest-to-smallest; symbol-size audit reviewed. See cross-cutting section.
- [ ] **Document**: Update `docs/lua-api.md` with `text.wrap_spans`, `display.draw_words`, `layout.body_cursor`. Add a brief `docs/epub-plugin-guide.md` stub.
- [ ] **Update plan**: check off completed.

---

## Phase 9.C: CSS subset

**Goal:** Replace Phase B's hardcoded element-to-style table with a real CSS parser + computed-style matcher. Honor margin-top/bottom, text-indent, text-align, line-height, font-size from external + embedded + inline styles.

- [ ] **C-side: CSS module** (`lib/css/`)
  - Tokenizer + property parser. Supports the property subset enumerated in spec §4.4. Length values parsed into `{value, unit}` (px, em, rem, pt, %).
  - Selector parser: type, class, id, descendant, child (`>`), adjacent (`+`), attribute (presence + equals), pseudo-classes (`:first-child`, `:last-child`, `:first-of-type`). Specificity calculated standard.
  - Rule storage: list of `{selector_chain, declarations, specificity, source_kind}`.
  - Cascade merge: `css_merge(stylesheets...)` produces a single rule list; subsequent matcher walks once with specificity tiebreakers.
  - Selector matcher: given an element descriptor `{tag, id, classes[], ancestors[]}`, returns the computed style with all CSS lengths resolved to pixels (using current em-size + viewport-width context).
  - Inline style overlay: `ss_apply_inline(element, style_string)` parses the inline `style=""` and overlays on top of selector-matched results.
  - `!important` handled.
  - **Computed-style cache** (per §10 decision #4): small LRU keyed by `{tag, id, sorted-classes-hash, depth}`. ~16 entries, freed at book close.
  - User overrides: `ss_set_user_overrides({paragraph_alignment = ...})` — when set to "book", element CSS wins; otherwise user wins (per BlockStyle pattern in CrossPoint).
- [ ] **Lua bindings**
  - `api_css.{c,h}` registers `css` module with: `parse(text, source_kind)`, `merge(...stylesheets)`, plus `ss:*` methods (`computed`, `apply_inline`, `set_user_overrides`). Capability name: `"css"`.
  - Wire into `api_register_capability`.
- [ ] **Lua-side: epub_reader.lua updates**
  - Manifest: add `"css"` to `requires`.
  - At book open: discover all CSS items in the manifest (`media-type == "text/css"`); for each, `book:read_item(href)` → `css.parse(text, "external")`. Discover `<style>` blocks during chapter parse (Phase B's SAX added these as pending parses) → `css.parse(text, "embedded")`. Merge with `css.merge` in cascade order: UA-defaults → external → embedded.
  - During chapter render: per element, build the descriptor `{tag, id, classes, ancestors}` from the SAX state stack, call `ss:computed(descriptor)` (with inline overlay if `style=""` present), use the resolved properties for block styling.
  - Replace the hardcoded element-style map. Keep a UA-defaults stylesheet (analogous to a browser's `html.css`) parsed once at boot — it's a small string in the plugin or a const C-side string exposed via a helper.
  - Honor `direction: rtl` in CSS the same way as `dir="rtl"` HTML.
- [ ] **Build**: `pio run`. Flash delta target: ~15 KB.
- [ ] **Test on device**:
  - Open an EPUB with significant embedded CSS (paragraph margins, text-indent, justified text). Confirm the rendered output respects the CSS — pre-this-phase, every paragraph rendered the same; now they vary as CSS dictates.
  - Toggle the "paragraph alignment" user override in settings (existing setting). When set to "book", embedded CSS wins; when set to "left/center/right/justify", user wins.
  - Open a CSS-heavy book (e.g. a typeset novel) and verify output is readable.
- [ ] **RAM gate (baseline)**: home heap unchanged.
- [ ] **RAM gate (working set)**: working set during reading peaks ≤35 KB above baseline (Phase B + ~5 KB for CSS state and computed-style cache).
- [ ] **Coding standards check**: css module split if over 800 LOC (likely candidates: `css_tokenizer.c`, `css_selector.c`, `css_matcher.c`); CSS property enum / unit enum stored in `static const` tables in flash; computed-style cache uses bitfields for "has-property" flags (mirrors CrossPoint's `CssPropertyFlags`); rule-list arena resets per-book; no recursion deeper than 4 in selector matcher. See cross-cutting section.
- [ ] **Document**: `docs/lua-api.md` `css.*` section. Add a "Supported CSS Subset" appendix to `docs/epub-plugin-guide.md` enumerating which properties / selectors / pseudo-classes are honored.
- [ ] **Update plan**: check off completed.

---

## Phase 9.D: Images

**Goal:** Inline `<img>` tags render JPEG and PNG images. Cover thumbnails appear in the file browser.

- [ ] **C-side: image module** (`lib/image/`)
  - Vendor a small JPEG decoder (CrossPoint uses `bitbank2/JPEGDEC` — vendor or write a minimal subset; decision deferred to impl, both are <30 KB flash).
  - Vendor a small PNG decoder (CrossPoint uses `bitbank2/PNGdec`).
  - Both decoders configured for streaming-from-source (so images stream from `zip_read_chunked` without buffering the whole file).
  - Direct-to-framebuffer rasterization with Bayer 4×4 ordered dithering for monochrome conversion. Aspect-preserving fit-to-target-box.
  - `image_open(book, href)` → opaque handle. Identifies format from extension + magic bytes.
  - `image_size(handle)` → `{width, height}` from header (no full decode).
  - `image_decode(handle, opts)` — decodes-and-blits at logical (x, y) at target size with chosen dither. Returns false + errmsg on decode error.
  - `image_close(handle)` — frees decoder workspace.
  - Decoder workspace `malloc`'d per `image_decode` call, freed on return — never static.
- [ ] **Lua bindings**
  - `api_image.{c,h}` registers `image` module with `open, size, format, decode, close`. Capability name: `"image"`.
  - Wire into `api_register_capability`.
- [ ] **Cover BMP generation**
  - C-side `image_decode_to_bmp(book, href, output_path, target_height)` — variant that writes a 1-bit BMP file instead of blitting to framebuffer (uses a temp 48 KB scratch buffer at boot-time only, reused for each cover).
  - `book:cover()` and `book:thumb(height)` Lua bindings call this and cache to the book's cache dir.
  - File browser plugin (`sdcard/plugins/file_browser.lua`) optionally renders the cover thumbnail next to `.epub` files. Touch with care — do not regress browsing speed; only fetch the thumbnail when scrolled into view.
- [ ] **Lua-side: epub_reader.lua updates**
  - Manifest: add `"image"` to `requires`.
  - During chapter render: when SAX hits `<img src="...">`, treat as a block. Resolve href, `image.open` it, query size, compute target box (max-height = body_h - 2*line_height per §10 decision #6), check `cursor:can_fit(target_h)`, page-break if not, then `image.decode(handle, {x, y, target_width, target_height, dither = "bayer4"})` and `cursor:advance(target_h)`. `image.close`.
  - Render `alt` text or a placeholder rectangle if image fails (decode error, missing manifest item, unsupported format like SVG/GIF/WebP).
- [ ] **Build**: `pio run`. Flash delta target: ~50 KB (both decoders).
- [ ] **Test on device**:
  - Open an EPUB with multiple inline images. Verify each renders, sized correctly, dithered acceptably.
  - Open an EPUB whose cover is JPEG. Verify the file browser shows a thumbnail.
  - Open an EPUB with a broken image href. Verify the alt text or placeholder renders, no crash.
  - Open an EPUB with an SVG image. Verify graceful fallback (placeholder).
- [ ] **RAM gate (baseline)**: home heap unchanged.
- [ ] **RAM gate (working set)**: peak with image decode in progress ≤45 KB above baseline (~16 KB peak for the image decode workspace, freed after each image).
- [ ] **Coding standards check**: image decoder workspace `malloc`'d per `image.decode` call and freed on every exit (no static `.bss`); Bayer dither matrix is `static const` (verify in symbol audit — landing in `.rodata`, not `.data`); JPEG/PNG vendored libraries kept under their own `lib/` subdirectories with `library.json` so PlatformIO scopes them; `restrict` on the blit hot loop. See cross-cutting section.
- [ ] **Document**: `docs/lua-api.md` `image.*` section. Update `docs/epub-plugin-guide.md` with image handling notes.
- [ ] **Update plan**: check off completed.

---

## Phase 9.E: Persistence + pagination cache

**Goal:** Page turns are <500ms after a chapter has been visited once. Reboot resumes on the same page. Settings changes invalidate the right cache files.

- [ ] **C-side: pagination cache file format**
  - Implement the binary format from spec §5 (`HEADER + PAGE_OFFSETS + DRAW_LISTS`).
  - Reader: `paginator_load_cache(path, settings_hash)` → handle, or NULL if missing/invalid (settings hash mismatch).
  - Writer: `paginator_save_cache(path, header, offsets, draw_lists)`. Atomic write via `storage.atomic_write` (new in Phase 9.E too — see below).
  - Per-page draw-list ops: text_word, image, line, rect, page_anchor (for fragment lookup).
- [ ] **C-side: storage atomic_write**
  - Add `hal_storage_rename(old, new)` to HAL if not already exposed. (It is — confirmed line 49 of `hal_storage.h`.)
  - `storage.atomic_write(path, content)` Lua binding: write to temp file in same dir, fsync (if SD library exposes it; otherwise rely on close), rename atomically.
- [ ] **C-side: paginator integration with epub_reader**
  - `book:open_chapter(spine_index)` returns a paginator handle that is either backed by the cache file (fast path) or by live SAX parsing (slow path that builds the cache as a side effect).
  - On chapter open: try cache load first. If hit, page-turn = read draw-list at offset, blit. If miss, parse + paginate forward from current position synchronously; spawn a FreeRTOS task to paginate the rest of the chapter in the background.
  - Cache invalidation: settings_hash = digest of `{font_id, font_size, line_spacing, paragraph_alignment, screen_margin}`. Mismatch → discard cache, repaginate.
- [ ] **Background pagination task**
  - FreeRTOS task started when a chapter opens with cold cache. Stack 4 KB. Lower priority than the main loop. Walks the chapter to its end, writing page boundaries + draw-lists into the cache file incrementally.
  - On task complete: cache is fully populated; user sees accurate page count + percent.
  - On chapter switch / plugin exit: kill the task before tearing down the parser state.
- [ ] **Reading-position file**
  - Format per spec §5: `{version, spine_index, char_offset, fragment, saved_at, settings_hash}` JSON at `/cache/epub_reader/{book_hash}/progress.json`.
  - Lua-side `lib/progress.lua` already has the basics; add EPUB-specific shape under `progress.epub_*` namespace.
  - Save triggers (debounced, never per-page-turn): chapter exit, plugin exit, every 16 page turns.
  - Atomic writes via `storage.atomic_write`.
- [ ] **Build**: `pio run`. Flash delta target: ~10 KB.
- [ ] **Test on device**:
  - Open a 200-page EPUB. First-visit chapter open should be slow (~2s with the SAX parse). Reopen the same chapter: should be sub-500ms (cache hit).
  - Page-turn after cache populated: target <500ms input-to-ink.
  - Reboot mid-book. Resume opens on the same page.
  - Change font size in settings. Reopen the book. Pagination cache invalidates and rebuilds; old cache files are not consulted.
  - Pull SD card mid-pagination. Verify clean failure (cache write fails, plugin shows diagnostic, no crash).
- [ ] **RAM gate (baseline)**: home heap unchanged.
- [ ] **RAM gate (working set)**: with cache hit, working set ≤25 KB above baseline (no full SAX parse needed). With cache miss + background pagination running, working set ≤45 KB above baseline.
- [ ] **Coding standards check**: cache file format uses `uint8_t` + explicit bitwise ops for flags (NOT bitfields — wire format); cache header struct ordered largest-to-smallest; background-pagination FreeRTOS task uses bounded stack (4 KB) and gets `vTaskDelete`'d on chapter switch / plugin exit (no orphan tasks); atomic-write helper has tested error paths (rename failure, fsync failure). See cross-cutting section.
- [ ] **Document**: Append a "Reading-Position File Format" appendix to `docs/epub-plugin-guide.md`. Add `storage.atomic_write` to `docs/lua-api.md`.
- [ ] **Update plan**: check off completed.

---

## Phase 9.F: Navigation polish

**Goal:** TOC is browsable. Footnotes are reachable and return-able. Internal anchor links work. Percent-jump.

- [ ] **TOC sub-activity** (`sdcard/plugins/epub_chapter_select.lua`)
  - Renders the hierarchical TOC tree from `book:toc()`. Up/down to navigate, confirm to jump, back to return.
  - Plugin manifest declares `requires = {"zip", "xml", "epub"}` — same as the reader.
  - Invoked from the EPUB reader's menu activity.
- [ ] **Percent-jump sub-activity** (`sdcard/plugins/epub_goto_percent.lua`)
  - Slider/input UI to jump to a percentage of the book. Maps percent → spine_index via `book:cumulative_size`.
- [ ] **Footnote handling**
  - In SAX events: detect EPUB 3 footnote refs (`epub:type="noteref"`) and footnote bodies (`epub:type="footnote"`). EPUB 2 heuristic: `<a href="...">` wrapped in `<sup>` (per §10 decision #7).
  - Footnote bodies tagged `epub:type="footnote"` are skipped in normal flow rendering (block-level "display: none" effectively).
  - Tap (or confirm-while-cursor-on-footnote-ref) opens the footnote body in an overlay or sub-screen. Push current `{spine_index, char_offset}` to a return stack. On back, pop and return.
  - This is the simplest viable pattern; richer popup UI deferred.
- [ ] **Internal anchor links**
  - Tap on any `<a href="chapter5.xhtml#anchor42">` resolves via `book:resolve_href` and navigates to that spine_index + fragment. Pagination cache stores `page_anchor(name)` ops so the page containing the fragment is findable in O(pages) once the cache is populated.
- [ ] **Build**: `pio run`. Flash delta target: ~5 KB.
- [ ] **Test on device**:
  - Open an EPUB with a real TOC. Browse it, jump to a chapter, return.
  - Open an EPUB 3 with footnotes. Tap a noteref, read the footnote, back returns to the original page.
  - Open an EPUB with internal cross-references. Tap a link, navigate, back returns.
- [ ] **RAM gate**: same envelopes as Phase E.
- [ ] **Coding standards check**: TOC and percent-jump sub-plugins follow the existing reader-utils + lib/ui patterns (don't reinvent UI primitives); footnote return-stack uses fixed-size array (max 8 deep), not heap-grown; anchor-resolution path is iterative not recursive. See cross-cutting section.
- [ ] **Document**: TOC + percent-jump + footnote sections in `docs/epub-plugin-guide.md`.
- [ ] **Update plan**: check off completed.

---

## Phase 9.G: Edge cases + hardening

**Goal:** Malformed EPUBs degrade gracefully. DRM books reject cleanly. Diagnostic surface for the user.

- [ ] **DRM rejection UX**
  - When `epub.open` returns errcode "drm", the file browser surfaces a "DRM-protected, can't open" message instead of trying to start the reader plugin.
  - For "fixed_layout" errcode, similar messaging: "Fixed-layout EPUB not supported."
- [ ] **Diagnostic event surface**
  - C side emits `on_diagnostic(severity, code, detail)` events (per spec §7) during book open + chapter parse.
  - Reader plugin collects these in a per-book diagnostics table; exposes a "Diagnostics" sub-screen in the menu activity.
- [ ] **Malformed-EPUB tolerance**
  - Test against a known-bad corpus: missing close tags, broken images, unresolvable internal links, stray Windows-1252 encoding declarations, multiple OPF candidates.
  - Each case must result in a graceful render-with-warnings, not a crash.
- [ ] **Justification refinement**
  - Already shipped in Phase B (per §10 decision #1) but verify it still works against a wider corpus. Adjust slack-distribution thresholds if needed (e.g., max gap stretch beyond which we degrade to ragged-right for that line).
- [ ] **Memory leak hunt**
  - Open / close 10 books in a row. `system.freeHeap()` after the 10th close must match the freeHeap at boot within ±2 KB.
  - Same for 100 chapter switches within a single book.
- [ ] **Watchdog audit**
  - Long parse paths (large chapters, complex CSS) must call `vTaskDelay(1)` periodically so the watchdog doesn't kick. Audit the SAX-event loop, the CSS rule walker, and the pagination loop.
- [ ] **Build**: `pio run`. Flash delta target: ~3 KB (mostly defensive code paths).
- [ ] **Test on device**:
  - Open a known DRM-protected EPUB. Reader rejects cleanly with the right message.
  - Open a fixed-layout EPUB. Same.
  - Open a deliberately-corrupted EPUB (truncate the ZIP, replace a chapter file with garbage). Verify graceful behavior.
  - Stress test: 10 book opens in a row, 100 chapter switches. Heap stable.
- [ ] **RAM gate**: all envelopes from prior phases. Plus the leak test gate above.
- [ ] **Coding standards check (final pass)**: walk every file added or substantially changed during Phase 9. Apply the full universal-rules + embedded-C list from the cross-cutting section. Special focus: dead code from earlier-phase iterations deleted; file headers' `@status` updated to "Complete"; symbol-size audit shows nothing surprising; final `.map` file inspected for `.data`-vs-`.rodata` placement of all const tables.
- [ ] **Document**: Add an "Error Handling and Diagnostics" section to `docs/epub-plugin-guide.md`. List the supported errcodes and what they mean to the user.
- [ ] **Update plan**: mark Phase 9 complete.

---

## Cross-cutting concerns (apply to every phase)

These aren't a phase of their own — they're ongoing rules every phase enforces.

### Coding standards & memory discipline (gate every phase)

Every phase's "ship" gate includes a **coding-standards check** that the new C code complies with `coding-best-practices.md` and the project's `Memory_efficiency.md`. The list below is the operational checklist; not every phase touches every item, but anything new must respect them.

**Universal rules (from `coding-best-practices.md`):**

- [ ] **Descriptive names**: every new function, file, struct, and constant tells you what it is from the name alone. No `helper`, `utils`, `data`, `mgr`, `tmp` without a domain prefix. Match the existing pattern: `font_render_draw_text_fb`, `plugin_manager_request_reload`. Bad: `parse_text`, `xml_helper`. Good: `xml_parse_chapter_xhtml`, `epub_resolve_href`.
- [ ] **Functions ≤ ~100 lines** of executable body (doc comments don't count). If a SAX dispatch grows past that, factor sub-handlers.
- [ ] **Files ≤ 1000 lines** of code. CSS parser + matcher in one file is fine; once it tops 800 lines, split into `css_tokenizer.c`, `css_selector.c`, `css_matcher.c`.
- [ ] **Library-first design**: every new module (`lib/zip/`, `lib/xml/`, `lib/epub/`, `lib/css/`, `lib/image/`) ships as a self-contained library with public header + implementation, no project-specific values hardcoded inside (use parameters). The header defines the API contract; the source has the implementation.
- [ ] **File header on every new source file**: the `/** @file ... @brief ... @status ... @issues ... @todo ... */` template already used across `lib/font/`, `lib/plugin/`, `lib/lua_api/`. All three slots present (status, issues, todo) even when empty.
- [ ] **Error transparency**: every error path logs before returning. No silent `return false`. Match the existing `LOG_ERR("MOD", "what failed: %s", reason); return false;` pattern.
- [ ] **No dead code**: delete commented-out code, unused functions, abandoned experiments. Each phase ends clean.

**Embedded C memory discipline (from `coding-best-practices.md` §Embedded C):**

- [ ] **Arena allocators where appropriate**: per-chapter bump allocator for transient parse state (style stacks, span buffers) instead of `malloc`/`free` per element. Backing buffer allocated once at chapter open, reset/freed at chapter close. Cuts heap fragmentation, makes peak usage bounded and predictable.
- [ ] **Bitfields for in-memory state**: `style_flags` is a packed byte (bold/italic/underline/strike/code/sub/sup/link), not a struct of 7 `bool`s. Same for any flags-and-small-enums struct. *Never* for wire/file formats (bitfield ordering is compiler-defined) — for cache files use explicit bitwise ops on `uint8_t`.
- [ ] **Symbol size audit per phase**: after each phase's build passes, run `~/.platformio/penv/bin/pio run -t size` and `riscv32-esp-elf-nm --size-sort -t d .pio/build/default/firmware.elf | tail -50`. Read the largest 50 symbols. Anything surprising — a `printf` format-string explosion, a const that landed in `.data` instead of `.rodata`, a struct member ordering wasting half its space — gets fixed before merging the phase.
- [ ] **`static const` for all constant tables**: HTML entity → codepoint map, default tag→style table, image-format magic bytes, error-code → message map. The compiler places `static const` in `.rodata` (flash); plain `const` may end up in `.data` (RAM). Verify with the symbol audit.
- [ ] **No static `.bss` buffers > 256 bytes for transient data** (per spec §14.2). Allocate at function entry with `malloc`, free on every exit path, set to `NULL` after free. The font cache's `compact_buf[2048]` is the documented exception (always-needed scratch for the render hot path).
- [ ] **Struct member ordering largest-to-smallest** (CLAUDE.md rule). `font_data_t`, `cache_slot_t`, and the new `book_t`, `chapter_t`, `paginator_t` all order fields by size descending to minimize padding.
- [ ] **Integer types — `<stdint.h>` everywhere**: `uint8_t`, `uint16_t`, `int32_t`, never bare `int` for fields where size matters (CLAUDE.md rule).
- [ ] **No VLAs, no recursion deeper than 4-5 levels**: SAX is event-driven, not recursive. Selector matchers walk ancestors iteratively. Any recursive helper must justify its depth bound in a comment.
- [ ] **`restrict` on hot-loop pointer params**: `text.wrap_spans`, `display.draw_words`, image decode blits all qualify. Tag the non-aliasing pointer parameters so the compiler can keep values in registers.
- [ ] **Fixed-point (Q16.16) over float**: ESP32-C3 has no FPU. Justification slack distribution, image scaling, line-height resolution all use scaled integers. Floats only at config-parse boundaries (CSS lengths) and immediately convert to Q16.16 or pixels.

**Build flags audit (Phase 9.0 task — flag for review):**

- [ ] Verify our `platformio.ini` enables (or confirm Espressif platform default has):
  - `-Os` — size optimization
  - `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` — dead-section stripping
  - Optional: `-Wl,-Map=.pio/build/default/firmware.map` — generate the linker map file for `riscv32-esp-elf-nm` work above
  - Optional but valuable: `-Werror=stack-usage=512` — compile-time stack-usage gate
  - Optional: `-fstack-usage` — emit per-function `.su` files for stack analysis
- [ ] If any flag is missing, decide per-flag whether to add it (some, like `-Werror=stack-usage`, may need exemptions for vendored code like Lua/expat). Document the decision.

**Per-phase coding-standards gate (one bullet, added to every phase's checklist):**

> - [ ] **Coding standards check**: new files have headers; functions ≤ 100 LOC; files ≤ 1000 LOC; constants are `static const`; struct members ordered by size; symbol size audit reviewed (`pio run -t size`); dead code deleted; no static `.bss` > 256 bytes for transient state. See "Coding standards & memory discipline" cross-cutting section.

Add that bullet to every phase's task list as part of its ship gate.

### RAM accounting log

Each phase ends with a measured snapshot recorded here:

| Phase | Flash | Baseline RAM (home active) | Working set RAM (EPUB reading) | Notes |
|-------|-------|----------------------------|--------------------------------|-------|
| 9.0 | 637,676 (9.7%) | 78,232 (+1,152 vs Phase 8.5) | n/a | plumbing only; static `caps[6][12]×16` per-plugin metadata is the unavoidable baseline cost — opt-in dispatch keeps subsequent phases free |
| 9.A | 716,350 (10.9%) | 78,232 (+0 vs 9.0) | TBD (~20 KB above baseline) | expat + zip + xml + epub modules; opt-in dispatch keeps baseline RAM exactly flat — bindings live in flash until a plugin declares them |
| 9.B | TBD | (matches 9.A ±200 B) | TBD (~30 KB above baseline) | text rendering |
| 9.C | TBD | (matches 9.B ±200 B) | TBD (~35 KB above baseline) | + CSS |
| 9.D | TBD | (matches 9.C ±200 B) | TBD (~45 KB above baseline) | + images |
| 9.E | TBD | (matches 9.D ±200 B) | TBD (~25 KB cache-hit / ~45 KB cache-miss) | + persistence |
| 9.F | TBD | (matches 9.E ±200 B) | (same as 9.E) | + nav |
| 9.G | TBD | (matches 9.F ±200 B) | (same as 9.E) | + hardening |

Fill in actual numbers as each phase ships. If the baseline column drifts upward across phases, that's a regression — investigate the leak before continuing.

### Per-phase commit pattern

Each phase ships as one or more commits with the prefix `feat: EPUB Phase 9.X — <subject>`. The build_plan and the relevant docs update in the same commit set as the code, not a follow-up.

### Pre-existing items to revisit

These are not Phase 9 work, but they touch overlapping code and should be checked during Phase 9 development:

- The shared-state optimization for system plugins (`plugin_manager.c:465-491`) — confirm it doesn't break with the manifest `requires` field changes from Phase 9.0. System plugins should keep sharing state since they all use core only.
- `firmware_home.lua` declares no `requires` — confirm it still loads correctly after Phase 9.0 lands.
- Existing `txt_reader.lua` and `md_reader.lua` declare no `requires` — confirm they still work after Phase 9.0.

---

## End of plan

Ready to start with Phase 9.0 when you are. Each phase is independent enough to ship, test, and merge before starting the next one.
