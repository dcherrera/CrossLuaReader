# EPUB Plugin Guide

How to write plugins that consume EPUB books in CrossLua Reader. Builds on `docs/lua-api.md` (the full module reference) and `docs/plugin-guide.md` (general plugin lifecycle and the `requires` field).

This document is built up across the EPUB phases. Phase 9.A (current) covers metadata and navigation only — chapter rendering arrives in 9.B.

---

## Capabilities a reader plugin needs

A plugin that opens EPUB books declares its capability dependencies in the manifest:

```lua
plugin = {
    name = "My EPUB Reader",
    id = "my_epub_reader",
    type = "reader",
    fileExtensions = {"epub", "epub3"},
    requires = {"zip", "xml", "epub"},  -- 9.A
    -- requires = {"zip", "xml", "epub", "css", "image"},  -- 9.D+
    system = true,
}
```

Without `requires`, the EPUB-related Lua bindings are not registered into the plugin's Lua state — every plugin pays only for what it declares. See `docs/plugin-guide.md` § Plugin Capabilities.

---

## Phase 9.A surface

What the current EPUB API supports:

- Open a book and reject DRM cleanly: `epub.open(path)`.
- Read top-level metadata: title, author, language, EPUB version, page-progression-direction, modified date, cover image id.
- Walk the spine (linear reading order) and the manifest (every resource).
- Walk the TOC tree (NCX and NavDoc both normalized to the same `{label, href, depth, children}` shape).
- Resolve internal hrefs to spine indices: `book:resolve_href(base_href, href)`.
- Read manifest items into Lua strings up to 256 KB.

What's **not** in 9.A (planned for later):

- Chapter rendering (9.B): SAX iteration over chapter XHTML producing styled spans.
- CSS-driven layout (9.C): proper margins, indents, alignment, font sizes per element.
- Inline images (9.D): JPEG / PNG decode + cover thumbnails for the file browser.
- Pagination cache (9.E): per-chapter draw-list cache on SD; reading-position persistence.
- TOC sub-activity, footnote handling, anchor resolution UX (9.F).

---

## Minimum viable open-and-display

The Phase 9.A diagnostic plugin (`sdcard/plugins/epub_metadata_dump.lua`) is the canonical example. It opens a book, surfaces metadata, and lists the first few TOC entries. Use it as a starting template.

---

## Errors

`epub.open` returns three values on failure: `nil`, an error-code string, and a human-readable message. The error codes:

| Code | Meaning |
|------|---------|
| `io_error` | File open / read failure. |
| `not_epub` | Bad/missing OCF mimetype. The file might be a plain ZIP, or a broken EPUB. |
| `drm` | DRM-protected (Adobe ADEPT, etc.). Cannot be opened. |
| `malformed_container` | `META-INF/container.xml` missing or unparseable. |
| `malformed_opf` | OPF unparseable. |
| `no_spine` | OPF has no `<spine>` or it's empty. |
| `fixed_layout` | Pre-paginated rendition; not supported in v1. |
| `oom` | Out of memory during open. |

Surface these in your reader's error UI so the user knows whether to try a different file, repair the SD copy, or look up DRM removal.

---

Future sections will be filled in as later phases land:

- *Reading chapters* (Phase 9.B): the styled-span SAX consumption pattern.
- *Honoring CSS* (Phase 9.C): style query and override layering.
- *Images* (Phase 9.D): inline `<img>` handling and cover thumbnails.
- *Pagination cache & resume* (Phase 9.E).
- *Footnotes & links* (Phase 9.F).
- *Diagnostic UI for malformed files* (Phase 9.G).
