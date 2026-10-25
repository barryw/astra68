# Initial native Atlas DH0 viewer

The user authorized the first Atlas slice against the existing mounted `/dh0`.
Double-clicking DH0 opens an ordinary native Atlas window with its root entries
as named file/folder icons. The supplied HTML was restored during implementation;
its SHA-256 is
`836e4991ea8a79ab58769471e24749a064eddd93195552a017596cd0e97cbab9`.

## Scope and shared contracts

Atlas uses native directory handles and reads until the zero-entry final batch.
It owns a growable snapshot with checked allocation arithmetic, retains names
and metadata, and closes the handle on every terminal path. A finite native
thread keeps initial enumeration off the window event loop. Loading, empty,
missing-directory, not-directory, access, capacity and loss statuses have visible
messages. Close cancels the worker and waits briefly; native process teardown
owns any worker still retiring, so its state is never freed underneath it.

Graphics library 2.2 supplies `astra_surface_from_aicon`: the existing AICON
decoder validates a strike and uploads one protected source surface. Desktop
and Atlas share it. Folder/document/volume/Atlas recipes use the existing
16/32/64-pixel format; Atlas scales the 64-pixel source to the design's 40 pixels.
Interface library 5.3 exposes retained frame/clip geometry and lets ScrollView
take keyboard focus for passive content. Wrap layout and the existing scroll
model own reflow and offsets; Atlas has no private layout or pixel backend.

Desktop verifies the actual DH0 directory before displaying its icon and uses
the native launcher argument API for `/apps/Atlas.app /dh0`. Atlas has explicit
read-only DH0 authority. The trusted bundle ceiling does not expand ordinary
application grants. Filesystem ABI 12/Kit 9/STOR 36 remains unchanged.

Atlas currently enumerates one directory. Folder/file activation, navigation,
sidebars, toolbars, views and file operations remain deferred, as requested.
DH0 is the existing always-mounted binding; this does not implement a live volume
catalog, Linux storage cutover, USB/optical hotplug, mount/unmount/eject or `/ram0`.
Those remain the next production filesystem phases in the
[approved design](superpowers/specs/2026-10-12-production-filesystem-volumes-design.md).

## Correctness fixes exposed by integration

The full Terminal gate exposed a latent process-reference validation defect.
A process handle queued through IPC owns a detached reference, but the validator
counted only handles in process tables. The existing process-open test now sends
such a handle through a real port before opening the process; it fails on the
old implementation. The shared handle query counts committed detached exports,
excluding reservations that still alias a table entry. Process validation adds
that count and keeps the corruption check. Transfer-pool validation combines
its live/reserved counts into its existing walk, removing two duplicate scans.

Clean-release filesystem qualification also exposed a gate race. The Supervisor
successfully compared the provider manifest and emitted sequence 20. Desktop
and Terminal startup could wrap the bounded trace ring before the gate inspected
it, overwriting that event or its arguments. The gate now captures the exact
bootstrap verdict before desktop startup. A regression makes the record disappear
when Terminal opens and verifies the previously captured payload still qualifies.
No production filesystem behavior or severity threshold was changed.

## Evidence

All builds, QEMU runs and physical work use Beast. Evidence root:
`/mnt/Documents/astra68/builds/atlas-dh0-20261012/`.

- `host-4`: Interface and Graphics host/sanitizer suites, Atlas model
  host/sanitizer/analyzer/MC68040 build, native bundles/contracts, image grant
  policy and NDK headers/documentation pass.
- `kernel-red-1`: queued-process-handle regression fails before the accounting fix.
- `kernel-green-1`: kernel host tests, MC68040 `all verify`, Atlas and desktop
  gates, all 111 Terminal commands, all 26 filesystem cases, quota/reset/recovery,
  HostFS death and provider `e2fsck` pass; runner exits zero.
- `guest-10`: Atlas and desktop gates pass, then the broad Terminal gate exposes
  the reference-validator defect above. This is retained failed evidence.
