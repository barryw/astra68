# Axiom kernel and Astra service ABI

Status: provisional ABI contract, revision 0.10 (2026-09-12)

The ABI is big-endian, 32-bit, naturally aligned, and independent of kernel C
layouts. Only the user/kernel ABI and versioned service protocols are stable.
Kernel-internal structures and function calls may change at any time.

## Unreleased native volume migration

Host transport is now `0x00030000`, command record 5. SESSION_OPEN takes a
32-byte payload: the 24-byte big-endian `AstraVolumeReference` (provider epoch,
object ID, generation), an authenticated requester PID and a zero reserved
word. Root sessions carry the exact reference; derived sessions carry a zero
reference and inherit the parent's exact identity and readonly policy.
HostFS stamps the requester from the kernel's sender identity; service roots
use zero. Fixed numeric selectors and older records are refused.

The provider catalog projects the shared native records in
`ndk/include/astra/volume.h`: version 1, a 32-byte query and an 8104-byte
page containing at most sixteen 504-byte records. Epoch and sequence checks
protect keyset pagination. Metadata grants no filesystem claim, rooted session
or mount-control authority. Linux paths, descriptors and private mount IDs do
not cross this interface.

STOR 38 and Filesystem Kit 14.0 (`filesystem.library.14`, kit 11.0.0) carry
the full 64-bit native volume identity. `AstraVfsNodeMeta` is 80 bytes, with
zero identity padding before the volume field; `AstraVfsReply` is 376 bytes.
Host node device and mount fields carry native identity, not Linux device
major/minor numbers or private namespace mount IDs. Native IDs are not recycled;
exhaustion refuses new sessions. Whole-mount sessions retain logical authority
only; subdirectory sessions and open directories retain their exact objects.
STOR requests remain 256 bytes. Complete pathname operands use validated
offset/length descriptors in the session's existing transfer area; providers
snapshot them before dispatch. Components support 255 bytes and complete paths
4095 bytes, excluding NUL. There is no short inline-path or extended rename
format. Continuation/readlink/path results use caller-owned transfer storage.
Native status 23 reports NAME_TOO_LONG; POSIX translates it to ENAMETOOLONG.
VOLUME_ADMIN and VOLUME_USAGE remain distinct from metadata and data brokers.
Native VOL protocol 2 / usage format 2 pages are 8392 bytes and contain two
complete 4160-byte records. Their cursor orders kind, full native handle and
holder PID, so shared retained roots identify every authenticated process.
HostFS projects holders from canonical native control sessions; admitted HELLO
and actual retirement invalidate the existing QEMU usage sequence. Derivation's
creator does not replace actual holder ownership. Coherent owned snapshots
include every page or return failure without partial output. PID, object kind,
handle, inode, exact volume, access and complete native path come from canonical
retained resources. RAM_READ/RAM_WRITE constrain root grants to the configured
RAM source's exact incarnation; old grants never follow replacement mounts.
Runtime 1.17 adds the canonical freestanding `memchr` needed by bounded catalog
validation.

System library ABI 4 (`system.library.4`, `ASTRA_SYSTEM_4.0`, Runtime.kit
2.0.0) gives bundle executable/icon strings and application-catalog paths
explicit heap ownership. Their existing destroy functions release these paths
with all other owned metadata. APPL 5 replaces inline argv with a 40-byte
control message and a transferred readonly shared area bounded by the native
4096-byte argument maximum. The area is handle zero and the call's reply-send
capability is handle one. Supervisor snapshots the declared byte span, closes
the area, validates exact UTF-8 string count and byte consumption, then uses
that owned snapshot through launch. There is no older inline wire format.

SVCM 7 separates the 64-byte service-identifier domain from filesystem names.
Its 120-byte request and 152-byte reply carry fixed control and runtime fields;
executable strings and complete pointer-free definition records use transferred
readonly areas. Public definitions and info objects own their executable paths.
The same native area snapshot/seal/create primitive serves APPL and SVCM. Both
validate a private byte copy after closing the transferred mapping/capability.
Definition decoding commits only a fully validated object; read or malformed
payload failures cannot become configuration defaults or partial output.

This coordinated source migration is not a production release. Bootstrap,
storage, live publication, usage/control, UI and launch/release integration are
still required. The selected board release retains its qualified older ABI;
never combine its launcher or kernel with this Host 3 QEMU.

## Process startup ABI 8

`sw/include/astra/process.h` defines the 92-byte `AstraStartupInfo` and
92-byte `AstraStartupCapability` records (`ASTRA_STARTUP_ABI_VERSION` 8). ABI
3 added the conventional null-terminated environment vector; ABI 4 replaces
the final reserved words with an optional read-only personality handoff address
and size used by atomic image replacement. The initial thread
enters `_start` with the read-only startup-block logical address in `D2`, its
process self handle in `D4`, and its thread self handle in `D5`. The block
carries explicit bounded argument, environment, and initial-capability
tables. The header and capability records occupy one 4 KiB page, with a
43-record capacity derived from their actual sizes. ABI 8 gives argv and the
environment an explicit `vector_data_address`/`vector_data_size` extent in
separate owned read-only pages. Both aligned NULL-terminated pointer vectors
precede their packed UTF-8 strings. The combined string budget remains 4,096
bytes including NULs; pointer storage follows the admitted counts and never
competes with capability records. Consumers validate every pointer and NUL
against the payload extent before reading strings. The payload follows the
rounded opaque exec handoff, with their combined rounded span admitted before
the executable's first PT_LOAD. Launch/exec allocation failures roll back the
new space; clone retains the payload through ordinary VM lifetime. A handoff
is either absent or a complete mapped range; its bytes are opaque to Axiom.
There is no ABI 7 startup format or fallback.
`docs/USERSPACE_RUNTIME.md` defines
validation, ownership, and exit.

The boot-supplied supervisor and every later protected loader use this same
contract. General ELF parsing remains userspace policy; Axiom validates the
final mappings, entry, stack, handles, and startup block before publication.

## Filesystem scopes (storage protocol 28)

A filesystem scope is the authority to drive host channels bound to one
provider session (docs/FILESYSTEM_PRODUCTION_PRD.md FS15, FS44). The kernel
object is `KERNEL_OBJECT_HOST_SCOPE` (17); only a host device handle carrying
`ASTRA_RIGHT_HOST_SCOPE` (`1u << 9`, `sw/include/astra/object_abi.h`) creates
one, and the supervisor grants that right only as `HOST_FILESYSTEM`, to
HostFS. Every other `HOST_DEVICE` grant arrives without it.

- `ASTRA_SYSCALL_HOST_SCOPE_CREATE` (109): `data[1]` the device,
  `data[2]` the provider word (a session of the claimed filesystem owner
  under host command record 3),
  `data[3]` `ASTRA_HOST_SCOPE_READ_ONLY | ASTRA_HOST_SCOPE_DELEGATE`.
  Answers a handle with READ, WRITE, TRANSFER and ADMINISTER; UNSUPPORTED
  without `ASTRA_HOST_CAP_FS_SCOPE`.
- `ASTRA_SYSCALL_HOST_SCOPE_REVOKE` (110): ADMINISTER; detaches the scope from
  every channel, after which open, attach and query answer PEER_DEAD. The
  creator's exit revokes every scope it created.
- `ASTRA_SYSCALL_HOST_SCOPE_ATTACH` (111): WRITE; binds the scope to the
  calling thread's channel, which must have been opened on a scope; at most
  `ASTRA_HOST_CHANNEL_SCOPE_MAX` (8) per channel.
- `HOST_QUERY`, `HOST_CHANNEL_OPEN` and `HOST_CHANNEL_CLOSE` take a scope
  where they took a device; `HOST_EXECUTE` does not. `AstraHostLeaseInfo`
  answers the scope's `scope` word and `scope_flags` (READ_ONLY only).
- A scope without `ASTRA_HOST_SCOPE_DELEGATE` loses TRANSFER when it is
  received: its holder may use it and fork with it, not hand it on.

Host transport `0x0001000d` adds `ASTRA_HOST_CAP_FS_SCOPE` (bit 10). The
kernel-to-provider channel configuration is version 2: `scope`,
`scope_flags`, `scope_owner` and the operations ATTACH (3) and DETACH (4).
On a bound channel the provider runs only filesystem commands whose session
names an attached scope (else `ASTRA_STATUS_ACCESS`), refuses mutations
under a read-only scope (`ASTRA_STATUS_READ_ONLY`), and answers a handle only
in the session it was opened in. `runtime.library` 1.14 exports
`astra_host_scope_create`, `astra_host_scope_revoke` and
`astra_host_scope_attach`. ATTACH makes the checks OPEN makes: an inactive
lease, a host that is not ready, or a channel of an earlier host generation
answers PEER_DEAD.

Host transport `0x0001000e` adds `ASTRA_HOST_CAP_FS_SESSIONS` (bit 11) and
host command record 3 (`ASTRA_HOST_COMMAND_VERSION` 3):

- **One claimed owner.** `ASTRA_HOST_FS_CLAIM` binds the filesystem service
  to the submitting owner (HostFS); every other owner's filesystem command
  on an unbound channel or `HOST_EXECUTE` answers `ASTRA_STATUS_ACCESS`.
  The kernel authenticates the claim: a channel opened on a device handle
  carrying `ASTRA_RIGHT_HOST_SCOPE` is configured with
  `ASTRA_HOST_CHANNEL_PROVIDER` (channel configuration version 3), and the
  provider takes CLAIM on no other channel, nor through `HOST_EXECUTE`.
  The claim, with every session and handle of the owner, goes when the
  owner releases it or dies.
