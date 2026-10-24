# Astra filesystem production requirements

Status: active implementation PRD. The architectural direction was agreed on
2026-10-06. Current candidate uses Filesystem ABI 14 / Kit 11.0.0, STOR 38
and the Linux-owned fixed/removable/optical/ISO/RAM provider. Native lifecycle,
exact file/CWD busy removal, recovery, durability and Atlas overflow pass their
current gates. Coherent release `04aa23c8` runs on DE25 after stopped-state
migration; native USB FAT32/exFAT mounts and read/write passed in private and
production boots. Physical optical/eject and complete performance qualification
remain open; [Current state](CURRENT_STATE.md) records exact evidence.
Network mounts are excluded from this completion task. Performance acceptance
follows correctness.

Historical qualification: Native-16, integration-1 and unprivileged DE25
provider gates qualified the preceding ABI 11 source on 2026-10-11.
Final-review fixes passed Filesystem ABI 12/Kit 9.0.0, STOR 36:
all 26 semantics, stress, NDK/docs, all 31 integration gates and isolated
unprivileged AArch64 provider checks. ABI 12 release `194b9202` is deployed
with unprivileged QEMU and the canonical worker-only resolver optimization.
Final five paired QEMU stat/read probes pass, but full workload budgets fail
(spawn median 1.73× baseline, among other failures). Physical five pairs also
fail unchanged budgets: warm-open median 3.51× and readdir median 2.59×,
among other failures. Every workload executes successfully; provider memory
and held-miss absolute floors pass. Performance acceptance remains open; those
measurements do not qualify the current candidate.

The 2026-10-12 user request makes the remaining production phases the active
priority. The user specifies `/ram0` naming, writable automatic mounts for
supported USB filesystems, labeled desktop volume icons, and double-click into
Atlas, and authorizes the Linux kernel work needed for CD-ROM support. The
[production volumes design](superpowers/specs/2026-10-12-production-filesystem-volumes-design.md)
records the implementation boundaries and migration/acceptance requirements.
Physical USB qualification and production cutover are recorded below; physical
data-CD qualification still requires attached media.

Initial Atlas scope is a single root-directory window with named file/folder
icons, proving native open and enumeration of the selected mounted volume.
Broader browsing and Atlas management UI wait until storage works; native mount
tools and desktop volume actions remain in the production filesystem scope.

Linux owns filesystem machinery. Astra owns the client experience: its native
API, process-visible namespace, named volumes, grants, mount policy, desktop
behavior, and diagnostics. Native and POSIX applications use one Astra
filesystem facility backed by Linux storage operations.

The production objective is broader than making `/dh0` work. Astra must discover
devices and partitions, distinguish attached media from mounted volumes, mount
additional filesystems safely, and survive removal without confusing old
handles or paths with new media. System storage, removable storage, optical
media, and RAM storage must use the same lifecycle and application contract.

## Architectural authority

This PRD supersedes the guest-filesystem implementation and Linux block-only
ownership assumptions in [Storage and VFS](STORAGE_AND_VFS.md),
[Devices and volumes](DEVICE_AND_VOLUME_PLAN.md), and the storage sections of
[Driver and service architecture](DRIVER_AND_SERVICE_ARCHITECTURE.md).
It also replaces the assumption that guest ext4 and guest RAMFS remain production
engines. Those documents now describe the Linux-owned production boundary.

The useful device and mount rules from the existing plan are retained: fixed
internal-SD system selection, separate device and volume identities, one native
mount authority, labels as display metadata, protected namespace publication,
generation-safe removal, and no alternate-media boot fallback.

Axiom continues to supply protected processes, typed handles, IPC, shared areas,
waits, cancellation, and resource ownership. It gains no pathname parser,
filesystem-format implementation, or mount policy. Linux services remain behind
versioned Astra protocols; applications gain no alternate Linux API.

## Current implementation and gaps

The table describes the deployed DE25 release and remaining qualification.

| Area | Existing implementation | Production gap |
|---|---|---|
| System storage | The canonical image selector validates one exact system partition. Supervisor bootstrap and HostFS access the same Linux-owned provider; guest ext4/storage services are absent from the production release. Mutable `/dh0` state has one release-independent path, `/var/lib/astra/state/system/storage-terminal.img`; release-owned files were refreshed from the verified image during stopped-state migration while user files and the old image were retained. | A later different-release upgrade and rollback still need a physical cutover gate. |
| Linux-backed files | HostFS owns `HOST_FILESYSTEM` and mints rooted, rights-bounded sessions/scopes. Clients drive kernel-bound host channels; QEMU enforces scope, ancestry, read-only rights, ownership and object tags for every command. Current storage protocol is 38. The Linux provider owns fixed, removable, optical, image and RAM backing instances; WORK and SOUND remain explicit exports. | Full performance acceptance and physical optical qualification. |
| RAM storage | Configured Linux tmpfs has explicit limits and source identity independent of its current mount. Native scoped brokers preserve rights across remounts. | Additional configured RAM volumes need physical lifecycle qualification. |
| Namespace | Canonical volume snapshots update native process assigns; exact incarnation checks prevent old grants from targeting replacement mounts. Ordered traversal confines rooted grants. | Physical CD insertion/removal remains unqualified. |
| File objects | Stable volume/node identities, retained objects/cwd, shared descriptions/offsets, fork/exec inheritance and ordered symlink behavior pass current correctness gates. | Production lifecycle across dynamic media and final performance acceptance. |
| Devices and mounts | Linux discovery subscribes before scanning; the provider implements device/source/mount identity, supported-media policy, safe unmount/eject, ISO admission and canonical notifications. Disposable USB FAT32/exFAT partitions passed native format, mount, read/write, unmount and private/production boot gates. | Physical optical discovery, disc change and eject remain unqualified. |

Implementation anchors are [supervisor bootstrap](../sw/userspace/supervisor/src/volume.c),
[Linux storage provider](../fpga/de25/linux/astra_storage_provider.c),
[device discovery](../fpga/de25/linux/astra_storage_discovery.c),
[HostFS service](../sw/userspace/services/hostfs/main.c),
[host backend](../sw/userspace/vfs/src/backends/host.c), and
[host protocol](../sw/include/astra/host.h). The native
[volume contract](../sw/include/astra/volume_abi.h) supplies general device,
source and mount inventory; WORK and SOUND remain explicit exports. Image
qualification does not establish physical storage hotplug. Current gaps are recorded in
[Current state](CURRENT_STATE.md).

The filesystem review identified client-only grant enforcement, premature inode
release in guest ext4 on unlink/replacement, lexical `..` handling before
symlink traversal, absent stable metadata IDs, pathname-based cwd, and incomplete
POSIX descriptor concurrency. Moving storage to Linux must remove these defects
rather than reproduce them in the bridge.

## Product outcomes

Production qualification requires these user-visible outcomes:

1. Astra boots from the explicitly selected internal-SD system volume even when
   other disks are attached, and stops normal startup if that selection cannot
   be validated.
2. A user can attach a USB drive, inspect its partitions, mount an eligible
   filesystem, and use it through the desktop, native applications, and POSIX
   tools without application-specific paths or backends.
3. Two filesystems on one device become two volumes. An unformatted partition
   remains visible in inventory and as an `NDOS` desktop source, without being
   presented as a usable mounted volume.
4. A supported USB optical drive exposes a data disc at `/cd0` or its next
   available optical name. Read-only status, insertion, removal, and eject are
   visible to clients. Audio-CD playback is a separate media feature.
5. `/ram0` is a bounded, volatile Linux tmpfs volume. Additional configured RAM
   volumes use `/ram1`, `/ram2`, and subsequent ordinals. `/tmp` uses a separate
   bounded tmpfs mount by default in the production profile; persistent temporary
   storage requires an explicit configuration choice.
