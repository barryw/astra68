# Network volumes after the local filesystem cutover

Status: requirements and source-backed feasibility assessment, updated
2026-11-03. NFSv4 passed a DE25 private trial for mount, guest read/write,
Desktop and Atlas, exact open-file unmount refusal, and source retirement.
Network mounting is not yet qualified. Private DE25 trials kept `/dh0`
responsive during stalled NFS read, directory and destination-copy operations;
read and directory traversal recovered, and a copied file committed after
traffic returned. NFS file and nonempty folder copies persisted with matching
hashes; a duplicate destination was refused. A later DE25 trial found that
`lsof` on a disconnected NFS volume held HostFS's single control pump; an
independent Terminal stayed at `Opening`. The corrected candidate returned
the exact held PID, inode, access and file path from `lsof` during the outage,
then completed `ls /dh0` before traffic resumed. This candidate uses retained
descriptor paths for network diagnostics without asking the server to
revalidate metadata. It also recovered after TCP2049 was dropped mid-write at
2 MiB and during a durable-save run when the pending file reached 65,536 bytes;
both completed 20 durable saves with `failed:false` and matching server bytes.
Exact in-flight `fsync` timing still needs an instrumented gate. Evidence is on Beast in
`/mnt/Documents/astra68/network-nfs-root-admission-20261103/` and
`/mnt/Documents/astra68/network-nfs-recovery-20261103/`, and
`/mnt/Documents/astra68/network-nfs-copy-20261103/`,
`/mnt/Documents/astra68/network-nfs-lsof-stall-20261103/`, and
`/mnt/Documents/astra68/network-nfs-lsof-write-sync-20261103/`. All five
600-second guards passed, production services recovered, and trial state was
removed.
SMB3, mapping UI, and production release gates remain open.

## AstraZ story workflow

User-owned local stories belong in `HOME:Stories` (`/home/Stories`). AstraZ's
Open Story panel creates that directory on first use, remembers the last
selected directory in its private Config Kit document, and returns there on
next launch. If that directory is unavailable, it falls back to Stories, Home,
then the volume root. Once network mounting exists, a share will appear in
Volumes and may be selected without copying its story into the application
bundle or local disk. Its path
uses the same native VFS access and file-type association as local stories.
Save files belong to the user's local Application Data area so unmounting a
story share does not hide saved games. Current AstraZ save-path code still
uses `STORE:saves`; align it with that policy while preserving existing saves.

## Reuse the native volume facility

A network share is another source and mounted volume, with its own native
epoch, identity and generation. It has no fabricated physical device or medium.
The proposed namespace is `/net0`, `/net1`, and so on; labels identify the share.
The existing HostFS backend, rooted grants, VFS operations, POSIX adapters,
desktop volume icon, Atlas root view, Unmount and `lsof` must serve it unchanged
at the application boundary. Mount credentials and Linux paths remain private
to the versioned provider protocol.

Mounted network volumes support the same file operations as local volumes,
subject to remote server permissions and the negotiated filesystem semantics.
Disk formatting and partitioning remain physical-device operations. A saved
connection is a definition, not a live mount; deleting one must not silently
detach a mounted volume.

Linux on the HPS owns the network filesystem client. No second guest filesystem
engine, SMB implementation in Atlas, independent POSIX backend or additional
FPGA feature is required. Extend the existing provider source/mount owner and
its bounded worker mechanism rather than introducing another mount service.
Haiku's read-only `src/add-ons/kernel/file_systems/nfs4/kernel_interface.cpp`
(`nfs4_mount`, `nfs4_unmount`) informed treating a remote export as a normal
published volume root. Astra adapts that boundary to its Linux provider and
native volume identity rather than embedding a second NFS client in QEMU.
Haiku's read-only `src/preferences/network/NetworkWindow.cpp` and
`InterfaceView.cpp` informed the interface list plus details-pane layout;
Astra uses its existing shared Settings Sidebar, List, Field, Button, and
selection controls.

## Settings and command surface

Add a `NETWORK` section to Settings' sidebar with `Ethernet`, `NFS`, and `SMB`
pages. Ethernet shows live link, address, route, and DNS status; offers DHCP
or manual address, prefix, gateway, and DNS configuration. NFS and SMB each
show a shared selectable list of saved mappings, `+` to add and `-` to remove,
and a detail form for the selected mapping. The detail form exposes label,
server, export/share, read/write policy, mount status, and Mount/Unmount.
SMB also exposes account/domain and a credential reference; the password
entry goes to the resident owner of Config Kit's protected secret API, never
directly into Settings' document. Reuse one
settings-page composition for both protocols rather than two separate UIs.
Editing an active mapping saves the proposed definition without changing its
current mounted identity; reconnecting applies it as a new mount generation.

Settings and CLI invoke the same typed native management operations. Store
saved mapping definitions and optional auto-connect policy in system Config
Kit; a mapping's ID remains stable while its mount gets a fresh provider
generation on each attachment. Auto-connect failure must leave the desktop
available and show a useful error. Removing a mapping with a live mount first
uses normal safe unmount: if files are open, show the exact holders and keep
both mapping and mount intact. Existing desktop and Atlas volume presentation
then need no protocol-specific branches.

