# Axiom kernel and Astra service ABI

Status: provisional ABI contract, revision 0.8 (2026-08-08)

The ABI is big-endian, 32-bit, naturally aligned, and independent of kernel C
layouts. Only the user/kernel ABI and versioned service protocols are stable.
Kernel-internal structures and function calls may change at any time.

## Process startup ABI 2

`sw/include/astra/process.h` defines the 64-byte `AstraStartupInfo` and
92-byte `AstraStartupCapability` records (`ASTRA_STARTUP_ABI_VERSION` 2, up
from 1 as of 2026-08-07 — the union-assigns milestone. The bump was a
coordinator instruction given during that milestone's execution and appears
in no plan or spec; this is where it becomes traceable). The initial thread
enters `_start` with the read-only startup-block logical address in `D2`, its
process self handle in `D4`, and its thread self handle in `D5`. The block
carries explicit bounded argument, environment, and initial-capability
tables. Counts are independent of addresses, the capability count is at most
32, and all reserved words are zero. `docs/USERSPACE_RUNTIME.md` defines
validation, ownership, and exit.

The boot-supplied supervisor and every later protected loader use this same
contract. General ELF parsing remains userspace policy; Axiom validates the
final mappings, entry, stack, handles, and startup block before publication.

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

`ASTRA_LAUNCH_GRANT_MAX` is 9 — the terminal service uses its ready endpoint,
display, input, input IRQ, work namespace, two-member `COMMANDS:` union,
events, and event control. `sw/kernel/process.h`'s
`KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX` is a **textual alias** of it, not a
second number: the two disagreeing once already let a launch of more than
four grants fail with `INVALID_ARGUMENT` from inside the loader, naming
neither the grant nor the reason, latent for four tasks before it was found.

## Machine ABI

