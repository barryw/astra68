# Production volume implementation

## Haiku reference review, 2026-10-14

Before further implementation, review corresponding code in `~/Git/haiku`.
The read-only reference checkout is commit
`e9ade57f61b587a9c6599cf4effadaa37ad7822f`; its contribution policy prohibits
AI-generated changes. Astra's Linux-owned provider and native capability
contracts remain authoritative; the reference supplies proven responsibilities
and lifecycle ordering, not another backend.

- `src/servers/mount/AutoMounter.cpp`, `AutoMounter` constructor and
  `ReadyToRun` (372–407): subscribe before the initial scan. Astra's discovery
  activates its processed udev monitor before enumerating disks/partitions,
  then consumes queued events; native observers subscribe before snapshots.
- The same file, `MountVisitor::Visit` (154–213) and `B_DEVICE_UPDATE`
  handling (449–460): keep device-scoped discovery and mount policy separate.
  Astra likewise retains device/medium/source identities and mounts supported
  removable sources through one provider policy. A manual unmount stays
  unmounted until a new observation or explicit request; labels do not choose
  the system source.
- `AutoMounter::_UnmountAndEjectVolume` (675–750) and
  `src/kits/storage/disk_device/Partition.cpp`, `BPartition::Unmount`
  (632–665): unmount before eject, reject eject while mounts remain, and update
  objects after successful removal. Astra keeps quiescing/failed resources
  visible until actual retirement and refuses eject with retained mounted
  children or pending cleanup. It preserves the requested safe-removal policy
  instead of exposing Haiku's optional force-unmount action.
- `src/kits/storage/VolumeRoster.cpp`, `StartWatching`/`StopWatching`
  (111–151), and `src/servers/launch/VolumeWatcher.cpp`, `MessageReceived`
  (82–113): notifications belong to the canonical mounted-volume roster.
  Astra's Desktop, Atlas, CLI and process namespaces consume the same coherent
  native catalog; no UI-only device registry or second mount implementation.
- `src/system/kernel/fs/fd.cpp`, `put_fd`/`get_fd_locked` (182/283), and
  `vfs.cpp`, `vfs_new_io_context` (5026): descriptors and CWD hold object
  references independently of namespace spelling. Astra retains native
  descriptions and adopts fork receipts through the same client facility;
  renamed/unlinked paths never reopen inherited objects.
- `vfs.cpp`, `fs_unmount` (7888; reference fence at 7957 and unmounting state
  at 8026): admission and busy inspection share a synchronization boundary.
  Astra's QEMU namespace lock serializes root/session admission with quiescing
  and busy inspection, and provider roots require MOUNTED state.
- `vfs.cpp`, `common_sync` (6556), and `node_monitor.cpp`,
  `NotifyMount`/`NotifyUnmount` (858/830): propagate per-object sync errors and
  notify after visible mutation. Astra retains those rules, complete snapshot
  reconciliation and failed-cleanup accounting. Haiku's separate `fs_sync`
  ignores some flush results; that behavior is not adopted.
- `src/kits/tracker/Tracker.cpp`, unmount handling (529), and
  `PoseView.cpp`, `CanUnmountSelection` (8421): validate selection and delegate
  removal to its owner. Haiku's mount-server alert is generic and offers force;
  Astra instead preserves the user's required safe refusal and exact holder
  list. `src/bin/fdinfo.cpp`, `get_next_fd_info` (50), includes CWD as well as
  descriptors; Astra's actual lifecycle gate must cover CWD-only busy removal.
- `src/servers/app/Window.cpp`, `SupportsFront` (1405), and
  `Desktop.cpp`, `SetFocusWindow`/`KeyboardEventTarget` (2069/2040): desktop
  stacking and keyboard focus are distinct; popup keyboard ownership has
  priority. Astra derives keyboard targets from existing window state and
  retains its existing popup ownership. Haiku's owner-scoped modal focus
  restrictions exposed Astra's unused MODAL flag; the common display fix must
  enforce that flag for the owning GUI session without preventing another
  application from closing files.
