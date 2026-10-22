# Devices, volumes, and boot selection

The [filesystem production PRD](FILESYSTEM_PRODUCTION_PRD.md) owns the complete
device, partition, namespace and removal contract. This page describes the
implemented native model; [Current State](CURRENT_STATE.md) distinguishes
software gates from physical release acceptance.

## Selection and identity

The internal SD card remains the fixed system disk. The Linux provider verifies
the configured system source; missing, ambiguous or corrupt system storage
refuses normal startup rather than selecting another attached disk. Supervisor
bootstraps read-only through this provider and transfers responsibility to
HostFS. The DE25 boot chain remains U-Boot, Linux, QEMU/MC68040, Astra ROM and
Supervisor.

Device, inserted medium, source, mounted instance and open object are distinct
identities. Every public reference carries its provider epoch, ID and
generation. Labels are display metadata, never authority. Catalog pagination
refuses a changed snapshot, and subscribers refresh after coalesced invalidation.

Mounted names are `/dh0`, `/dh1`, `/usb0`, `/usb1`, `/cd0`, `/cd1`, `/ram0`,
and so on. Ordinals count mounted volumes of a class, not physical devices.
Names are reusable only after actual retirement; a new incarnation cannot
reuse an old reference. `/system` is a protected namespace alias for `/dh0`
and exposes link metadata. Independently granted roots such as `/home`, `/apps`,
`/commands`, `/libs`, `/local`, `/config`, `/cwd` and `/work` report directories
and retain their own rights. Their backing location does not make them aliases
or create independent mounted instances.

## Mount and removal

Linux discovery and probing feed one provider catalog. Supported writable USB
filesystems default to writable mounts, subject to filesystem and device
restrictions. Optical volumes are always read-only. RAM uses bounded tmpfs.
An ISO mount retains an already-open native file and its actual backing inode;
it does not accept an application-supplied Linux path.

`volumes`, `volumes --all --references`, `df`, `mount`, `unmount` and `lsof`
use native inventory, root brokers and administrator/usage capabilities.
`mount --image /native/file.iso` mounts an ISO; `unmount --eject /cd0` performs
safe unmount before physical eject. Successful removal waits for actual cleanup.
Failed cleanup remains visible for retry; the running system volume cannot be
unmounted. See [Filesystem Kit](FILESYSTEM_KIT.md) for command/API details.

Desktop watches the same catalog, shows mounted volume labels (or class names),
and opens Atlas with an exact incarnation on double-click. Atlas lists the
selected root's files and folders through a readonly native grant. Desktop's
Unmount context action fences admission and reports exact retained objects when
busy; it removes the icon after confirmed removal. Device loss withdraws stale
results and cannot redirect an existing window or CWD to replacement media.

Current discovery creates a separate source for each Linux-reported partition
and can mount multiple supported partitions on one device. It does not yet
publish partition number/bounds in the native catalog, show unformatted or
unsupported sources as `NDOS` on the desktop, or provide formatting and
partition editing. The [production PRD](FILESYSTEM_PRODUCTION_PRD.md) now
requires `NDOS` error presentation and a native Disk Setup application with
exact-reference destructive-operation checks. The 2026-10-22 physical USB
test qualified one ext4 partition, not multi-partition automount or editing.
The provider currently accepts ext4, FAT32 and exFAT data volumes, ISO9660/UDF
optical media and tmpfs. Current source also recognizes FAT12/FAT16 and mounts
them read-only, admits ISO9660/UDF on USB read-only, and mounts NTFS on
fixed/USB data partitions read-only. Those additions pass focused Beast
software gates and an exact-kernel NTFS3 module build, but have no coherent
guest or physical gate. Other cross-platform formats need individual qualification.
[AstraFS](ASTRAFS.md) remains a design, not an available format.

## Acceptance limits

Real provider ISO mounts, RAM remounts, native/POSIX access, busy refusal,
namespace refresh, source retirement and HostFS recovery have software gates.
Physical USB ext4 read/write passed on 2026-10-22; FAT12/16, FAT32, exFAT and
multiple simultaneous partition mounts still lack physical qualification.
CD/DVD remains untested without a connected drive and disc; image mounts do
not qualify physical discovery, optical media change or eject.
The final kernel must ship the supported Linux drivers and pass together with
the exact provider, QEMU, ROM, libraries, policy and preserved data image.

Network filesystem mounts are outside the current work.
