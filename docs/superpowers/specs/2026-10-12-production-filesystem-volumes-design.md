# Production filesystem, live volumes, and Atlas

Date: 2026-10-12

Status: proposed written design; user review pending. Product code, runtime,
kernel, and deployed data are unchanged at this checkpoint.

## Requested outcome

Finish the production filesystem described by
[the production PRD](../../FILESYSTEM_PRODUCTION_PRD.md), including Linux-owned
system and RAM storage, additional fixed and removable volumes, optical media,
mount/unmount/safe-remove/eject, and live desktop volume objects. Preserve
current files and qualified application behavior. Complete the native facility
and its POSIX adapters together; do not introduce application-specific storage
backends or retain guest production engines as fallbacks.

The user explicitly requests writable automatic mounts for supported USB
filesystems and authorizes the kernel work required for CD-ROM support. Mounted
volumes appear on the desktop, display their volume label when present, open
Atlas at their root when double-clicked, and disappear after successful unmount
or media loss. Attaching an empty optical drive exposes a device in inventory;
inserting a supported data disc creates the mounted volume and desktop icon.

This document refines the existing PRD. It changes the canonical RAM volume
name to `/ram0`, makes writable USB mounting the shipped trusted-policy default,
and fixes the desktop-to-Atlas behavior. The existing authority, identity,
durability, revocation, and performance requirements remain binding.

## Current implementation and measured constraints

Source checkpoint: `f073478dc1091488e4811fc25c344eacb572e592` on `fs-phase1`.
Selected DE25 release:
`36791f1e24e86a94856446a7389392127f6e6db5fe831d5bd8d3cc88cc97f175`.
The qualified Phase 1 contract is Filesystem ABI 12, Filesystem.kit 9,
STOR 36, Host 0x00010015. Its correctness evidence and failed performance
acceptance remain in [Current State](../../CURRENT_STATE.md) and
[the filesystem handover](../../HANDOVER_2026-10-10_FILESYSTEM.md).

Existing mechanisms to extend:

- `sw/userspace/supervisor/src/volume.c` selects and temporarily mounts guest
  lwext4; the storage service independently mounts it afterward. Both production
  owners must be replaced by the verified Linux provider selection.
- `sw/userspace/supervisor/src/bootstrap_host.c` already reads through the
  canonical HostFS backend before services exist. It currently probes a
  manifest rather than supplying the actual bootstrap loader source.
- HostFS grants, kernel-bound scopes, per-thread direct channels, retained
  descriptions, and `astra68-resolve.c` already enforce the Phase 1 contract.
- `vfs_host.c`, `vfs_process.c`, and the Filesystem Kit currently route through
  startup-seeded assigns. They need a live public catalog while preserving
  private bindings and retained cwd/file objects.
- Desktop currently enumerates application bundles and waits on window events.
  It has no mounted-volume collection or storage subscription.
- Atlas is marked pending in the Interface implementation ledger; no Atlas
  application implementation exists. The launcher already supports validated
  arguments through `astra_application_launch_with_arguments`.
- Collection controls required by Atlas remain unfinished. Extend the public
  Interface Kit, following its retained layout and command contracts, before
  Atlas consumes those controls. Do not put private list/icon painters in Atlas.

Read-only DE25 inspection through Beast on 2026-10-12 found:

- Linux `6.12.11-gd7d192a9ddd9`, one internal SD device, and no attached USB
  storage or optical drive. The Linux root partition UUID is
  `ff27d0ad-7439-445a-97a4-701971c23fa3`; it is the host backing store, not
  automatically the Astra `/dh0` filesystem.
- Existing Astra storage is `state/storage-terminal.img`, passed to QEMU as a
  raw drive. The Linux-owned cutover must mount the exact selected image
  partition instead of treating the Linux root as Astra system storage.
- exFAT, ISO9660, UDF, and UAS modules are installed under `updates/astra-storage`.
  `modprobe -n -q sr_mod` fails. The recorded vendor configuration disables
  `CONFIG_BLK_DEV_SR`; enabling CD-ROM changes `struct gendisk` and requires a
  matching kernel Image and module set, not an extra module on the old Image.