- `src/kits/interface/PopUpMenu.cpp`, `_Go`/`_StartTrack` (328/422): popup
  tracking is asynchronous. The native UI gate waits for successful menu
  admission and drawing before keyboard selection; consumed device input is
  not evidence that the menu is ready.
- `src/kits/interface/Alert.cpp`, `BAlert::_Init` (497–503): use the
  canonical plain font for measurement and wrapped rendering. Astra's busy
  dialog now uses `ASTRA_THEME_SYSTEM_BODY_FONT_HEIGHT` consistently; its
  previous 14-pixel request had no installed strike and stopped layout before
  painting. The existing layout test exercises real font metrics and UTF-8
  wrapping rather than mocking font fitting.
- `src/kits/storage/Node.cpp`, attribute read/info/iteration (263–309),
  and `SymLink.cpp`, `ReadLink` (97): inspect attribute data and link targets
  without resolving the target path. The read-only state-preservation verifier
  must compare full binary attribute values and exact link bytes. Haiku's
  `ReadAttrString` (372) accepts nonnegative short reads; Astra's observer must
  require the complete declared value instead. Canonical image installation
  remains separate from observation, and recovery never modifies the archived
  predecessor image.

## Native metadata checkpoint, 2026-10-13

HostFS now publishes a metadata-only VOLUMES port. Filesystem 13.1 / Kit 10.1
provide coherent native catalog pages and coalesced subscriptions with
kernel-stamped ownership, configured owner limits and actual endpoint cleanup.
The existing host catalog adapter and public client share one validator.
Terminal forwards VOLUMES to commands, and `volumes [--all]` uses that API.
Host/sanitizer, MC68040, real boot/CLI, all 27 filesystem semantics, finite
resource recovery, flood, provider reset, HostFS restart, independent clean fsck
and NDK headers/HTML/PDF pass. Evidence is retained in
`production-volumes-20261012/native-volume-catalog-1`.

This completes public metadata only. Data-grant policy, live namespace,
mount/unmount/eject/ISO control, usage/lsof, desktop/Atlas, long names/paths and
production migration remain required. Final UTF-8/graph validation and migrated
standalone launcher checks are under qualification. Network work stays excluded.

The user authorizes all remaining production filesystem work, with correctness
and data safety before performance qualification. The governing contract is
[the volume design](../specs/2026-10-12-production-filesystem-volumes-design.md)
and [the production PRD](../../FILESYSTEM_PRODUCTION_PRD.md).

The latest instruction explicitly excludes network mount work. Finish fixed,
removable, optical, ISO, native namespace/control/usage, UI and tooling first.

## Removal behavior

Removable mounted desktop volumes offer **Unmount** through the common context
menu and existing command framework. This requests safe unmount of the selected
mount ID and generation. It does not force-close applications or lazy-unmount.
The native transaction quiesces new opens, checks retained file, directory,
current-directory, shared-description and in-flight references, drains accepted
work, syncs writable storage and performs a real Linux unmount. Only successful
unmount or confirmed loss removes the icon.

Busy results carry a coherent, paginated usage snapshot: authenticated process
identity, resource kind, canonical volume identity, retained object identity,
current or last known volume-relative pathname, access mode and pending operation
where applicable. Supervisor resolves names from its process records. Renamed
or unlinked objects retain their identity and are marked accurately. The dialog
lists actual users and objects, including directories used as cwd and pending
I/O; it never substitutes a generic refusal for available details. More rows
remain reachable rather than being silently truncated. Retry takes a fresh
snapshot. Busy or failed sync/unmount restores usable state only while the
original mount remains intact.

The same native usage query also backs the requested `lsof` command. Process
and volume filters, pagination, ownership checks and object interpretation live
in the shared facility; the command and dialog only format its results.

