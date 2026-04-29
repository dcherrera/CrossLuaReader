# Random Tasks

## Dead Code Check

- [ ] Check if `ui.request_refresh()` / `ui.check_refresh()` are called anywhere — if not, delete them
- [ ] Check if `text_layout.lua` functions are still needed — C-side `text.wrapString()` may have replaced everything except `detect_rtl()`

## MD Reader Magic Numbers

- [ ] Parameterize hardcoded indent values in `md_reader.lua` render callback:
  - `16px` code block / blockquote indent
  - `20px` per-level list indent
  - `10px` bullet offset
  - `4px`/`5px` blockquote border position
  - `2px`/`3px` code block border position

## Layout Engine Docs (Phase 7)

- [ ] Write `docs/layout-engine.md` — user-facing guide replacing the spec
- [ ] Update `docs/architecture.md` with layout engine section
- [ ] Update `docs/plugin-guide.md` with `layout.*` usage examples

## Clean Up Build Plan

- [ ] Update Dead Code Audit Summary table in `layout_engine_plan.md` — most items already removed
