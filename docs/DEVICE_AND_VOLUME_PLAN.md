# Devices, volumes, and boot selection

Status: implementation plan. Slash paths are the only Astra and POSIX path
spelling. The internal SD card is the fixed Astra system disk. `/dh0` and
protected root links pass the fresh-image QEMU terminal gate; the expanded
root layout and logical `/ram` volume still need DE25 qualification. Validated boot-partition identity
and hotplug are not yet implemented.

## Implementation gap audit, 2026-10-04

The existing substrate is useful: `sw/kernel/device.c` registers devices,
issues exclusive leases, revokes owners and supports reset; its storage grows
with resources. `sw/kernel/block.c` validates DMA-backed requests, queues
completions and checks media/host generations. Userspace already has
`AstraBlockDevice`, `AstraLeaseBlock`, bounded partition windows, an MBR reader,
and the common VFS session/shutdown contract. These mechanisms should be
extended rather than replaced.

The remaining work is broader than exposing `/dev`:

One native inventory/mount manager chooses names by device type: the fixed SD
system volume is `/dh0`, USB storage uses `/usb0`, `/usb1`, and so on, and
optical drives use `/cd0`, `/cd1`, and so on. Optical classification takes
precedence over transport: a USB CD/DVD drive is a `/cdN` drive, not a USB
storage volume. Drivers report identity, media and I/O capabilities; they do
not each implement naming or mount policy. POSIX adapts this same namespace.

| Step | Concrete gap and change | Acceptance gate |
| --- | --- | --- |
| 1. Inventory and system-volume identity | `devices` sees granted capabilities, not an authoritative disk inventory. Add native device/media descriptors and a rights-checked inventory/change feed. Classify fixed SD, USB disk and optical media, with stable physical identity and separate media generations. Consolidate the independent first-`0x83` selection in Supervisor `volume.c` and storage `main.c` into one validated partition/UUID handoff. Reuse the MBR parser; explicitly reject unsupported GPT/extended layouts until their parsers exist. | Multiple/ambiguous partitions, duplicate UUIDs, malformed tables, changed media and missing system disk fail before writable mount; verify neighboring partitions remain intact. |
| 2. Multiple block devices | The kernel block engine has one global `device_state`; the syscall dispatch accepts only `ASTRA_DEVICE_ID_BLOCK0`. Platform access has one MMIO aperture. QEMU has one `AstraBlockState`, claims only `drive_get(IF_NONE, 0, 0)`, fixes sectors at 512 bytes and sets `no_cdrom`. Make those existing paths device-aware, with per-device geometry, queues, generations and leases. Linux remains the physical USB/SCSI driver owner. Update the versioned host transport and every producer/consumer together. | Read concurrently from the system disk and a second read-only device; reset/remove the second while preserving the first device's I/O and completions. Establish cycle baselines and budgets before new kernel paths become dependencies. |
| 3. Mount authority and tools | `AstraVfsMountOps` supplies shutdown callbacks, not a mount table. Supervisor `publish()`/assigns currently bind service roots. Extend that authority with mounted-volume instances, verified identity, source capabilities, labels, mountpoints and generation-pinned lifetimes. Apply `/system` mount policy there; add native-backed `lsblk`, `blkid`, `mount`, `umount` and `df` views. `/dev` is a capability adapter over the same inventory, not a second registry. | Conflicting/nonempty mountpoints, duplicate identity, denied grants and failed publication roll back. Aliases share one volume identity and capacity row; old handles never retarget. |
| 4. Hotplug and removal | Low-level generation checking exists, but QEMU currently reads media size once at startup and no common mount authority handles removal. Add Linux/QEMU media insertion/ejection notifications, revoke the affected mount generation and namespace publication, then close sessions/caches. Reuse ordinary flush/unmount for clean removal and report forced removal distinctly. | Remove during I/O, enumeration and cwd use; fail stale/cache-hit requests, preserve unrelated devices and mounts, reject generation reuse and exercise shutdown failures. |
| 5. Optical controls | CDFS now mounts images through VFS and accepts native block sources. Physical drives still need device discovery/read transport; TOC/session discovery, media-change detection and eject are native device operations supplied by Linux behind the versioned protocol. Only add raw CDDA transport when audio-CD support is required. | Real USB CD and DVD mounts on the DE25, repeated insert/eject, multisession selection if implemented, corrupt/unsupported media refusal, and a read-only guarantee. |