ISO files are also mount sources. Image control accepts a retained native file
and its rooted authority, never a host path. Verify exact readable object and
format, refuse conflicting writers, retain its backing mount dependency and
create a read-only loop-backed ISO9660/UDF mount. Publish it through the same
optical-class `/cdN` namespace, desktop icon and Atlas path. Safe removal and
`lsof` expose image backing files and dependent mounts; source loss invalidates
children. Qualify rename/unlink, concurrent write refusal, stale file tokens,
nested images, backing-device loss and complete loop/descriptor cleanup.

## Existing mechanisms and required new responsibility

- QEMU already owns canonical files, shared descriptions, sessions and requests;
  usage queries derive from these objects instead of maintaining another count.
- HostFS already owns rooted grants, direct scopes and process cleanup. Extend
  its authority to all mounted volumes and grant lifecycle.
- Supervisor owns process identity, launch policy and lifecycle requests. Extend
  that authority for publication and mount-control policy.
- VFS process/assign state and eager-loader bootstrap subset own routing. Extend
  them for sequence-checked live publication; native and POSIX clients share it.
- Interface owns alerts, menus, retained scrolling and command invocation.
  Desktop consumes native catalog/control results; no private menu or mount API.
- Linux privileged mount/discovery control has no existing implementation.
  A confined provider is necessary because qualified QEMU has no root privilege,
  groups or capabilities. It owns actual sources/mounts and uses shipped
  libudev, libblkid and libmount; QEMU retains existing file I/O.
- Preserved UID/GID-0 data includes mode-0600 files. The provider must perform
  rooted opens and permission-sensitive path/metadata mutations under native
  authority and lend descriptors internally to unprivileged QEMU. Preserve
  ownership/modes; reuse the existing resolver rather than adding another path
  engine. Test privilege separation with the preserved-data profile.

## Coordinated milestones

1. **Provider foundation.** Versioned authenticated internal control, separate
   device/medium/source/mount records, generation checks, bounded inventory and
   operation results, private root-owned mount namespace, processed udev before
   enumeration, bounded probe workers, stale observation rejection. Test raw
   request refusal, source replacement, duplicate identity and rollback.
2. **Linux storage cutover.** Select exact trusted system image/partition; read
   first bootstrap bytes through existing HostFS backend. Replace production
   guest ext4 and RAMFS with Linux-owned `/dh0`, bounded `/ram0` and `/tmp`.
   Preserve approved exports, data and metadata; remove second writable owner.
3. **Live native namespace and control.** Versioned public catalog/subscription,
   sequence validation, current root enumeration and late initialization,
   policy-controlled grants, native inventory/mount/unmount/safe-remove/eject
   tools, coherent usage snapshots, stale operation and busy/refusal semantics.
   Update startup, eager loader, fork/exec and all NDK/POSIX consumers together.
4. **Removable lifecycle.** Manual controls before automount; supported USB
   writable by default, optical read-only. Qualify detach/reinsert, multi-source
   device removal, provider loss/reconciliation, cancellation, flush refusal,
   namespace withdrawal and stale retained objects.
5. **Optical platform.** Rebuild exact Linux Image and every matching module
   with CD-ROM support; package and retain rollback. Qualify empty drive,
   ISO9660/Joliet/Rock Ridge/UDF, disc change and truthful eject results.
6. **Desktop and Atlas.** Live labeled/fallback mount icons keyed by identity;
   exact root launch; Unmount command and detailed scrollable busy dialog.
   Atlas remains root icon viewer; loss retires enumeration and old results.
7. **Whole-stack correctness.** Host/sanitizers/analyzer, exact immutable guest,
   all current filesystem/stress/confinement/Terminal/app/audio/power/NDK gates,
   disposable Linux-owned durability oracle, finite resource recovery and
   physical DE25 input/storage captures. Remove obsolete production integration
   and update every owning document.

