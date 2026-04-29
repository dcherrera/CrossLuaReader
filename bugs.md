# Known Bugs

## Faint first render in landscape mode

**Status:** Open
**Severity:** Minor (cosmetic)

After boot or sleep wake in landscape orientation, the first screen render appears faint/light. Moving the selection or triggering any subsequent render causes the screen to fill in properly.

**Root cause (suspected):** The e-ink controller's internal "old image" buffer doesn't match the actual panel state after orientation change. The waveform calculation uses old vs new image comparison to determine drive voltages — if the old image is wrong (portrait layout vs landscape layout), pixels are under-driven on the first refresh.

**Not the cause:**
- Deep sleep conditioning (portrait mode wakes fine)
- Refresh mode (full refresh doesn't fix it)
- Boot-time black→white conditioning cycle (no effect)
- Double full refresh on first render (no effect)

**Workaround:** Press any button to trigger a re-render — screen fills in correctly.

**Possible approaches to investigate:**
- `EInkDisplay::requestResync()` — SDK method that hints the controller to do a full resync on next update
- Writing the framebuffer to both the controller's "old" and "new" RAM banks before triggering refresh
- Panel-specific initialization sequence after orientation change
