# Axiom kernel and Astra service ABI

Status: provisional ABI contract, revision 0.2 (2026-07-25)

The ABI is big-endian, 32-bit, naturally aligned, and independent of kernel C
layouts. Only the user/kernel ABI and versioned service protocols are stable.
Kernel-internal structures and function calls may change at any time.

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

## Trap ABI 0.2

The syscall instruction is `TRAP #15`, vector 47.

| Register | Entry | Return |
|---|---|---|
| `D0` | syscall number | result code |
| `D1-D4` | scalar arguments or sizes | syscall-defined values |
| `A0-A1` | user logical addresses where specified | volatile |
| `D2-D7/A2-A6` | caller state | preserved by C-compatible wrappers |
| USP | user stack | preserved unless the syscall explicitly changes it |

The entry stub saves all `D0-D7/A0-A6`, USP, PC, and sanitized user SR. Kernel
code never trusts an address solely because it arrived in an address register.
Pointer arguments are copied through `copy_from_user`/`copy_to_user`.

Current syscall numbers are provisional until the first NDK ABI release:

| Number | Name | State | Contract |
|---:|---|---|---|
| 0 | `QUERY_ABI` | CURRENT | `D1=0x00010002`, `D2=process handle`, `D3=calling-thread handle` |
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

Unknown syscalls return `BAD_SYSCALL`. Invalid values return an error; they do
not panic. `QUERY_ABI` reports revision `0x00010002`; a later revision will add
feature bits before additional calls freeze.

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
members. Process authority is still a generation-safe process-local handle;
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
| 5 | transfer/duplicate |
| 6 | administer/reset |
| 7 | debug/inspect |

Object protocols may narrow these rights but cannot reinterpret a bit.
Transfer is atomic: all receiver slots and message storage are reserved first;
either every transferred handle is committed once or ownership remains with
the sender.

Event and semaphore creation accepts only `read`, `signal`, `wait`, and
`administer`. Waiting requires `wait`; signaling requires `signal`; event reset
requires `administer`. Closing one's own handle never requires an additional
right. Thread wait cancellation requires the generic `administer` right and
cannot target a thread for which the caller has no process-local handle.
Timer creation accepts only `read`, `wait`, and `administer`.

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
    uint16_t protocol;
    uint16_t header_size;
    uint32_t operation;
    uint32_t flags;
    uint32_t transaction_id;
    uint32_t reserved;
} AstraMessageHeader;
```

`header_size` is 24 for version 1. `total_size` is 24 through 280 bytes, so at
most 256 payload bytes are copied. At most eight handles accompany one
message. Larger payloads use an area plus a bounded producer/consumer ring.

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