- Board libraries are libblkid/libmount `2.37.2-4ubuntu3` and libudev
  `249.11-0ubuntu3.11`. The pinned Jammy sysroot contains their development
  interfaces; Beast's newer native libraries are not the DE25 build authority.
- Host MemTotal is 957,380 KiB; sampled MemAvailable is 133,668 KiB. Swap is
  absent. These are observations, not an allocation budget. RAM volume limits
  must be charged against a measured platform/recovery reserve.

## Architecture and selected approach

Keep the existing Astra-native filesystem contract, namespace resolver,
rooted grants, direct file-I/O lanes, and Linux filesystem semantics. Add the
missing storage lifecycle around them. Linux owns actual sources, mounts,
filesystem formats, recovery, cache, and journal; Supervisor owns Astra policy,
names, publication, and grants. Desktop, Atlas, CLI tools, and POSIX adapters
consume the same native records.

QEMU remains UID/GID 999, without groups or capabilities, with NoNewPrivs set.
Dynamic mount/probe/eject operations require privilege unavailable to that
process. This is the demonstrated need for a confined Linux storage provider
process. Its privileged control component owns discovery, source validation,
mount state, and required Linux operations. QEMU keeps the existing file-object
implementation and data workers, receiving mounted roots through the internal
provider connection. Neither process exposes a Linux API to Astra applications.

The provider and QEMU share one runtime-private mount namespace, established by
the privileged launcher before QEMU drops privilege. Mount propagation is
private. Provider mount directories and control endpoints are root-owned and
unmodifiable by QEMU. QEMU cannot mount, enter other namespaces, open arbitrary
block devices, or choose host source paths. Its authenticated connection accepts
typed operations naming exact provider-owned records and generations. Host paths,
shell commands, arbitrary mount options, and client-supplied owner identities
are never accepted as authority. Root descriptors may be transferred over the
authenticated Linux connection; they are not guest capabilities.

Keep actual device/source/mount state authoritative in the Linux provider.
QEMU caches acknowledged records and maintains its existing canonical live
description/request references; Supervisor publishes bindings to those records.
Do not introduce independent guessed mount tables or duplicate description
accounting. Provider loss advances the epoch and invalidates QEMU sessions;
reconciliation precedes new publication. An uncertain mutation is not replayed.

Alternatives considered: adding mount privilege to QEMU would break the current
qualified trust boundary; using host automounters and watching mount directories
would create a second authority with incomplete lifetime semantics. The selected
provider split preserves the existing data path and isolates the privilege that
the remaining production work requires.

## Names, labels, and identity

| Kind | Canonical Astra path | Desktop fallback | Backing |
|---|---|---|---|
| Selected system volume | `/dh0` | `DH0` | Verified Linux-owned ext4 |
| Additional fixed volumes | `/dh1`, `/dh2`, ... | `DH1`, `DH2`, ... | Linux ext4 |
| USB data volumes | `/usb0`, `/usb1`, ... | `USB0`, `USB1`, ... | ext4, FAT32, exFAT |
| Optical data volumes | `/cd0`, `/cd1`, ... | `CD0`, `CD1`, ... | ISO9660, UDF |
| RAM volumes | `/ram0`, `/ram1`, ... | `RAM0`, `RAM1`, ... | Bounded tmpfs |

The `x` in `/dhx` or `/cdx` denotes numbered instances, not a literal name.
Numbers count volumes; two partitions on one USB device can publish two USB
volumes. Reserve `/dh0` for the verified system selection. Automatically assigned
names are not recycled during an Astra boot; a later insertion can receive
`/cd1` or `/usb1` even after the first volume disappeared. Persistently configured
names may be rebound only after the old binding is withdrawn and its generation
is retired. Old handles and pending UI actions cannot target the replacement.