6. Removing a drive with live files produces a precise loss/error state. An
   editor's old file or cwd can never silently become a file on replacement
   media.
7. Safe removal drains work and flushes storage before reporting that the medium
   may be disconnected. A failed flush never produces a success indication.
8. Every already-running application and command sees newly published public
   mounts and their removal without restart. A shell started before a USB drive
   was attached can immediately list and access its mount within its granted
   rights. Existing file handles and cwd retain their original object identity.

## Scope of the first production release

| Source | Required filesystem support | Default policy |
|---|---|---|
| Fixed internal SD | Linux ext4 for the selected system partition. | Mount after identity and recovery checks; never select by enumeration order alone. |
| Additional fixed disk or partition | Linux ext4. | Explicit persistent mount policy or an authorized manual mount. |
| USB mass storage | ext4, FAT32, and exFAT with drivers included in the release kernel/modules. | Supported data volumes automatically mount writable under the shipped trusted policy; requested writable mounts that fail remain unmounted with a reason. Explicit policy/actions may request read-only access. |
| USB data CD/DVD | ISO9660 including common extensions, and UDF. | Read-only; publish only after Linux has identified and mounted the inserted medium. |
| RAM storage | Linux tmpfs. | Explicit byte and inode limits; production RAM storage must not use swap. |

Filesystem support is a per-format capability, not a single "supported" bit:
detect/inventory, mount read-only, mount read/write, and create/format are
separately qualified. A format never gains write or format access merely because
Linux can identify or mount it. Current source mounts ext4, FAT32 and exFAT
on eligible storage, FAT12/FAT16 read-only on USB, ISO9660/UDF read-only on
optical or USB media, NTFS read-only on fixed/USB data partitions, and tmpfs for
RAM. FAT12/FAT16, USB ISO9660, NTFS, FAT32 and exFAT have passed guarded
physical DE25 trials; FAT32 and exFAT also passed native Disk Setup formatting
and automatic remount on disposable media. Physical data-CD, optical media
change and eject still need qualification. The supported
format/transport policy is extended in this order:

| Priority | Formats | Intended capability and qualification |
|---|---|---|
| First interoperability | FAT12/FAT16, FAT32, exFAT, ext4, ISO9660/UDF on USB as well as optical media | Detect every partition and mount each eligible filesystem independently. FAT-family and ext4 data partitions gain read/write only after disposable-media write, sync, unplug, and recovery gates. ISO9660/UDF remain read-only on every transport. Format only writable data formats with qualified provider tools. |
| Next interoperability | NTFS | Detect and inventory first; add read-only mounts before considering writes. NTFS writes require separate feature, recovery, and physical-media gates. |
| Further evaluation | APFS, HFS+, XFS, Btrfs, Amiga OFS/FFS | Assess actual demand, available trustworthy drivers, metadata semantics, and safe read-only support individually. Unknown or unsupported variants remain visible with an exact reason. No implied write or format promise. |
| Native format | AstraFS | Remains a deferred design in [AstraFS](ASTRAFS.md); no implementation or disk format exists. Qualify on disposable images before exposing it as a format choice or system-data backend. |

Partition-map support covers MBR and GPT, including several partitions per
device. A filesystem may also occupy an unpartitioned device. CD/DVD media
with multiple discoverable data sessions or partitions must expose each
mountable filesystem as a separate source; media topology must be verified
against actual drives before claiming general multisession support.
Haiku's `src/add-ons/kernel/file_systems/fat/kernel_interface.cpp` handles
FAT12, FAT16 and FAT32 as variants of one FAT implementation, while
`src/kits/storage/disk_device/DiskDeviceTypes.cpp` retains distinct format
identities. Astra follows that separation using Linux vfat behind distinct
native format records, rather than adding three filesystem engines.
Haiku's `src/add-ons/kernel/file_systems/ntfs/kernel_interface.cpp` mounts a
hibernated NTFS volume read-only. Astra's initial NTFS policy is read-only for
all volumes while using Linux ntfs3 as the single provider backend.

The provider contract must also accommodate network filesystems and approved
host-directory exports without inventing block devices for them. NFS and SMB
enablement follow this contract when qualified; they are not required to delay
the first local-storage release. Unknown or disabled formats remain visible in
inventory and return a precise no-filesystem or unsupported-format result.

Astra's synthetic process, event, metric, and service-control trees continue to
describe Astra objects through the native VFS contract. They are not Linux
`/proc` or `/sys`, and do not need a FUSE implementation merely to appear in the
Astra namespace.

Building AstraFS, maintaining guest filesystem-format engines, alternate boot
selection, audio-CD playback, and a general raw-device interface for every
peripheral are outside the first release. Native Disk Setup provides the
authorized partition/format operations specified in FS41.

## Ownership and integration

| Responsibility | Authoritative owner |
|---|---|
| Hardware storage drivers, partition probing, filesystem implementations, Linux mounts, cache, journaling, and physical eject | Linux storage provider and Linux kernel. |
| Astra mount policy, published volume names, process namespace bindings, application grants, and UI behavior | Protected Astra mount authority, initially integrated with the supervisor. |
| Enforcement of those grants on every storage operation | Trusted filesystem provider boundary: the guest HostFS service checks a kernel-stamped sender owner, never a client-supplied one, against the grant on every request. Linux confines each rooted session with `openat2` `RESOLVE_BENEATH`. QEMU never trusts a client-supplied owner. |
| Native file and directory API, mount/device query API, status mapping, and client transport | Astra native libraries and services. |
| POSIX descriptors, `errno`, `umask`, standard symbols, and compatibility bookkeeping | Userspace POSIX adapters over the native facility. |
| Process information, event streams, and other synthetic trees | Their existing Astra object services. |

There is one logical mount authority. Linux's actual mount state is authoritative
for whether a storage filesystem is mounted; Astra's published state determines
which processes can see and use it. Astra records Linux's acknowledged objects
and generations, not an independently guessed copy of their state. Linux
detection proposes inventory changes; it cannot choose an Astra name, grant an
application access, or change the selected system volume by itself.

Use the existing host channels, VFS transport, service lifecycle, and ownership
mechanisms where their contracts fit. A new daemon, worker process per
application, FUSE layer, or kernel object type requires a demonstrated need.
The initial implementation extends the existing Linux-side QEMU filesystem
provider with inventory and mount control. Separate its storage implementation
from device/transport glue, and keep Astra policy in the supervisor. Confined
namespace views must pass the Phase 1 prototype before the guest engines are
removed. A separate Linux service is introduced only if required privilege or
lifecycle isolation cannot be achieved in the existing provider.

The current qualified runtime keeps QEMU unprivileged, with no capabilities
and NoNewPrivs set. Dynamic source probing, mount/unmount, and optical eject
require a confined privileged Linux control component. The production volumes
design records this concrete isolation need: that component owns actual
source/mount state, while QEMU retains its canonical scoped file-I/O path in
the same runtime-private mount namespace. Applications still use only the
versioned native contract; QEMU does not gain mount or raw-device privilege.

## Devices partitions volumes and identities

FS01. Inventory separates a physical device, inserted medium, partition,
filesystem, mounted instance, namespace binding, and open object. Queries expose
their associations, capabilities, state, and last failure. Attached/unformatted
devices appear in device inventory; `df` lists mounted filesystems only.
Dynamic storage inventory belongs to the provider; it does not require unsealing
Axiom's boot device registry or granting an MMIO lease for each Linux disk.

