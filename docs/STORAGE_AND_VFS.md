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
- **HostFS.** `sw/userspace/services/hostfs/main.c` is the only holder of
  `HOST_FILESYSTEM` -- the host device with `ASTRA_RIGHT_HOST_SCOPE`; the
  supervisor grants every other `HOST_DEVICE` holder the device without it --
  and runs two fixed host-backed volumes, `ASTRA_HOST_FS_VOLUME_WORK` and
  `ASTRA_HOST_FS_VOLUME_SOUND` (`sw/include/astra/host.h`). This is not a
  dynamic volume inventory: the volume a session reaches is fixed at
  compile/link time, not discovered or mounted. At startup HostFS claims the
  host filesystem (`ASTRA_HOST_FS_CLAIM`, host command record 3), opens one
  root session per volume, mints one filesystem scope per session
  (`ASTRA_SYSCALL_HOST_SCOPE_CREATE`) and serves each volume's port with a
  single pump; the port is control only (HELLO, BYE). QEMU takes the claim
  only on a channel the kernel opened for a device handle carrying
  `ASTRA_RIGHT_HOST_SCOPE` (`ASTRA_HOST_CHANNEL_PROVIDER`), so no other
  `HOST_DEVICE` holder can claim, even while HostFS is restarting. Session
  ids are never reset; they are unique until 2^32 sessions have been opened
  in one QEMU process, after which the id of a closed session could be
  issued again to the claimant (a known limit, not a guarantee).
- **The scoped direct lane.** Since storage protocol 28 a HELLO on a host
  volume's port attaches a duplicate of that volume's scope, never the
  device, without ADMINISTER, and without TRANSFER on arrival (the kernel's
  import hook strips it unless the scope is delegable). The client drives
  its own host channel through it (`vfs_host_direct.c`, one process-wide
  transport, one lane per thread): the kernel binds the channel to the
  scopes the thread attaches (up to eight, `ASTRA_SYSCALL_HOST_SCOPE_ATTACH`)
  and writes their words into the provider's channel configuration, and
  QEMU runs on a bound channel only filesystem commands whose session names
  an attached scope, refuses mutations under a read-only scope, and answers
  a handle only in the session it was opened in. Revocation detaches a
  scope from every channel at once; HostFS's death revokes everything it
  minted. HostFS's METRICS port, and the ramfs, storage and proc services,
  still serve every operation through their service core.
- **Grant enforcement: volumes in the provider, roots in the client.** A
  client reaches only the volumes whose scopes it was handed, enforced by
  the kernel and QEMU. Rights and assign roots *within* a volume are still
  checked only in the client library (`astra_assign_resolve`,
  `sw/userspace/vfs/src/vfs_assign.c`), and `..` is folded lexically before
  lookup (`astra_path_normalise`): grant roots move into the provider in
  Phase 1 Task 5. Host-root opens are confined with
  `openat2(..., RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_SYMLINKS)`.
- **Cost of the lane.** Measured on QEMU/TCG on `beast` (not the board) with
  `fsprobe` (`emu/qemu/fs-probe.py`, `tools/fs-probe-compare.py`), ten
  interleaved pairs against the pre-relay direct path (`60f2d30a`), median
  of the ten, latency in µs (median / p95):

  | Operation | Direct path (`60f2d30a`) | Lane | Ratio |
  |---|---|---|---|
  | warm stat | 36.2 / 47.0 | 33.1 / 41.7 | 0.91x / 0.89x |
  | 4 KiB pread | 32.2 / 45.0 | 32.9 / 43.9 | 1.02x / 0.98x |

  HostFS used no CPU on either path, and the client made 3.0 syscalls per
  operation on both. Cross-address-space switches per stat were 0.012 on
  both; in the read window both paths showed about one per read (direct
  1.06, lane 1.17, median): background services preempting the probe
  (`/services/events` alone ran 30-50 ms per window), not HostFS.

  The relay it replaced (storage protocol 27) cost 2.55x stat-warm and
  2.62x rand-read at the median in fsbench
  (`/mnt/Documents/astra68/builds/fs-phase1-task3-60f2d30a/`). Records:
  `/mnt/Documents/astra68/builds/fs-phase1-task3b-20261008/`. The DE25 cost
  is to be measured in Task 13.
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