- `release-1`: clean immutable build and its Atlas, desktop and bit-exact SDL
  audio checks pass. The first Doom gate used the production image without its
  auto-start test fixture and timed out; corrected fixture in `release-qualify-2`
  passes gameplay, MIDI/SFX and normal quit with no faults. Filesystem bootstrap
  qualification then exposes the trace-wrap race. Immutable ROM/image hashes
  remain unchanged after these failed attempts.
- `bootstrap-diag-1`: raw records show the correct sequence-20 manifest comparison
  and its later overwrite. The unrelated Desktop event is misdecoded as a
  bootstrap cleanup failure because its private descriptor shares a catalog ID;
  it is not a Supervisor cleanup failure.
- `release-2`: immutable release
  `5bc3bbeae347c964d73971ec12cfbb8036608e0a26d503bc3dc187ddae62849e`
  passes Atlas, three desktop applications, bit-exact SDL loopwave with zero
  underruns, Doom gameplay/MIDI/SFX/normal quit with zero faults, all 26 filesystem
  cases and recovery checks, bootstrap match/mismatch/absent and provider fsck.
  ROM/image hashes remain unchanged and the runner exits zero.
- `deploy-1`: the stopped previous runtime image is archived and its remote/local
  SHA-256 matches. All four `/home`/`/local` entries retain contents and metadata
  after installation; the migrated image passes desktop and Atlas gates. The
  canonical deployment selects `5bc3bbea`, retains `36791f1e` as previous and
  verifies QEMU's UID/GID 999, no groups or capabilities and `NoNewPrivs=1`.
  Runtime, audio and remote desktop are active with zero restarts; the panic
  record's hash is unchanged. Runner exits zero.
- `board-2`: authenticated physical input double-clicks the logged DH0 target.
  Atlas enumerates the actual root: 16 entries, 15 folders and one file, status 0.
  Fresh 1920×1080 capture shows the named icons and normal window chrome. Close
  retires the process; restored desktop pixels match the initial frame exactly.
  Every PNG decodes back to its raw RGB frame byte for byte. Services and the
  panic record's hash/mtime remain unchanged; runner exits zero. Captures are
  [Atlas](images/astra-atlas.png) and [desktop](images/astra-desktop.png).
- `board-1`: the first capture asserted against an old one-second sampler record
  immediately after enumeration. Atlas remained alive and idle. `board-2` uses
  the existing fresh-sample wait, retires that known test instance, and repeats
  the entire launch/capture/close proof. No product change was required.

The Atlas guest gate uses the normal DH0 icon and native `ls` oracle on the same
boot, then tests 257 entries including UTF-8, empty/missing/not-directory paths,
wheel and focused keyboard scrolling, resize through 480/96/760 pixels, and
actual process retirement. The old 500 ms idle check conflicted with Terminal's
caret blink after close; the close-specific quiet interval is 200 ms and process
retirement remains separately required. Native enumeration/model tests exercise
allocation/read refusal and cancellation, with exactly-once directory close.
A forced guest I/O stall during close is not part of this DH0 proof; retain it
for the live-volume removal qualification.

## Retained development failures

The early guest attempts found real shared focus-state validation and wrap-extent
defects, an Atlas present/STATE feedback loop, and incorrect borrowed-surface
replacement on resize. All were corrected before the green gates above. Other
attempts failed because of wrong API names, a pre-boot root-count oracle, echoed
command markers, or unrelated debug text in CLI output. The final oracle writes
controlled native output under WORK and requires its exact completion marker.
Temporary Desktop/Atlas debug logging was removed. Evidence directories are kept;
failed attempts are not release acceptance.

Broad filesystem performance acceptance remains failed/open at the previously
recorded budgets. This viewer does not waive them. No USB or optical drive is
attached for physical qualification, and the current Linux Image lacks `sr_mod`;
optical support needs a matching rebuilt Image/modules set.
