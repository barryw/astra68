# Network volumes after the local filesystem cutover

Status: requirements and source-backed feasibility assessment, updated
2026-11-04. NFSv4 passed a DE25 private trial for mount, guest read/write,
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
Broader SMB fault/recovery and interoperability gates remain open. Saved
Settings creation and auto-connect passed a private
DE25 trial after correcting Beast's board-only TCP445 firewall rule. Manual
authenticated mount, write/read with matching server bytes, unmount, Desktop
label, and Atlas enumeration passed in the same 600-second no-panic guard.
Evidence: `/mnt/Documents/astra68/network-smb-saved-firewall-de25-20261104/`.
Release `51f2363a...` passed Settings action visibility, saved mapping restart
auto-connect, UI unmount pause/remount and exact open-file refusal on DE25.
Release `7326966f...` corrected Terminal.app's `NETWORK_ADMIN` bundle grant;
`mount --saved 1`, file read, `unmount --saved 1`, and remount passed a 600-second
DE25 guard. Evidence: `/mnt/Documents/astra68/network-smb-saved-final-ui-de25-20261104/`
and `/mnt/Documents/astra68/network-smb-saved-cli-de25-20261104/`. A
`48e9e00a...` was selected on DE25 after full deployment with system-image
migration, matching-kernel confirmation, boot stage 8, zero Astra restarts,
active production services, and 8.0 GiB free. ABI 15.15 now looks up the
exact managed source for a saved mapping ID. Two DE25 private guards on release
`199561fd...` confirmed an active `EditOld` share survives a saved label edit,
Unmount removes its icon, Mount publishes `EditNew` at `/net0`, and Atlas
enumerates its files. Both guards passed without panic, restored production,
and left 8.0 GiB free after cleanup. Evidence:
`/mnt/Documents/astra68/network-smb-edit-candidate-20261104/` on Beast.
The Settings Mount busy message passed a later 360-second DE25 private guard.
Native `shares` now lists, shows, adds, edits and removes saved NFS/SMB
definitions, with a terminal password prompt for authenticated SMB. A DE25
hot-install gate found and fixed fresh-mapping state: `show` now reports
`not-mounted` without registering a source. On DE25, editing a fresh mapping,
mounting authenticated SMB at `/net0`, reading server data, unmounting and
removing the mapping all passed. Clean release `68cbf32d...` repeated these
checks in a 360-second DE25 private guard without panic. Saved read-only
policy allowed reads and refused a copy into the share; the server had no
destination file. Standard deployment selected that release, verified its
kernel and migrated the system image. Astra, audio and remote desktop are
active with 7.9 GiB free.
Evidence: `/mnt/Documents/astra68/network-smb-edit-ui-20261104/` on Beast. A
credentialed SMB candidate passed a Beast provider test: bad password refused;
good account wrote, synced, and read back the same bytes over SMB3.1.1 with a
password containing `,` and `=`.
Native Volume, HostFS, QEMU, and provider handoff builds pass. DE25 booted the
matching `storage3` kernel; a 600-second private runtime trial mounted the
authenticated share writable as `/net0`, wrote and read back matching server
bytes, showed the icon on Desktop and root in Atlas, then unmounted and removed
the icon. The trial restored selected production runtime. Evidence:
`/mnt/Documents/astra68/network-smb-auth-de25-20261103/` on Beast. It uses one sealed password descriptor for
the selected mount worker and keeps public source records secret-free. Haiku's
read-only `src/add-ons/kernel/file_systems/netfs/client/ShareVolume.cpp`
informed separation of share authentication from volume publication.
The native `mount --smb-user` command now prompts through libc's reusable
terminal secret reader and passes credentials through Volume authority without
placing the password in argv or environment; MC68040 link and prompt tests
pass. Saved mapping definitions, Settings, and mount lifecycle are covered above.
An SMB TCP445 outage trial on DE25 confirmed local `/dh0` access and `lsof`
continued while a share read stalled; the read resumed after traffic returned.
The trial exposed a delayed ordinary Unmount busy response. Shared Volume
removal now checks known holders before requesting provider unmount. Its host
test and a second 300-second DE25 private trial pass: Unmount returned an
active PID 23 operation while TCP445 was dropped, before network recovery.
Both guards restored production. Evidence is on Beast at
`/mnt/Documents/astra68/network-smb-fault-de25-20261103/` and
`/mnt/Documents/astra68/network-smb-unmount-fix-de25-20261104/`.
QEMU's final namespace admission now returns BUSY instead of waiting behind
an active same-volume operation after the preflight race window. A full
release containing this change passed a 300-second DE25 private trial with
SMB3.1.1 mount, TCP445 outage, exact active holder, responsive `/dh0`, 4 MiB
read recovery and successful later unmount. The early preflight caught that
trial's holder, so the new trylock branch still needs a deterministic gate.
Evidence: `/mnt/Documents/astra68/network-smb-namespace-busy-de25-20261104/`.