- **Rooted sessions.** Every command names `session`, a root directory
  opened by `ASTRA_HOST_FS_SESSION_OPEN` beneath a volume (session 0,
  `value_lo` the volume) or beneath another session; a read-only session,
  and one derived from it, refuses every mutation. Session ids are never
  reused. Closing a session closes every handle opened in it, whoever
  opened them; a handle answers only in its own session.
- **Scopes name sessions.** A bound OPEN or ATTACH is accepted only for a
  live session of the claimed owner whose creator is that owner. A bound
  channel cannot claim, open or close sessions.
- **Path operands.** `path_offset`, `path_length` and `path2_length` place
  the unterminated operands in the data span, ahead of the data; each is
  under `ASTRA_HOST_FS_PATH_MAX` (4096) bytes and holds no NUL.
- **Node record.** OPEN, STAT and READDIR answer an `AstraHostNode`
  (statx: inode, device, blocks, nanosecond and birth times, mount id) with
  a validity mask.
- **Base handles.** Every path operation resolves beneath `handle`, an open
  directory of the session, or beneath the session root when it is 0. The
  operation table is renumbered densely (1..19); the former `*_AT`,
  `STAT_FILE` and `CHMOD_FILE` are the plain operations with a handle.
  `ASTRA_HOST_METRICS_VERSION` is 3 and the snapshot size follows the
  operation count.

Storage protocol 28: a HELLO on a port that serves a host volume attaches
one handle, the grant's scope; such a port answers every other operation
except BYE with `ASTRA_VFS_ERR_UNSUPPORTED`, runs no operation fused with a
HELLO, and refuses a HELLO it cannot attach the scope to. At that checkpoint
28 became `ASTRA_VFS_VERSION_MIN`: no earlier request layout is served, a HELLO below
it is answered `ASTRA_VFS_ERR_PROTOCOL`, and every pre-28 compatibility path
(per-request reply ports, unfused HELLO retry, two-message rename and
symlink in the client, version-gated operations) is deleted. The server
side of the two-message forms goes too: operations 17, 18, 23 and 24
(RENAME_FROM/TO, SYMLINK_TARGET/TO) and their staged session state are
gone, and the numbers are not reused.

Storage protocol 29: a grant is a HostFS port
(docs/FILESYSTEM_PRODUCTION_PRD.md FS15). A session is bound to the grant
port it said HELLO on, and a request for it on any other port is refused
unanswered. HELLO answers `AstraVfsReply.features` (which replaces
`reserved2`; every other operation answers 0) with
`ASTRA_VFS_FEATURE_GRANTS`, and attaches a duplicate of that grant's scope.
`ASTRA_VFS_OP_GRANT_DERIVE` (36) takes `flags`, a subset of
`ASTRA_VFS_GRANT_READ | WRITE | DELEGATE` that must include READ and may
not exceed the grant's own, and `body.path`, the new root relative to the
grant's root (`""` keeps it). Its reply carries one send handle, the derived
grant's port; HELLO on that port hands out the derived grant's scope, which
is read-only without WRITE and keeps TRANSFER only with DELEGATE. A port
derived from a non-delegable grant arrives without TRANSFER (the reply uses
the kernel's send dispositions, syscall ABI 0x00010042), so it cannot be
moved on; a fork child keeps it as usual. A derived grant is retired when
the session that derived it ends. A grant whose last holder closes its port is retired: HostFS revokes its scope
through the kernel and closes its provider session. The grant port is
control only (HELLO, BYE, GRANT_DERIVE); the client's lane carries every
data operation.

Storage protocol **36** is the current version and minimum. Host transport
`0x00010015` retains `ASTRA_HOST_CAP_FS_RESOLVES` (bit 12). Filesystem Kit 12.0
provides ordered `LOOKUP` (37), raw assign resolution, and explicit metadata
validity and identity. `ASTRA_STATUS_ESCAPED` (20) carries a complete
absolute Astra namespace continuation, original-request bytes consumed, the
escape kind and links followed. The provider never follows that continuation
in Linux. `LOOKUP` uses one 40-link budget across namespace hops and returns
a validated `AstraVfsEscape` payload on OK or ESCAPED; the inline continuation
limit is 191 bytes, covering every native pathname. Longer continuations
return BUFFER_TOO_SMALL. Requests are 248 bytes, two-path requests 440 bytes,
and replies 368 bytes with 208 inline payload bytes. The reply's separate
72-byte `AstraVfsNodeMeta` preserves LOOKUP link accounting beside metadata.
FOLLOW, MISSING and ENTRY select final-link following, a missing final leaf,
and mutation of the final entry. FOLLOW and ENTRY are mutually exclusive.
Host STAT carries the same policies plus an explicit remaining-link budget.
UNLINK and UNLINK_AT require a non-directory with zero flags, or an empty
directory with `ASTRA_VFS_UNLINK_DIRECTORY`. The provider enforces that kind
during removal; Filesystem Kit 12.0 exposes the same flags on path removal.
Host UNLINK has the same zero/file and DIRECTORY/empty-directory contract;
the old automatic-kind policy and FILE flag are removed from the unreleased
`0x0001000f` contract.
`ASTRA_VFS_OPEN_NOFOLLOW` (bit 7) refuses a final symlink before truncation;
exclusive creation also leaves a dangling link intact.


## Atomic write bounds

ABI 12 requires an exclusive `max_end` byte position on sequential and
positioned writes, including their accelerated callbacks. `UINT64_MAX` means
unbounded. Providers select the current position or append EOF and check the
bound within the same transaction as the write. A request crossing the bound
returns a short write; a nonempty request starting at or beyond it returns
`ASTRA_VFS_ERR_FILE_TOO_LARGE` (status 22) without moving the position. Zero
writes succeed without changing size or position. Positioned writes ignore
the open description's APPEND flag.

STOR 35 introduced `AstraVfsRequest.write_limit` for inline and shared-area
writes; Host transport `0x00010015` carries the bound in WRITE's input value
and returns the resulting position there. Configuration version 4 is unchanged.
HostFS serializes writes and truncations with its existing namespace lock,
including writes through independent descriptions. RAMFS uses its filesystem
lock; ext4 selects position, clamps length and writes under its mount lock.
The POSIX adapter supplies `RLIMIT_FSIZE`, maps status 22 to `EFBIG`, and sends
`SIGXFSZ` only when a finite bound refuses all bytes. Limit and signal policy
remain in POSIX; providers implement the generic bound once.

STOR 36/minimum 36 makes ordered LOOKUP accept a retained directory token.
Filesystem Kit ABI 12.0 adds the attached namespace as the first argument
of path-based `astra_filesystem_*_at` APIs. Provider continuations retain
the same 40-link budget and use the shared namespace resolver; POSIX adapters
call that native contract. Joined-assign opens use the member selected by
lookup, including dangling-link shadowing. Rename/link retain namespace
escapes instead of conflating them with a cross-device operation.

## Syscall ABI 0x00010043: filesystem admission

Runtime 1.16 exports `astra_host_scope_limit`. Host transport 0x00010015
binds the per-owner handle/token quota to each scope with `scope_files` at
byte 48. Current channel configuration version 5 is 68 bytes: `process` at
byte 52 carries the kernel-authenticated native PID independently of the
resource owner. File, pending-share and active-operation usage records retain
that PID. Image admission confirms the PID and full resource owner against
current channels, so a recycled PID cannot claim an older retained token.
OPEN reserves Linux
descriptor and directory capacity before any create or truncate. SHARE and
ADOPT account aliases and pending receipts without charging another Linux
descriptor. Refusal returns LIMIT; an unsuccessful adoption keeps its token.
Disconnected handle identities remain charged until acknowledged or owner
death. Provider sessions, handles, lost identities and receipts also share
the configured global object bound, preventing reset from bypassing admission.

`ASTRA_HOSTFS_MAX_DESCRIPTORS` and `ASTRA_HOSTFS_MAX_DIRECTORIES` accept
positive uint32 decimal values only. QEMU clamps descriptors to RLIMIT_NOFILE minus 256 and DIRs to descriptors,
including explicit limits. The production launcher defaults to 2048 objects/
descriptions and 1024 DIRs. Its conservative 8 KiB per object/description plus
40 KiB per DIR budget totals 56 MiB; direct QEMU starts without those launcher
values default to the available host descriptor allowance. QOM exposes filesystem descriptor,
directory and object counts plus host RSS and descriptor count. HostFS reads
positive `owner_files`, `owner_sessions` and `queue_messages` values from its
CONFIG settings; the queue count cannot exceed ASTRA_PORT_MESSAGES_MAX.
Control-session limits apply per owner to each native volume service.

Scopes use reusable page-backed kernel metadata, not a fixed global pool.
Each process can hold 16 DMA records, placed by page inside the unchanged
32 MiB window; one buffer remains at most 8 MiB.

## Syscall ABI 0x00010042: send dispositions

`ASTRA_SYSCALL_PORT_SEND_TRY` takes `ASTRA_PORT_SEND_DISPOSITIONS` (bit 31)
in data[5]. With it, data[4] points at `AstraHandleDisposition` records
(`{handle, rights}`) instead of bare handles, and each handle arrives with
its own rights AND the record's mask: the sender chooses what the receiver
gets, and a mask only narrows. This is Zircon's `zx_channel_write_etc` with
`zx_handle_disposition_t` (operation MOVE, a rights mask); Haiku has no
capability handles to follow. Every handle is still moved and needs
ASTRA_RIGHT_TRANSFER to be; a mask may strip TRANSFER, and then what
arrives cannot move again. A zero mask is refused with
`ASTRA_SYSCALL_INVALID_ARGUMENT` and nothing moves. Without the flag, D4
continues to point at a plain handle array. A reply to `PORT_CALL` honours
the same masks. The handles a `PORT_CALL` *request* carries take no
dispositions and move with their own rights. A mask must leave the handle a
right besides TRANSFER
that survives the receiver's import filter (a non-delegable scope loses
TRANSFER on import); otherwise the send is refused with INVALID_ARGUMENT and
nothing moves. The same rule refuses an unmasked handle that would arrive
with no usable right.
`runtime.library` 1.15 adds `astra_port_send_rights`.