Display the nonempty validated label, otherwise the uppercase fallback name.
Duplicate labels remain distinct objects and show their canonical name as
secondary identification. Labels never choose paths, grants, source selection,
launch arguments, or mount options. Metadata changes update the displayed label
without replacing mount identity. Use stable mount ID plus generation as the
desktop key, not label, icon position, or provider descriptor number.

`/system`, `/home`, `/apps`, `/commands`, `/libs`, `/local`, `/config`, `/work`,
and `/tmp` remain native directory bindings with their defined grants.
Aliases to one mount do not create extra desktop disks or duplicate `df` rows.
Private/service export roots such as SoundFonts remain rooted bindings, not
fabricated block devices. Preserve existing WORK/SOUND data and audio-host
access through the typed provider contract.

Canonical RAM naming changes every in-tree producer, client, manifest, fixture,
test, and document together. Remove the obsolete `/ram` spelling; no fallback
alias remains. `/tmp` is a separate bounded volatile mount exposed as a directory
binding, not an additional user-facing RAM disk icon.

## Bootstrap and system-storage cutover

Trusted release configuration outside the selected filesystem identifies the
internal backing store and exact Astra system image/partition. Validate backing
device identity, opened image identity and size, partition window, filesystem
type/UUID, and provider epoch. Never scan for the first matching label or Linux
partition. Missing, ambiguous, changed, or corrupt selection stops normal boot.

Linux mounts and recovers that exact ext4 filesystem once. Supervisor reads
the manifest and executable sources through the existing canonical host backend
from the first bootstrap read onward. Reuse the bootstrap reader/source path
when HostFS assumes managed authority; remove the comparison-only probe and
temporary guest ext4 mount after the real path passes its gates. Bind `/dh0`
and `/system` to the same verified mounted object.

The launcher must not give QEMU a writable raw drive for a Linux-mounted system
image. Remove the guest production raw-storage owner and duplicate partition
selector in the same cutover. Linux's host root remains outside guest authority;
approved exports grant only their configured directory roots. Qualification
uses disposable real Linux-mounted images, with QEMU always unprivileged.

Before physical migration, stop the runtime, archive its mutable image and
approved exports to the durable evidence store, record content and metadata
inventories, and verify checksums. Qualify a copied dataset before selecting the
new release. Never mount the same image through both writers. Preserve names,
bytes, modes, links, and supported timestamps; restart retires old process
capabilities rather than pretending they survive a provider epoch change.
Rollback requires stopping and unmounting the new owner before selecting the
old release and its retained pre-cutover data snapshot. Do not run the old guest
writer against a live Linux mount.

## Live native catalog and access

Extend the existing versioned host/storage and Filesystem Kit contracts with
provider/device/medium/source/mount/binding/operation records, bounded paginated
snapshots, sequence-based subscriptions/rescan, and typed control operations.
Use the PRD's record separation. Optical drive attachment and disc insertion
have independent generations. Unformatted, unknown, ambiguous, empty, externally
used, and failed sources remain inspectable without becoming usable volumes.

Every process receives a native namespace session through normal startup,
including the eager-loader bootstrap subset and processes whose first filesystem
call occurs after insertion. Public mount metadata and sequence are shared
read-only; private bindings and file grants remain process-specific. A routing
cache validates the sequence and refreshes through the existing namespace
facility when it changes. Root enumeration and fresh path operations observe
committed publications without application restart. An enumeration already in
progress may finish its coherent snapshot; rewind obtains the current catalog.

Eligible sessions acquire rooted authority under Supervisor policy. Catalog
visibility alone grants no data or control rights. New media cannot broaden a
restricted application's grant. Retained file/DIR/cwd operations use existing
generation-safe tokens and do not gain a catalog IPC round trip on each I/O.
Notifications use existing native ports/waits; overflow forces a rescan. Desktop
events cannot be the only mechanism keeping the native namespace current.

Incompatible wire/layout/startup/library changes advance affected ABI majors,
update all consumers and packaged NDK contracts together, and delete superseded
formats. Include eager ELF loading, POSIX fork/exec, service manifests, host
audio/entropy/metrics/remote consumers of shared headers, and test image tools.
Exact record layouts and version increments belong to the reviewed implementation
plan and ABI probes; never repurpose unrelated fields.