DE25 currently runs NetworkManager and systemd-resolved on its Linux HPS.
The native Network Kit exposes sockets, DNS resolution, and interface address
enumeration, but no interface configuration or route/DNS control API. Add a
typed privileged host control boundary; Settings must not invoke `nmcli`,
edit Linux profiles, or edit `/etc/resolv.conf` directly. Config Kit is the
user-visible desired configuration; any NetworkManager profile generated for
early host boot is derived state, with one owner and clear reconciliation.
Use NetworkManager's
[checkpoint/rollback transaction](https://www.networkmanager.dev/docs/api/1.36/gdbus-org.freedesktop.NetworkManager.html)
when changing the active management link. Apply and validate a candidate, require confirmation before
the checkpoint expires, and restore the last known good configuration if
connectivity or confirmation fails. Local Astra boot must not depend on a
reachable network.

## Protocol choice and measured platform state

Implement NFSv4 first because the selected DE25 kernel already contains its
client; add SMB3 after remote-I/O lifecycle is qualified. SMB3 remains the
interoperability target for Macs, Windows, Samba and NAS devices. The Linux
client supports signing, encryption and Unicode names;
require a modern dialect rather than silently downgrading to SMB1. These
capabilities and interoperability are described in the upstream
[SMB client introduction](https://docs.kernel.org/admin-guide/cifs/introduction.html).

The built DE25 package `build/de25-kernel-storage/kernel-2/.config` currently has
`CONFIG_NFS_FS=y`, NFSv3 and NFSv4.1/4.2, TCP transport and SUNRPC. It has
`# CONFIG_CIFS is not set`. SMB therefore needs a new full kernel Image and
matching modules, distinct release identity, rollback and hardware acceptance.
Do not add a CIFS module built against a different Image.

The pinned Jammy AArch64 sysroot currently contains neither `mount.cifs` nor
`mount.nfs` in `/sbin` or `/usr/sbin`. The local mount owner deliberately
disables helpers, fstab, mtab and path canonicalization. Network admission must
explicitly resolve its endpoint and construct validated filesystem options,
or invoke a narrowly scoped, pinned helper in its existing bounded worker.
Turning on arbitrary mount helpers for all sources is not an acceptable change.
Upstream documents the optional `mount.cifs` helper and its credential-input
mechanisms in [SMB client usage](https://docs.kernel.org/admin-guide/cifs/usage.html).

NFSv4 is a reasonable second protocol and already has kernel support. It still
needs explicit authentication/identity policy, DNS/referral handling, server
state recovery and disconnected-I/O qualification. Kernel support alone does
not provide native mount control. The upstream
[NFS client documentation](https://docs.kernel.org/admin-guide/nfs/nfs-client.html)
describes stable client identity and DNS upcalls.

## Required work

1. Add a network source class/origin and supported format to the canonical
   native volume contract. Bump breaking private protocols and update every
   producer, consumer, installed header, manifest and document; additive native
   operations retain their ABI major.
   Keep server/share display metadata distinct from authority and secrets.
2. Admit typed server, share, protocol, credential reference and read/write
   policy through the existing native mount transaction. Refuse arbitrary
   Linux paths, option strings, shell commands and application-supplied mount
   targets. Validate lengths and Unicode without truncation.
3. Save credentials in the network service's private Config Kit document,
   with sensitive fields declared in its schema. Settings sends validated
   changes through the owning service's typed control API; only that service
   opens the protected values. Public network status belongs in its separate
   read-only system view.
   Never put passwords in arguments, environment, public catalog, logs or
   persistent plaintext settings. Give only the selected mount worker access
   and destroy temporary material on every exit path. A volume's authenticated
   server principal is independent of a guest POSIX UID.
4. Mount in the provider's private namespace with the existing restricted
   flags. Publish a grant only after the real mount and its exact root identity
   are verified. Server permissions remain authoritative; local metadata must
   not pretend unsupported ownership, links or case semantics are available.
5. Qualify every operation that can block on a remote server. Existing path and
   mount workers are bounded; direct QEMU data I/O also needs fault tests for
   delayed reads, writes, sync and directory traversal. A disconnected share
   must not stop unrelated volumes, desktop control or shutdown. Timeout or
   cancellation must not report an uncertain write as committed or free a
   still-running worker's resources. Do not use NFS `soft` retries as a
   shortcut for responsive UI: the Linux
   [NFS manual](https://man7.org/linux/man-pages/man5/nfs.5.html) warns that `soft` timeouts
   can silently corrupt data. Isolate or account for stalled host workers
   while retaining truthful write results.
6. Preserve safe unmount semantics: quiesce new opens, expose exact retained
   users and pending I/O, drain accepted work, sync, then real unmount. Busy,
   server loss and uncertain durability remain distinct results. Reconnection
   cannot silently revive stale handles after a different share or mount has
   replaced the original identity.
7. Add CLI mounting and a small Connect to Server flow using common controls.
   After successful mount, the existing desktop icon and Atlas path apply.
   Remembering a connection stores a credential reference and policy, never
   a password or a boot dependency that prevents local desktop startup.

## Acceptance evidence

Run disposable Samba and NFS servers on Beast before testing the DE25 against
real shares. Cover Unicode/long names, case behavior, read-only/writable shares,
authentication refusal, rename/unlink of open files, server-side permissions,
capacity, cross-volume moves, concurrent users, busy unmount, and source loss.
Inject delayed replies, connection loss during writes and sync, server restart,
reconnect to a different export, DNS failure and shutdown with pending I/O.
Verify bounded queues/workers/descriptors, truthful write outcomes, no secret
leaks, persistent server-side bytes and responsive unrelated local volumes.
Only then qualify performance separately.