FS02. Preserve the planned names: internal storage starts at `/dev/disk0`, with
partitions numbered from one such as `/dev/disk0p1`. Filesystems on unpartitioned
media associate directly with the device. Volume conveniences include `/dh0`,
`/dh1`, `/usb0`, `/cd0`, and `/ram0`, with subsequent ordinals for each class;
`/df0` is reserved for an enabled floppy
provider. Ordinals count volumes of that class, not physical devices. Linux
paths such as `/dev/sda` remain provider details.
`/dev` is a rights-checked adapter over the same native storage/device objects.
Publish only endpoints with defined device-file operations; inventory visibility
alone does not grant raw access or manufacture nodes for unrelated peripherals.

FS03. Device identities and mounted-instance IDs are never recycled within a
provider epoch. Automatic volume names identify active namespace slots, not
mounted-instance identity. A name may be reused only after the previous Linux
mount, worker and retained cleanup resources have actually retired; lost or
cleanup-pending instances still reserve their names. A reinserted medium obtains
a fresh mounted-instance ID and the lowest available name for its class.
Persistently configured mountpoints may be rebound only through an explicit
mount transaction after old bindings are withdrawn. Retained files, directories,
roots and cwd remain tied to their exact epoch/ID/generation and never resolve
to replacement media through a reused name.

FS04. Stable policy selection uses verified filesystem UUID plus device and
partition identity where needed. Formats without a sufficiently unique UUID
use an explicitly documented composite identity or manual selection. Labels
are mutable display metadata and never grant authority. Duplicate identities
and conflicting preferred names block automatic selection until explicitly
resolved.

FS05. Object metadata carries a canonical mounted-filesystem identity, node ID,
and generation sufficient to distinguish reused objects. Native metadata and
POSIX `st_dev`/`st_ino` agree. Bindings and aliases to the same filesystem retain
that identity; `df` does not count them as additional volumes. Linux descriptor
numbers, paths, and client pointers are not stable identity.

FS06. Inventory and mount lists use bounded paginated snapshots with a change
sequence. Notifications are paired with rescan: a dropped/overflowed event
causes an explicit rescan, not permanent loss of a device. Resource limits may
bound admission but cannot be hidden fixed two-volume tables. Capacity failure
leaves inventory inspectable and creates no partial mount.

## Boot and system volume

FS07. Keep the existing U-Boot, Linux, QEMU, Axiom, and initial-supervisor boot
chain. Before guest filesystem bootstrap, the Linux provider resolves exactly
one configured internal-card system selection and verifies the device identity,
media generation, partition identity/window, filesystem type, and UUID. The
trusted handoff includes the provider generation and selected filesystem object.

FS08. Missing, ambiguous, changed, unsupported, or corrupt system media stops
normal desktop startup with a diagnostic. Removable media, a matching label,
or another Linux-type partition cannot replace it automatically. Data mounts
never change the source of the running ROM.

FS09. The initial supervisor reads its startup manifest and service images
through the same Linux-backed native filesystem contract used at runtime.
Provider bootstrap must be available before loading a guest service from that
filesystem. Reuse the bootstrap client/transport when the managed service takes
over; no temporary guest ext4 mount or second partition-selection policy remains.

FS10. System-volume selection comes from trusted release/host configuration
outside the volume it selects. After `/system` is accessible, Astra loads the
data-volume policy from `/system/config/mounts.conf`. The file records identity,
preferred Astra mountpoint, expected format, access mode, and automatic/manual
behavior. Invalid entries receive diagnostics and never broaden access; missing
required mounts prevent dependent services from starting.

FS11. Linux is the sole writable filesystem owner. Release launchers and tools
must not export the same live filesystem to a guest raw writer or open a second
writable image mount. QEMU image-based qualification uses a Linux-mounted image
or equivalent Linux filesystem provider, not the retired guest engine. QEMU
gates on `beast` run QEMU itself as the unprivileged caller -- never as root
-- through a drop-privilege helper (`tools/astra-qemu-root`): root acts
only inside its own `/var/tmp` directory (the image copy, the loop mount
and its removal), the caller's image is read and written back with the
caller's credentials, and the helper `setpriv`-drops to the caller before
QEMU ever executes, so QEMU opens every file, including its own
`-bios`/`-drive`/`-qmp` paths, with the caller's own permissions. The
gates exercise a real Linux mount without widening what sudo access on
`beast` can reach.

## Namespace grants and path behavior

FS12. Slash paths are the only spelling. Astra retains named volumes and
process-specific bindings. The root is enumerable and openable as a directory,
including native directory-relative operations and POSIX `dirfd`/`openat`.
Ordinary create/unlink/rename directly under `/` returns read-only. A separate
authorized namespace operation publishes or removes bindings.

FS13. `/system` is a protected namespace alias for `/dh0` and exposes link
metadata and its target. Independently granted roots such as `/home`, `/apps`,
`/commands`, `/libs`, `/local`, `/config`, `/cwd`, and `/work` report directories
and retain their own rights. Their backing location does not make them aliases.
Explicit namespace aliases and filesystem symlinks report their targets and
traverse them under the caller's authority rules.

FS14. Astra defines the namespace view and provider routing; Linux performs
storage pathname resolution and object operations within a confined view.
Storage paths follow components in order, including symlinks, `.`, `..`, mount
crossings, and trailing separators. Neither libc nor the Kit removes `..` before
lookup. Native and POSIX calls resolve the same objects, with final-link behavior
selected by the operation. Cross-volume symlinks are supported only when the
destination is present and authorized in the caller's view.

FS15. Grants bind authenticated owners to rooted directory objects and rights.
The trusted boundary enforces them for raw messages, native calls, POSIX calls,
directory-relative operations, and acceleration. Changing a client assign table,
guessing a volume ID, forging startup flags, walking `..`, or following a symlink
cannot escape the grant. A shared transport capability alone grants no volume
or mutation authority. Mount/administer and raw maintenance rights are separate
from ordinary file read/write rights; delegation cannot increase them.

Enforcement is split between an authority and a boundary, and neither is a
server on the data path. HostFS is the authority: it alone holds the host
device's scope right, opens a rooted provider session per grant and mints
that grant's *scope*, a kernel object naming the session and its rights.
A client receives the scope when it says HELLO on the grant's port; the
kernel stamps the scope's session, rights and creator into every channel
the client binds to it, and never a client-supplied owner. QEMU is the
boundary: on a bound channel it runs only filesystem commands, resolves
every path beneath the session's root with `openat2` `RESOLVE_BENEATH`,
refuses mutations through a read-only scope and handles from another
session, and accepts a scope only if the claimed filesystem owner created
it. A scope travels further only when its grant is delegable, and a holder
can narrow a grant only through HostFS (`GRANT_DERIVE`); a port derived
from a non-delegable grant arrives without TRANSFER and cannot be moved on,
and every derived grant ends with the session that derived it. Revocation goes
through the kernel, which detaches the scope from every channel at once.

FS16. Namespace changes are atomic publications tied to a sequence and object
generation. Public mount names and metadata form a live catalog visible to every
running application and command, including processes launched before insertion
or before their first filesystem call. Startup grant records are not a frozen
copy of the mount namespace. Native root enumeration, POSIX root enumeration,
path lookup, and mount queries observe publication without relaunch, exec, or
an application-specific refresh API. New path operations after publication use
the current namespace; existing open objects and cwd remain pinned. Visibility
does not confer file read/write or mount-control authority. Trusted grant policy
updates eligible sessions when a mount appears; private `/config` and `/cwd`
bindings remain process-specific. Namespace caches validate their sequence and
recover from missed notifications. Revoked grants reject new work and retire
existing authority according to one documented native revocation contract.