Each milestone extends existing ownership rather than adding another data path.
ABI changes reach headers, producers, consumers, exports, manifests, fixtures,
packaged documentation and examples in the same change. No new kernel or FPGA
filesystem engine is required.

## Execution and evidence

Builds, QEMU and hardware work run through Beast under its shared build lock.
Use exact checksum/delete source mirrors excluding all `build/` directories.
Retain live session IDs through completion; confirm process and artifact state
before replacement. Evidence goes under
`/mnt/Documents/astra68/builds/production-volumes-20261012/`.

Do not activate incomplete storage migration on the production board. Validate
copied data and archive stopped predecessor state before selecting a coherent
release. Rollback first stops and unmounts the new writer.

Current physical USB/optical qualification lacks attached devices. Image tests
can prove format and namespace behavior, but cannot qualify actual hotplug,
loss or eject. Record that limit until physical devices are available.

Two-frame UI timing, long-menu timing and broad filesystem performance remain
deferred. Existing failed budgets stay visible and unchanged. Preserve relevant
target baselines before changing shared hot paths; correctness does not imply
performance acceptance.

## In-progress foundation checkpoints

Internal catalog, bounded probe workers and the isolated Linux mount seam are
implemented, with tests on Beast and the pinned AArch64 sysroot. They are not
yet connected to an authenticated provider daemon or Astra's live namespace.

`catalog-1`, `foundation-1`, `probe-images-1`, and `mount-images-1` through
`mount-images-5` retain source hashes and verdicts. `board-images-1` through
`board-images-3` use disposable ISO/UDF fixtures in private namespaces on DE25.
Final mount checks cover Rock Ridge, pure Joliet and UDF, UTF-8 contents,
UID/GID-999 reads, read-only/nodev/nosuid/noexec restrictions, busy restoration,
and actual loop-detach confirmation. Holding another loop opener delays terminal
cleanup and keeps resources charged. Root-owned mode-0600 descriptor lending
preserves owner/mode and demonstrates the required cutover permission boundary.

`provider-core-2` extends these primitives with the actual private control/path
transport and Linux resource owner. Credentials, provider epochs, generations,
descriptor counts and record shapes are checked at the boundary. Private bind
mounts give every approved export its own mount ID; descriptor validation refuses
the same directory inode reached through the original host mount. This prevents
retained-descriptor mutations from escaping an export while preserving ownership
and modes. The original ordered resolver performs opens and path mutations.

Source admission charges descriptors while queued and bounds active probe
children; cancelled work stays charged until reaped. Explicit image windows are
probed at their configured offset/length. Image admission uses real Linux read
leases to refuse existing writers; lease breaks withdraw source and descendant
mount authority. Native opens of admitted image objects are refused for writing.
Temporary volumes enforce byte/inode and aggregate limits. Real unmount, delayed
loop detach and failed-mount cleanup preserve resource charges until completion.

Clean host/sanitizer/pinned-AArch64 gates and native DE25 execution pass in
`provider-core-2`, including all of the above, catalog paging, rooted descriptor
lending, busy restoration and resource return. Source hashes are unchanged across
the build; transferred binary/image hashes and selected board services/releases
match. The earlier runner-only `provider-core-1` failed because source mirrors
intentionally omit `.git`; the corrected runner takes the verified local base
commit explicitly. Earlier development assertions caught missing delayed-loop
completion and binding retirement; those contracts are now covered by tests.

This remains a core/library and exercised private transport. No daemon launcher,
udev integration, QEMU bridge, public namespace or application feature has been
delivered. Do not publish it as native filesystem completion.

`provider-async-1` advances the runtime substrate: loop/backing/target preparation
remains in the catalog owner, while bounded forked workers perform Linux attach
and detach. Workers close every unrelated inherited descriptor. Resources stay
charged through actual `waitpid`; protected-target reconciliation establishes
whether Linux committed, even when the acknowledgement was lost. Source loss
invalidates and cancels mounting work without discarding its cleanup. Failed
unmount can restore `Mounted` only with confirmed root authority. Catalog and
root requests remain available while accepted mutations are in flight.