## Mount, removal, and automatic policy

Use one lifecycle: discovered/unmounted, mounting, mounted, quiescing, then
unmounted or lost/failed. An accepted control operation has an owner, correlation
ID, exact source/mount generations, bounded status/progress, cancellation, and
one terminal outcome. Reuse current request/lifecycle machinery.

Manual mount and automatic policy invoke the same transaction: validate source,
reserve name, mount/recover through Linux, retain and verify root/mount identity,
establish rooted grants, atomically publish the binding, then notify clients.
Failure before publication rolls back. Failed rollback retains an inspectable
record and its charges; it never reports mount success or abandons a Linux mount.
Explicit directory mountpoints must be empty. Source generation is rechecked
after asynchronous probe/mount work, so removal cannot publish a stale result.

Ship trusted policy enabling writable automount for supported ext4/FAT32/exFAT
USB data sources. A requested writable mount that fails remains unmounted and
shows its reason; the user may explicitly request a read-only mount. Read-only
rules and optical mounts cannot authorize recovery writes implicitly. Unsupported
formats, duplicate UUIDs, corrupt media, and conflicts are reported; no automatic
formatting or repair occurs. Mount data media with nodev/nosuid/noexec restrictions;
Astra reads guest executables through its loader rather than executing them in
Linux. Media insertion never autoruns code.

Start processed udev subscription before enumeration, merge queued observations,
probe with shipped libblkid APIs in bounded workers, and reject old-generation
results. Use shipped libmount APIs and typed allowlisted options for mounting.
Detection proposes changes; Supervisor evaluates policy and publishes them.
Enable automount only after manual mount and safe removal pass. If a particular
optical device lacks reliable media-change events, use bounded Linux-side status
checks with explicit qualification; never guest/desktop busy polling.

Plain unmount with live file/DIR/cwd/in-flight references returns busy and names
owners while leaving the mounted volume usable. Catalog visibility is not a busy
reference. Safe removal quiesces new opens, coordinates client release, drains
accepted work, syncs writable storage, closes provider references, and performs
a real Linux unmount before withdrawing the final binding and reporting success.
Flush/unmount refusal restores usable mounted state only when authority and
mount state remain intact. No lazy unmount substitutes for normal success.

Device safe removal checks all partitions, including externally used mounts.
Eject follows successful unmount and reports hardware failure truthfully.
Ordinary runtime removal of the selected system volume is refused. Surprise
removal immediately withdraws new access, advances generations, reports loss to
pending and retained objects, and leaves close/cleanup possible. No stale object
silently reconnects to a new disc or drive.

## RAM, durability, and resources

Create `/ram0` and `/tmp` with explicit nonzero tmpfs byte/inode limits and
noswap enforcement. Additional RAM volumes require configured limits charged
against the same aggregate host budget. Linux 6.12 documents tmpfs `noswap`;
verify it on the exact shipped kernel before accepting the release. Refuse
missing/invalid limits and preserve a measured margin for Linux metadata,
providers, graphics/audio, and recovery. Capacity queries report enforced limits
and current availability. Never use default half-RAM sizing or unbounded ramfs.
RAM volumes report their volatile nature in the native metadata and Atlas;
successful unmount destroys their contents. Do not report a persistence promise
for RAM writes or restore vanished contents after remount.

Sync and atomic-save behavior retain the native durability contract. Shutdown
stops clients, drains work, syncs all writable Astra volumes, and unmounts those
whose lifecycle permits it before filesystem READY. Provider restart reconciles
real mount state before republishing. Guest-only restart re-registers native
sessions without creating another writable owner. Unknown mutation completion
returns the existing precise uncertain-I/O contract rather than replaying writes.

Adapt the existing power-cut oracle to Linux-owned storage; guest termination
alone is not loss of the Linux page cache. Run destructive fault qualification
on isolated disposable storage, preserving production data.