FS17. The trusted namespace implementation must prove, before integration,
cross-volume symlinks, grant confinement, `..` at mount roots, and same-volume
operations through different bindings. Merely creating Linux bind mounts is
not sufficient evidence: Linux mount boundaries can affect rename even when
the underlying filesystem is shared. Canonical volume identity and both
operands' rights govern Astra rename/link; genuine cross-volume operations
return the cross-device result without changing either operand.

FS18. Names remain case-preserving and byte-exact. Foreign filesystem case
behavior is reported as a filesystem property rather than normalized in the
Kit. Ext4 and tmpfs support 255-byte components and 4096-byte pathname buffers
including the terminator; other formats advertise their actual limits. Wire
payload size must not impose the current 192-byte pathname ceiling. Long paths
use bounded validated payloads and directory handles. No field is silently
truncated, and unrepresentable foreign names receive a defined diagnostic.

## File directory and POSIX contract

FS19. An open file or directory references a retained Linux object. Unlink and
rename replacement remove names without freeing objects still held open.
Hard links share identity. Open-object metadata remains available after rename
or unlink. Cwd is a retained directory object; renaming it does not break
relative access, and `getcwd` reconstructs a valid path or returns a defined
failure when no reachable pathname exists.

FS20. Native open descriptions define access mode, position, append behavior,
references, and lifetime. Sequential I/O on a shared description atomically
advances its position; positioned I/O leaves it unchanged. Descriptor duplication
and supported fork inheritance preserve that sharing. The POSIX adapter keeps
its descriptor-number table and flags but does not implement a second file
object or reopen files by pathname to imitate inheritance. Exec transfers
retained objects and honors close-on-exec without changing their identity.

FS21. Complete the common operations: open/create/exclusive create, read/write,
positioned I/O, seek, append, truncate, file/directory stat, enumeration/rewind,
mkdir/rmdir, unlink, rename replacement, hard links, symlinks/readlink,
directory-relative operations, capacity, flush, and file/directory sync.
`O_NOFOLLOW`, `AT_SYMLINK_NOFOLLOW`, trailing slashes, nonempty-directory refusal,
and root-directory operations must have matching native and POSIX behavior.
Unsupported flags return an error; they are not silently ignored.

FS22. Metadata conveys real IDs, modes including mode zero, link count, supported
timestamps, size, and allocation information. Unknown fields have an explicit
validity/capability representation rather than treating legitimate zero as
unknown. POSIX queries do not fabricate authoritative values such as inode zero
for every file or modification time for every timestamp. Query advertised
filesystem limits and feature support before depending on optional features.

FS23. Astra's first release remains a capability-controlled, single-owner
desktop. POSIX `umask`, descriptors, and `errno` stay in libc; an independent
Unix multiuser access-control subsystem is not introduced. The provider must
define how reported Linux ownership/modes and the supported `access`/`chmod`
profile interact with native capabilities. Mode bits never override missing
capability authority. Read-only format limitations remain visible.

FS24. Same-description thread races, concurrent open/dup/close, and process
termination must have defined reference and synchronization behavior. A blocking
operation retains its object until it completes. Process death releases Linux
descriptors, directory streams, session state, and charges, including accelerated
clients. No unsafe pathname-based recreation is used after fork or service loss.

## Mount hotplug unmount and eject

FS25. A mount transaction starts from an exact generation-checked source and
authorized policy. It validates filesystem support and identity, asks Linux to
mount/recover it in the provider's storage domain, establishes rooted authority,
and atomically publishes the Astra binding only after success. Failure unwinds
unpublished resources. Explicit mounts may use empty directories outside `/`;
mounting over existing contents is refused.

FS26. Use one mount lifecycle: discovered/unmounted, mounting, mounted,
quiescing, then unmounted or lost/failed. Failed quiescence can restore a mounted
state only if authority and provider state remain intact. Terminal loss advances
the generation. Reuse the service lifecycle machinery; do not build a parallel
filesystem-only cancellation/accounting framework.

FS27. Linux reports storage attach, detach, optical media change, and relevant
mount failure. Astra updates inventory, evaluates policy, and publishes change
events for native clients, the desktop, and tools. Optical drive identity stays
distinct from the inserted disc. Unsupported, empty, ambiguous, and inaccessible
media are inspectable states, not empty usable directories. No medium autoruns
code or installs application grants when inserted.

FS28. A normal unmount with live file/directory/cwd references returns busy and
identifies the owning clients. Safe removal rejects new opens, coordinates
client release, drains accepted work, synchronizes writable storage, and removes
the Linux mount before withdrawing the final binding and reporting success.
Busy/refused operations preserve usable state. Unmounting a shared source must
account for every binding that references it.

FS29. Surprise removal and forced unmount immediately withdraw new access,
advance the affected generation, and complete pending requests with one defined
loss result. Existing handles and cwd never reconnect to replacement media.
Cached data must not make a revoked volume appear live. Close and teardown remain
possible after loss. Forced removal reports possible loss of unsynchronized data.

FS30. Eject is separate from unmount. The provider ejects optical/removable media
only after authorized unmount succeeds, and reports hardware failure accurately.
Other media classes may support safe removal without a physical eject operation.
Data volumes may be removed independently; ordinary runtime removal of the
selected system volume is refused.

FS31. Automatic removable mounting follows an explicit filesystem allowlist.
The shipped trusted persisted policy mounts supported ext4, FAT32, and exFAT
USB data volumes writable, as requested on 2026-10-12. Optical data volumes are
read-only; FAT12/FAT16, NTFS and recognized ISO9660/UDF data volumes on USB are
read-only until separate write gates qualify a writable variant.
Configured rules and explicit mount actions may request read-only USB access.
A requested writable mount that fails remains unmounted with a useful
reason instead of silently changing mode. Inventory never formats or repairs
media automatically. Mounts use restrictions suitable for data media, and labels
cannot supply executable paths or shell commands.

## Durability shutdown and provider failure

FS32. Native sync and POSIX `fsync`/`fdatasync` return success only after the
defined Linux durability operation succeeds. Parent-directory sync is supported
for filesystems that support it, so write/sync/rename/directory-sync is a tested
atomic-save contract. A read-only filesystem may use an explicitly supported
no-op flush; it cannot hide an unrelated I/O error.

FS33. Integrate with the existing [shutdown contract](SHUTDOWN.md). Stop clients,
drain requests, close sessions, synchronize all writable Astra volumes, and
unmount those whose lifecycle permits it before acknowledging filesystem READY.
The mounted host system disk need not be unmounted by the guest, but its Astra
writable filesystem must be explicitly synchronized before READY. Do not rely
solely on a later Linux poweroff flush. A failure cancels clean shutdown without
asserting that already-stopped services have been restored.

FS34. Every request has a finite client deadline or documented cancellable wait.
A timeout, close, owner death, provider death, or late completion wins exactly
one terminal outcome. Work still executing in Linux remains accounted and cannot
reuse buffers or tokens before retirement. A mutation whose completion is
unknown returns a defined uncertain-I/O result and is never automatically
replayed. Bounded worker/queue admission keeps a stalled volume from consuming
all service-recovery resources.

FS35. Provider restart advances its generation and invalidates all old sessions
and storage tokens. Clients receive loss; applications explicitly reopen through
fresh grants. The registrar reconciles real Linux mount state before publishing
new bindings. Unrelated synthetic Astra services remain available where their
dependencies permit; loss of the selected system volume enters a diagnostic
failure state rather than silently selecting another disk.

## RAM capacity and resource policy

FS36. `/ram0`, additional configured numbered RAM volumes, and default volatile
`/tmp` are Linux tmpfs mounts with explicit
byte and inode limits, no swap use, and inspectable usage. Configuration is
charged against host memory remaining after the reserved guest and other
platform commitments. Missing/invalid limits refuse the mount; a required mount
failure prevents its dependent services from starting. Never create an unbounded
ramfs or accept tmpfs defaults implicitly.

