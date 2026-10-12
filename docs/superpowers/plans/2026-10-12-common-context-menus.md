# Common menus and context-menu implementation

User authorizes reusable right-click context menus in the common control
framework, reusing the existing compositor-owned ASTRA menu interaction.

## Scope

One Interface menu model owns row geometry, hit testing, enabled/checked state,
separators, keyboard traversal, scrolling, and painting through the existing
draw-list renderer. The display service presents that same control above the
window scene for both ASTRA and application context menus. Existing command
query/invoke remains the authority for application actions, including a fresh
enabled-state query at invocation. No application-local popup painter or modal
input loop is added.

The common retained UI dispatcher emits a context request for right-clicks and
keyboard context gestures without treating them as primary activation. A caller
may supply a command-backed menu for the returned control or its window. Desktop
icons use their existing Open operation as the first real consumer. Flat menus
are sufficient here; app menu-strip publication and submenus remain later work.

## Contract and ownership

- Menu rows and strings are copied into a counted, versioned area document.
  The owning private window endpoint is the authority; requests cannot target
  another window. The server copies the document before parsing and discards
  transferred capabilities after construction, so caller mutation cannot alter
  an open menu.
- Correlated terminal menu events carry selection or cancellation, never
  arbitrary client pointers. Stale replies cannot invoke a different menu.
- Open menus consume pointer/key input. Outside clicks dismiss without falling
  through. Escape, input reset, owner loss, window geometry/deactivation and
  teardown remove the overlay and restore the existing focus scope.
- Labels, sizes, flags, offsets, command references, overflow and allocation
  failure are validated. Long menus scroll; there is no fixed item-count slab.
- GUI wire/event versions and compatible library revisions advance coherently;
  every in-tree caller, manifest, export, test and public document is updated.

## Work

- [x] Record existing ASTRA menu behavior and relevant target timing baseline.
- [x] Add shared menu control/model and focused host/sanitizer tests.
- [x] Add common context request dispatch and command-backed client session.
- [x] Extend the native window transport and event validators, with malformed,
  ownership, cancellation and stale-result tests.
- [x] Replace compositor's hardcoded row/input implementation with the common
  menu control and preserve ASTRA power/about behavior through it.
- [x] Consolidate Desktop Open action for double-click and context selection.
- [x] Run Beast builds, host/sanitizers, NDK contracts/docs and focused guest
  context-menu, desktop, Atlas and power regressions.
- [x] Stage and qualify exact immutable release; preserve user state, deploy
  and exercise/capture real context input on DE25. Keep previous release.
- [x] Update continuation records, commit/push and durable memory.

## Retained checkpoints

Evidence root: `/mnt/Documents/astra68/builds/context-menus-20261012/`.

- `baseline-1` captures the accepted Atlas release's physical ASTRA menu and
  12 open/close pairs using HPS monotonic time and FPGA scene generation.
  Authenticated local RFB input-to-present is roughly 33 ms median with
  66–69 ms tails; transport-inclusive diagnostics do not qualify the internal
  two-frame feedback budget.
- System tests/sanitizers/analyzer/MC68040 contracts pass in `host-2` and
  current-code requalification in `host-6`. Interface model/context/control
  tests, sanitizers, analyzer and target contracts pass in `host-5`/`host-6`.
  Display/Desktop target and host gates and NDK headers/HTML/PDF pass in
  `host-8`; `host-9` adds held popup-key/text/release ownership coverage.
  `host-10` preserves cache-commit and double-click-chain resets and rebuilds
  the corrected NDK event integration example.
- `host-1`/`host-3` expose naming/placement mistakes before compilation;
  `host-4` exposes a test using a range getter for a checkbox. `host-6`'s
  final assertion incorrectly expected dense text to need multiple batches.
  Inspection shows grouped glyph runs already fit the compositor. The
  unneeded render-only refactor was removed; the original renderer is reused.
  Dense viewport rendering now checks over 1,024 glyphs with fewer than
  1,024 commands. `host-7` catches missing API macro documentation.
