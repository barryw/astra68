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
and can mount multiple supported partitions on one device. Current source now
publishes partition map, number, byte offset and capacity in native catalog
version 6; a distinct detected type keeps unsupported filesystems separate
from unformatted `NDOS` sources. Desktop presents nonmountable source icons
with specific open errors. Native Disk Setup ships a physical
device/partition inventory using shared Interface Kit controls, with exact
reference launch from a physical volume's desktop context menu. It combines
complete catalog and usage snapshots only when their catalog sequences agree,
then reports mounted volumes and retained resources for the selected disk or
partition. Disk Setup now sends typed exact-device MBR/GPT initialize,
partition create and delete requests through native Volume protocol 4 and
Filesystem Kit 15.2. Its size control uses MiB; Filesystem Kit finds the
first aligned free extent from coherent inventory, and the provider converts
byte geometry using the observed disk's logical sector size. All edits
require `ERASE` and refuse mounted siblings. The provider retains
`PARTITIONING` through worker completion, source retirement and Linux map
reread. Native GPT edits, post-edit rediscovery and FAT32/exFAT formatting
passed private DE25 physical gates. Native MBR initialize, FAT32 create,
format, access and delete passed a later physical DE25 gate on the disposable
SanDisk; the restored GPT fixtures passed native access checks. The
[production PRD](FILESYSTEM_PRODUCTION_PRD.md) requires exact-reference
destructive-operation checks. The 2026-10-22 physical USB
test qualified one ext4 partition. A 2026-10-23 private DE25 run qualified
concurrent ISO9660, FAT12 and ext4 partitions on the same GPT USB device;
later native partition edits passed the physical gates recorded in
[Current State](CURRENT_STATE.md).
The provider currently accepts ext4, FAT32 and exFAT data volumes, ISO9660/UDF
optical media and tmpfs. Current source also recognizes FAT12/FAT16 and mounts
them read-only, admits ISO9660/UDF on USB read-only, and mounts NTFS on
fixed/USB data partitions read-only. Those additions pass focused Beast
software gates and an exact-kernel NTFS3 module build. FAT12 and USB ISO9660
also passed the private physical gate; FAT16 and NTFS passed a later private
DE25 guest/physical gate. Other cross-platform formats need individual qualification.
[AstraFS](ASTRAFS.md) remains a design, not an available format.
Disposable Beast provider gates exercise USB-class ISO9660/UDF, FAT12/16 and
NTFS as real loop block devices through probe, mount, write refusal, root
access and cleanup. The same fixture tests FAT32/exFAT writable creation,
readback after unmount/remount, deletion and cleanup. FAT32/exFAT also pass
physical DE25 native guest and offline durability gates. FAT16/NTFS now pass
private DE25 native read, read-only mount and Atlas root gates under the
verified `storage2` kernel. The selected production release includes these
formats; ISO9660 on physical DVD-R passed native read and Atlas gates. CD-R
media remains unqualified.

Before this inventory change, Haiku's
`src/kits/storage/disk_device/Partition.cpp` and
`src/apps/drivesetup/MainWindow.cpp` were reviewed. Haiku exposes partition
number, offset, size and free regions through its shared disk-device model;
DriveSetup consumes those objects rather than reparsing paths. Astra adapts
that pattern to its single provider-owned Volume catalog and exact generations.
Before designing native edit controls, Haiku's
`src/apps/drivesetup/MainWindow.cpp` action gating (`_UpdateMenus()`),
initialization and `CommitModifications()` flow, and
`src/kits/storage/disk_device/Partition.cpp` capability/validation methods
were reviewed again. Its separation of disk and partition actions, refusal of
mounted or read-only targets, and validation before commit are adopted. Astra
must additionally revalidate an exact native device/partition reference and
generation inside its one provider immediately before any host mutation;
desktop labels and Linux path strings must never become authority.
For the native disk CLI and shared DOS API, Haiku's read-only
`src/kits/storage/disk_device/DiskDevice.cpp` (`PrepareModifications()` and
`CommitModifications()`) and `src/apps/drivesetup/DiskView.cpp` were also
reviewed. Astra retains one authoritative provider edit path in
`filesystem.library`; `dos.library` combines coherent catalog and usage
snapshots, applies the same GUI/CLI selection policy, and dispatches those
existing exact-reference format and partition calls. Both clients poll one
asynchronous job contract, then refresh catalog identities after completion.
Catalog partition admission also checks each extent against its parent's known
device capacity. Discovery keeps the device alive when a malformed partition
exceeds that capacity and accepts a later corrected observation; it never
offers that invalid extent as a mount or format target.
Mount admission refuses overlapping physical source extents on the same medium
while either source has an active or retained mount. Sources without known
bounds conservatively overlap; disjoint partition mounts remain concurrent.
The fence spans retired media on the same physical device until their Linux
mounts detach, so a new media generation cannot overlap retained old mounts.
Automount leaves a conflicting source unmounted and visible after `EBUSY`.
Persistent USB name reservations use verified device serial and partition
number, or partition UUID when serial is unavailable. Mount publication waits
for atomic state-map commit; retained mounts continue reserving their active
names until cleanup. Haiku's `src/servers/mount/AutoMounter.cpp` restores prior
partition mount settings from saved device/partition identity. Astra adapts
that restoration to native `/usbN` paths and exact mount generations.
Disk capacity, partition-map and write-protection changes retire the current
medium and require new partition admission. Per-partition write-protection
changes retire that source before it can mount again.
Filesystem Kit now exposes exact snapshot record/ancestor lookup, so Disk Setup
can group the native device, medium, partition source and mount records without
duplicating graph traversal. The catalog/record ABI is 15 and its exported
symbol namespace is coherently `ASTRA_FILESYSTEM_15.0`.
Desktop retains the full volume graph while indexing visible icons; this keeps
physical source ancestry available for mounted-volume actions. The provider
publishes observed block-device write protection on devices and sources and
refuses writable mounts from a read-only source. A partition also inherits
its parent disk's write protection. Effective read-only filesystem policy is
reported separately from physical write protection, so writable FAT16/NTFS
media can still be considered for a qualified reformat after unmount. Disk
Setup reports both states.