FS37. Data limits and inode limits are not a promise that Linux metadata is free.
The provider deployment must also bound/account host memory and retained
resources, with a demonstrated margin for recovery. Native capacity reports
the enforced limit and available space. Full mounts produce a defined no-space
or valid short-write result, and truncation/unlink plus final close return
reclaimable charge. Guest process memory limits cannot substitute for these
host-side limits.

FS38. Open descriptions, namespace views, mount records, directory snapshots,
queued bytes, and pinned transfer areas have explicit owners and budgets.
Resource exhaustion causes refusal without partial publication. The implementation
must demonstrate that a full RAM volume or file-handle flood leaves input,
desktop recovery, unmount, and diagnostics responsive.

## Tools and desktop experience

FS39. Provide native operations and small Astra-specific tools for `lsblk`,
`blkid`, `mount`, and `umount`, plus safe-remove/eject through the same mount
control API. `df`, `stat`, `ls`, recursive tools, and application I/O use the
normal native substrate through their POSIX adapters. Inventory output includes
device/partition/volume relationships, format, identity, access mode, state,
capacity where meaningful, and the reason a source is not mounted.

FS40. The desktop exposes mounted volumes with labels or fallback names and
supports open, mount, unmount, and safe-remove/eject where authorized. It shows
progress, busy clients, read-only status, and failed/lost state. Backend paths,
Linux tool command lines, and raw transport details do not appear in ordinary
user flows. Existing native apps, Vim, Lua, zsh, and shipped game ports require
no filesystem-specific workaround.

For mounted volumes, use the nonempty label or uppercase canonical fallback (`DH0`, `USB0`,
`CD0`, `RAM0`). Duplicate labels remain separate generation-safe mount objects.
Double-click opens Atlas at the selected volume's canonical root through the
existing native launcher argument contract; labels are not launch paths.
Successful unmount and media loss remove the icon; failed/busy unmount preserves
it. A present, unformatted partition or source with no recognized filesystem
appears as `NDOS`, including each such partition of a multi-partition disk.
`NDOS` is a display label for an exact source or empty medium, never a fake
mount or filesystem name. Double-click reports that Astra cannot open it because
no supported filesystem was found, without launching Atlas or guessing that
the media are safe to erase. A recognized but unsupported format reports its
detected type and the unavailable capability; it is not described as blank
media. Probe I/O errors and ambiguous signatures report their own failure,
not `NDOS`. Empty optical drives remain in device inventory
without a desktop volume icon. Atlas and the desktop
consume the same live native catalog as filesystem tools. Initial Atlas shows
the selected root's real files and folders as named icons, with scrolling and
truthful loading/empty/error/loss states. Subdirectory navigation, file actions,
sidebar/toolbar, and Atlas mount-management UI are deferred. The production
volumes design records this minimum viewer scope; Atlas is not assumed to be an
already implemented application.

FS41. Selecting a physical disk or partition exposes `Disk > Format...` in the
desktop menu bar. It launches the native Disk Setup application with the exact
selected reference; it does not format from the desktop. Disk Setup shows the
device, all its partitions, filesystem/partition-map identity, capacity,
read-only state and mounted/busy state. It supports partition-map creation and
partition create/delete before filesystem formatting, without assuming one
volume per USB device or optical medium. Format choices are limited to
qualified writable formats; AstraFS stays unavailable until its implementation
and safety gates pass. Each recognized, mountable partition
has its own catalog source and mount; read-only filesystems on USB are mounted
read-only rather than hidden merely because their transport is USB. Optical
media cannot be formatted through this path.

Formatting and partition edits require typed native administration requests
naming an exact device or partition identity and generation. The provider
validates current identity, partition bounds, sibling mounts and users, system
disk protection, write permission and format options immediately before the
mutation. Disk Setup shows the exact target and data-loss consequence, then
requires explicit confirmation; cancellation writes nothing. No application
supplies a Linux path, assembles a host shell command, or bypasses native
safe-removal/accounting. A completed change triggers fresh discovery/probe and
catalog publication; a partial failure remains visible and cannot be reported
as success. Destructive operations need separate disposable-media and physical
gates before production enablement. Haiku's
`src/apps/drivesetup/MainWindow.cpp` separates disk and partition actions,
disables unsafe operations on mounted/read-only targets and validates before
commit; Astra adapts those checks to exact native references and its one
provider-owned lifecycle.

## Protocol and migration requirements

FS42. Define one versioned native storage contract covering data operations,
identity, rooted sessions, dynamic inventory, mount control, notifications, and
loss. Fixed-width records and checked lengths/offsets work on big-endian MC68040
and AArch64. Reuse existing transports and shared-area batching; command-record
storage is not the pathname limit. Typed capability negotiation may describe
optional formats/features but cannot conceal different core semantics.

FS43. Bump affected protocol and library ABI majors, update all in-tree producers,
consumers, manifests, NDK documentation, tests, release packaging, and image
tools together, and remove obsolete wire formats and fallback behavior. The
shared host command header also serves audio, entropy, metrics, and remote
desktop; any incompatible change must update and verify those consumers. Do
not introduce filesystem records by repurposing unrelated private fields.

FS44. Existing ordinary and accelerated requests converge on the same trusted
validation and Linux implementation. Accelerators carry scoped authority and
generations. The broad host-device handoff was removed at storage protocol
27. Protocol 28 hands a client a kernel-bound filesystem scope instead, which
reaches only that scope's session; the device itself must not return as a
grant bypass. Keep acceleration only where measurements justify it, with
behavioral parity tests.

FS45. Cut over the selected system volume, bootstrap loader, existing HostFS
exports, RAM storage, and application bindings. Preserve existing files and
identities, verify content before and after, and require offline conversion only
when independently necessary. The old restricted ext4 profile can be mounted
by Linux; enabling new on-disk features is not required for cutover. Never run
old and new writable owners simultaneously.

FS46. Remove guest lwext4 integration, guest ext4/RAMFS production services,
duplicate partition selectors, client filesystem policy, fabricated root links,
and obsolete fixed-export assumptions after the replacement passes its gates.
Delete vendor/build code only after checking every consumer. Keep useful
behavioral oracles as native/provider contract tests. No legacy production
engine or automatic fallback remains in the release image.

## Delivery sequence

| Phase | Deliverable | Exit evidence |
|---|---|---|
| 1 | Baseline and trusted contract prototype. Prove the namespace/authority strategy, IDs, retained objects, host memory budgets, bootstrap availability, and measured budgets. | Tests for raw-request confinement, symlink traversal, cwd/rename, same-volume aliases, shared descriptions, and provider loss; target baseline recorded. |
| 2 | Linux-owned system storage and RAM storage. Move bootstrap and `/dh0`, make bindings honest, and replace RAMFS with bounded tmpfs. | Fresh-image QEMU boots, native/POSIX semantic suite, full terminal/app gate, durability and shutdown failures, unchanged file content. |
| 3 | Dynamic device/partition inventory and additional fixed/USB mounts. | Multiple devices/partitions, UUID conflicts, unsupported/unformatted media, mount rollback, busy unmount, real DE25 USB lifecycle. |
| 4 | Optical media and desktop mount experience. | ISO9660/UDF data discs, media changes, busy eject, forced removal, notifications/rescan, DE25 optical drive evidence. |
| 5 | Production cutover and removal of obsolete engines. | Exact-source release build, sustained load and fault gates, retained evidence, documentation/ABI reconciliation, old engines absent. |
| 6 | First interoperability expansion, NDOS presentation and native Disk Setup. | FAT12/FAT16 and ISO9660/UDF on USB, multi-partition USB and optical fixtures, blank media, distinct probe faults, exact-reference menu handoff, system/read-only/busy refusals, canceled and failed format/partition edits leave data unchanged, successful edits rescan and publish correct mounts, physical read/write/recovery gates per writable format. |
| 7 | Additional formats as individually qualified. | NTFS read-only and any later APFS/HFS+/XFS/Btrfs/Amiga support have their own format-specific semantic, failure, and physical-media evidence; unsupported variants keep clear inventory and open errors. |