## Filesystem Kit ABI 12.0

STOR version/minimum 33 adds DIRECTORY_PATH (42) and anchored operands on
MKDIR, RENAME, LINK, SYMLINK and READLINK. Existing STAT_AT gains an explicit
ASTRA_VFS_STAT_FOLLOW flag; zero retains no-follow semantics. RENAME/LINK use
file and offset for independent directory bases. Zero bases denote the grant
root, and real bases require relative paths. Host version 0x00010012 adds
PATH (24), FS_PATHS (bit 15), and metrics version 5.

Process VFS owns current directory identity, including fork receipt adoption
and exec export/import. Its current-directory setter now takes one path.
AstraFilesystem's private layout includes native CWD acquisition callbacks,
so the library identity is filesystem.library.12 and all exports use
ASTRA_FILESYSTEM_12.0. Filesystem.kit 9.0.0 provides ABI 12.0; every in-tree
consumer requests that identity. libc stays ABI 4.0. Native process VFS exec
state is version 8; private POSIX file exec state is version 3 and contains no
independent CWD spelling. There is no obsolete setter, stat callback, library
identity or dual wire format.

STOR 34 adds HELLO resumption: a nonzero session must already belong to the
caller and grant. A resumed HELLO retains the control session and reply lane,
and replaces only the scoped accelerator. This keeps derived grants alive
across a provider reset without weakening the deriver-session ownership rule.
Host transport 0x00010013 adds provider epochs and filesystem-only reset.
ASTRA_STATUS_LOST (21) maps to ASTRA_VFS_ERR_LOST and POSIX ESTALE. A LOST
reply with a nonzero result value identifies a lost scope/session; a lost
individual handle has zero there and does not fail a healthy accelerator.
The public library layouts remain ABI 12.0.

Provider resets close Linux descriptors immediately and retain owner-bound
disconnected handles until close or owner death. SHARE/ADOPT can inherit a
disconnected description, but never reopen its old path. HostFS retains
grant ports, reclaims the provider and remints scopes after checking the
recorded root's volume, inode and generation. A replacement root is refused.
HostFS death revokes those grants permanently. Supervisor restarts managed
processes whose delivered namespace depended on a republished service;
applications retain LOST on their old grants.

## Filesystem Kit ABI 9.0 and libc ABI 4.0

Host transport `0x00010011` adds `ASTRA_HOST_CAP_FS_DESCRIPTIONS` (bit 13)
and `ASTRA_HOST_CAP_FS_DIRECTORY_BATCH` (bit 14),
metrics version 4, and SEEK, SHARE, ADOPT and FILE_FLAGS. Storage protocol 32
adds their native operations and `ASTRA_VFS_FEATURE_DESCRIPTIONS`. Its reply
has separate 64-bit position and share-token fields and 32-bit open flags;
`node_size` remains node metadata. The reply is 368 bytes; payload capacity
remains 208 and extended metadata remains 72 bytes. NONBLOCK is open bit 8;
FILE_FLAGS may update only APPEND/NONBLOCK.

`filesystem.library.9` changes the I/O callback contract to carry sequential
flags and a resulting position. The provider owns a shared description's
position, directory cursor and mutable status flags. Sequential I/O is one
transaction; positioned I/O preserves the position and positioned writes
ignore append. A random one-use SHARE token retains a pending reference;
ADOPT consumes it only through the same grant. DISCARD uses the original
owner and works after that owner's file closes. Owner/session loss discards
pending references; already adopted owners retain their descriptions.
READDIR now returns only packed native directory records. One command holds
the description lock for its complete batch, and a record that does not fit
is rolled back before the lock is released. `ASTRA_VFS_CURSOR_CURRENT` reads
the shared stream without replaying a caller's cached cookie. An explicit
rewind uses SEEK, so every owner sees the rewind.

All Filesystem exports use `ASTRA_FILESYSTEM_9.0`. Filesystem.kit 6.0.0 and
every in-tree dependent manifest require major 9. The POSIX fork prepare/
release contract and opaque file-exec state changed, so libc moves to 4.0,
with `ASTRA_LIBC_4_0` exports and Posix.kit 2.0.0. The fork adoption barrier
prevents parent exit from destroying pending shares. File-exec state version
2 preserves process wait handles and accounting as well as file state.
Older major binaries fail dependency resolution; no dual contract remains.

## Filesystem Kit ABI 8.0

`filesystem.library.8` adds `AstraVfsNodeMeta` to `AstraFileInfo` and
`AstraVfsDirEntry`, and grows the client's embedded wire records. Unknown
fields have clear validity bits; mode zero and full-width link counts are
real values when marked valid. Host volumes use the device's 12-bit major
and 20-bit minor as a stable volume identity across bindings. Inode numbers
come from the provider; birth time distinguishes reused nodes where available.
Allocation and nanosecond timestamps also come from the provider. Synthetic
and bootstrap backends report unknown identity. Batched directory entries
retain their existing compact wire record and report unknown extended metadata.
All in-tree library dependencies require ABI 8; ABI 7 binaries do not load it.

## Filesystem Kit ABI 7.0

`filesystem.library.7` adds `AstraVfsClient.features` (what the client's
HELLO answered) and `AstraVfsPortThreadState.received_handle`, so both
layouts change and the major moves; and `astra_vfs_grant_derive(client,
root, rights, &handle)`, which a client connected to a grant uses to make a
narrower one. A lane client sends GRANT_DERIVE to the grant's port on the
lane its HELLO bound, never to its lane. The process exec state carries
`features` (exec state version 7). Kit 6 binaries do not load it.

## Filesystem Kit ABI 6.0