- `release-1` stages immutable source checkpoint `1e478b21`. Actual right-click,
  outside/Escape dismissal, release commit, pointer/keyboard DH0 Open and ASTRA
  keyboard interaction complete, but the new gate incorrectly treats ordinary
  demand-page materialization records as fatal faults. The gate now uses the
  established panic and process-retirement oracle. This release is not deployed.

- `release-2` stages `dab87987`; actual menu interactions and the About window
  assertion pass. The appended retirement oracle tried to consume the already
  consumed boot marker a second time. The gate now opens Terminal once at the
  start, reuses its existing lifecycle oracle, and verifies that terminating a
  menu with Enter cannot execute an unfinished shell line. `host-11` covers the
  final shared key-event conversion and nonblocking modifier release under a
  full owner queue. No board deployment has happened yet.

- `release-3` stages `5085f59b`; a delayed initial Terminal launch log exposed
  an overbroad no-launch assertion. The gate now checks the actual DH0/Atlas
  target. `preflight-1` passes all interactions, the unfinished-shell-line
  guard and process retirement against that exact staged image. `host-12`
  passes current Interface/Display/Desktop host, sanitizer, analyzer and
  MC68040 gates, including title changes preserving an open menu and bounded
  PageUp/PageDown traversal. Clean `release-4` qualification is running.

- `release-4` stages `e7023a1b` and passes menus/Atlas/desktop. A runner used
  positional image syntax for Terminal; `qualification-1` corrects it to
  `--image --prepared-image` and passes all remaining exact-release gates:
  common controls, four power actions, 111 Terminal commands, 26 filesystem
  cases/recovery/fsck, bit-exact audio, Doom and 15 packaged NDK examples.
  ROM/image hashes remain unchanged. `deploy-1` preserves all four `/home` and
  `/local` entries including metadata, verifies the selected/previous releases
  and unprivileged credentials, and records zero restarts/no new panic.
- `board-1` verifies DH0 and app context Open, keyboard selection, ASTRA About,
  real process retirement and identical restored pixels. The 10,000-action
  menu workload averages 1.980808 µs below 12.5 µs. Captures convert losslessly.
  `candidate-1`/`candidate-2` show frame-quantized presentation shifts. `pair-1`
  stops on RFB startup readiness, restoring the new release; `pair-2` waits
  for the listening socket and completes two interleaved 24-pair runs per
  release. Old open/close medians are roughly 33/33 ms; new pairs distribute
  an extra frame between open and close. The selected release is restored.
- The common menu's fresh revision cannot identify the previous popup's
  pixels. The compositor now extends its existing bitmap cache with one
  16 KiB committed draw-list snapshot. Exact content/offset equality permits
  reuse across lifetimes; different labels and uncommitted paints invalidate
  it. This avoids retaining caller rows or adding another painter/backend.
  Focused cache regressions and current-source release qualification follow.

- `host-13` passes final cache regressions, sanitizers, analyzer and MC68040
  contracts. `release-5` stages `0aa2d713` and passes all exact-release gates
  with unchanged immutable hashes. `deploy-2` preserves user contents/metadata
  and credentials; `board-2` passes actual menus/Atlas/app/About, retirement,
  restored pixels and 1.947464 µs/action over 10,000 operations. `pair-3`
  completes two interleaved 24-pair runs per release and restores current;
  timing remains frame-quantized and does not establish a cache latency gain.
  `nodelay-1` explicitly sets TCP_NODELAY and measures current open/close at
  33.128/32.969 ms; different socket configuration is diagnostic. The cache
  eliminates redundant GPU painting, proven by command-count regressions.

Final correctness and representative semantic-action gates pass. Selected
release is `0aa2d713`, previous is `e7023a1b`, and FPGA `b61dbda8` is unchanged.
RFB-inclusive diagnostics do not qualify the internal two-frame feedback
budget; none is waived. Continuation, public documentation and physical image
provenance are updated; the source checkpoint is recorded in Git history.