The smallest usable hardware milestone is steps 1–4 plus read-only optical
data transport: keep the SD system volume mounted, attach a second device,
publish a CDFS volume, list/read it, and remove it without affecting `/system`.
TOC/audio, formatting and partition editors do not block that milestone.
Arbitrary mountpoints, stable device/inode identity and the complete edge-case
gate below remain requirements for a finished general device layer. This
audit does not implement those missing facilities.

The optical namespace identifies attached drives: the first drive owns
`/cd0`, the second `/cd1`, and so on. Each attached drive publishes its empty
root even without a disc. Insertion classifies the medium and mounts a
supported data filesystem beneath that same root; ejecting the disc returns
the root to an empty directory. Unplugging the drive withdraws its root and
inventory entry. Surviving drives are not renumbered, and retired drive slot
names are not reused within the boot.

Each inserted disc has a fresh media/mount generation. Open files and cwd
handles below the drive root remain pinned to their original generation and
fail after ejection; a new path lookup can see the newly inserted disc.
Directory iteration must detect a disc change rather than reuse its old
cookies. The attached drive root itself remains usable while empty. Media
type, unsupported-format and mount-error status belong in the device
inventory, leaving an empty root empty. The current CDFS image service does
not implement this drive discovery/media transition flow.

Hotplug must update the native namespace of already-running authorized
applications. Today's Terminal ceiling and child mount list grant only
`CD0:r` at startup, and opening a new Terminal is required after starting the
image service. Finishing the manager includes a rights-checked live namespace
refresh and authority for additional permitted drives; hardcoding more mount
names in Terminal or implementing discovery separately in POSIX is not the
general device layer.

Qualify the full sequence with two drives: attach both empty, insert different
supported discs, eject/reinsert one without changing its `/cdN`, then unplug
it without renumbering the other. Reusing a filename on the second disc must
not make a stale file/cwd handle read that disc. Exercise disc changes during
directory iteration and I/O, plus unsupported media and mount failures.

## One namespace

`/dh0` names the first mounted hard-disk volume, **not** the raw SD controller
or an entire unpartitioned card. The initial DE25 implementation mounts the
selected ext4 partition of its internal SD card there. `/system` is a root-level
symlink to the selected boot volume (`/dh0` for this boot). Disk-backed root
conveniences, including `/apps`, `/commands`, `/home`, `/libs`, `/local`,
`/work`, and `/tmp`, link to their granted backing roots under `/system`.
`/work` and `/cwd` may point at the selected working root, and `/config` may
point at a private config scope; their targets are process-specific rather
than globally invented paths. `/tmp` is currently persistent on the SD
volume. Local command overrides
live in `/local/commands`; the shell searches
`PATH=/local/commands:/commands`. It does not need a union mount.

The same slash path is used by the desktop, native services, shell, libc, and
ported programs. `DH0:` and similar colon paths are not accepted. POSIX `/`
lists the namespace entries granted to the process: mounted volume roots such
as `/dh0`, and links such as `/system`, `/apps`, and `/commands`. `ls -l /`
shows those entries and each link target, not a flattened listing of every
volume's files. `ls /dh0/` shows that volume's root. Guessing a path grants no
authority. The native root is read-only to ordinary filesystem operations,
even for privileged processes: creating, renaming, or removing an entry
directly under `/` fails with a native read-only status (`EROFS` in POSIX).
Only the protected mount
manager can publish or remove root mounts and links through a separate
namespace operation. `mkdir /stuff` fails; `mkdir /home/stuff` succeeds only
when `/home` grants write access and its backing volume can accept the change.
The root links must be implemented once in the native VFS namespace, then
visible through native `stat`/`lstat`/`readlink` and POSIX adapters. A string
rewrite in libc or three hardcoded POSIX-only links would be a second
implementation and is not acceptable. Following a link cannot create
authority: grant checks apply to the link and its target. Loops, missing
targets, revoked media, and failed link publication must have negative tests.

