# Astra storage and VFS

Status: superseded as the production design by
[the filesystem production PRD](FILESYSTEM_PRODUCTION_PRD.md). Linux now owns
filesystem machinery; this page summarizes what that means for the VFS layer
and links to the PRD for the full contract (FS01–FS46) instead of repeating
it. The ["Current implementation (to be replaced)"](#current-implementation-to-be-replaced)
section below is the only part of this page describing what actually runs
today.

## Architecture

Linux owns filesystem machinery: mounts, formats, partition probing, caching,
and journaling. Astra owns the client experience: its native API,
process-visible namespace, named volumes, grants, mount policy, desktop
behavior, and diagnostics. Native and POSIX applications share one native
client facility over that Linux-backed storage; there is no separate guest
filesystem implementation to keep in sync with it.

The trusted boundary, per the PRD's ownership table and FS15: grants bind
authenticated owners to rooted directory objects and rights, and are enforced
in the guest HostFS service using a kernel-stamped sender owner on every
request, never one supplied by the client. On the Linux side, each rooted
session is confined with `openat2` `RESOLVE_BENEATH`, so `..` and symlink
traversal cannot walk a session out of its root. QEMU never trusts a
client-supplied owner field.

Kept from the earlier device/volume plan, per the PRD's architectural
authority section: fixed internal-SD system selection, separate device and
volume identities, one native mount authority, labels as mutable display
metadata only, protected namespace publication, generation-safe removal, and
no alternate-media boot fallback. See
[Devices and volumes](DEVICE_AND_VOLUME_PLAN.md) for the namespace and naming
rules built on those.

## Superseded by the PRD

This document used to specify the production filesystem as guest code. That
design is retired, not merely deferred:

- **Guest lwext4 as the production writable engine.** Adopted 2026-08-05 and
  measured extensively — see `third_party/lwext4/ASTRA_VENDOR.md` and
  `sw/userspace/storage/lwext4-eval` for that record, and this document's git
  history for the performance/memory baselines that used to live here. Linux
  now mounts ext4 directly; the PRD's cutover (FS45–FS46) removes the guest
  ext4 link entirely.
- **Guest CDFS (optical media).** That work was removed from `main`; it is
  archived on branch `feat/cdfs`. Optical media becomes a Linux ISO9660/UDF
  mount, published the same way as any other Linux-mounted volume (PRD FS01,
  scope table).
- **Guest RAMFS as the production RAM engine.** `/ram` becomes a Linux tmpfs
  mount with explicit byte/inode limits and no swap (PRD FS36–FS38), not a
  guest-side allocator.
- **Block-only Linux ownership.** The layering below — a synchronous block
  facade, a lease-backed block device, an Arty bring-up image — was built on
  the assumption that Astra parses the on-disk format and Linux only moves
  blocks. Under the PRD, Linux performs the mount, cache, and journal; Astra's
  job is the namespace, grants, and policy on top of it.

## Filesystem handler contract (interface retained, implementations retiring)

VFS still owns path parsing, `.`/`..`, assign expansion, mount crossing,
access policy, current directories, and process-visible handles; a handler
still receives one validated, bounded path component at a time. Names remain
UTF-8, case-sensitive, and byte-exact — a foreign filesystem's own
case-folding behavior is a property it reports, never something the Kit
normalizes. These rules carry forward unchanged. What changes under the PRD
is what sits behind that contract: a Linux-backed HostFS session with dynamic
volumes and rooted grants, rather than a guest-side format driver per media
class.

## Current implementation (to be replaced)

What actually runs today, verified against source:

- **Bootstrap mount.** `sw/userspace/supervisor/src/volume.c`
  (`supervisor_verify_volume`) finds the MBR Linux-type partition on the
  Axiom-leased block device, mounts it with guest lwext4 at a fixed internal
  mountpoint, runs journal recovery, and keeps it mounted only long enough to
  read the manifest and launch the storage service.
- **Storage service.** `sw/userspace/services/storage/main.c` independently
  re-finds and mounts that same partition with guest lwext4 over the Axiom
  block lease, and serves the native VFS protocol from it. Bootstrap and the
  storage service each select the partition on their own — the duplicate
  selector the PRD's gap table calls out and FS07 retires.
- **HostFS.** `sw/userspace/services/hostfs/main.c` runs two fixed
  host-backed volumes, `ASTRA_HOST_FS_VOLUME_WORK` and
  `ASTRA_HOST_FS_VOLUME_SOUND` (`sw/include/astra/host.h`), each its own VFS
  service bound to one host directory export at startup. This is not a
  dynamic volume inventory: the volume a session reaches is fixed at
  compile/link time, not discovered or mounted.
- **Grant enforcement is client-only, and the host device is handed to
  clients.** Rights and assign roots are checked only in the client library
  (`astra_assign_resolve`, `sw/userspace/vfs/src/vfs_assign.c`); the VFS
  service binds a session to the kernel-stamped sender pid and checks
  neither root nor rights. `..` is folded lexically before lookup
  (`astra_path_normalise`). On HELLO the port service duplicates the
  host-device capability (READ|WRITE|TRANSFER) to every client
  (`vfs_port_transport.c`, HELLO version 18 and later), and the client then
  sends raw host commands itself (`vfs_host_direct.c`, WORK hard-coded).
  QEMU's per-handle owner stamp (`ASTRA_HOST_CAP_OWNER_SCOPED`) is the
  kernel-authenticated owner of the process holding that device -- HostFS,
  or any client that received the duplicate -- never the client
  application, and QEMU takes the command's `volume` field as given. So a
  client of one host volume can address the other, and can issue other
  host services' commands. Host-root opens are confined with
  `openat2(..., RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS)`,
  which keeps every request inside the two fixed exports but cannot
  separate one client's grant from another's. This is the FS15/FS44 gap
  Phase 1 closes.
- **RAM storage.** `sw/userspace/services/ramfs/main.c` is a guest-side
  allocator bounded by `max_bytes` read from
  `/system/config/services/ramfs/settings.conf` (`astra_vfs_ram_init`) — not
  Linux tmpfs.
- **Namespace.** `sw/userspace/supervisor/src/vfs_host.c` binds root
  conveniences (`DH0`, `SYSTEM`, `APPS`, `HOME`, `COMMANDS`, `CONFIG`,
  `LIBS`, ...) as assigns into the one storage-service volume, not as
  independent volumes or genuine symlinks. See
  [Devices and volumes](DEVICE_AND_VOLUME_PLAN.md) for what that namespace
  does and does not implement today.

Everything above is in scope for removal under PRD FS46 once Phase 2 and
Phase 5 land: no legacy production engine or automatic fallback is meant to
remain in the release image.