### Historical implementation checkpoints

The following design notes and trial results record how the native edit path
was built. [Current State](CURRENT_STATE.md) owns current release status.

Disk Setup must receive a separate destructive administration capability;
Desktop and Terminal retain only their existing mount/unmount authority. Its
native requests will identify a catalog device or partition by exact
epoch/ID/generation and typed map, extent, filesystem and label fields. HostFS
and QEMU forward those requests to the same Linux storage provider; neither
the application nor the guest chooses a Linux device path or command string.
The provider must recheck device identity, disk sequence, reserved system-disk
ancestry, write protection, every sibling mount and holder, and extent bounds
at admission. A bounded worker performs accepted writes while the provider
continues serving filesystem I/O. Discovery must suppress stale probes during
the edit and publish fresh objects only after worker completion. A failed
format or table edit must remain visible with its actual result. This is the
remaining implementation seam, not an authority granted by the current
inventory app.

The formatter must operate on the already verified exclusive block descriptor.
On 2026-10-23 a disposable 128 MiB Beast loop-device gate wrote an MBR through
an inherited `/proc/self/fd/9` and formatted its FAT32 partition through the
same kind of nonexclusive descriptor; probe returned `vfat`. A second gate held
`O_RDWR|O_EXCL` on a fresh loop device and ran stock `mkfs.fat` against its
inherited `/proc/self/fd/3`. `mkfs.fat` failed with `Device or resource busy`
because it reopens the path. Dropping exclusivity between validation and the
formatter's open would leave a mount/identity race. Haiku's
`src/add-ons/kernel/file_systems/fat/mkdos.cpp` instead initializes from the
supplied `fd`. Astra's FAT/exFAT format implementation needs that descriptor
model, whether by adapting established formatter code or adding an inherited-FD
entry point to a pinned formatter. No current Disk Setup action may invoke the
stock path-reopening tool as a destructive workaround.
The pinned implementations are now under `third_party/storage-format/`:
dosfstools 4.2 and exfatprogs 1.4.3 accept a private inherited exclusive
block descriptor and preserve their upstream format logic. A Beast loop gate
formats FAT32 and exFAT, rejects regular and mounted targets, and passes both
fsck tools. A separate exclusive-FD `sfdisk` loop edit with `--no-reread
--force` publishes a partition after the descriptor closes. Beast builds all
four native host and DE25 AArch64 formatter/checker executables from verified
source archives; the target formatter binaries load with the DE25 sysroot.
Both AArch64 formatters also passed the held-descriptor format gate under
Beast QEMU user mode, with FAT/exFAT checked by the host fsck builds.
Partition-edit groundwork now has a provider-only exact parent-disk opener.
It checks catalog generation/class/geometry against the current discovery
observation, reserved system ancestry and physical write protection, then
opens the exact block device `O_RDWR|O_EXCL` and verifies its disk sequence,
read-only flag and capacity. The real-loop provider gate covers stale identity,
exclusive ownership and mounted-device refusal; full format/provider and
discovery gates plus the AArch64 provider build pass on Beast. This opener
does not authorize a partition edit by itself: sibling mounts/holders,
bounded worker execution, table reread and a typed native request still need
implementation. A non-root `sfdisk` experiment on Beast failed to reopen the
held descriptor through `/proc/self/fd/3` with `Permission denied`; dropping
exclusivity to make that path work is not acceptable. A fixed-command,
provider-only worker now retains the descriptor while stock `sfdisk` runs
with the privilege needed for its reopen. It accepts typed MBR/GPT
initialize/create/delete operations, validates bounds against actual block
geometry, and never accepts a client path, command or script. The actual
worker passed MBR and GPT edits on disposable Beast loop media, followed by
a separate kernel reread after the descriptor closed. Provider lifecycle
admission, native request, and physical gate are still open.
The exact parent-disk opener also checks canonical device-wide edit admission:
active descendant mounts or retained cleanup, concurrent sibling format,
system disks and physical write protection all refuse before the exclusive
open. The provider's resource gate also rejects queued/running probes, live
workers and retained mount/format resources before that open. Fencing discovery
during an edit, retiring old sources and rereading kernel partitions remain open.
Current unshipped candidate adds separate `DISK_ADMIN` authority only
to Disk Setup. Native VOL protocol 3 forwards exact-source FAT32/exFAT requests
through HostFS and QEMU to provider protocol 5. Provider retains the exclusive
descriptor through an asynchronous non-root formatter and reports the result
using the admitted old source generation. Disk Setup composes shared fields,
labels and buttons for label choice, filesystem choice and typed `ERASE`
confirmation. Host, MC68040 HostFS/Disk Setup, QEMU host/DE25 build and provider
loop gates pass. Fresh native QEMU boot launches Disk Setup with the new
`DISK_ADMIN` grant, and the Volume UI lifecycle regression passes. Native
FAT32/exFAT formatting passed on user-authorized physical SanDisk
`/dev/sda3` and `/dev/sda4` in an isolated DE25 trial. Both mounted writable,
passed native write/read/remove, and passed stopped-media read-only fsck.
Desktop showed both labels; Atlas opened the exFAT root. That trial found
exFAT automounted after format while FAT32 needed an explicit native mount.
Haiku's
`src/servers/mount/AutoMounter.cpp` was reviewed for this gate: it attempts
mount on media-add/change and respects prior manual unmount state. Astra's
provider retains that distinction. A focused provider test reproduced the
missed retry when a sibling source temporarily held format exclusivity. The
discovery loop now leaves a formatting source unattempted and retries an
`EBUSY` mount after exclusivity clears. The test failed before the fix and
passed afterward; full real loop and discovery gates passed on Beast. A fresh
coherent candidate `e8a19a632becd5449e9f741fedf3dbdb009f48f1f85aa5fd35b93560c965139d`
then reformatted physical `/dev/sda3` to FAT32 and exFAT through native Disk
Setup. Both automatically mounted `/usb0` writable, passed native
write/read/remove, and showed their label on Desktop; exFAT opened in Atlas.
Neither format needed manual mount or host udev trigger. Guarded DE25
restoration passed with production image and selector unchanged. Native
partition-map edits,
physical optical media-change/eject and production deployment remain open.