The inherited-endpoint service loop authenticates the peer, watches its pidfd,
wakes for SIGCHLD/SIGIO, and uses actual probe deadlines. Cancelled children
blocked in kernel I/O remain charged without a zero-timeout polling loop. Fresh
host/sanitizer/pinned-AArch64 and native DE25 tests pass, including mounting loss,
lost acknowledgement after real attach, busy restoration and final resource
return. Source and board transfer hashes match; selected production services and
release are unchanged. Provider binary SHA-256:
`382f90b85d1e4a9184d92dd1f804d7fd894bfee283ada3ceb96bdb83e1d9d2a8`.

The requested launcher/discovery/QEMU/native namespace/UI/tools integration
remains required. This checkpoint has not delivered that final state.

Remaining: discovery/control daemon, QEMU integration,
live native namespace and usage query, `lsof`/mount controls, bootstrap/RAM
cutover, desktop/Atlas integration, complete matching CD-ROM kernel/modules and
whole-stack release qualification. No native feature or production migration is
claimed complete by these primitive tests.


## Launcher and private QEMU bridge checkpoint

`provider-launcher-3` passes fresh host/sanitizer/pinned-AArch64 and native DE25
execution with actual QEMU UID/GID 999, groups/caps zero and no-new-privileges.
Exact system image/window/UUID, exclusive lease/loop-retained flock, private
exports and bounded ram0/tmp are provisioned by the root launcher. Canonical
QEMU descriptions/data I/O remain; rooted paths and sensitive metadata use the
existing resolver in bounded provider workers. Blocked FIFO catalog response,
loss cancellation/stale lending, actual resource return, root0700/0600 data,
real shutdown/fsck/persistence and twelve provider resets pass. Source/transfer
hashes and production state match. Evidence retains earlier failures including
failed-lease cleanup, mkdir trailing slash, private-mount inode observation,
claim-race scheduling assumption and executable fixture ancestry.

Claim races now establish a real issued session before each concurrent release,
require issued identities and racing refusals, and verify every issued identity
retires. Slow I/O may correctly let release win all racing opens. This replaces
a timing assumption with direct identity/retirement evidence.

Next: versioned dynamic native catalog/control/usage and Linux-first bootstrap/
HostFS, updating every launch/test/release/startup/eager/fork/exec/NDK/POSIX
consumer together. QEMU source removes ASTRA_HOSTFS_ROOT; do not publish it with
the old launcher. Distinguish whole-mount authority from retained directory/cwd
objects so housekeeping roots cannot make removable volumes perpetually busy.
Derive usage from canonical descriptions/shares/sessions/jobs and explicit image
dependencies. Processed discovery, native namespace/UI/tools, full CD-ROM Image/
matching modules and whole release gates remain. Production is unchanged.

## Native catalog/identity and optical build checkpoint

`native-volume-abi-3` qualifies Host 2 / command record 4, the shared native
catalog projection and exact epoch/ID/generation root references on Beast and
actual DE25 UID/GID-999 QEMU. STOR 37, Filesystem ABI 13 / Kit 10, runtime 1.17
and every in-tree C library/header/manifest consumer change together. Production
launch/bootstrap/live namespace consumers still need the coordinated cutover;
this checkpoint does not claim a booted filesystem migration.

Forty whole-mount grants retain zero Linux descriptors. Exact directory opens
retain descriptors and close on revocation; disconnected native identities stay
charged until their holder closes them. Metadata carries native 64-bit identity,
not Linux device numbers or private mount IDs. Unknown/stale identities,
obsolete numeric selectors and malformed root payloads fail closed. Session IDs
refuse exhaustion instead of recycling.

