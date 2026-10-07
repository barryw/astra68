# Devices, volumes, and boot selection

Status: superseded as the production design by
[the filesystem production PRD](FILESYSTEM_PRODUCTION_PRD.md), which now owns
the device/partition/volume model, boot-selection rules, namespace grants, and
mount/hotplug/eject lifecycle (PRD FS01–FS06, FS07–FS11, FS12–FS18, FS25–FS31,
FS39–FS41). This page summarizes what is retained and links to the PRD rather
than restating it. The
["Current implementation (to be replaced)"](#current-implementation-to-be-replaced)
section below is the only part of this page describing what actually runs
today.

## What the PRD retains from this plan

Per the PRD's architectural-authority section, these rules carry forward
unchanged and are specified in full there, not here:

- The internal SD card is the fixed Astra system disk; selection is explicit
  and verified, never enumeration order (PRD FS07–FS08).
- Devices, inserted media, partitions, filesystems, mounted instances,
  namespace bindings, and open objects are distinct identities (PRD FS01).
- Naming: `/dev/disk0`, `/dev/disk0p1`, and volume conveniences `/dh0`,
  `/dh1`, `/usb0`, `/cd0`, `/ram` (`/df0` reserved for an enabled floppy
  provider); ordinals count volumes of a class, not physical devices
  (PRD FS02).
- One native mount authority; labels are mutable display metadata and never
  grant authority; stable policy keys are verified filesystem UUIDs
  (PRD FS04).
- Names are not recycled within a boot; a reinserted medium gets a new mount
  generation (PRD FS03).
- No alternate-media boot fallback: missing, ambiguous, or corrupt system
  media stops normal startup with a diagnostic rather than falling back to
  another partition or device (PRD FS08).

## What changes

- **Linux, not Astra, does the mounting.** The device/medium/partition/mount
  records the PRD defines (FS01, "Inventory records and control operations")
  are owned by the Linux provider; Astra's supervisor owns policy and
  published bindings over those records, not a second guessed copy of them.
- **Grants replace the old display-only root links.** `/system`, `/home`,
  `/apps`, `/commands`, `/libs`, `/local`, `/config`, `/cwd`, and `/work`
  become independently scoped directory bindings with their own rights;
  `lstat` reports directories rather than invented symlinks (PRD FS13). This
  deliberately replaces this document's earlier "disk-backed root
  conveniences... link to their granted backing roots" description.
- **CDFS is gone, not deferred.** Optical media is a Linux ISO9660/UDF mount
  like any other Linux-mounted volume; there is no guest CDFS adapter to
  finish (see [Storage and VFS](STORAGE_AND_VFS.md#superseded-by-the-prd)).
- **`/ram` is Linux tmpfs**, not the guest `ramfs` service described below
  (PRD FS36).
- **Hotplug, unmount, safe-remove, and eject** get one real lifecycle
  (discovered/unmounted → mounting → mounted → quiescing → unmounted or
  lost/failed) driven by actual Linux mount state, replacing this document's
  "storage services have shutdown hooks" starting point (PRD FS25–FS31).

## Current implementation (to be replaced)

What actually runs today, verified against source:

- **Namespace.** `sw/userspace/supervisor/src/vfs_host.c`
  (`bind_standard_assigns`) binds `DH0`, `SYSTEM`, `SERVICES`, `STARTUP`,
  `CONFIG`, `LIBS`, `APPS`, `HOME`, and `COMMANDS` as assigns into the one
  storage-service volume. These are assign-table entries with independent
  rights, not separate mounted volumes and not genuine symlinks — matching
  the PRD's own description of this gap ("Some conveniences report synthetic
  symlink metadata while operations use independent grants").
- **System-volume selection.** `sw/userspace/supervisor/src/volume.c`
  (bootstrap) and `sw/userspace/services/storage/main.c` (the running
  service) each independently scan the MBR for the first Linux-type
  partition and mount it with guest lwext4; there is no verified UUID/
  identity handoff between them yet, and no persisted
  `/system/config/mounts.conf` policy file exists in the tree.
  `/dev/disk0`-style naming, `/dev` as a rights-checked adapter, `/usb0`,
  `/cd0`, device/partition inventory, and hotplug are design in the PRD, not
  implemented.
- **Boot chain.** The DE25 HPS runs U-Boot, then Linux, then the Astra QEMU
  ROM containing Axiom and the initial supervisor, unchanged from this
  document's earlier description; nothing above changes that chain.
- **Tools.** No `lsblk`, `blkid`, `mount`, `umount`, or safe-remove/eject
  tools exist yet; inventory and mount control are PRD FS39 work.

See [Storage and VFS](STORAGE_AND_VFS.md#current-implementation-to-be-replaced)
for the HostFS, storage-service, and RAM-storage facts this namespace sits on.