## Saved share CLI

`shares list` lists saved NFS and SMB definitions without passwords.
`shares show ID` includes mount state. `shares add nfs SERVER EXPORT LABEL`
and `shares add smb SERVER /SHARE LABEL [ACCOUNT [DOMAIN]]` create mappings;
an SMB account causes a terminal password prompt. `shares set ID FIELD VALUE`
edits `server`, `share`, `label`, `account`, `domain`, `auto`, or `read-only`;
boolean values are `on` or `off`. `shares set ID FIELD VALUE --password`
changes a field and its password together. `shares password ID` and
`shares forget-password ID` manage the saved secret. `shares remove ID`
deletes an unmounted mapping. `mount --saved ID` and `unmount --saved ID`
control its volume through the same Network Kit lifecycle used by Settings.
Active source changes take effect after unmount and remount.

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
Beast built the distinct `storage3` package with `CONFIG_CIFS=m`, xattrs,
extended statistics, and legacy SMB dialects disabled. Its complete manifest
and `cifs.ko` vermagic pass; DE25 staged, armed, booted `storage3`, and loaded
`cifs.ko` during the authenticated mount trial. Kernel rollback and full
runtime release gates remain open. Package path:
`/home/barry/astra68/build/de25-kernel-smb3/package`.
Shared native source validation now admits a single SMB share name and rejects
nested paths and option delimiters; Beast Volume client and provider admission
tests and `make -C ndk dist-check` pass. Anonymous SMB3.1.1 attachment uses
the existing provider worker and `mount --smb-guest`; a disposable Samba
server constrained to SMB3.1.1 passed mount/write/fsync/read/unmount with
persisted bytes. The NFS provider regression passed with a temporary localhost
export; it and the Samba container were removed afterward. Evidence is on Beast
at `/mnt/Documents/astra68/network-smb-guest-provider-20261103/`. Credentialed
SMB mount/write/read and DE25 private runtime qualification now pass; saved
mappings, network fault gates, and production runtime cutover remain open.

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

Current SMB evidence (2026-11-04): authenticated DE25 mount, saved mapping
edit/CLI, Atlas volume icon and root view, Unicode and long names, case-fold
lookup, writable and read-only policy, persistent fsbench read/write/append/
directory/durable-save operations, and exact busy-unmount holders passed on
disposable Samba. Guest and authenticated provider tests cover mount, refusal,
read, write, fsync and unmount. The Unicode case exposed a missing CIFS
`iocharset=utf8` option; corrected provider and Filesystem Kit passed a DE25
hot-install test. Coherent release `7eecee711b408ce87725a6a71dce9b4ebe615e150112eaeb2596f3784b22c701`
passed a 360-second private DE25 guard with authenticated mount, complete
Terminal/Atlas listing and unmount, and was selected. On that release, a
15-second TCP445 drop at creation of `save.tmp` delayed durable-save work;
all 20 saves recovered, final 65,536-byte server file persisted, and no
temporary file remained. A RAM-to-SMB copy preserved SHA-256. New shared `mv`
CLI passed a focused DE25 install and moved a disposable RAM file to SMB with
matching server bytes and no RAM source. Native same-volume SMB rename,
rename and unlink with an open readable descriptor, server-enforced access
denial, and nonzero `df` capacity passed. Evidence is archived at
`/mnt/Documents/astra68/network-smb-edit-ui-20261104/mv-gate/` on Beast.
In-flight write and `fsync` interruption, concurrent users and other fault
cases below remain acceptance gates. Beast's provider regression covers the
same permission, capacity and open-handle cases.
Release `e1c81852e8f1953a74386dcce29cea7de9a9d6ca76ee060b6f213be78eead0c1`
then passed a 360-second private DE25 guard, including exact-release RAM-to-SMB
move, server byte check, cleanup and unmount. Deployment selected it, confirmed
the packaged kernel, migrated system image, booted to stage 8, and left all
three services active with 7.9 GiB free. Trial and deploy evidence is in the
same Beast archive.

