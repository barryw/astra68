# Shared native context menus

The user requests right-click context menus in the common control framework,
reusing the existing ASTRA menu interaction. Interface 5.4 and System 3.3
provide that native facility. Flat menus are the current scope.

## Contract and consumers

`AstraMenu` owns row validation, placement, hit testing, disabled/checked state,
separators, scrolling, keyboard traversal and painting. ASTRA and application
popups use that same control and the existing compositor draw-list replay.
Input/render paths allocate no memory; opening snapshots consumes bounded
resources. The retained UI dispatcher emits context requests on secondary
release, Menu key and Shift-F10, clearing prior press state without primary
activation. Pointer requests identify the most specific visible control;
keyboard requests identify the focused control.

`AstraContextMenu` resolves the existing command scope, snapshots labels and
state, and invokes only a matching still-enabled command. References and typed
arguments remain caller-owned until completion or explicit close. Stale
results cannot invoke another menu; clearing a session precedes invocation.
One popup owns input. Escape, outside clicks, loss/reset, owner/focus/geometry
changes and resource failure cancel it. Consumed presses retain ownership
through text, repeats and release. An unowned modifier release uses existing
best-effort delivery, recording loss rather than blocking the seat.

System's private window endpoint transfers AMNU v1: a counted, validated
document bounded to 1 MiB. The server copies mapped bytes before parsing;
caller writes cannot mutate an open snapshot. Correlated menu results are
mandatory, independent of subscriptions. Existing event delivery retains one
terminal cancellation under queue pressure; loss suppresses invocation.
GUI wire is 20, window event version 8, with the existing event byte size.

Desktop's DH0/application context Open and double-click share one command.
DH0 launches native Atlas with `/dh0`; no second launcher or file-operation
implementation is introduced. Public usage is in
[`interface_menu.c`](../ndk/examples/interface_menu.c) and the
[Interface Kit guide](../ndk/docs/source/interface-kit.md). All in-tree library
exports, callers, manifests and packaging use Interface 5.4/System 3.3.
Filesystem ABI 12/Kit 9/STOR 36 and Graphics 2.2/Kit 2.3 are unchanged.

## Release and qualification

Evidence root on Beast:
`/mnt/Documents/astra68/builds/context-menus-20261012/`.

Cache-corrected immutable release:
`0aa2d7130c3120df7d7b64481e5ee2b6393b603767814213b673285fd68f9b36`.
`release-5` passes actual context menus, Atlas DH0/257-entry/empty/error/input/
resize/retirement, desktop applications, common controls, four CLI/menu power
actions, all 111 Terminal commands, all 26 filesystem cases and recovery/fsck,
bit-exact SDL audio, Doom gameplay/music/effects/quit, NDK distribution checks
and all 15 native examples. ROM/image hashes remain unchanged. Host,
sanitizer, analyzer, MC68040 contracts and NDK headers/HTML/PDF pass; `host-13`
covers the final cache change.

The preceding menu release is
`e7023a1b88968b6f36e25c8203dbb7e55a4c9a632f7a982dc059b38b9b9fb3c9`.
Its `qualification-1`, `deploy-1` and `board-1` pass the same functional
contracts, preserve `/home` and `/local` contents/metadata, verify QEMU's
UID/GID 999, empty groups, zero capabilities and `NoNewPrivs=1`, and record
active services with zero restarts/no new panic. Physical input opens actual
DH0 entries and applications, selects ASTRA About through the keyboard,
retires actual processes and restores identical desktop pixels.

`deploy-2` archives the stopped predecessor disk and preserves all four user
entries including contents/metadata. Migrated desktop, Atlas and menu gates
pass before activation. Current is `0aa2d713`, previous is `e7023a1b` under the
normal retention policy. All nine QEMU threads retain UID/GID 999, empty groups,
zero effective/bounding capabilities and `NoNewPrivs=1`. Services are active,
zero restarts and no new panic. `board-2` repeats actual menu/Atlas/app/About
input, retirement and identical restored pixels. The fresh physical context
capture appears in the README; [image provenance](images/README.md) records
lossless conversion and its checksum.

## Cache and timing evidence

`board-1` measures 10,000 four-row model actions at 19,808,080 ns total:
1.980808 µs/action, below the 12.5 µs regression threshold. Interface Gallery's
existing `--benchmark` and Terminal's checked parser own that workload.

`baseline-1` records old Atlas release `5bc3bbea` at about 33 ms median for
both ASTRA open and close. Candidate samples expose a frame shift. Two
interleaved 24-pair runs per release in `pair-2` show old medians 32.900/33.000
and 32.903/32.918 ms, versus 49.409/32.874 and 33.002/49.455 ms for `e7023a1b`.
Inspection also finds unconditional bitmap-cache invalidation on each fresh
popup. Fresh control revisions alone cannot identify another popup's pixels.

The compositor extends its existing bitmap cache with one 16 KiB committed
paint snapshot. Complete rendered-content and resource-offset equality permit
reuse across lifetimes. Different labels with the same initial revision must
repaint; uncommitted paints invalidate the cache. Existing render commit
records the snapshot. Caller rows still release on dismissal; no alternate
painter, retained client pointers or builtin-only cache path is added. Host
regressions prove repeated identical menus emit zero new GPU paint commands.

Final `board-2` model timing is 19,474,640 ns for 10,000 actions:
1.947464 µs/action, below 12.5 µs. `pair-3` completes two interleaved 24-pair
runs per predecessor/current release and restores current. Open/close medians
are 33.281/33.285 and 49.476/33.077 ms for predecessor, versus 49.710/32.929 and
41.077/33.087 ms for current. These variable transport-inclusive samples do
not establish an end-to-end speedup. `nodelay-1` explicitly sets TCP_NODELAY
and measures current at 33.128/32.969 ms across 24 pairs; its different socket
configuration is diagnostic, not a matched release comparison.

`final-1` verifies the installed current release and QEMU executable after all
diagnostics, checks every thread's credentials, active services/zero restarts
and the unchanged panic hash. Source is committed on `fs-phase1`; Git history
records its identity. Generated build products remain outside source control.

These RFB-input-to-physical-scene measurements include transport and frame
phase. They do not establish the internal two-frame feedback budget. No budget
is waived; long-menu timing and broader filesystem performance remain open.
FPGA stays `b61dbda8`; no RTL, QEMU source or timing-closure change is claimed.

## Retained failures and remaining scope

The [implementation plan](superpowers/plans/2026-10-12-common-context-menus.md)
records compile/test corrections and the removed unneeded renderer refactor.
Release runners 1–3 exposed harness assumptions about demand faults, consumed
boot markers and delayed initial launch logs. `release-4` stopped on incorrect
Terminal CLI syntax; `qualification-1` completed its gates using
`--image --prepared-image`. `pair-1` stopped before RFB listened and restored
the selected release; subsequent pairs wait for the listening socket.

Submenus, application menu-strip publication, palette/shortcut consumers,
Desktop icon keyboard focus and broader Atlas file operations are deferred.
The production filesystem volumes/hotplug/mount/eject work remains the next
separate integration slice. Supported USB automount should be writable;
optical media remain read-only. Existing filesystem performance failures stay
recorded without a waiver.

The user reaffirms correctness first: functional correctness and data safety
precede further performance qualification. Shared flat-menu correctness gates
pass; two-frame feedback, long-menu timing and broad filesystem tuning remain
deferred while the production volume lifecycle is completed. This sequencing
does not change the timing targets or establish performance acceptance.