Network filesystem mounts remain outside this local-media contract. Destructive
maintenance features enter only after their separate qualification. A phase
cannot claim the whole PRD complete while its later required local-media gates
remain outstanding.

## Storage inventory hotplug and mount control implementation plan

This plan implements FS01 through FS17 and FS25 through FS31. Read-only
inventory can be developed before system-storage cutover. Mount mutation and
automatic publication require the trusted sessions, Linux ownership, and
object-lifetime foundation from Phases 1 and 2; they must not be layered on the
current client-only grant checks.

### Implementation boundaries

| Work | Integration point | Responsibility |
|---|---|---|
| Linux inventory and mount execution | Extract a storage component from the existing Linux filesystem code in [the QEMU machine](../emu/qemu/qemu-9.2/hw/m68k/astra68.c). | Discover and probe sources, retain Linux objects, execute mounts, track actual state and generations, and report changes. |
| Transport and native client operations | Extend [host records](../sw/include/astra/host.h), [host backend](../sw/userspace/vfs/src/backends/host.c), and existing channel/VFS transports. | Bounded snapshots/events and correlated control requests, with the same authority checks as file I/O. |
| Astra mount authority | Extend [supervisor namespace management](../sw/userspace/supervisor/src/vfs_host.c) and [service publication](../sw/userspace/supervisor/src/loader.c). | Load policy, reserve names, authorize operations, publish live bindings, and register rooted grants. |
| Guest filesystem bridge | Adapt [HostFS](../sw/userspace/services/hostfs/main.c) to dynamic provider objects. | Native sessions and file operations; no physical discovery or independent mount policy. |
| Runtime integration | Update [launcher](../emu/qemu/run-arty.sh), [runtime unit](../fpga/de25/astra.service), and [release packaging](../emu/qemu/create-de25-release.sh). | Configure the provider before guest bootstrap, isolate Linux mounts, include dependencies, and verify shutdown/restart. |
| Tools and desktop | Existing Filesystem Kit, command, and desktop consumers. | Query the shared live catalog and invoke authorized controls; no separate desktop disk database. |

Use Linux udev discovery and the util-linux `libblkid` and `libmount` APIs rather
than parsing `lsblk` output, filesystem superblocks, or `/proc` text with a new
guest implementation. Verify headers, libraries, kernel features, and runtime
versions against the DE25 sysroot and release before integration. The reference
manuals describe current APIs; implementation must use the shipped versions.

Establish a private Linux mount namespace for the runtime before filesystem
workers start, with private propagation so Astra data mounts cannot alter the
host's general mount layout. Manage mounts below a provider-owned directory
keyed by opaque mount IDs, not labels or client pathnames. Existing host mounts
are not automatically adopted: the system configuration may explicitly approve
an existing backing mount, while other externally used sources report in-use.
The existing launcher privilege requirements do not grant ordinary Astra
clients Linux mount authority.

### Inventory records and control operations

The provider owns actual source/mount records. The supervisor owns policy and
published bindings referencing those records. Use these logical identities in
the existing versioned native contract; fix exact layouts in the ABI probes.

| Record | Required contents |
|---|---|
| Provider | Restart epoch, inventory sequence, protocol/features, and admission limits. |
| Device | Opaque ID, attachment generation, class/transport, stable identity when available, capabilities, size, state, and diagnostic. |
| Medium | Parent device, insertion generation, present/empty/lost state, and current identity. Optical drive and inserted disc are separate records. |
| Source | Medium, partition identity/index/window or unpartitioned source, probed format/UUID/label, probe state, and supported access modes. RAM/export/network sources omit block-only fields. |
| Mount | Opaque instance ID and generation, source reference, actual Linux state, effective access/limits, root object, ownership, and active-reference counts. |
| Binding | Astra path, mount/root reference, publication sequence, public/private visibility, and grant policy. Aliases retain the mount's canonical identity. |
| Operation | Caller, correlation ID, expected generations, requested action, state/progress, deadline, cancellation, and terminal outcome. |

Linux device numbers and sysfs paths locate current resources but are not
authority or stable identity. A returned source token names an exact record and
generation; the provider revalidates it when opening/probing/mounting the source.

Provide snapshot/query, subscribe/wait/rescan, mount, unmount, safe-remove,
eject, operation-status, and cancellation operations. Inventory visibility and
control authority are separate. Public mount names/metadata are readable by all
running processes; data operations require rooted grants, and mutation requires
the native mount-administration capability. Unknown flags and stale records
fail before any Linux action.

Reuse existing request correlation and exactly-once terminal completion. A
control request first receives an accepted operation identity or an immediate
refusal; the caller can wait or query it without resubmitting the mutation.
Bound retention of completed outcomes so a lost reply is distinguishable from
an operation that was never accepted. Provider restart invalidates those tokens
and requires reconciliation, not automatic replay.

### Inventory before automatic mounting

1. Subscribe to processed udev block events before enumerating current devices,
   then merge the queued events with enumeration. This closes the initial
   scan/subscription race without imposing a global wait for all devices.
2. Create device/medium records and identify partitions through Linux topology
   and probing libraries. Probe using freshly opened sources and `libblkid`'s
   low-level interface; stale cached labels or UUIDs cannot select a volume.
   Distinguish no signature, unsupported format, ambiguous signatures, probe
   pending, and I/O failure.
3. Run probes in bounded existing filesystem workers. Publish PROBING records
   immediately, and attach results only if their source generation still
   matches. Device removal must invalidate records without waiting for a slow
   probe or mount worker to return.
4. Expose coherent paginated snapshots and native queries. A cursor is tied to
   its provider epoch and snapshot sequence; expiration returns rescan rather
   than a mixture of old and new rows. Keep returned pages bounded and owned.
5. Build `lsblk` and `blkid` on those queries. Confirm the selected internal card,
   multiple partitions, an unpartitioned filesystem, an empty optical drive,
   unformatted media, and unknown formats before enabling automatic mount.

Enumeration alone never publishes a usable volume or chooses the system disk.
The verified boot selection uses these same source records, not another scan
with different matching rules.

### Manual mount transaction

Start with an authorized manual mount on a disposable source. The same
transaction later handles persistent policy, USB automount, and tmpfs.

1. The caller identifies a source token, expected generation, requested mode,
   and desired Astra mountpoint. The supervisor verifies caller rights and
   policy, resolves identity conflicts, and reserves the name without publishing
   a directory that looks usable.
2. The provider pins and freshly validates the source, including partition
   window, identity, existing use, driver support, and effective mode. Reject
   changed media and conflicting mounts; never resolve a client-supplied Linux
   device path or arbitrary mount-option string.
3. Linux mounts below the private provider root using a format-specific
   allowlist of options. Retain the mounted root, verify actual format/mode and
   mount identity, and acknowledge a generation-safe provider object.
4. The supervisor registers rooted grants and atomically commits the binding
   into the live namespace. Readers see either the old catalog or the complete
   new entry. Public lookup and enumeration work after this commit; it is the
   publication boundary for all running clients.
5. Emit the namespace event and complete the mount operation. Native/POSIX
   callers, tools, and the desktop query the same committed record.

Refusal before the commit releases the reservation and closes/unmounts prepared
objects. If rollback itself fails, retain an inspectable failed mount and its
charges for cleanup; never publish success or lose track of a Linux mount.
Serialize conflicting operations by source/mount while keeping unrelated
inventory and volumes responsive. Cancellation that wins before publication
requires rollback when late Linux work retires.

