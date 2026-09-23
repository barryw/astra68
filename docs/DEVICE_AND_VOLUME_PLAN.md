# Devices, volumes, and boot selection

Status: implementation plan. Slash paths are the only Astra and POSIX path
spelling. The internal SD card is the fixed Astra system disk. `/dh0` and
protected root links pass the fresh-image QEMU terminal gate; the expanded
root layout and logical `/ram` volume still need DE25 qualification. Validated boot-partition identity
and hotplug are not yet implemented.

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
