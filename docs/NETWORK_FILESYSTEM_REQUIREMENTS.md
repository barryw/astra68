# Network volumes after the local filesystem cutover

Status: requirements and source-backed feasibility assessment, 2026-10-13.
Network mounting is not implemented or qualified. Finish the production volume
lifecycle, native namespace, usage queries and desktop integration first.

## Reuse the native volume facility

A network share is another source and mounted volume, with its own native
epoch, identity and generation. It has no fabricated physical device or medium.
The proposed namespace is `/net0`, `/net1`, and so on; labels identify the share.
The existing HostFS backend, rooted grants, VFS operations, POSIX adapters,
desktop volume icon, Atlas root view, Unmount and `lsof` must serve it unchanged
at the application boundary. Mount credentials and Linux paths remain private
to the versioned provider protocol.

Linux on the HPS owns the network filesystem client. No second guest filesystem
engine, SMB implementation in Atlas, independent POSIX backend or additional
FPGA feature is required. Extend the existing provider source/mount owner and
its bounded worker mechanism rather than introducing another mount service.

## Protocol choice and measured platform state

Recommend SMB3 first for interoperability with Macs, Windows, Samba and NAS
devices. The Linux client supports signing, encryption and Unicode names;
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
   native volume contract. Bump affected public/private protocol majors and
   update every producer, consumer, installed header, manifest and document.
   Keep server/share display metadata distinct from authority and secrets.
2. Admit typed server, share, protocol, credential reference and read/write
   policy through the existing native mount transaction. Refuse arbitrary
   Linux paths, option strings, shell commands and application-supplied mount
   targets. Validate lengths and Unicode without truncation.
3. Store credentials through a native capability-controlled secret facility.
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
   still-running worker's resources.
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