## Acceptance limits

Real provider ISO mounts, RAM remounts, native/POSIX access, busy refusal,
namespace refresh, source retirement and HostFS recovery have software gates.
Physical USB ext4 read/write first passed on 2026-10-22. A private DE25
catalog-4 trial on 2026-10-23 qualified concurrent ISO9660 (read-only), FAT12
(read-only) and ext4 (read/write) partitions from one GPT USB stick, plus an
NDOS desktop icon and Atlas root enumeration. A later user-authorized
disposable-media trial repartitioned that USB stick into FAT32, exFAT and
blank NDOS sources. The matching candidate mounted FAT32 and exFAT
concurrently writable; native Terminal writes survived provider teardown and
passed offline readback/fsck. Both volumes safely unmounted, lost their
Desktop icons, remounted from exact sources, recovered their files and opened
in Atlas. A later guarded trial under the exact `storage2` kernel mounted
all four physical partitions at once: FAT32 and exFAT writable, FAT16 and
NTFS read-only. Native Terminal read seeded FAT16/NTFS files, attempted writes
left no files, Linux mount namespace showed `ro` for both, and each desktop
icon opened Atlas with one root entry. The original runtime and user-data
image remained selected and were restored after the private trial.
Physical USB DVD discovery, read-only UDF automount and native root enumeration
passed on DE25 with a `DVD_VIDEO` disc. Later physical media change and eject
passed on the selected release. A burned physical ISO9660 DVD-R then passed
`/cd0` read-only automount, Desktop and Atlas root, nested directory and
multi-sector file reads with exact image/file hashes. CD-R media remains
unqualified. Image mounts do not qualify these physical gates.
The inactive `e8a19a63` candidate also passed a guarded physical four-partition
trial on the SanDisk Cruzer: FAT32 plus three exFAT partitions mounted
concurrently writable, with native create/read/remove on every mount and four
named desktop icons. A shell CWD on `/usb0` appeared in `lsof`, exact holders
blocked unmount, and leaving the directory allowed unmount and native
exact-source remount without disturbing three sibling mounts. Disk Setup
showed all four active mounts and disabled format controls for selected
partition 3. Production image and selected release were unchanged after
guarded restoration; no candidate panic appeared. Later native Disk Setup
partition-map edits and post-edit rediscovery passed physical gates recorded
in [Current State](CURRENT_STATE.md).
The final kernel must ship the supported Linux drivers and pass together with
the exact provider, QEMU, ROM, libraries, policy and preserved data image.

Network filesystem mounts are outside the current work.