`filesystem.library.6` brings the lane back, scoped. `AstraVfsClient`
regains `port_direct_address`, `port_direct_area`, `port_direct_scope` (the
grant's scope handle), `port_direct_session`, `port_direct_detached`, the
`direct_backend_*` fields and `port_accelerator_ops`; `port_direct_lock` is
gone, because the lane's lock belongs to the process-wide transport.
`AstraVfsPortLane` regains its direct area. `AstraVfsPortAcceleratorOps.connect`
takes a scope. Kit 5 binaries do not load it.

## Filesystem Kit ABI 5.0

`filesystem.library.5` removes the host direct path. `AstraVfsClient` loses
the `port_direct_*`, `direct_backend_*` and `port_accelerator_ops` fields and
`AstraVfsPortLane` loses its direct area, so the layouts change and the major
moves. Storage protocol 27 never attaches a capability to HELLO: every request
for a host volume reaches the provider through HostFS, which enforces the
grant (docs/FILESYSTEM_PRODUCTION_PRD.md FS44). Connect with
`astra_vfs_port_connect` (eager) or `astra_vfs_client_connect_service` (HELLO
fused with the first operation).

## Filesystem Kit ABI 3.0

ABI 3 replaces the fixed startup-sized `AstraAssignTable`
array with a growable pointer/count/capacity table and adds
`astra_assign_table_destroy`. The startup page bounds only the namespace
records delivered at launch; aliases, union members, and mounts added later
grow until allocation fails. All in-tree consumers require ABI 3, and the
obsolete ABI 2 symbol versions and compatibility path are removed.

## Streams Kit ABI 1.1

`streams.library.1` ABI 1.1 adds
`astra_stream_source_rebind_storage`, which moves unread circular-buffer data
into larger caller-owned storage without imposing a library growth policy.
Failure preserves the source and both buffers.

`AstraStartupCapability` grew from 28 to 92 bytes to add
`char root[ASTRA_CAPABILITY_ROOT_MAX]` as its last field.
`ASTRA_CAPABILITY_ROOT_MAX` is 64, and userspace's `ASTRA_ASSIGN_ROOT_MAX`
(`sw/userspace/vfs/include/astra/vfs_assign.h`) is defined *as* that same
constant, a textual alias rather than a second number tied to the first by a
`_Static_assert` — there is no second definition, so there is nothing that
could drift out of sync with the first in the first place. The root is
normalised, mount-relative, with no leading separator: `""` for the mount's
own root, `"local/commands"` for a directory inside it. The kernel copies it
(`astra_capability_root_set`) into the published startup block and **never
reads it — the same contract `flags` already has**: what a name means is the
launcher's statement to the child, not the kernel's business.

`AstraLaunchGrant`, the array a caller passes to
`ASTRA_SYSCALL_PROCESS_CREATE` (48), is the same 92 bytes and carries the
same `root` field for the same reason and under the same contract. A grant is
where a root enters the system from outside, so the syscall refuses
(`INVALID_ARGUMENT`) a root field with no NUL in it, a leading `/`, or any
`..` component, before copying it into the record the kernel eventually
publishes; resolution's own `..` rule is separate and unaffected.

`ASTRA_LAUNCH_GRANT_MAX` is 12 — the terminal service also passes its current
directory and read-only `PROC:` process view. `sw/kernel/process.h`'s
`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` is a **textual alias** of it, not a
second number: the two disagreeing once already let a launch of more than
four grants fail with `INVALID_ARGUMENT` from inside the loader, naming
neither the grant nor the reason, latent for four tasks before it was found.

## Machine ABI

- CPU: MC68040, `-m68040`, software floating point.
- Integer byte order: big-endian.
- `char` 8 bits, `short` 16 bits, `int`, `long`, and pointers 32 bits.
- Public 64-bit values are aligned to 4 bytes, not a compiler-selected 8-byte
  boundary. Public headers use fixed-width integer types and static assertions.
- Stacks are at least 4-byte aligned at every C call boundary.
- No FPU register, `long double`, C bitfield, packed hot structure,
  compiler enum, raw pointer, or kernel address crosses an ABI boundary.

All reserved fields are written as zero and ignored on input unless a protocol
version says otherwise. Structure input begins with `size`; the kernel accepts
only the documented minimum through maximum and never reads beyond `size`.

## Host-device ABI 1.5

`sw/include/astra/host.h` defines the guest/host command ABI. Version 1.5 adds
the `ASTRA_HOST_CAP_METRICS` capability and service 2 without changing the
filesystem service. `ASTRA_HOST_METRICS_SNAPSHOT` returns one zero-reserved,
352-byte `AstraHostMetricsSnapshot`: a coherent set of 42 split-64-bit block,
host-channel, and per-filesystem-operation counters. The guest validates the
advertised capability, exact result size, version, value count, and every
reserved field before accepting it.

The developer-facing representation is the independent 144-byte
`AstraMetricRecord` from `sw/include/astra/metrics.h`. It carries a versioned
header, bounded NUL-terminated group and value names, and one split 64-bit
value. `METRICS:snapshot` is a sequence of those records; consumers need no
hostfs-private structure knowledge.

## Trap ABI 0.6

The syscall instruction is `TRAP #15`, vector 47.

| Register | Entry | Return |
|---|---|---|
| `D0` | syscall number | result code |
| `D1-D5` | scalar arguments, sizes, or user logical addresses | syscall-defined values |
| `A0-A1` | caller scratch | volatile |
| `D2-D7/A2-A6` | caller state | preserved by C-compatible wrappers |
| USP | user stack | preserved unless the syscall explicitly changes it |

The entry stub saves all `D0-D7/A0-A6`, USP, PC, and sanitized user SR. Kernel
code never trusts an address solely because it arrived in an address register.
Pointer arguments are copied through `copy_from_user`/`copy_to_user`.

Current syscall numbers are provisional until the first NDK ABI release:

| Number | Name | State | Contract |
|---:|---|---|---|
| 0 | `QUERY_ABI` | CURRENT | `D1=0x00010043`, `D2=process handle`, `D3=calling-thread handle` |
| 1 | `PROGRESS` | K1 TEST ONLY | monotonic test progress, not a product ABI |
| 2 | `YIELD` | CURRENT | voluntary rotation behind equal-priority peers; higher priorities still win |
| 3 | `PROCESS_EXIT` (`EXIT` compatibility alias) | CURRENT | terminates the calling process and all of its threads |
| 4 | `CLOSE` | CURRENT | closes `D1` in the caller's handle table |
| 5 | `CLOCK_MONOTONIC` | CURRENT K4 | returns signed monotonic nanoseconds in `D1:D2` (high:low) |
| 6 | `EVENT_CREATE` | CURRENT K4 | `D1=flags`, `D2=rights`; returns handle in `D1` |
| 7 | `SEMAPHORE_CREATE` | CURRENT K4 | `D1=initial`, `D2=maximum`, `D3=rights`; returns handle in `D1` |
| 8 | `WAIT_ONE` | CURRENT K4 | `D1=handle`, `D2:D3=absolute deadline` (high:low) |
| 9 | `SIGNAL` | CURRENT K4 | `D1=handle`, `D2=release count`; returns woken count in `D1` |
| 10 | `EVENT_RESET` | CURRENT K4 | `D1=event handle`; requires administer right |
| 11 | `CANCEL_WAIT` | CURRENT K4 | `D1=thread handle`; requires cancel-wait right |
| 12 | `THREAD_CREATE` | CURRENT K5 | `D1=entry`, `D2=argument`, `D3=priority`, `D4=rights`; returns handle in `D1` and diagnostic thread ID in `D2` |
| 13 | `THREAD_EXIT` | CURRENT K5 | `D1=exit status`; terminates only the calling thread and does not return |
| 14 | `WAIT_MULTIPLE` | CURRENT K6 | `D1=aligned handle-array pointer`, `D2=count`, `D3:D4=absolute deadline`; returns member index in `D1` and object detail in `D2` |
| 15 | `TIMER_CREATE` | CURRENT K6 | `D1=rights`; returns handle in `D1` |
| 16 | `TIMER_SET` | CURRENT K6 | `D1=timer handle`, `D2:D3=absolute deadline`; returns woken waiter count in `D1` |
| 17 | `TIMER_CANCEL` | CURRENT K6 | `D1=timer handle`; returns woken waiter count in `D1` |
| 18 | `PORT_CREATE` | CURRENT K7 | `D1=max messages`, `D2=max queued bytes`; returns receive handle in `D1`, send handle in `D2` |
| 19 | `PORT_SEND_TRY` | CURRENT K7 | `D1=send handle`, `D2=message pointer`, `D3=message bytes`, `D4=handle-array pointer`, `D5=handle count`; bit 31 of D5 = `ASTRA_PORT_SEND_DISPOSITIONS`, and D4 then points at `AstraHandleDisposition` records |
| 20 | `PORT_RECEIVE_TRY` | CURRENT K7 | `D1=receive handle`, `D2=message output`, `D3=message capacity`, `D4=handle output`, `D5=handle capacity`; returns actual/required message bytes in `D1` and handle count in `D2` |
| 21 | `HANDLE_DUPLICATE` | CURRENT K8 | `D1=source handle`, `D2=requested rights`; returns duplicate handle in `D1` |
| 22 | `AREA_CREATE` | CURRENT K8 | `D1=byte size`, `D2=rights`; returns area handle in `D1` |
| 23 | `AREA_MAP` | CURRENT K8 | `D1=area handle`, `D2=map flags`; returns logical base in `D1` and rounded size in `D2` |
| 24 | `AREA_UNMAP` | CURRENT K8 | `D1=logical mapping base`; removes the complete mapping |
| 25 | `RING_CREATE` | CURRENT K8 | `D1=area handle`, `D2=offset`, `D3=element size`, `D4=capacity`; returns producer in `D1` and consumer in `D2` |
| 26 | `RING_NOTIFY` | CURRENT K8 | `D1=endpoint`, `D2=owned position`, `D3=flags`, `D4=endpoint role`; returns producer and consumer positions in `D1:D2` |
| 27-32 | `IRQ_*` | CURRENT K10 | bounded read, acknowledge, arm, mask, recover, and revoke on an IRQ endpoint |
| 33 | `DEVICE_QUERY` | CURRENT CANDIDATE | `D1=device handle`, `D2=aligned AstraDeviceInfo output`; requires read |
| 34 | `DEVICE_RESET` | CURRENT CANDIDATE | `D1=device handle`; requires administer and advances generation |
| 35 | `DEVICE_REVOKE` | CURRENT CANDIDATE | `D1=device handle`; requires administer; quiesces and resets |
| 36 | `INPUT_READ_TRY` | CURRENT CANDIDATE | `D1=input-device lease`, `D2=aligned AstraInputEvent array`, `D3=capacity 1-16`; returns count in `D1` and overflow flags in `D2` |
| 37 | `PROCESS_INFO` | CURRENT CANDIDATE | `D1=process handle with QUERY right`, `D2=aligned AstraProcessInfo`; copies one 80-byte record including runtime, elapsed nanoseconds, and the last fault PC/address/vector/status |
| 38 | `DMA_CREATE` | CURRENT CANDIDATE | `D1=byte size`, `D2=aligned AstraDmaBufferInfo`; allocates owner-charged contiguous transfer memory, maps it cache-inhibited, and returns a handle closed by `CLOSE` |
| 39 | `BLOCK_QUERY` | CURRENT CANDIDATE | `D1=block lease with QUERY right`, `D2=aligned AstraBlockLeaseInfo` |
| 40 | `BLOCK_SUBMIT` | CURRENT CANDIDATE | `D1=block lease with TRANSFER right`, `D2=aligned AstraBlockRequest`; returns the request handle in `D1` |
| 41 | `BLOCK_COLLECT` | CURRENT CANDIDATE | `D1=block lease with TRANSFER right`, `D2=aligned AstraBlockCompletion`, `D3=request handle`; `WOULD_BLOCK` until the device answers |
| 42 | `CONSOLE_INFO` | CURRENT CANDIDATE | `D1=display lease with QUERY right`; returns character-plane `D1=columns`, `D2=rows` |
| 43 | `CONSOLE_WRITE` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=first cell index`, `D3=cell bytes`, `D4=count`; writes any run that fits in the character plane |
| 44 | `LOG_WRITE` | CURRENT CANDIDATE | `D1=message id`, `D2=flags and level`, `D3=payload`, `D4=length` (at most 24); emits one bounded typed event into the kernel ring as the caller. No handle or right is required; ids 2-15 are the kernel's and refused |
| 45 | `ACTIVITY` | CURRENT CANDIDATE | `D1=0` allocates a new activity, a nonzero value adopts it, and `D1=0xffffffff` clears it; returns the current activity in `D1` |
| 46 | `TRACE_READ` | CURRENT CANDIDATE | drains the caller-authorized kernel event stream with explicit cursor and loss accounting |
| 47 | `DIAGNOSTIC_CONSOLE_OPEN` | CURRENT CANDIDATE | marks the userspace diagnostic reader active so the boot console stops narrating the same stream |
| 48 | `PROCESS_CREATE` | CURRENT CANDIDATE | transactionally loads a protected ELF from caller bytes, copies bounded grants and arguments, and returns a QUERY/WAIT/TERMINATE/ADMINISTER process handle plus process ID; ADMINISTER covers ordinary priority, not DEBUG |
| 50 | `CONSOLE_CURSOR` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=row`, `D3=column`, `D4=visible`; publishes the terminal cursor, accepting `column=columns` as pending wrap |
| 51 | `DISPLAY_SUBMIT` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=aligned AstraDisplayFrameRequest`; submits one nonzero fenced solid, RGB565 DMA-frame, or bounded native Astraea render-batch request |
| 52 | `DISPLAY_COLLECT` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=aligned AstraDisplayFrameCompletion`; returns `WOULD_BLOCK` until the submitted fence completes |
| 53 | `LIBRARY_MAP` | RETIRED | retired whole-image mapping interface; returns `BAD_SYSCALL` |
| 54 | `AREA_DECOMMIT` | CURRENT CANDIDATE | `D1=address`, `D2=length`; releases the committed pages inside a reservation and returns how many went |
| 55 | `CLOCK_REALTIME` | CURRENT CANDIDATE | returns the date as nanoseconds since the Unix epoch in `D1:D2` (high:low); `UNSUPPORTED` when the machine's wall clock is not valid, never zero |
| 56 | `LIBRARY_ATTACH` | CURRENT CANDIDATE | `D1=aligned 44-byte AstraLibraryReference`; maps an exact resident identity, or the newest resident compatible ABI when `LATEST` is requested; returns base/span/transaction handle in `D1:D2:D3`, and `WOULD_BLOCK` on a cache miss |
| 57 | `PROCESS_PRIORITY` | CURRENT CANDIDATE | `D1=process handle with ADMINISTER right`, `D2=priority 1-23`; atomically changes the process default and requeues every live thread, returning the previous priority in `D1` |
| 58 | `PROCESS_CLONE` | CURRENT CANDIDATE | atomically clones the caller with COW private pages and exact clone-safe handles; parent gets child handle/id in `D1:D2`, child gets zero/zero |
| 59 | `RING_READ_TRY` | CURRENT CANDIDATE | `D1=kernel-copy consumer`, `D2=user output`, `D3=capacity` (at most 4 KiB); returns copied bytes in `D1` |
| 60 | `RING_WRITE_TRY` | CURRENT CANDIDATE | `D1=kernel-copy producer`, `D2=user input`, `D3=length` (at most 4 KiB), `D4=flags`; returns copied bytes in `D1` |
| 61 | `VM_PRIVATE_RESERVE` | CURRENT CANDIDATE | `D1=requested/minimum bytes`, `D2=R/RW permissions`, `D3=EXACT/LARGEST`; reserves clone-private anonymous root slots without committing frames and returns base/actual span in `D1:D2` |
| 62 | `VM_PRIVATE_DECOMMIT` | CURRENT CANDIDATE | `D1=address`, `D2=length`; releases whole committed pages inside the anonymous reservation and returns the page count in `D1` |
| 63 | `SIGNAL_CONFIGURE` | CURRENT CANDIDATE | registers the calling process's POSIX signal trampoline, dedicated writable stack, and blocked mask; returns pending and previous blocked masks |
| 64 | `INTERVAL_TIMER` | CURRENT CANDIDATE | arms `ITIMER_REAL` from relative delay/interval nanoseconds in `D1:D2`/`D3:D4`, returning the previous remaining/interval values |
| 65 | `SIGNAL_RETURN` | CURRENT CANDIDATE | restores the kernel-held context saved by the active signal upcall |
| 66 | `PROCESS_EXEC` | CURRENT CANDIDATE | `D1=image`, `D2=image bytes`, `D3=32-byte AstraExecRequest`; atomically replaces the caller's image, startup vectors, private VM, and threads while preserving process identity and eligible handles; success resumes at the new entry and does not return |
| 86 | `PROCESS_SNAPSHOT` | CURRENT CANDIDATE | Initial-supervisor-only fixed-slot `AstraProcSnapshot` capture; `D1=self process handle`, `D2=record array`, `D3=record capacity`, returning the live count in `D1` |
| 87 | `LIBRARY_SNAPSHOT` | CURRENT CANDIDATE | Initial-supervisor-only fixed-slot `AstraProcLibrarySnapshot` capture of resident identities, process mappings, references, and memory; arguments match `PROCESS_SNAPSHOT`, returning the resident count in `D1` |
| 88 | `PROCESS_LOAD_INTERPRETER` | CURRENT CANDIDATE | appends the supervisor-resolved interpreter image to the active streamed process-load transaction |
| 89 | `LIBRARY_LOAD_BEGIN` | CURRENT CANDIDATE | begins one streamed exact-identity library-load transaction and returns its move-only handle |
| 90 | `LIBRARY_LOAD_WRITE` | CURRENT CANDIDATE | writes one positioned immutable source chunk into the active library-load transaction |
| 91 | `LIBRARY_LOAD_MAP` | CURRENT CANDIDATE | validates and maps the completed image, returns base/span, and retains the handle so close can roll the mapping back |
| 92 | `LIBRARY_LOAD_COMMIT` | CURRENT CANDIDATE | seals the validated GNU RELRO range read-only and atomically drops rollback authority |
| 93 | `PROCESS_DYNAMIC_COMMIT` | CURRENT CANDIDATE | `D1=page-aligned combined TLS template`, `D2=exact template bytes`, `D3=power-of-two TLS alignment`, `D4=exclusive page-rounded storage span`; while the interpreted process still has only its initial thread, atomically installs current/future-thread TLS and seals program, interpreter, and template RELRO pages; success transfers the immutable storage interval to the process until exit |
| 94 | `THREAD_INFO` | CURRENT CANDIDATE | `D1=thread handle with QUERY right`, `D2=AstraThreadInfo`; copies one 48-byte record including identity, scheduler state, activity, priorities, counters, and runtime nanoseconds |
| 95 | `LIBRARY_ATTACH_RESIDENT` | CURRENT CANDIDATE | `D1=pointer to fixed-size zero-padded ABI identity`; maps the sole matching resident library, returning `WOULD_BLOCK` when absent or ambiguous so userspace resolves an exact provider and retries with `LIBRARY_ATTACH` |
| 96 | `VM_PRIVATE_COMMIT` | CURRENT CANDIDATE | `D1=address`, `D2=length`; commits every touched page in an existing writable private reservation, with allocation bounded only by available charged frames |
| 112 | `HOST_SCOPE_LIMIT` | CURRENT CANDIDATE | `D1=scope with ADMINISTER`, `D2=owner handle/token limit (0 unlimited)`; INVALID_ARGUMENT while any channel carries that scope |

Areas are at most 4 MiB. This is an address-map allocation unit, not an
application-size policy; the VM publishes and rolls back every MC68040 table
touched by the mapping as one transaction. VFS bulk transfers use this same
bound instead of imposing a smaller protocol limit.

Unknown syscalls return `BAD_SYSCALL`. Invalid values return an error; they do
not panic. `QUERY_ABI` reports revision `0x00010043`; a later revision may add
feature bits before additional calls freeze.

Process and thread priorities are one number, 1 through 27, with 16 as
normal; larger values run first. Bands name its ranges: 1-7 background, 8-19
applications, 20-23 system services, 24-27 media. A process may set itself
anywhere up to the higher of 19 and its launch priority
(`AstraLaunchArguments.priority`). POSIX `nice`, `getpriority`, and
`setpriority` translate that exact scale to nice values -11 through 15, so
there is no second scheduling policy or unrepresentable nice level. Ready and blocked threads are reinserted into their
existing priority/FIFO queues when a process priority changes.

`AstraDeviceInfo` is 24 bytes and naturally four-byte aligned. It contains
size, device ID, class ID, capabilities, generation, device state, lease
state, and a zero reserved field. It exposes no pointer, owner, or kernel
layout. Only trusted bootstrap code can grant the initial physical lease.

`INPUT_READ_TRY` returns `ASTRA_INPUT_READ_OVERFLOW` in `D2` when records were
lost. The successful syscall atomically acknowledges that sticky hardware bit;
the input service must discard or reconstruct held logical state before
publishing later events. Overflow without a queued record returns `OK` with a
zero count. A copy fault does not consume its record or acknowledge overflow.

### Logical input service protocol

`sw/include/astra/input_service.h` defines protocol `INPT`, version 2. A
logical event is exactly 36 bytes with size/version, type/flags, millisecond
timestamp, nonzero service sequence, focus generation, code, normalized
modifier state, and two signed values. Event types are physical key, Unicode
text, pointer motion, pointer button, focus, and state reset. No raw pointer or
compiler-dependent enum crosses the boundary.

Clients connect with `CONNECT=1`, receive `CONNECTED=2`, and select motion,
button, wheel, key, text, and focus classes with an explicit bit mask. Only the
display service may request the seat-owner flag. Other clients are restricted
to pointer classes and require an explicitly delegated `INPUT_SERVICE`
capability.

Events use operation `EVENT=3` and are carried as a normal 24-byte
`AstraMessageHeader` followed by one logical event. The complete message is 60
bytes. `transaction_id` equals the logical event sequence. Keyboard `code` is
a USB HID usage; text `code` is a Unicode scalar value; pointer-button `code`
is an `ASTRA_INPUT_BUTTON_*` value. Pointer motion and pointer buttons carry
the current clipped screen X/Y position. Every event carries the normalized
modifier state captured when that event was created; coalesced pointer motion
retains that snapshot rather than substituting the state at delivery time.

The GUI protocol is version 12. A successful window create returns a private
window-control handle and a coalescing vblank wait handle after accepting the
bounded event sender, content area, and reply capability. The create request
selects a window event mask; `SET_EVENT_MASK` changes it through the private
window-control capability. The vblank handle is always present so subscription
can change without rebuilding window state, but it is signaled only while the
vblank bit is selected. Each 52-byte window event carries the normalized
modifier snapshot in pointer and wheel payloads, and pointer events also carry
client-local and screen coordinates. Every ordinary window subscribes by
default to a complete state snapshot after active, inactive, minimized,
maximized, fullscreen, restored, moved, or resized transitions. A separate resize event is
emitted only when the client extent changes. State, resize, close-request, and
loss-reset messages use the same per-window FIFO.

The flags distinguish down, captured, repeat, synthetic, and loss events.
Clients must discard held-key/button state on `STATE_RESET`. A focus generation
change also invalidates assumptions made under the prior focus owner. Queue
full is bounded behavior, never implicit growth: pointer motion may coalesce,
while loss of a critical event forces a reset before later delivery.

Version 10 added `SET_POINTER_SHAPE` and `SET_POINTER_IMAGE` to that same
private window-control capability. Shapes are the canonical default,
horizontal-resize, vertical-resize, northwest/southeast-resize,
northeast/southwest-resize, text, wait, and custom values. Version 12 added
the diagonal shapes and commits interactive frame-resize state and resize
events when the captured button is released rather than on pointer motion. A custom
image command transfers a second, immutable area containing a normalized 32 by
32 RGBA image and carries an in-bounds hotspot. The display service maps it
read-only, uploads it through the display device, and commits image, hotspot,
position, visibility, and scene state at vertical blank. Applications never
receive the display-device capability or a writable mapping of the server's
copy.

The thread-entry register contract is `D2=initial argument`, `D4=process self
handle`, and `D5=thread self handle`; all other general registers begin at
zero. `THREAD_CREATE` accepts an even logical entry address inside the caller's
immutable executable image. K5 allocates exactly one zero-filled 4 KiB user
stack and an adjacent unmapped 4 KiB lower guard. Stack size is intentionally
fixed in ABI 0.2; accepting an arbitrary size would imply a commitment and
growth policy that does not yet exist. Priority must be 1 through the process
ceiling, which is 23 for an ordinary process. Creation is transactional: on
success the handle and runnable thread become observable together; on failure
the call returns no handle and leaves every stack, frame, mapping, thread, and
quota count unchanged.

A thread handle may contain only `read`, `wait`, and `administer`. `read`
permits status inspection when that call is added, `wait` permits `WAIT_ONE`,
and `administer` permits `CANCEL_WAIT` while the target is blocked. At least
one requested right is required. K5 deliberately has no asynchronous
thread-termination right or syscall. Closing a live thread's final handle does
not terminate it; the execution reference remains until `THREAD_EXIT`, process
exit, or a process-local fault.

K3's relative-cycle timed event remains a historical qualification call and is
not ABI 0.1. K4 removes that path after the handle-backed calls above cover the
same target behavior. Public waits use absolute monotonic nanoseconds only.

K6 `WAIT_MULTIPLE` copies an array of 1 through 16 big-endian 32-bit handles
in the development build. The complete array is validated before any object is
consumed or linked. Success or an object-originated terminal result returns the
zero-based winning member in `D1`; `D2` is zero except that thread death returns
the target's complete 32-bit exit status. Timeout, cancellation, and validation
failure return `0xffffffff` in `D1` and zero in `D2`. A zero deadline polls.
The first ready member in input order wins, including duplicate handles.

K6 accepts events, semaphores, timers, thread death, and process death as wait
members. K7 adds send and receive port endpoints; K8 adds producer and consumer
ring endpoints. Process authority is still a generation-safe process-local
handle;
numeric process IDs are never waitable. Waiting on the calling thread or
calling process is rejected. Thread and normal process death return the exact
32-bit exit status as detail. A process terminated by a fault or other abnormal
path returns its terminal result and zero detail. Process death is
level-triggered while a handle remains open, exactly like thread death.

Timers are one-shot, level-triggered objects from the same page-backed 16-bit
namespace as events and semaphores. There is no creator quota; object and timer
heap metadata grows on demand until memory or the encoded namespace is
exhausted. Timer rights are a
nonzero subset of `read`, `wait`, and `administer`; setting or cancelling
requires `administer`. `TIMER_SET` replaces any prior arm and clears a prior
fired state. A deadline at or before the current time fires immediately.
Expiry wakes every current waiter with `OK` and remains ready until rearmed or
cancelled. `TIMER_CANCEL` removes any arm, clears readiness, and wakes current
waiters with `CANCELLED`. Final close and creator death retain the ordinary
`CLOSED` and `PEER_DEAD` object rules.

Event-create flags are `MANUAL_RESET` bit 0 and `INITIALLY_SIGNALED` bit 1;
every other bit is rejected. An auto-reset event wakes one priority/FIFO waiter
or retains one signal. A manual-reset event wakes every waiter and remains
signaled until `EVENT_RESET`. `SIGNAL` requires release count 1 for either
event type. A semaphore requires `0 <= initial <= maximum <= 0x7fffffff` and
`SIGNAL` atomically releases a nonzero count without exceeding `maximum` after
direct waiter handoff.

## Result model

Zero is success. Nonzero errors are stable semantic classes, not internal enum
values. The initial common set is:

| Value | Meaning |
|---:|---|
| 1 | operation/syscall not supported |
| 2 | invalid argument, size, alignment, or range |
| 3 | stale or invalid handle |
| 4 | rights failure |
| 5 | resource/queue limit reached |
| 6 | would block / try again |
| 7 | absolute monotonic deadline expired |
| 8 | peer or service died |
| 9 | user address fault |
| 10 | operation cancelled or device reset |
| 11 | committed memory unavailable |
| 12 | I/O or physical bus failure |
| 13 | object closed while an operation was pending |
| 14 | output message or handle capacity is too small |
| 15 | the machine cannot answer at all: no clock, no such device |

Subsystem detail is returned in an output record, not encoded into ad hoc
negative values.

## Handles and rights

Handles are opaque unsigned 32-bit values scoped to one process. Zero is
invalid. ABI 0.1 uses bits `[31:8]` as a nonzero 24-bit generation and bits
`[7:0]` as a one-based slot. A process may never infer object type, rights, or
ownership from the number. The kernel checks generation, occupied state, type,
rights, and process table on every use.

The generic rights namespace is:

| Bit | Right |
|---:|---|
| 0 | read/query |
| 1 | write/modify |
| 2 | map |
| 3 | signal/send |
| 4 | wait/receive |
| 5 | transfer |
| 6 | administer/reset |
| 7 | debug/inspect |

Object protocols may narrow these rights but cannot reinterpret a bit.
Transfer is atomic and moves authority by default. Send first reserves one
fixed message slot and detached-authority records, then validates the complete
source set. Either the complete message and every source handle move into the
queue in one serialized commit, or every source remains usable by the sender.
Receive reserves every destination slot before user copy; either all slots are
published with one dequeued message or the message remains queued and no new
handle is usable. K8 `HANDLE_DUPLICATE` is non-destructive: the source must
carry `transfer`, the requested rights must be a nonzero subset of the source,
and the object must explicitly support retaining another reference. Failure
publishes no destination handle and leaves the source unchanged.

Event and semaphore creation accepts only `read`, `signal`, `wait`, and
`administer`. Waiting requires `wait`; signaling requires `signal`; event reset
requires `administer`. Closing one's own handle never requires an additional
right. Thread wait cancellation requires the generic `administer` right and
cannot target a thread for which the caller has no process-local handle.
Timer creation accepts only `read`, `wait`, and `administer`.

A receive endpoint has `read`, `wait`, and `administer`; it is not
transferable. A send endpoint has `read`, `signal`, `wait`, and `transfer`.
`PORT_SEND_TRY` requires `signal`, `PORT_RECEIVE_TRY` requires `read`, and a
port endpoint in either wait call requires `wait`. K7 moves handles and does
not duplicate them or reduce their rights.

An area accepts only `read`, `write`, `map`, `transfer`, and `administer`.
Every mapping requires `read` and `map`; a writable mapping also requires
`write`. Ring creation requires `administer` and returns move-only endpoints.
The producer carries `write`, `signal`, `wait`, and `transfer`; the consumer
carries `read`, `signal`, `wait`, and `transfer`.

Kernel-copy byte-ring endpoints are the deliberate exception to move-only
retention: they are clone-safe and reference counted so post-clone readers and
writers share one byte stream. They are never mapped into userspace and reject
`RING_NOTIFY`; all position changes pass through `RING_READ_TRY` or
`RING_WRITE_TRY` under kernel serialization.

## Versioned structures

Every public input/output object begins with this 8-byte prefix:

```c
typedef struct AstraAbiHeader {
    uint16_t size;
    uint16_t version;
    uint32_t flags;
} AstraAbiHeader;
```

`size` includes the entire structure. `flags` rejects unknown required bits;
optional unknown bits are ignored only when the protocol defines their mask.

The initial copied-message header is exactly 24 bytes:

```c
typedef struct AstraMessageHeader {
    uint32_t total_size;
    uint16_t header_size;
    uint16_t flags;
    uint32_t protocol;
    uint16_t protocol_version;
    uint16_t reserved;
    uint32_t operation;
    uint32_t transaction_id;
} AstraMessageHeader;
```

`header_size` is 24. K7 requires `flags` and `reserved` to be zero.
`protocol_version` belongs to the service protocol rather than the trap ABI.
`total_size` is 24 through 280 bytes, so at most 256 payload bytes are copied.
At most eight handles accompany one message. Larger payloads use an area plus
a bounded producer/consumer ring.

`PORT_CREATE` accepts 1 through 8 message slots and 24 through 2,240 queued
bytes. Both endpoint handles publish atomically or neither does. The receive
endpoint remains owned by the creator. The send endpoint may move to another
process through a message once that process already has a route to the port.

`PORT_SEND_TRY` copies and validates the complete message and handle vector
before commit. Success moves every attached handle and enqueues one FIFO
datagram. Every error, including full byte/count capacity, preserves the
source handles and queue charges. Duplicate source handles are invalid.

`PORT_RECEIVE_TRY` returns `BUFFER_TOO_SMALL` with both required capacities
without reserving or dequeuing. After sufficient capacities are supplied it
reserves hidden destination slots, copies both outputs, and publishes those
slots together with FIFO removal. A copy fault cancels the hidden reservation,
leaves the message queued, and exposes no destination handle.

`sw/include/astra/service.h` defines the common protected-service bootstrap
protocol `SRVC`, version 1. A service receives the `SERVICE_READY` capability,
sends one `AstraServiceReady`, and on success attaches the send endpoint
declared by its manifest `serves` clause. Sending the endpoint moves it to the
loader. Failure carries no handle and returns the shared or program-specific
status that kept the service from becoming ready.

`sw/include/astra/event_control.h` defines protocol `EVCT`, version 1. It is a
control capability separate from the read-only `EVENTS:` namespace. A 32-byte
`SET` request carries one subsystem (0 through 7), one runtime threshold
(`debug` through `error`, 0 through 4), and a one-use reply endpoint. Its
28-byte reply returns a shared status. The events service forwards the request
to the process that owns the runtime threshold and returns `OK` only after that
runtime applies it. The change lasts for the current boot; no VFS write or
kernel ABI change is involved.

The events service attaches two endpoints to its successful `SRVC` ready
message: `EVENTS:` first and `EVENT_CONTROL` second. Other current services
attach only their manifest-declared endpoint.

`sw/include/astra/gui.h` defines the userspace `GUI` protocol, version 12. A
108-byte `OPEN_WINDOW` request transfers a read-only content area, a private
event sender, and a reply sender. It carries bounded frame, content format,
chrome recipe, flags, gadgets, title, preview states, and an event mask, with
no pointer. The 36-byte
`WINDOW_OPENED` reply returns status and an opaque identity; success also
transfers a private control sender. A 104-byte `WINDOW_COMMAND` sent through
that capability performs query, frame, z-order, activation,
minimize/maximize/fullscreen, restore, title, event-mask, or close operations. Its 64-byte `WINDOW_STATE`
reply returns the resulting frame, state, flags, z-order, and generation. A
76-byte `WINDOW_EVENT` carries motion, button, wheel, full window state,
resize, close, reset, physical-key, or Unicode text state. Pointer records include both
screen and content-relative coordinates and the event-time normalized
modifier snapshot. Each NDK window owns an eight-record
bounded event port; overflow is explicit rather than unbounded allocation.
The server maps content read-only while the client retains its writable
handle. Pointer-shape commands carry a canonical built-in identifier. A
pointer-image command additionally transfers one immutable area holding the
server-owned normalized RGBA pixels; replacement is transactional, and the old
image remains selected if hardware preparation fails. This protocol is
userspace policy, not a kernel syscall ABI.

`sw/include/astra/vfs_service.h` defines storage protocol `STOR`, version 8,
with version 2 as the rolling-update floor. Version 3 transfers one reply send
endpoint during `HELLO` and reuses the client's receive endpoint for the whole
session; `BYE` releases the service-side endpoint and any bound area. A client
may attach one area with `BIND_AREA`, then use `READ_AREA` for at most 16,384
bytes per request. The service maps the area only for that session and returns
the moved byte count in the ordinary reply. Version 2 keeps its per-request
reply endpoints and inline reads. Version 4 adds `READDIR_BATCH`: one reply
packs bounded `(kind, name-length, name)` records into the existing 192-byte
payload and returns the next backend cursor. Version 2/3 peers retain the
single-entry fallback. Directory offsets are backend cursors in every
supported version, so a scan resumes rather than reopening at entry zero.
Version 5 adds whole-file `READ_PATH`; version 6 adds full node metadata;
version 7 keeps small `READ_PATH` replies inline even after a shared area is
bound; larger requests explicitly use that bound area.
Version 8 permits `HELLO` to carry the first path operation, removing one
round trip for a short-lived process. Older services answer the ordinary
`HELLO`, after which the client retries the operation under the negotiated
session.

`AREA_CREATE` accepts 1 through 2,097,152 bytes, rounds upward to complete 4 KiB
pages, commits and zeroes every frame before publication, and charges the
creator's real commit budget. `AREA_MAP` accepts `READ` or `READ|WRITE` and
chooses a free process-local slot in the area window; the same shared area may
occupy a different logical slot in another process. MC68040 caches are
physically addressed, so those aliases remain coherent. Mapping and unmapping
are complete transactions. A failed descriptor publication, copy, cache/ATC
maintenance step, or quota check restores every frame, descriptor, reference,
and accounting count to its prior state.

`RING_CREATE` places a 64-byte native-big-endian `ARIN` header at a 64-byte
aligned, nonoverlapping area offset. Element size is four-byte aligned from 4
through 4,096 bytes; capacity is a power of two from 2 through 1,024, and the
complete header plus payload must fit the area. The producer alone writes the
producer position and payload publication; the consumer alone writes the
consumer position. `RING_NOTIFY` validates the caller-owned monotonic position,
returns both kernel shadow positions, and wakes the peer. The only current flag
is `ASTRA_BULK_RING_NOTIFY_CORRUPT`, which terminally closes a corrupt ring.
The exact shared-header layout, fence ordering, quotas, and lifecycle are
normative in `SHARED_AREAS_AND_BULK_RINGS.md` and the public NDK headers.

`ASTRA_BULK_RING_CREATE_KERNEL_COPY` selects the POSIX compatibility byte
mode. Its element size is one, its power-of-two capacity is bounded only by the
charged containing area and unsigned position arithmetic, and its endpoints
are not user-mappable. Writes through the 4 KiB transfer maximum may request
atomic publication; larger POSIX writes make a short forward-progressing
transfer. Peer closure, readiness, wait queues, ownership charges, and backing
storage remain the common ring mechanisms.

## Physical input ABI 1.1

`sw/include/astra/input.h` defines the host-to-machine input record. This is a
big-endian hardware ABI between the QEMU/physical adapter and Axiom; it is not
the application input protocol.

```c
typedef struct AstraInputEvent {
    uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
    uint32_t host_generation;
} AstraInputEvent;
```

The structure is exactly 20 bytes with natural four-byte alignment. Header
bits 31..24 are class, 23..16 are kind, and 15..0 are flags. Keyboard values
are USB HID Keyboard/Keypad Usage IDs. Relative pointer values are signed
two's-complement 32-bit deltas; button values use the public normalized button
constants. `device_sequence` contains a 16-bit logical device followed by a
16-bit per-device sequence. Sequence wrap is legal; discontinuity, generation
change, or sticky overflow requires logical held-state repair by the input
service.

The producer owns tail publication and record contents. Axiom owns head pop
and overflow acknowledgement. The 32-slot ring intentionally exposes at most
31 records. The consumer reads all five words before writing `POP_EVENT`.
Application-visible events will be copied or normalized into bounded service
ports; applications never consume this MMIO record directly.

The input IRQ endpoint captures the head record's `device_sequence`, not the
raw valid bit. Completion succeeds when the queue is empty or its current head
has a different sequence; this accepts a new key racing an acknowledgement
without accepting an undrained old key. A consumer drains the device and
acknowledges that IRQ before invoking code which may re-enter its event loop.

## Blocking and time

- Deadlines are signed 64-bit monotonic nanoseconds and are absolute.
- `INT64_MAX` means no deadline; zero means poll without blocking.
- Other negative deadline values are invalid.
- The 12.5 MHz clock advances in exact 80 ns units. Conversion to a timer
  deadline rounds upward, so a wait never expires before the requested time.
- Every blocking call documents cancellation and peer-death results.
- Wait-multiple accepts 1 through 255 handles, the complete per-process handle
  namespace, and returns the lowest input index among simultaneously ready
  objects.
- Port sends support blocking, nonblocking, and absolute-deadline modes. Full
  queues provide backpressure or `WOULD_BLOCK`; they never grow.
- K7 implements blocking port calls in the NDK by retrying the bounded raw
  operation around K6 wait-one/wait-multiple with one unchanged absolute
  deadline. The kernel therefore retains no user pointer and owns no second
  timeout queue.
- A failed send records one per-thread endpoint/queue sequence token. The next
  matching wait blocks until that sequence changes even when generic 24-byte
  writable readiness is already true. This prevents a larger byte-capacity
  failure from spinning or silently defeating its finite deadline. Any
  intervening syscall consumes the token; the NDK emits the try/wait pair
  without an intervening call.
- K8 ring endpoints use the same wait operations. A producer is ready when the
  ring has capacity and a consumer is ready when data is available. Peer close,
  creator death, area revocation, and detected corruption wake waiters with the
  ring's defined terminal result.

For one synchronization wait, exactly one serialized terminal transition wins:

| Winner | Waiting call result |
|---|---:|
| event signal or semaphore release | 0 (`OK`) |
| absolute deadline | 7 (`TIMED_OUT`) |
| `CANCEL_WAIT` | 10 (`CANCELLED`) |
| final handle close | 13 (`CLOSED`) |
| creator-process death observed through another process's handle | 8 (`PEER_DEAD`) |

The winner removes the thread from the object queue and deadline heap before
making it ready. A later contender observes that no wait remains and never
changes the first result.

`WAIT_ONE` also accepts a thread handle carrying `wait`. If the target is
alive, the caller blocks on the target's bounded death queue using the same
deadline heap and cancellation machinery as synchronization objects. Normal
thread death returns `OK` and places the complete 32-bit exit status in `D1`.
The death state is level-triggered: later waits through a still-open handle
return the same status immediately. A zero deadline polls, and waiting on the
calling thread is rejected as `INVALID_ARGUMENT`.

Exactly one of normal death, timeout, cancellation, or final-handle close wins
for a blocked thread-death wait. Timeout returns `TIMED_OUT`, cancellation
returns `CANCELLED`, and final-handle close returns `CLOSED`; those results set
`D1` to zero. A final handle close does not change the target's execution
state. If `THREAD_EXIT` removes the last live thread, the process enters the
ordinary process-exit state machine and all remaining process resources are
reclaimed there.

`WAIT_ONE` also accepts process and timer handles carrying `wait`. Process
death follows the same level-triggered detail and exact-once arbitration rules
as thread death. A timer wait returns `OK` on expiry, `CANCELLED` on explicit
timer cancellation, and the ordinary terminal result on close or owner death.
All five waitable object classes use the same scheduler registration and
deadline machinery.

## Time page

`sw/include/astra/time_page.h` defines `AstraTimePage`, version 1: one
kernel frame mapped read-only at `ASTRA_TIME_PAGE_ADDRESS` (`0x48000000`,
one page) in every process, so a program reads monotonic and wall time
without a syscall. It is Haiku's `real_time_data`. The kernel maps it at
launch and into the replacement space at exec; fork copies it like every
shared frame. Only the kernel's raw-image processes (`kernel_process_create`,
the qualification kernel's test images) run without it.

```c
typedef struct AstraTimePageCopy {      /* 32 bytes */
    uint32_t monotonic_high;            /* ns since boot */
    uint32_t monotonic_low;
    uint32_t wall_high;                 /* ns since 1970-01-01T00:00:00Z */
    uint32_t wall_low;
    int32_t  utc_offset;                /* seconds east of UTC */
    uint32_t zone;                      /* packed 4-character zone name */
    uint32_t flags;                     /* ASTRA_TIME_PAGE_WALL_VALID (0x1) */
    uint32_t reserved;
} AstraTimePageCopy;

typedef struct AstraTimePage {          /* 80 bytes */
    uint32_t magic;                     /* 0x54494D45, "TIME" */
    uint32_t version;                   /* 1 */
    uint32_t sequence;                  /* copy[sequence & 1] is current */
    uint32_t reserved;
    AstraTimePageCopy copy[2];
} AstraTimePage;
```

The kernel writes the copy readers are not using, fences, and then advances
`sequence`. It does this on every timer tick and every context switch, so the
values trail the clock registers by at most one quantum (5 ms). Wall fields
are meaningful only when `flags` carries `ASTRA_TIME_PAGE_WALL_VALID`.

A reader (`astra_time_page_read`, and `astra_time_now` in the runtime):

1. checks `magic` and `version`, and falls back to `CLOCK_MONOTONIC` and
   `CLOCK_REALTIME` when either differs or the page is absent;
2. loads `sequence` with acquire and copies `copy[sequence & 1]`;
3. fences acquire and loads `sequence` again;
4. retries from step 2 if it moved.

The kernel publishes the magic word last at boot, so a reader never sees a
page with magic and no time. Callers that need the clock register's 80 ns
resolution keep the clock syscalls.

## Log ring

`sw/include/astra/log_ring.h` defines the per-process log ring that carries a
program's lines to the log service (`docs/SYSTEM_LOG.md` section 3). It is a
bulk ring in a 262,144-byte area created `ASTRA_AREA_CREATE_RESERVED`, so a
page commits when a slot in it is first written. The ring sits at offset 0:
the 64-byte `AstraBulkRingHeader`, then 1024 slots of 252 bytes, 258,112
bytes in all.

- **Slot.** A 4-byte commit word, then 248 payload bytes. A writer stores the
  commit word, the slot's ring position + 1, after a release fence.
- **Record.** One to eight consecutive slots, reserved together. The first
  slot's payload is the 40-byte record header and 208 text bytes; each
  continuation slot carries 248 text bytes. Text is at most 1,944 bytes;
  longer text is truncated and flagged `ASTRA_LOG_FLAG_TRUNCATED`.

```c
typedef struct AstraLogRecordFields {   /* 40 bytes, native byte order */
    uint32_t sequence;        /* ring position of the record's first slot */
    uint32_t monotonic_high;  /* from the time page */
    uint32_t monotonic_low;
    uint32_t wall_high;
    uint32_t wall_low;
    int32_t  utc_offset;
    uint32_t zone;
    uint32_t dropped;         /* lines this process dropped, ever */
    uint16_t text_length;
    uint8_t  level;           /* ASTRA_EVENT_LEVEL_* */
    uint8_t  stream;          /* 0 log, 1 stdout, 2 stderr, 3 kernel */
    uint8_t  slots;           /* 1..8 */
    uint8_t  flags;           /* TRUNCATED 0x1, WALL_VALID 0x2 */
    uint16_t reserved;
} AstraLogRecordFields;
```

`sequence` is a ring position, not a record count: a gap between records is
not loss. Loss is the drop count, which also lives in the ring header as the
first `producer_reserved` word (`ASTRA_LOG_RING_DROPPED`). The kernel never
reads ring memory, but this makes a log ring header fail the generic
`bulk_ring.c` validator, which requires the reserved words to be zero; a log
ring is read only through `astra_log_consumer_next`.

Writers inside a process reserve slots with one compare-and-swap on a
process-local counter and advance `producer_position` with CAS over committed
slots, never mid-record. A full ring drops the line and counts it; nothing
waits.

The process hands the ring over with one `PORT_SEND_TRY` to its `LOG`
startup capability. Both messages begin with `AstraMessageHeader`, protocol
`ASTRA_LOG_SETUP_MAGIC` (`0x414C4F47`, "ALOG"), protocol version 2:

| Operation | Message | Handles |
|---|---|---|
| 1 `SETUP` | 80-byte `AstraLogSetup`: the header, then `magic`, `version`, `area_size`, `ring_offset`, `slot_size`, `capacity` restating the layout above, and a NUL-terminated 32-byte `ident` | the consumer endpoint, then an area duplicate with `read`, `write`, `map` and `transfer` |
| 2 `DOORBELL` | the 24-byte header alone | none |

The service refuses a setup whose fields or mapped size differ from this
build's layout, and a ninth setup from one process (`ASTRA_LOG_SETUPS_MAX`).
The kernel stamps the sender's process id on the message, so a ring is
always attributed to the process that sent it.

Doorbell rule: a writer publishes first and decides on the doorbell after.
After its CAS on `producer_position` it fences sequentially consistent and
sends a doorbell only when `consumer_position` equals its record's first
position, that is, when the service had read everything before it. The
consumer mirrors this: it stores `consumer_position`, fences sequentially
consistent, and re-reads `producer_position` before it decides there is
nothing left to read. A full ring
also rings on its first drop and every 64th after
(`ASTRA_LOG_DROP_DOORBELL_INTERVAL`). `WOULD_BLOCK` on a doorbell is
ignored: a wake is already queued.

Neither side calls `RING_NOTIFY` on a log ring. The kernel ring object exists
so that the consumer endpoint reports `PEER_DEAD` when the producer dies; a
consumer position reported past the kernel's producer shadow would fail the
ring.

## Service protocol rules

Service messages use the same header, transaction IDs, deadlines, and handle
transfer rules. Service names and opcodes are versioned by protocol, not by
kernel build. Bulk rings define producer/consumer ownership, element size,
capacity, cache policy, and fence ordering in their public structure.

If a service endpoint dies, queued synchronous callers wake with `PEER_DEAD`,
uncommitted transferred handles return to senders, and committed handles close
through ordinary object lifetime rules.

## Compatibility gate

Every release generates and compares:

- `sizeof`, alignment, and offset assertions for every public structure;
- syscall number and result-code tables;
- exported NDK symbol/version lists;
- a big-endian byte fixture for each message structure;
- old-client/new-kernel and new-client/old-service negotiation tests.

No provisional K1 test call is promoted into the stable ABI accidentally.

STOR 36/minimum 36 makes ordered LOOKUP accept a retained directory token.
Filesystem Kit ABI 12.0 adds the attached namespace as the first argument
of path-based `astra_filesystem_*_at` APIs. Provider continuations retain
the same 40-link budget and use the shared namespace resolver; POSIX adapters
call that native contract. Joined-assign opens use the member selected by
lookup, including dangling-link shadowing. Rename/link retain namespace
escapes instead of conflating them with a cross-device operation.