## Desktop and Atlas

Desktop maintains application entries and mounted-volume entries as distinct
objects in its existing layout. Volume entries derive from the native committed
catalog and update through a waitable subscription. Initial snapshot plus
subscription reconciliation prevents missed insertion. Notifications refresh
affected entries; overflow/provider restart rescans. GUI/window waits and catalog
waits share existing native wait mechanisms without a timer-polling loop.

Use filesystem-class icons matching the white/cyan/navy visual language for
fixed, USB, optical, and RAM volumes. Include read-only state where relevant.
Display one entry per published user-visible mount. Keep applications available
and deterministic layout/hit testing as icons appear and disappear. Store selection
by object identity; removal cannot shift a click onto another volume or app.

Double-click resolves the selected mount ID/generation against the live catalog,
then uses the existing launcher argument API to open installed `Atlas.app` at
its canonical root. Atlas validates and opens that root through the native
Filesystem Kit. If unmount wins before open, show a precise unavailable-volume
result. Never use the label as the pathname or retarget a stale click.

The first Atlas delivery required by this filesystem work is a functional native
browser: mounted-volume sidebar, directory list, path/location display, parent
and back/forward navigation, directory activation, refresh, capacity/read-only
status, and truthful loading/error/lost state. Use retained Interface Kit layout,
shared collection controls, scroll support, and command model. Perform directory
I/O outside UI interaction paths and return bounded generation-checked results;
slow media cannot freeze window input. The larger Atlas grid/columns and document
editing feature set remain tracked in the Interface ledger; do not require an
unrequested Finder feature clone to qualify storage.

Provide authorized mount controls in Atlas's device/volume view and desktop
volume actions: mount, unmount, safe remove, and optical eject, with progress,
busy-owner details, and error presentation. Atlas releases its own directory
references when it initiates removal, returns to a stable volume overview on
success, and reports other owners without killing their applications. External
unmount may correctly report Atlas as busy while it is browsing the volume.
On surprise loss, drop authority, retire pending I/O, and show lost-volume state.

An empty drive and unsupported/unmounted source appear in the inventory view,
not as an empty mounted desktop disk. After successful unmount or loss, its disk
icon disappears; a failed unmount retains the icon. After Linux reports mount
readiness or loss, catalog/desktop updates target two seconds under qualified
load. Probe/recovery time is measured separately and displays progress.

## Delivery sequence and touched responsibilities

These are coordinated milestones, not optional substitutes for the whole result.
Write the detailed implementation plan after this spec is reviewed.

| Milestone | Existing integration points | Exit requirement |
|---|---|---|
| Provider foundation and inventory | QEMU filesystem/resolver code, host protocol, Linux helper build/release, pinned sysroot | One authoritative catalog, confined privileged controls, raw-request refusal, deterministic probe/event races |
| System/bootstrap and RAM cutover | Supervisor bootstrap/loader/VFS, HostFS, launcher, image tools, manifests | Linux-owned `/dh0`, bounded `/ram0` and `/tmp`, no guest writable owner, preserved copied data, full current app/semantic gates |
| Live namespace and manual controls | VFS process/assign/library/transport, runtime startup, POSIX adapters, native tools | Already-running clients and late first initialization see mounts; manual mount, busy/unmount, cancellation and rollback pass |
| USB policy and lifecycle | Provider udev/probe/control, Supervisor persisted policy, lifecycle/shutdown | Writable default, two drives/partitions, loss/reinsertion, flush refusal and whole-device removal on DE25 |
| Optical platform and media | Exact-source Linux Image/modules, kernel packaging, provider optical lifecycle | USB optical discovery, ISO9660/UDF, media change, eject, no mixed-kernel module set |
| Desktop and Atlas | Desktop/layout tests, native Atlas bundle, public Interface collections/commands, launcher and image manifest | Live labeled icons, double-click exact root, removal and busy/loss UI, native browser works on all media classes |
| Production reconciliation | Release/deployment, storage oracles, NDK/contracts/docs, current-state records | Exact-source whole-stack qualification, legacy engine removal, retained data/rollback evidence, performance/resource disposition |