Other mounted volumes will use `/dh1`, `/df0`, `/usb0`, `/cd0`, `/ram`,
`/nfs0`, and `/smb0` according to media class. These are transient mount
names. `/dev/disk0` names a whole block device, and `/dev/disk0p1` its first
partition: disk ordinals start at zero, partition numbers at one. A filesystem
directly on an unpartitioned device mounts from `/dev/disk0`, with no invented
`p0`. Volume ordinals count mounted filesystems of that media class, not disks:
two single-partition USB drives can expose `/dev/disk1p1` at `/usb0` and
`/dev/disk2p1` at `/usb1`. Two mounted partitions on the first USB drive take
`/usb0` and `/usb1`, making the second drive's volume `/usb2`. `lsblk` reports
the association. No fixed count of
devices or volumes is imposed beyond actual resources and ABI representation.
Do not recycle a removed name within one boot, because an old path must not
silently identify new media. `/dev` will be a rights-checked adapter over the
same native device services, not a second device model. Publish only endpoints
with meaningful device-file operations; keyboard, mouse, and display do not
gain raw nodes merely to make them appear in `/dev`.

One native mount table binds a device or service source, filesystem instance,
and mountpoint. It supports block, RAM, network, and synthetic filesystems
without assuming every source has a block device. Automatic root names are
conveniences, not the only possible mountpoints: an authorized mount may use an
empty directory elsewhere, but never hide that directory's existing contents.
`/home` and `/tmp` are stable writable destinations subject to their grants
and backing volumes; `HOME` and `TMPDIR` name them. The optional `ramfs`
service publishes a logical `/ram` volume through the same VFS backend and
service protocol as ext4 and hostfs. It allocates guest memory only as files
grow, enforces `max_bytes` from
`/system/config/services/ramfs/settings.conf` against data and metadata
together, and reports no-space or a POSIX-compliant short write at the cap.
Freeing or truncating files returns their charge. It has no raw block device
or preformatted image, so disabling the service retains no filesystem driver
in the kernel. `/tmp` becomes volatile only when its backing link is
deliberately moved to `/ram/tmp`. An absent or invalid config must not mount
an unbounded RAM volume.

A filesystem label is mutable display metadata. The desktop shows it when
present and unambiguous; otherwise it shows a fallback such as `DH0`. A label
never selects the boot volume or authorizes a mount. Stable policy keys are
verified filesystem UUIDs (or another explicitly selected stable identity);
duplicate UUIDs and conflicting names require a diagnostic and explicit
resolution. A partition type byte such as MBR `0x83` is not identity.

One mounted volume reached by several paths has one device/inode identity,
free-space result, and open-handle lifetime. Same-volume rename must work;
cross-volume rename must return the cross-device error. Rebinding a name must
not retarget an already-open file or current directory. Current directories
must eventually pin a generation-checked VFS directory object across
fork/exec, rather than rely only on a path string.

## Boot chain and policy

The DE25 HPS currently runs U-Boot, then Linux, then the Astra QEMU ROM. The
ROM contains Axiom and the initial supervisor. No additional Astra bootloader
or boot-order manager is needed for the fixed-SD design. The internal SD card
is `/dev/disk0`; its selected system partition mounts at `/dh0`, and `/system`
points there. Other media are data devices discovered after boot and never
silently replace the system volume. Storage failure stops normal desktop
startup.

The supervisor and storage service currently select the first Linux-type MBR
partition independently. Replace that interim rule with one checked handoff:
the internal-card identity, media generation, exact partition window,
filesystem type, and verified filesystem UUID. Both consumers validate the
same selection before writable mount. An absent, ambiguous, mismatched, or
corrupt system partition stops boot with a diagnostic; it must not fall back to
another partition or removable device. A partition type byte such as MBR
`0x83` is not sufficient identity. U-Boot/Linux continue to load the Astra ROM
from their existing fixed source; selecting a different Astra volume is not
equivalent to booting a different ROM.

