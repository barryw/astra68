# Atlas DH0 Root Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Double-click the existing mounted DH0 desktop volume and open Atlas
showing its actual root entries as named file/folder icons.

**Architecture:** Reuse the current verified DH0 binding, native Filesystem Kit
directory batches, protected Graphics surfaces, retained Interface layout, and
native launcher arguments. This initial integration uses the existing mounted
volume; dynamic catalog, Linux storage cutover, and optical hardware remain
subsequent production milestones. Atlas owns directory data, not another
filesystem or private icon renderer.

**Tech Stack:** MC68040 C11, native Astra libraries, deterministic AICON assets,
Beast builds/QEMU, and physical DE25 through Beast.

**Spec:** [Production volumes design](../specs/2026-10-12-production-filesystem-volumes-design.md).
User approves the narrowed viewer and explicitly requests starting the DH0
build on 2026-10-12. Execution is native in this session.

## Global constraints

- Keep current Filesystem ABI 12/STOR 36 semantics and QEMU UID/GID 999.
- Initial window enumerates one directory; no folder/file activation or Atlas
  sidebar, toolbar, file operations, or administration pane.
- Derive names and kinds from real native directory batches; keep all admitted
  entries and report capacity/errors truthfully.
- Existing mounted DH0 is authoritative for this slice. Do not advertise USB,
  optical, or dynamic-mount acceptance based on this proof.
- Match supplied HTML Atlas grid vocabulary: lunar client, graphite document
  marks, folder accent, 40-pixel icons, standard window chrome.
- All builds and tests on Beast; preserve exact source mirrors and runner IDs.
- Before release, preserve mutable user data and retain the previous release.

## Review focus

- Enumeration ends on the zero-entry final batch; no first-batch truncation or lost
  entries when a directory grows beyond the initial allocation.
- Close while enumeration waits never frees memory or filesystem state still
  used by the worker; process teardown remains the native owner-death path.
- Unknown/malformed names or kinds cannot select arbitrary resources or escape
  grants; clipping never changes the stored filename.
- Resize/scroll keeps entries reachable and clips images/text to the viewport.
- DH0 authority is granted explicitly to the desktop and trusted Atlas bundle,
  without widening every application's launch ceiling.

## Task 1: Shared retained geometry and AICON surface construction

**Files:** `ndk/include/astra/interface_library.h`,
`sw/userspace/interface/src/control.c`, Interface exports/identity/contracts;
`ndk/include/astra/graphics_library.h`, Graphics source/build/exports/identity;
the corresponding Kit manifests and host tests.

**Interfaces:**

```c
AstraResult astra_interface_ui_geometry(const AstraUIContext *context,
    uint32_t index, AstraControlFrame *frame, AstraControlFrame *clip);
AstraResult astra_surface_from_aicon(AstraDisplay *display, const void *bytes,
    uint32_t length, uint16_t pixel_size, AstraSurface *surface);
```

- [x] Add malformed/context/index/geometry tests to the existing layout suite.
  Check outputs remain unchanged on refusal, and clipped/scrolled geometry
  agrees with retained layout.
- [x] Add AICON constructor tests using the real format decoder and fake
  Graphics operations: valid strike/upload, malformed bytes, missing strike,
  allocation/create/upload refusal, and no surviving surface on failure.
- [x] Extend existing implementations: indexed geometry reads existing retained
  state in O(1); icon construction decodes once, creates/uploads one protected
  ARGB surface, and unwinds failure. No alternative widget or pixel backend.
- [x] Keep scroll content extents within the child's declared maximum, testing
  constrained wrapping instead of compensating for it in Atlas.
- [x] Run host/sanitizer/contracts and cross-build. Advance compatible library
  revisions and update in-tree manifest/build contracts coherently.

## Task 2: Minimal shared object assets

**Files:** `tools/aicon.py`, its tests, `tools/astra-bundle.mk`.

- [x] Add `atlas`, `volume`, `drawer`, and `document` recipes using the supplied
  design's geometry. Reuse existing 16/32/64 AICON format; the protected 64-pixel
  source is displayed at 40 pixels through native scaled blits. Existing
  application recipes keep their original strikes.
- [x] Test header/strike offsets, palette/index validity and deterministic
  encoding at 16/32/64 for these recipes.
- [x] Register built-in recipe names in the existing bundle maker. Generate
  final assets through build rules, not checked-in build products.

## Task 3: Native Atlas root viewer

**Files:** new `sw/userspace/applications/atlas/{main.c,directory.c,directory.h,Makefile}`
and focused model tests; applications umbrella; `sw/userspace/apps/Atlas.app/manifest`
and bundle inventory/build rules.

**Interfaces:**