Source-loss recovery at source `2c8780c4` then passed focused DE25 tests:
disconnected SMB did not block local `/dh0` commands; an inherited open SMB
descriptor retained its original bytes across server restart; busy unmount
named exact holders; failed saved mounts could be retired and remounted.
Provider shutdown detached only lost network mounts and finished in 4.3
seconds. Beast's full HostFS and storage-provider suites pass. Six raw DE25
captures are checksum-verified under the archive's `source-loss/` directory.
Coherent release `58858b645e4195b86c0978196371f386c195ea4ae120bb13ea8be6e3fa9d4c5f`
passed Beast verification and a 360-second private DE25 guard at stage 8,
with Terminal `/dh0` listing and no panic. Console and trial frames match
board checksums in the same archive. Standard deployment selected it,
confirmed the packaged kernel and stage 8, and left Astra, audio host and
remote desktop active with 7.9 GiB free after disposable trial cleanup.

Further DE25 gates on that selected release passed two simultaneous
authenticated durable-save clients, exact first-write and first-`fsync`
TCP445 outages, different-export identity refusal while an old descriptor
remained open, and prompt DNS failure without a desktop icon. In-flight
write and sync outages recovered all 20 saves, left the expected 65,536-byte
server file and no temporary file. Provider shutdown with a write pending
then exposed explicit CIFS `hard`: its uninterruptible Linux writeback
prevented service stop. After syncing local SD state, an emergency reboot
restored stage 8 and services. Updated authenticated and guest mounts use
`soft,cache=none`; Beast provider tests and a DE25 hot install pass. The
board reported those exact CIFS options. With TCP445 blocked at `fsync`,
service stopped in 2 seconds, provider and mount disappeared, and no final
save was published. After restart, 30-second write and sync outages both
recovered 20 saves with matching server SHA-256 and no temporary file.
A 95-second first-write outage also recovered 20 saves with matching server
bytes; a second Terminal completed `/dh0` listing during the outage. Clean
unmount left no CIFS mount and the provider's normal two processes. Evidence
resides in `source-loss/` under the Beast archive above. A sustained outage
returned a write error after 218 seconds: `fsbench` reported zero committed
bytes, no final file existed, and only a zero-byte temporary file remained.
An initial unmount refused the retained writeback error; retry after server
recovery unmounted and both disposable network sources were released. Candidate
release `acccff5bfeef37eb8ed730a1046f9adbf082e8127db14c6b1d44c987a7671a57`
built and verified on Beast. Its 360-second private DE25 guard reached stage 8
without panic, authenticated `/net0` with `soft,cache=none`, read matching
server text, completed 20 durable saves with matching server SHA-256, and
unmounted. Guard restored the selected release and all three services.
Console, 10 RFB frames, and report were checksum-verified on Beast; disposable
board trial tree was removed. Standard deployment selected this exact release,
confirmed packaged kernel and stage 8, and left all three services active.
Post-deployment authenticated SMB read matched the server, mount options
remained `soft,cache=none`, and unmount plus source release removed the volume
without a CIFS mount leak. Seven selected-release frames and 11 reports were
checksum-verified on Beast before board copies were removed. No dev shadow
or disposable trial tree remains; board has 7.9 GiB free.
Linux CIFS `soft` returns I/O errors when a server fails;
`cache=none` avoids normal cached file data
([mount.cifs](https://man7.org/linux/man-pages/man8/mount.cifs.8.html),
[Linux CIFS usage](https://docs.kernel.org/admin-guide/cifs/usage.html)).

Run disposable Samba and NFS servers on Beast before testing the DE25 against
real shares. Cover Unicode/long names, case behavior, read-only/writable shares,
authentication refusal, rename/unlink of open files, server-side permissions,
capacity, cross-volume moves, concurrent users, busy unmount, and source loss.
Inject delayed replies, connection loss during writes and sync, server restart,
reconnect to a different export, DNS failure and shutdown with pending I/O.
Verify bounded queues/workers/descriptors, truthful write outcomes, no secret
leaks, persistent server-side bytes and responsive unrelated local volumes.
Only then qualify performance separately.