A read-only mount request must not secretly authorize recovery writes.
Linux ext4 can replay its journal even with `ro`; use a non-writing policy where
consistent, otherwise refuse the mount with recovery-required status. Do not
publish a dirty filesystem as healthy merely by skipping required replay.
The configured writable system mount may perform authorized Linux recovery.

### Live visibility in already running processes

Maintain the public mount catalog as shared native namespace state, with
process-specific private bindings and data grants layered onto it. A runtime
namespace session is a live capability to this state, not a launch-time list of
volumes. Every application/command obtains that session through the normal
native startup/runtime contract, including a process that initializes the
Filesystem Kit only after a mount was published.

Replace startup-seeded assign tables as the source of current public mounts.
Trusted policy can register rooted access for existing sessions permitted to
use public/removable storage; newly mounted media cannot grant broader rights
than that policy allows. Private bindings and retained cwd remain separate.
Applications need no mount-specific callback to make a subsequent open work.

Path lookup and a new root enumeration use the committed current catalog.
Any client-side routing cache validates the namespace sequence; notification
loss forces refresh, and a stale binding cannot return false ENOENT or route to
replacement media. An enumeration already in progress may finish its bounded
snapshot; reopening or rewinding the directory obtains the latest catalog.
This update must not depend solely on desktop events or on child-process launch.

Do not add a control IPC round trip to every file read/write. Retained object
operations use their generation-safe token; namespace changes affect fresh
path resolution and grant revocation. Measure cache validation and fresh lookup
against the Phase 1 budgets, and implement live updates in the common native
facility so libc contains no second mount table.

The acceptance test starts a long-lived native process, a long-lived POSIX
process, a shell, and the desktop before attaching/mounting media. Each must
observe `/usb0` through fresh root enumeration and queries, and eligible
processes must open it without restarting. Include a command whose first
filesystem call occurs after publication, missed notifications, fork/exec,
unmount, and reinsertion under a different automatic name. The old cwd and open
descriptions must never change identity during these tests.

### Hotplug and policy execution

Process attach/change/remove as observations about current Linux state, not
trusted instructions to mount. Deduplicate/coalesce probe work for the same
source, reject late results from an old generation, and increment the inventory
sequence on committed changes. Detect receive overflow or discontinuity and
request reconciliation. A Linux-to-Astra notification wakes the existing
wait/IRQ path; neither the guest nor the desktop polls a directory continuously.

The mount authority owns one host inventory subscription and distributes bounded
native notifications through existing port/event mechanisms. A sleeping observer
does not occupy a filesystem worker. Client gap detection uses the provider's
own committed-change sequence; gaps in Linux's global udev sequence are not
alone evidence of loss because filtering can omit unrelated events. Linux
receive errors/overflow and provider restart trigger an authoritative rescan.

On attach, publish inventory first, finish generation-checked probing, then
evaluate `/system/config/mounts.conf`. Reserve `/dh0` from the verified boot
handoff, apply exact configured identity matches, and allocate the lowest free
numbered name for eligible removable volumes. Duplicate UUIDs, overlapping
rules, unsupported formats, or ambiguous media remain unmounted with a reason.
The shipped trusted removable rule mounts supported USB data volumes writable
from the allowlist. Optical mounts and explicitly read-only rules remain
read-only. A writable mount refusal leaves the source unmounted and inspectable;
an explicit read-only request is a separate operation.

On remove, mark the medium and dependent mount generations lost before accepting
further requests, withdraw bindings, notify sessions, and retire work through
the shared cancellation/lifetime machinery. Keep tombstones only as needed for
stale-token refusal and diagnosis; reclaim detailed records under the resource
budget. Release a name only after its previous mount and all retained Linux
resources have retired; object identities never recycle within an epoch.

Qualify actual optical media-change notifications independently of drive attach.
If the shipped drive/kernel does not reliably emit them, use bounded Linux-side
optical status checks and record that constraint; do not add guest busy polling.
Each inserted disc is a new medium generation even when its label/UUID matches
the preceding disc. Do not reset the drive generation for an ordinary label
update on unchanged media.

### Unmount safe removal and eject

Track file, directory, cwd, namespace-view, and in-flight references by mount
and owner in the canonical object/session records. A plain `umount` with live
client references returns busy without changing the usable binding. Safe-remove
enters quiescing, blocks new opens, reports owners, and requests release through
existing native lifecycle notifications. The provider's own root references are
distinguished from client references.

Seeing a mount in the public catalog does not itself pin the medium as a busy
client. Published bindings and provider-managed namespace-view references are
withdrawable by the mount authority after new opens are blocked. Explicitly
opened files/directories, cwd, and accepted I/O are the client references that
must drain; a running application with no such reference cannot prevent removal
merely because it saw the volume.

After clients release and work drains, synchronize writable storage, close
provider descriptors/views that retain the mount, and perform a real Linux
unmount. Only then remove the final binding and report safe removal. Flush or
unmount failure either restores mounted state when intact or retains a failed,
inspectable quiescing state. A cancellation cannot claim restoration after the
mount has actually been removed. Normal safe removal never substitutes lazy
unmount or forced discard for this sequence.

Device-level safe-remove checks every mounted partition, including externally
used mounts; removing `/usb0` alone does not make a two-partition drive safe to
disconnect. Eject waits for those dependencies and then invokes Linux's supported
media operation. Eject failure leaves the device unmounted and inspectable;
it does not pretend the disc left the drive. Runtime system-volume removal is
refused by this control path.

Provider restart creates a new epoch, invalidates all old operations/sessions,
re-enumerates sources, and reconciles actual Linux mounts before policy runs.
A guest-only restart preserves Linux ownership but re-registers trusted
namespace sessions rather than reusing dead guest owner IDs. Cleanup and
reconciliation share the shutdown implementation.

### Work packages and qualification order

| Package | Concrete result | Required gate |
|---|---|---|
| S1 Provider inventory and ABI | Linux discovery/probe component, dynamic records, snapshots, event sequencing, source tokens, and shipped dependency checks. Starts in Phase 1. | Attach/detach during initial enumeration, delayed probes, signature ambiguity, allocation failure, and source-generation reuse. No mounts performed. |
| S2 Native catalog and tools | Supervisor authority, live namespace sessions, query/subscription operations, `lsblk`/`blkid`, and public/private visibility. | Existing processes and late first filesystem initialization see catalog updates; unauthorized queries/data access cannot escape their scope. |
| S3 Manual mount | Typed mount request, name reservation, Linux mount object, rooted grants, atomic publication, and operation status/cancel. Requires Phase 2 ownership and sessions. | Mount a second disposable Linux-backed volume beside `/dh0`; failure/cancel at each transaction step leaves no usable half-mount or leaked Linux object. |
| S4 Unmount and safe removal | Canonical busy-owner accounting, quiescence, sync, actual unmount, whole-device dependency checks, and shutdown integration. | Two mounted partitions, held cwd/files, stalled I/O, failed sync/unmount, cancel races, and surprise removal; no false safe-remove success. |
| S5 Policy and USB hotplug | Load persisted rules, writable USB automount default, explicit read-only rules, names reserved through actual retirement, overflow/rescan recovery, and current-session updates. Completes Phase 3. | Two real USB devices, multiple partitions, duplicate UUIDs, unplug/reinsert, missed events, writable refusal, and native/POSIX use without restarting clients on DE25. |
| S6 Optical and desktop controls | Drive/media separation, ISO9660/UDF mounting, eject, volume UI, progress, and owner/error presentation. Completes Phase 4. | Physical optical drive: insert, replace disc, busy eject, failed eject, and detach while mounted; pre-existing apps observe every transition. |
| S7 Release reconciliation | Launcher/kernel/dependency packaging, restart recovery, budgets, documentation, and final legacy-engine removal. Completes Phase 5. | Exact-source QEMU and DE25 stress/fault/semantic gates under input, graphics, and audio load. |