```c
typedef struct AtlasDirectory {
    AstraDirectoryEntry *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t status;
} AtlasDirectory;
uint32_t atlas_directory_load(AtlasDirectory *model,
    AstraFilesystem *filesystem, const char *path, const uint32_t *cancelled);
void atlas_directory_dispose(AtlasDirectory *model);
```

- [x] Test real batch accumulation through injected native-directory responses:
  empty, several batches, dot-prefixed/UTF-8 names, allocation refusal, read
  refusal/cancellation, and close exactly once.
- [x] Implement growable owned data with checked arithmetic. Preserve entry
  metadata and filenames, close the retained directory on every terminal path,
  and return precise native errors.
- [x] Create a normal native surface window with title/application name Atlas;
  start finite enumeration on a native thread and wait for worker/window events
  through the existing wait set. Show loading before I/O completes.
- [x] Build nonvisual retained wrap cells and ScrollView; use public geometry,
  native Graphics text and shared icon surfaces for data presentation. Render
  only visible entries, with two shared resources and clipping. Reflow on
  resize; existing scroll model handles offsets and wheel/key movement.
- [x] Close promptly. Join/dispose completed work; active work remains owned by
  native process teardown rather than freeing its state underneath it.
- [x] Package Atlas with GUI/APPS/LIBS/DH0/LOG read authority and current library
  requirements. No POSIX layer, write grants, or filesystem workaround.
- [x] Build host/sanitizers/analyzer and MC68040 through Beast.

## Task 4: Existing mounted DH0 desktop entry and launch

**Files:** desktop `main.c`, layout tests, `emu/qemu/astra_image.py`, Atlas manifest.

- [x] Confirm the selected DH0 directory through native metadata before showing
  its volume object. Use existing canonical name, separate volume identity from
  application bundle entries, and retain deterministic layout/hit targets.
- [x] Reuse shared icon construction for current application icons and DH0.
- [x] Double-click DH0 invokes:

```c
const char *arguments[] = { "/dh0" };
astra_application_launch_with_arguments(launcher, "/apps/Atlas.app",
    sizeof("/apps/Atlas.app") - 1u, ASTRA_LAUNCH_SOURCE_DESKTOP,
    arguments, 1u, &process_id);
```

- [x] Add DH0:r to the desktop service's manifest and a trusted Atlas launch
  ceiling through the same mechanism used by Terminal. Keep ordinary ceilings
  unchanged; image policy validation proves requested rights fit the declaration.
- [x] Extend desktop gates for volume/application hit-target separation and
  test trusted read-only Atlas grants without widening ordinary ceilings.
  Missing bindings use the shared native metadata refusal contract; dynamic
  disappearance remains part of the live-volume phase.

## Task 5: Exact guest/physical proof and continuation

**Files:** focused Atlas QEMU gate using existing `test-terminal.Machine`,
desktop-app integration, image fixture tests, docs/NDK continuation.

- [x] Build exact-source userspace/ROM and coherent image on Beast under its
  shared lock. Run library/NDK checks plus the current desktop and filesystem
  semantics gates.
- [x] Boot the image, double-click logged DH0 target, require successful Atlas
  startup and complete real-root enumeration. Compare names/kinds/counts with
  the same fixture's native filesystem oracle and inspect fresh rendered pixels.
- [x] Exercise empty and multi-batch/Unicode directories through normal launcher
  arguments, scrolling/resize, read refusal and process retirement. Host model
  tests cover cancellation between batches and exactly-once close. A forced
  guest I/O stall remains part of the later live-volume removal qualification.
- [x] Stage a coherent immutable DE25 release, preserve mutable data, deploy,
  verify credentials/services/no new panic, and capture DH0 -> Atlas with real
  input. Retain previous release and exact hashes.
- [x] Record which initial-viewer gates pass and leave production cutover,
  dynamic hotplug/optical and broad performance acceptance explicitly open.

## Verification commands

Run each group on Beast from its mirrored source under `/home/barry/astra-mg/locked`:

```sh
make -C sw/userspace/interface test sanitize
make -C sw/userspace/graphics test sanitize
make -C sw/userspace/applications/atlas test sanitize analyze all
make -C sw/userspace/apps all test
make -C sw/userspace all
make -C ndk header-contract docs-check
```

Use the current publisher and existing QEMU/DE25 qualification helpers rather
than another release or mount implementation. Failed attempts and source/ELF
catalog identities remain part of evidence.

## Completed checkpoint

Initial DH0 viewer accepted on immutable DE25 release `5bc3bbea`, with previous
release `36791f1e` retained. Exact staged Atlas/desktop/audio/Doom/filesystem gates
pass; native root capture shows 16 entries and close restores identical desktop
pixels. `/home` and `/local` contents/metadata survive migration. Evidence and
retained failures are in [Atlas continuation](../../HANDOVER_2026-10-12_ATLAS.md).
Production storage cutover, live volume catalog, USB/optical and mount controls
remain subsequent milestones; broad performance acceptance remains open.