- CPU: MC68030, `-m68030`, software floating point.
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
| 0 | `QUERY_ABI` | CURRENT | `D1=0x0001000e`, `D2=process handle`, `D3=calling-thread handle` |
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
| 19 | `PORT_SEND_TRY` | CURRENT K7 | `D1=send handle`, `D2=message pointer`, `D3=message bytes`, `D4=handle-array pointer`, `D5=handle count` |
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
| 37 | `PROCESS_INFO` | CURRENT CANDIDATE | `D1=process handle with QUERY right`, `D2=aligned AstraProcessInfo`; copies one 48-byte record |
| 38 | `DMA_CREATE` | CURRENT CANDIDATE | `D1=byte size`, `D2=aligned AstraDmaBufferInfo`; allocates owner-charged contiguous transfer memory, maps it cache-inhibited, and returns a handle closed by `CLOSE` |
| 39 | `BLOCK_QUERY` | CURRENT CANDIDATE | `D1=block lease with QUERY right`, `D2=aligned AstraBlockLeaseInfo` |
| 40 | `BLOCK_SUBMIT` | CURRENT CANDIDATE | `D1=block lease with TRANSFER right`, `D2=aligned AstraBlockRequest`; returns the request handle in `D1` |
| 41 | `BLOCK_COLLECT` | CURRENT CANDIDATE | `D1=block lease with TRANSFER right`, `D2=aligned AstraBlockCompletion`, `D3=request handle`; `WOULD_BLOCK` until the device answers |
| 42 | `CONSOLE_INFO` | CURRENT CANDIDATE | `D1=display lease with QUERY right`; returns character-plane `D1=columns`, `D2=rows` |
| 43 | `CONSOLE_WRITE` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=first cell index`, `D3=cell bytes`, `D4=count` (at most `ASTRA_CONSOLE_WRITE_MAX`); writes one run of character cells |
| 44 | `LOG_WRITE` | CURRENT CANDIDATE | emits one bounded typed event into the kernel ring |
| 45 | `ACTIVITY` | CURRENT CANDIDATE | `D1=0` allocates a new activity, a nonzero value adopts it, and `D1=0xffffffff` clears it; returns the current activity in `D1` |
| 46 | `TRACE_READ` | CURRENT CANDIDATE | drains the caller-authorized kernel event stream with explicit cursor and loss accounting |
| 47 | `DIAGNOSTIC_CONSOLE_OPEN` | CURRENT CANDIDATE | marks the userspace diagnostic reader active so the boot console stops narrating the same stream |
| 48 | `PROCESS_CREATE` | CURRENT CANDIDATE | transactionally loads a protected ELF from caller bytes, copies bounded grants and arguments, and returns a waitable process handle plus process ID |
| 50 | `CONSOLE_CURSOR` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=row`, `D3=column`, `D4=visible`; publishes the terminal cursor, accepting `column=columns` as pending wrap |
| 51 | `DISPLAY_SUBMIT` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=aligned AstraDisplayFrameRequest`; submits one nonzero fenced solid or RGB565 DMA-frame request |
| 52 | `DISPLAY_COLLECT` | CURRENT CANDIDATE | `D1=display lease with TRANSFER right`, `D2=aligned AstraDisplayFrameCompletion`; returns `WOULD_BLOCK` until the submitted fence completes |

Unknown syscalls return `BAD_SYSCALL`. Invalid values return an error; they do
not panic. `QUERY_ABI` reports revision `0x0001000f`; a later revision may add
feature bits before additional calls freeze.

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

`sw/include/astra/input_service.h` defines protocol `INPT`, version 1. A
logical event is exactly 32 bytes with size/version, type/flags, millisecond
timestamp, nonzero service sequence, focus generation, code, and two signed
values. Event types are physical key, Unicode text, pointer motion, pointer
button, focus, and state reset. No raw pointer or compiler-dependent enum
crosses the boundary.

Events use operation `EVENT=1` and are carried as a normal 24-byte
`AstraMessageHeader` followed by one logical event. The complete message is 56
bytes. `transaction_id` equals the logical event sequence. Keyboard `code` is
a USB HID usage; text `code` is a Unicode scalar value; pointer-button `code`
is an `ASTRA_INPUT_BUTTON_*` value. Pointer motion carries the current clipped
X/Y position. Key and text `value_x` carries the modifier mask.

The flags distinguish down, repeat, synthetic, focused, and loss events.
Clients must discard held-key/button state on `STATE_RESET`. A focus generation
change also invalidates assumptions made under the prior focus owner. Queue
full is bounded behavior, never implicit growth: pointer motion may coalesce,
while loss of a critical event forces a reset before later delivery.

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

Timers are one-shot, level-triggered objects from the same fixed 32-slot pool
and eight-object creator quota as events and semaphores. Timer rights are a
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

`sw/include/astra/gui.h` defines the userspace `GUI` protocol, version 1. An
`OPEN_WINDOW` request is 40 bytes and transfers a read-only area plus a reply
sender. It carries bounded x/y/width/height and RGB565 pitch, with no pointer.
The 36-byte `WINDOW_OPENED` reply returns status, a server-issued window ID,
and the presentation generation. The server maps the transferred area
read-only; the client keeps its separately duplicated writable area handle.
This protocol is provisional userspace policy, not a kernel syscall ABI.

`sw/include/astra/vfs_service.h` defines storage protocol `STOR`, version 4,
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

`AREA_CREATE` accepts 1 through 2,097,152 bytes, rounds upward to complete 4 KiB
pages, commits and zeroes every frame before publication, and charges the
creator's real commit budget. `AREA_MAP` accepts `READ` or `READ|WRITE` and
chooses a fixed process-local address from `0x40000000..0x40ffffff`; mappings
of one area use the same logical base in every process. Mapping and unmapping
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
- ABI 0.2 wait-multiple accepts 1 through 16 handles and returns the lowest
  input index among simultaneously ready objects. Sixteen is a hard current
  limit, not an allocation-dependent suggestion.
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