First make inventory observable, then prove manual mount and safe unmount, then
enable automatic policy. Keep automount disabled until S3 and S4 pass. Reuse
the existing host-filesystem and stress tests for the new lifecycle, adding
deterministic event/probe/control fault seams where the current tests cannot
exercise a race. A USB/optical image proves format semantics; physical devices
are required to prove hotplug, loss, whole-device removal, and eject.

## Acceptance and performance gates

Run host/unit checks and builds on Beast, then exact-source QEMU gates, followed
by DE25 qualification through Beast. Test native calls first and the POSIX
adapter over those same operations. Each acceptance family includes success,
refusal, resource exhaustion, and cleanup where applicable.

QEMU gates run QEMU itself as the unprivileged caller on Beast, never as
root, through a drop-privilege helper (`tools/astra-qemu-root`) whose root
actions stay inside its own `/var/tmp` directory; QEMU `setpriv`-drops to
the caller before it ever executes (FS11). A gate that passes under this posture has exercised a
real Linux mount.

| Acceptance family | Required evidence |
|---|---|
| Authority | Raw-message and accelerated attempts to broaden rights, forge roots/owners, access another view, traverse symlinks/`..`, and use revoked tokens are refused. Read-only grants cannot mutate shared writable backing storage. |
| Bootstrap | Multiple partitions and attached devices, duplicate IDs, missing/corrupt media, generation changes, and unavailable providers cannot select the wrong system volume or start a dependent desktop. |
| Path resolution | Root and root dirfd, missing/non-directory intermediates, `link/..`, absolute/relative and cross-volume links, loops, trailing separators, mount-root `..`, and advertised name/path limits agree between native and POSIX. |
| Object semantics | Open/unlink, open/replacement, hard links, renamed cwd, duplicate/shared offsets, positioned I/O, append races, fork/exec inheritance, and concurrent close preserve identity and lifetime. |
| Metadata and tools | Stable IDs across aliases, genuine mode zero, timestamps/capacity, `ls -i`, recursive cycle detection, `df` deduplication, and directory-relative no-follow operations work. |
| Inventory and mounts | Two partitions on one USB drive and two drives, unpartitioned media, unknown formats, duplicate labels/UUIDs, notification overflow, mount rollback, and nonempty mountpoint refusal behave predictably. Already-running native/POSIX processes, shells, and the desktop observe new mounts without relaunch, including late first filesystem initialization. |
| Removal | Busy unmount, client release, failed flush, physical disconnect during read/write/enumeration, reinsertion, optical media change, eject failure, provider restart, and stale cwd/handles are exercised. |
| Durability | Atomic-save sequence, file and directory sync, clean shutdown/restart, provider termination, and controlled storage cuts verify acknowledged durable data after Linux recovery and offline checking where applicable. |
| Resource pressure | Byte/inode caps, metadata pressure, open-handle floods, full queues, stalled media, process death, and cancellation return charges and preserve recovery/UI responsiveness. |
| End-to-end apps | Fresh-image desktop/Terminal gate; Vim save and Lua execution; zsh and recursive tools; representative shipped games; mounts beyond `/dh0` accessed by both native and POSIX clients. |

The existing [host protocol test](../emu/qemu/test-hostfs.py),
[filesystem stress runner](../emu/qemu/test-filesystem-stress.py),
[native HostFS benchmark](../emu/qemu/benchmark-hostfs-native.c), and
[power-cut oracle](../emu/qemu/test-storage-power-cuts.py) are starting points.
The old power-cut runner observes guest block transitions; it must be adapted
to the Linux-owned storage boundary. Killing only the guest does not simulate
loss of the Linux page cache or a physical power cut.

Before changing the data path, capture the installed source/artifact identity
and DE25 baselines for cold/warm stat/open/close, batched directory scans,
sequential and random reads/writes, append, durable saves, concurrent cached
reads under a held miss, application startup, and guest/host memory use.
Record MC68040 cycles per operation, median/p95 latency, throughput, queue
depth, bytes copied, and host CPU load. Durability settings and datasets must
match across comparisons.

For existing workloads, the initial release regression budget is at most 10%
higher median/p95 latency and at least 90% of baseline throughput. Set explicit
cycle budgets from the measured baseline before accepting new native IPC or
user-copy hot paths. After Linux has acknowledged mount readiness or media
loss, the product target for Astra inventory/desktop updates is two seconds
under the qualified load. Linux probing/recovery time is measured separately
and must expose progress or failure rather than freeze the UI. Budget exceptions
require a recorded product decision with measurements, not an unmeasured claim
that host execution is faster.

Durability and confinement are mandatory even if the faster configuration omits
them. Full queues and device loss cannot produce busy polling on the guest.
Sustained tests must include storage traffic alongside input, graphics, and
audio, with representative filesystems and multiple mounted volumes.

## Production completion

Completion requires the exact release kernel/module set, Linux provider, QEMU,
ROM, native/POSIX libraries, manifests, mount policy, and data image to pass
together. Physical DE25 evidence must cover system storage, bounded tmpfs,
USB storage, and an attached optical drive; image mounts alone do not qualify
physical discovery or eject. Explicitly record unsupported media rather than
advertising Linux capabilities absent from the shipped kernel.

Retain source/artifact hashes, kernel and filesystem options, policy/budgets,
test commands, measurements, and fault outcomes under the durable evidence
store required by [Artifact policy](ARTIFACT_POLICY.md). Update Current State,
Devices and volumes, Storage and VFS, Filesystem Kit, NDK/POSIX documentation,
driver ownership, and shutdown documentation to the final implemented contract.

The release image must have one writable owner per storage filesystem, one
trusted authority contract, and one native client facility. Additional media
must enter through that facility rather than require a new guest filesystem
implementation or application workaround.

## Linux implementation references

- [Linux VFS](https://www.kernel.org/doc/html/latest/filesystems/vfs.html)
  describes directory entries, filesystem objects, and retained open files.
- [Linux tmpfs](https://www.kernel.org/doc/html/latest/filesystems/tmpfs.html)
  documents byte/inode limits and swap behavior. The shipped kernel must support
  the selected no-swap enforcement; otherwise a release-wide swap restriction
  must enforce the same RAM-storage contract.
- [Linux ISO9660](https://www.kernel.org/doc/html/latest/filesystems/isofs.html)
  and [Linux UDF](https://www.kernel.org/doc/html/latest/filesystems/udf.html)
  provide optical filesystem support; driver availability is a release gate.
- [POSIX pathname resolution](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap04.html)
  and [unlink](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unlink.html)
  define the compatibility semantics that the native substrate must support.
- [libudev monitor implementation](https://github.com/systemd/systemd/blob/main/src/libudev/libudev-monitor.c)
  documents processed device events and monitor integration.
- [libblkid low-level probing](https://www.kernel.org/pub/linux/utils/util-linux/v2.42/libblkid-docs/libblkid-Low-level-probing.html)
  and [libmount](https://www.kernel.org/pub/linux/utils/util-linux/v2.42/libmount-docs/)
  provide existing Linux probing and mount-management machinery.
- [Linux mount namespaces](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html)
  defines mount visibility and propagation; provider isolation must be explicit.
- [Linux ext4 mount options](https://www.kernel.org/doc/html/latest/admin-guide/ext4.html)
  documents journal replay on read-only mounts and the hazards of skipping replay.
