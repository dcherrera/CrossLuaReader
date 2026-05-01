# Web Installer

**Status:** Spec.
**Owner:** Project maintainers.
**Goal:** Define the web property, install/update flow, and plugin distribution architecture for CrossLua devices.

---

## 1. Overview

A single web property hosts the platform's user-facing surface: install, plugin store, library, docs, community links. One URL, one trust signal, one onboarding path.

- **Domain:** `crosslua.teamide.dev`
- **Framework:** Quasar (Vue 3, SPA mode)
- **Binary host:** `git.teamide.dev` (self-hosted, project maintainer's VPS)
- **Hosting:** static SPA, deployed alongside the binary host or to any static host pointing at the same domain
- **Install mechanism:** USB mass-storage passthrough ("SD mode"), with WebSerial fallback for hardware that cannot enumerate as MSC

---

## 2. Site Structure

```
crosslua.teamide.dev/
├── /              — Landing
├── /install       — Install firmware (SD mode + WebSerial fallback)
├── /plugins       — Plugin catalog (download .ticl)
├── /library       — Web library reader (GitHub-backed)
├── /docs          — Kernel API, plugin guide, BYO font, etc.
└── /community     — Discord / Reddit / X / YouTube
```

Each section is independently shippable. Recommended order: Install → Docs → Plugins → Library.

---

## 3. The "SD Mode" install mechanism

The defining UX choice. Instead of WebSerial flashing strangers' firmware over USB, the device exposes its SD card as a USB mass-storage volume. The browser writes firmware update files or `.ticl` plugin files directly to the SD card via the OS's normal file APIs. The device picks them up on next boot.

### How it works

1. User holds a button combo at power-on (e.g., `BACK + POWER`) → device boots into **SD mode** instead of running normal firmware.
2. Device shows "SD MODE — connected to computer" on the e-paper display.
3. Device enumerates over USB as a removable mass-storage volume (label: `CROSSLUA`). The SD card is the backing storage, exposed read/write to the host.
4. Web installer instructs the user to drag the appropriate firmware file or plugin file to the volume, or copies it via the File System Access API where supported.
5. User ejects the volume and reboots the device. Bootloader checks for `/system/firmware.bin` (or scans `/plugins/*.ticl` for new entries) and applies updates.

### Why this is better than WebSerial flashing

- **Browser-agnostic.** OS file dialogs work in Safari, Firefox, mobile browsers, anywhere. WebSerial is Chromium-only.
- **Lower trust burden.** The user is copying a file, not authorizing a binary flash. The device's bootloader gates what gets applied.
- **Same flow for firmware and plugins.** Drop a `.bin` or a `.ticl`, both work.
- **Recoverable.** A bricked firmware can be fixed by the user dragging a known-good `.bin` to the volume — no JTAG, no recovery cable.
- **Works without internet on the device side.** The host computer has internet; the device is just a flash drive.

### Hardware support matrix

| Board | USB capability | SD mode? | Fallback |
|---|---|---|---|
| Xteink X4 (ESP32-C3) | USB-Serial/JTAG only — **no MSC** | No | WebSerial flash + manual SD card reader for plugins |
| LilyGO T5 (ESP32-S3) | USB OTG + TinyUSB | **Yes** | n/a |
| Custom P4 board | USB 2.0 HS + TinyUSB | **Yes** (fastest) | n/a |

The C3's USB peripheral does not support mass storage class. X4 users get WebSerial firmware flashing and either remove the SD card or use the future on-device plugin store (see section 6.2).

### Implementation notes (firmware side)

- TinyUSB is included with ESP-IDF; MSC class adds ~15 KB.
- "SD mode" is implemented as an alternate boot path: the bootloader (or first-stage firmware) checks the button combo and either runs normal firmware or jumps to the MSC stub.
- While in SD mode, normal firmware does not run. The MSC stub owns the SD bus and shows a static splash screen.
- Exit by ejecting the volume on the host or holding the button combo again.
- Update detection: bootloader scans for `/system/firmware.bin` and (if present + valid checksum) applies it on next normal boot, then deletes the file. Plugins do not need an "install" step — they are just files on SD.

---

## 4. Firmware distribution

### Where binaries live

`git.teamide.dev` hosts a release repository for CrossLua firmware artifacts. Per release:

- One `.bin` per supported board (`crosslua-x4-v0.5.bin`, `crosslua-t5-v0.5.bin`, `crosslua-p4-v0.5.bin`)
- A signed `manifest.json` with version, board ID, SHA-256, release notes URL, minimum bootloader version
- A `latest.json` pointer for "give me the most recent stable build" queries

The web installer fetches `latest.json`, presents board options to the user, downloads the matching `.bin`, and instructs the user how to install (drop on the SD-mode volume, or flash via WebSerial for X4).

### Versioning and channels

- **stable** — tagged releases that have been on the `main` branch for at least one minor version
- **beta** — `main` branch HEAD, fresh builds
- **dev** — feature branches, opt-in only

Web installer defaults to `stable`. Power users can toggle `beta` in the UI. `dev` requires entering a branch name.

### Build pipeline

CI (on `git.teamide.dev`) builds all board variants on tag push and uploads artifacts to the release repository. The web installer reads the manifest; no manual deployment per release.

---

## 5. Plugin store ("Tickle catalog")

### Catalog architecture

A single curated catalog repo on `git.teamide.dev` (e.g., `crosslua/tickle-catalog`) contains:

- `catalog.json` — array of plugin entries with id, name, version, author, description, capabilities, runtimeVersion, source repo URL, download URL, screenshot URLs
- `plugins/<id>/` — per-plugin metadata, icon, longer description, changelog

Plugin authors submit a PR to the catalog repo to register a new plugin or update an existing one. Maintainers review for licensing, capabilities, and obvious quality issues.

### Submission requirements

- Plugin source must be public (any Git host)
- License must be MIT / Apache 2.0 / OFL or equivalent open-source license
- `manifest.lua` must declare `runtimeVersion`, `capabilities`, version, author, license
- No redistribution of copyrighted content (fonts, ebooks, audiobooks the author does not have rights to)
- Build output (`.ticl` file) attached to a release on the source repo; catalog points at that URL

### Moderation policy

For v1: **curated.** Maintainers review every submission. Documented as such on the submit page so contributors know the bar.

For v2 (when submission volume exceeds review capacity): **hybrid.** Anyone may submit; some entries get a "verified by maintainer" badge. Unverified entries display a clear warning. This is the VS Code Marketplace / Home Assistant / OBS plugin model — proven to scale.

### Download flow

**Phase 1 (web only):**

1. User visits `/plugins`, browses catalog, picks a plugin.
2. Clicks "Download" → browser downloads the `.ticl` file.
3. User puts device into SD mode (or pulls SD card on X4).
4. Drags the `.ticl` to the device's `plugins/` folder.
5. Reboots device. Plugin appears in the menu.

**Phase 2 (on-device store, optional):**

A `tickle_store.ticl` plugin shipped as a community plugin queries `catalog.json` over WiFi from the device, displays the catalog on the e-paper UI, downloads selected plugins to SD over WiFi. Installs without touching a computer.

This is gated on:
- Network APIs being mature (Phase 12 in `LongTermPlan_CrossLuaKernal.md`)
- HTTPS + TLS support
- Trust mechanism (signed catalog or signed plugins)

For v1, decision is **defer.** Web download is sufficient. Revisit once kernel networking is stable.

---

## 6. Library Web App

A Git-repo-backed reading library. Lives at `/library`. Ships **after** kernel network APIs are mature (post Phase 12). Route stubs to "Coming Soon" until then. This section captures enough architecture to resume design in a future session.

### Core thesis

**Your library, annotations, and reading progress are a Git repository.** Every device is a working copy. Every highlight is a commit. Every read is in your history forever, queryable with `git log`. Goodreads is server-locked, Calibre is single-device, Kindle is Amazon-locked. A Git-versioned reading life is *yours*, *forever*, *portable*, with *complete history*. That is the unique value proposition — not "GitHub as generic file server."

### Architecture

Static Quasar SPA on `crosslua.teamide.dev/library`. No backend. No database. The user's authenticated Git host is the entire storage layer.

- User authenticates via OAuth against their chosen host (GitHub primary; Gitea on `git.teamide.dev` secondary; other Git hosts as future expansion).
- The SPA reads/writes a designated private repo via the host's REST/GraphQL API.
- All session state lives in `localStorage`: OAuth token, repo selection, current book, reading position cache.
- No server-side state. SPA can be hosted anywhere static.

### Repo layout (single-library-per-user model)

```
my-library/                       (private repo)
├── books/
│   ├── frankenstein.epub
│   ├── dracula.epub
│   └── ...
├── metadata.json                 (catalog: titles, authors, tags, file paths)
├── progress.json                 (per-book reading position, last-opened, finished)
├── highlights/
│   ├── frankenstein.json         (highlights + notes per book)
│   └── ...
└── README.md                     (optional: human-readable library description)
```

One repo per library. Simpler mental model than one-repo-per-book. Whole library shareable by sharing repo. Grows over time; soft 1 GB recommendation is plenty for hundreds of books.

### Repo layout (open-content / forkable-book model — future)

For public-domain books, the alternative is **one repo per book** at the project level (e.g., `crosslua/gutenberg-frankenstein`). Users fork "their copy" of a book to attach personal highlights/annotations. Public forks are visible — you can see what other readers thought of chapter 7 by browsing their forks. This is the social-reading angle (Hypothes.is-meets-margin-scribbles, Git-native).

Defer this until the personal-library model is shipped and proven. The two models can coexist on the same SPA — they just point at different repos.

### Constraints (GitHub specifics; adapt for other hosts)

| Constraint | Limit | Impact |
|---|---|---|
| Max file size | 100 MB without LFS | Most EPUBs fit. Audiobook M4B (often 100–500 MB) does not. |
| Repo size soft cap | ~1 GB recommended | Personal libraries fine; comic collections at scale are not. |
| API rate limits | 5,000 req/hr authed | Plenty for personal use; busy users may approach the limit during initial sync. |
| LFS bandwidth | 1 GB/month free | Audiobooks via LFS bills overage. Avoid for v1. |
| Public repos with copyrighted content | DMCA target | Personal libraries **must** be private. |
| `raw.githubusercontent.com` | CORS + Range requests supported | Browser fetch works directly; partial content reads are possible. |

Equivalent caps on Gitea / self-hosted hosts depend on operator config; `git.teamide.dev` can configure higher limits if needed.

### Auth flow

1. SPA initiates OAuth with the selected host (GitHub or Gitea).
2. User authorizes; callback returns to `/library/callback`.
3. SPA exchanges code for token; stores in `localStorage`.
4. SPA queries the user's repos, prompts to select an existing library repo or create a new one.
5. All subsequent reads/writes go through the host's API with the stored token.

Use `octokit/rest` for GitHub. Gitea has REST API compatible enough that a thin wrapper handles both. Multi-host support is a small effort; bake it in from the start.

### Multi-device sync

Each device (web SPA, CrossLua reader, future iOS/Android app) is a Git working copy. Reading position changes are commits. Sync is `git pull` followed by `git push`.

**Conflict-handling decision (open):**

Two devices simultaneously updating `progress.json` cause merge conflicts. Three viable strategies:

1. **Per-device branches** — each device commits to a `device/<deviceId>` branch; the SPA merges them on read. Simple, robust, but every read does a multi-branch merge. Acceptable for small-N device users.
2. **CRDT-encoded state** — use a JSON CRDT format (Automerge, Yjs) for `progress.json` and `highlights/*.json`. Conflicts resolve automatically. More setup, but device count is unlimited.
3. **Last-write-wins with timestamps** — simplest, occasional silent loss of progress on conflict. Probably fine for a single-user-multiple-devices case where simultaneous writes are rare.

**Lean:** start with last-write-wins + clear timestamps. Upgrade to CRDT if real conflicts emerge. Per-device branches are a fallback if neither works.

### CrossLua reader plugin: `library_github.ticl`

The on-device counterpart. Likely a service plugin (long-lived, runs in background) plus a reader plugin (foreground UI for browsing the library).

- Configured with: Git host URL, OAuth token (entered once via paired web flow or QR-code-from-web), repo path
- On WiFi connect: pulls latest `metadata.json` and `progress.json`
- User browses the library on-device; selects a book; plugin downloads the EPUB to local SD card cache
- Reading progress writes to `progress.json` and commits back via API
- Highlights commit back as updates to `highlights/<book>.json`

Depends on:
- Kernel HTTPS/TLS (Phase 12)
- Async task scheduler (`task.*`, Phase 10)
- KV store for credential storage (Phase 10)
- Storage sandbox so the plugin only writes to its own cache dir

Until those land, the library web app can ship as web-only and the plugin lands later.

### What this idea is NOT good for

- **Generic ebook hosting for strangers** — copyright. Personal-library only, or open public-domain content only.
- **Real-time collaborative reading** — wrong tool. Use a database for that.
- **Audiobooks at meaningful library size** — file size cap + LFS bandwidth cost.
- **Mainstream-user onboarding** — "sign in with GitHub" is fine for developers, friction for everyone else. Acceptable for the early-adopter audience this product targets.

### Open questions for next session

1. Which Git hosts to support at launch? GitHub is required. Gitea on `git.teamide.dev` is desirable. Others (GitLab, Codeberg, Bitbucket) are easy adds if API surface is similar.
2. Conflict resolution choice (last-write-wins vs CRDT vs per-device branches) — decide before kernel plugin work begins so the data format matches what device-side code expects.
3. Should the SPA support ePub + PDF rendering in-browser? `epub.js` and `pdf.js` both work. Decision affects scope and bundle size.
4. Highlights / annotations format — Hypothes.is uses W3C Web Annotations; reusing that schema would let third-party annotation tools interop. Otherwise design a CrossLua-native format.
5. Encryption-at-rest of book content in the repo — useful if user puts books in a less-private repo or shares with collaborators. Out of scope for v1; flag for later.
6. Search across the library — full-text search across hundreds of EPUBs in-browser is a non-trivial perf problem. Index files (per-book or repo-wide) committed to the repo? Defer until v2.

### TL;DR for resuming the conversation

Build a static Quasar SPA at `crosslua.teamide.dev/library` that authenticates against GitHub (and Gitea on `git.teamide.dev`) and reads/writes a private repo as the user's entire library + progress + annotations. The defining pitch is "Git-versioned reading life." A companion `library_github.ticl` plugin syncs the same repo from the device. Conflict resolution defaults to last-write-wins, upgradable to CRDT. Audiobooks deferred (file size). Personal-library-only at launch; forkable open-content books are a future evolution. Ships post Phase 12 of the kernel plan.

---

## 7. Framework + tooling decisions

| Concern | Choice | Rationale |
|---|---|---|
| SPA framework | **Quasar (Vue 3)** | Project standard. Component library, theming, mobile-friendly out of the box. |
| State | Pinia | Quasar-recommended. Lightweight. |
| Routing | Vue Router (built into Quasar) | n/a |
| Build/deploy | Quasar CLI → static build → served from `crosslua.teamide.dev` | Same VPS as `git.teamide.dev` |
| OAuth (library) | GitHub OAuth + (optionally) Gitea OAuth for users on `git.teamide.dev` | Multi-host support is small effort; lets self-hosters use their own Gitea |
| WebSerial library (X4 fallback) | `esptool-js` | Espressif's official browser flasher |
| TinyUSB MSC (firmware side) | ESP-IDF native | Standard component |

---

## 8. Trust and security

The web installer asks users to write firmware to their device. Trust must be earned visibly.

- **Open-source firmware.** Source link displayed per build on the install page.
- **Reproducible builds.** Document build steps so anyone can verify a binary matches source.
- **SHA-256 displayed before install.** User can verify against the GitHub release page.
- **Signed manifests.** `manifest.json` and `catalog.json` are signed with a project key; web app verifies the signature before presenting options. Prevents tampered manifests on a compromised CDN.
- **Bootloader recovery.** A bricked device can always be recovered by the user dragging a known-good `.bin` to the SD-mode volume. Document this prominently.
- **Plugin sandboxing.** Plugins are gated by the kernel's capability system (Phase 9.0). The catalog displays declared capabilities so users see what a plugin will be permitted to do.

---

## 9. Risks and open questions

### Risks

1. **WebSerial fallback friction for X4 users.** Chromium-only. About 25% of users will hit this. Mitigation: clear messaging and a "use SD card directly" alternative path documented.
2. **SD mode requires hardware that supports USB MSC.** X4 (C3) cannot use it. Acceptable; X4 is the legacy reference platform and most users will move to T5/P4 over time.
3. **Plugin moderation bandwidth.** Curated v1 model gates submissions on maintainer time. Risk of bottleneck as catalog grows. Documented escalation path: hybrid model in v2.
4. **Self-hosted infrastructure.** `git.teamide.dev` is a VPS. If it goes down, installer breaks. Mitigation: deploy a redundant CDN mirror for `latest.json` + binaries on a second host. Low priority for v1.
5. **Trust on first install.** First-time users have nothing to validate against. Mitigations: open-source source link per build, social-proof via Discord/community, signed releases.

### Open questions

1. **Should the SD-mode UI on the device show a QR code linking to `crosslua.teamide.dev/install`?** Probably yes — turns "I plugged in my reader, what now" into a one-tap journey for users with phones nearby.

2. **Bootloader vs first-stage firmware for SD mode.** Implementing SD mode in the bootloader gives stronger recovery guarantees but bigger bootloader footprint. Implementing it as a first-stage firmware mode is smaller but fails to recover from a corrupted main firmware. Lean: bootloader, accept the size cost.

3. **Multi-arch Quasar app or per-board variants?** Single SPA with runtime board selection is simpler. Per-board landing pages might convert better for marketing but multiply maintenance. Lean: single SPA.

4. **Library web app at `/library` vs separate domain.** Single domain wins on trust and discoverability. Lean: keep it on `crosslua.teamide.dev/library`.

5. **Should plugin downloads route through `crosslua.teamide.dev` or directly from the source repo?** Direct from source repo is simpler and shows no centralized failure. Routing through the domain enables analytics and version pinning. Lean: direct, with the catalog providing the URL. Add proxy later if needed.

6. **Mobile install flow.** USB MSC works fine when a phone is the host (Android exposes mounted volumes; iOS via the Files app). WebSerial does not work on mobile. The X4's lack of MSC means X4 users can't install from a phone at all. Acceptable for v1; documented.

7. **Telemetry.** None proposed for v1. Whether to add anonymous install/version telemetry (opt-in) is a decision for after launch.

---

## 10. Sequencing

Don't build all four sections at once. Suggested order:

1. **Landing + Install (X4 WebSerial first, T5 SD mode when T5 ships)** — covers "what is this" and onboarding.
2. **Docs** — port `/docs/` markdown to Quasar pages with proper styling.
3. **Plugin catalog** — once 3+ community plugins exist worth showing.
4. **Library web app** — after kernel network APIs are mature (post Phase 12).

Each section ships independently. The site can launch with just (1) and grow from there.

---

## 11. Success criteria

The web installer ships successfully if:

1. A new user can go from "I just got my T5" to "running CrossLua" in under 5 minutes, without installing any tools.
2. A new user can go from "I want this plugin" to "it's running on my device" in under 2 minutes (Phase 1: download + drag; Phase 2: on-device store + WiFi).
3. A bricked device is recoverable by the user without a JTAG cable, in under 5 minutes.
4. The same firmware artifact published on `git.teamide.dev` runs on every device that build was targeted to, without per-user customization.
5. Plugin authors can register a new plugin via PR and see it live in the catalog within a week.

---

*This document is the source of truth for the web installer architecture. Update it when scope or decisions change.*