Full protocol oracles now run through the real provider: scopes, descriptions,
directory paths, claim races, twelve resets, single-worker scope revocation/
orphaned opens, owner/descriptor/directory budgets, system read-only bootstrap
read/claim handover, root0700/0600 data, fsck and actual cleanup. C/MC68040,
sanitizers, kernel host/target, ownership boundaries, installed NDK headers and
HTML/PDF gates pass. Source and transferred artifact hashes match and board
release/services remain unchanged.

Retained failures exposed a malformed root request reporting success with no
session, obsolete Linux-device assertions, native tombstone-vs-descriptor
accounting assumptions and nested sudo discarding configured test budgets.
The ownership guard also confused Menu's own private fields with Filesystem's;
typed Menu receivers are now recognized with negative filesystem cases retained.
Graphics builds its own host/sanitizer surface objects instead of another owner
compiling Graphics source.

Complete optical kernel build 2 uses exact clean vendor source
`d7d192a9ddd955fe206aa350afed8f82e368a74c` and a separate output tree/release
`6.12.11-astra-storage1-gd7d192a9ddd9`. Full Image, 1344 matching standard/Astra
modules and DTBs pass source/vermagic/checksum verification. First build's
Astra module failure revealed the fixed relative header path; the recipe now
takes matching source headers explicitly. Image SHA-256 is
`f8b42ca62f93ba7d3d83caeb001cefdcf1d5815fc5d3f715b687d56151b81890`.
No kernel installation or activation occurred. Physical boot, graphics, storage,
HDMI and optical gates remain required.

Next required work stays Linux-first Supervisor/HostFS bootstrap and settings,
all mounted volumes and bounded RAM/tmp, deletion of production guest writers,
processed discovery, live native namespace/control/usage, lsof, desktop/Atlas and
whole immutable release qualification. Do not deploy Host 2 with old launchers.


## Linux-first bootstrap checkpoint

`linux-first-1` qualifies the actual Supervisor/HostFS boot path on Beast.
Supervisor reads only through a read-only canonical provider session and
releases the claim before static HostFS starts. HostFS publishes the Linux-owned
system volume, private WORK/SOUND exports and bounded RAM0/TMP through one
allocated per-volume owner. Production guest Storage and RAMFS services and
initial kernel block write grants are removed from source.

Two actual full boots pass desktop, Terminal, zsh, dh0 reads, HOME/RAM0/TMP/WORK
writes and ps. Native shutdown returns 88, independent e2fsck is clean, and
image/external-export byte inspection proves persistence. QEMU runs with explicit
non-root UID/GID, no groups/caps and no-new-privileges; root observer inspects
preserved root-owned logs without metadata changes. Fresh host/sanitizer,
MC68040, kernel, ownership and image-contract checks pass.

Retained failed boots exposed duplicated namespace prefixes after narrowing,
oversized libc reads, the unaccelerated public connector, empty config roots,
and exhaustion of the channel's finite scope binding cache. The existing
namespace/grant, transport-layout and channel-open mechanisms are consolidated
and extended; no second data engine or larger compiled ceiling is introduced.
Channel rotation retires bindings while preserving sessions/descriptions;
forgotten numeric handles remain cache tombstones until that rotation.

ROM SHA-256: `b7b79fb61a31dc240858c2e817a07a2c8f41eb2923cc8313b625b144d3e52d4f`.
Image SHA-256: `7dfba8a2c256e1260ab1f4c18063b1c4863d23f5b33cae1969d4f581f514e3b4`.
Evidence includes source manifest, artifacts and all development logs under
`/mnt/Documents/astra68/builds/production-volumes-20261012/linux-first-1/`.
This is a bootstrap slice, not whole production migration acceptance.

Next: migrate production/test launchers, processed udev, live native namespace,
control/usage/lsof, desktop/Atlas removable lifecycle, long names/paths and
complete immutable release qualification. Production remains unchanged; no
activation or optical-kernel installation occurred. Network mounting requirements
are recorded in [the assessment](../../NETWORK_FILESYSTEM_REQUIREMENTS.md),
with local/removable correctness still taking precedence.