After `/system` is mounted, a supervisor-owned mount-policy file on `/system` is the
fstab equivalent for data volumes. It maps verified UUIDs to preferred mount
names, expected filesystem type, access mode, and optional aliases. `/system`
itself comes from the fixed internal-card selection, not this file, avoiding a
bootstrap loop. One native VFS authority applies policy; Linux-side helpers
only provide block transport.
On removal, revoke the media generation and fail in-flight I/O and stale
handles. A clean unmount flushes and quiesces writes; forced removal reports
possible data loss. CDFS, VFAT, AstraFS, RAM, NFS, and SMB adapters all enter
through the same VFS mount contract when implemented.

## Tools and gates

Use upstream POSIX utilities unchanged where possible. Add only Astra-specific
device and mount tools, each with one job: `lsblk` for inventory, `blkid` for
validated filesystem identity, `mount`/`umount` for mount state, `mkfs.ext4`
for formatting, `fdisk` for partition editing, `fsck.ext4` for offline repair,
and `bootctl` for boot policy. `devices` remains the IRQ/device diagnostic,
not a disk inventory. `df` lists mounted filesystems once each with capacity
and free space; aliases such as `/system` do not duplicate `/dh0`. Attached but
unmounted or unformatted devices appear in `lsblk`, not `df`, because they have
no mounted filesystem usage to report. Formatting and partition edits require
privileged raw
block authority, an exact target identity, an unmounted/in-use check, and
readback; never forward a pathname to a Linux tool as the native operation.

Qualification order:

1. Complete the slash-path migration and initial POSIX command gate,
   including Vim creating/saving a Lua file and Lua executing it.
2. Bind `/dh0`, then implement the three root symlinks in the native VFS with
   positive and negative resolution, rights, `lstat`, `readlink`, and root
   listing tests. Boot a fresh image and run the complete terminal gate.
3. Add volume UUID/label reading, device and partition inventory, and a
   single checked internal-SD system-partition handoff. Test multiple and
   malformed partitions, duplicate IDs, missing media, generation change,
   allocation failure, and storage-start failure. Verify on QEMU and the DE25.
4. Add persistent mount policy, hotplug, and privileged tools one at a time,
   with power-cut and neighboring-partition integrity tests before any
   destructive operation reaches hardware. Add alternate-media boot only if
   explicitly requested later; it requires a separate trust and recovery
   design, not an implicit change to this fixed-SD contract.

An image is published only from current source products. Host/unit tests and
an exact-source Beast cross-build precede QEMU; DE25 release qualification
follows a fresh QEMU pass. Record source and artifact identities so a stale
library, ROM, or image cannot masquerade as a successful test.

## Filesystem edge-case gate

Each case needs a success case and a refusal/failure case. Exercise the native
VFS contract first, then the POSIX adapter on QEMU, and the storage-sensitive
cases on the DE25. In particular:

- Path walk: `/`, repeated separators, `.`, `..` across mount roots, trailing
  separators, path/name length, invalid UTF-8, a nonexistent intermediate
  component, and `ENOTDIR`. Follow symlinks component by component so
  `link/..` means the parent of the resolved target, not a lexical shortcut;
  test dangling links, loops, absolute/relative targets, and authority checks
  after each hop.
- Namespace and mounts: creation/removal/rename at `/` fails read-only;
  mutations under writable volumes succeed. Reject mounting over a nonempty
  directory, conflicting mount names, and duplicate UUID policy. Verify
  aliases do not add duplicate `df` rows or change device/inode identity,
  and cross-volume rename/link return `EXDEV` without changing either side.
- Files and directories: exclusive create, truncate, append, sparse/large
  offsets, short I/O, zero-length I/O, full filesystem, read-only media,
  unlink-while-open, rename-over-existing, nonempty `rmdir`, directory
  iteration/rewind during changes, and concurrent readers/writers. Verify
  failed operations leave source data and metadata intact.
- Media lifecycle and recovery: remove media during I/O, stale open handles,
  directory/cwd after unmount, no name reuse within one boot, failed flush,
  crash/journal replay, corrupt partition tables, UUID collisions, and
  neighboring-partition integrity after any format or repair operation.

The current VFS tests cover several file, link, union, and path refusals; the
new root tests cover direct namespace mutation. Cross-mount `..`, resolved
`link/..`, namespace symlinks, mount lifecycle, and hotplug are not yet
qualified. They are release gates, not presumed working behavior.