Delete production lwext4/ext4 and RAMFS service integration, duplicate partition
selection, fixed-export-only tables, and obsolete protocols after replacements
pass their owning milestone. Retain useful behavioral tests as native/provider
oracles. Keep synthetic Astra proc/events/metrics trees on their current native
service contracts. Do not rebuild FPGA fabric for filesystem functionality.

## Qualification and completion

Run all builds/tests/QEMU/kernel/FPGA tooling on Beast and hardware operations
through Beast. Preserve exact checksum/delete source mirrors, shared build locks,
and live runner session IDs. Retain each failure and final source/artifact identity
under `/mnt/Documents/astra68/builds/` according to Artifact Policy.

| Family | Required acceptance |
|---|---|
| Existing behavior | All current 26 filesystem semantics, stress, raw confinement, host/sanitizers, 31 integration gates, NDK headers/docs, Terminal/apps/power, Doom audio and normal quit |
| Bootstrap | Exact configured image/partition, multiple unrelated devices, absent/ambiguous/corrupt selection, first-read Linux backend, no second writable owner |
| Live visibility | Native/POSIX clients, shell, desktop and late first initialization started before mount; root enumeration/open/rewind, fork/exec, gaps and rescan |
| Media/identity | Two drives and two partitions, unpartitioned source, duplicate labels/UUIDs, stale probe/token/click, cross-volume links, aliases and retained cwd/description identity |
| Removal | Busy owners, Atlas self-release, stalled I/O, failed sync/unmount, cancel at each stage, surprise detach, reinsertion, whole-device dependencies and provider restart |
| Optical | Rebuilt Image and all matching modules, empty drive, insert/replace/eject physical discs, ISO9660/Joliet/Rock Ridge/UDF contents, read-only and eject failure |
| RAM/resources | Byte/inode/full-metadata pressure, aggregate RAM/recovery margin, handle/admission floods, process death, charges returned, UI/audio remain responsive |
| Durability/data | Pre/post byte and metadata inventories, atomic saves and directory sync, clean restart, Linux-owned fault oracle, offline checking on disposable media |
| Desktop/Atlas | Labeled/fallback class icons, duplicate labels, first snapshot/event races, exact root open, mount controls, disappearance only on successful unmount/loss, physical captures/input |

Existing broad performance budgets remain failed and visible; this work grants
no waiver. Record target baselines before new namespace/IPC/copy/UI hot paths,
set explicit cycle budgets before consumers depend on them, and re-run matched
median/p95/throughput/resource qualification after correctness. The old failed
Phase 1 results are historical evidence, not acceptance of the final design.
The current 10% latency and 90% throughput gates remain unchanged.

Production completion requires exact kernel/module/provider/QEMU/ROM/library/
manifest/policy/image identities to pass together, physical USB and optical
evidence, current software/audio behavior, finite resource recovery, final docs,
and absent obsolete production engines. Images prove format semantics; they
cannot substitute for physical hotplug, loss, or eject. No USB/optical device is
currently attached, so those physical gates remain pending until devices are
available.

## Reference contracts

- [Filesystem production PRD](../../FILESYSTEM_PRODUCTION_PRD.md)
- [Interface implementation ledger](../../INTERFACE_IMPLEMENTATION.md), public
  Interface/Graphics Kit contracts, and `ndk/include/astra/application.h`
- [Shutdown](../../SHUTDOWN.md), [Artifact Policy](../../ARTIFACT_POLICY.md),
  and [DE25 timing/release record](../../../fpga/de25/TIMING_CLOSURE.md)
- [Linux 6.12 tmpfs](https://www.kernel.org/doc/html/v6.12/filesystems/tmpfs.html):
  explicit byte/inode limits and `noswap`
- [Linux mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html):
  common namespace visibility and private propagation
- [Linux 6.12 ext4](https://www.kernel.org/doc/html/v6.12/admin-guide/ext4.html):
  mount/recovery and durability behavior
