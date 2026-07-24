# Astra 68 kernel and service ABI

Status: provisional ABI contract, revision 0.1 (2026-07-24)

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

## Trap ABI 0.1

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
| 0 | `QUERY_ABI` | CURRENT | `D1=0x00010000`, `D2=process handle`, `D3=calling-thread handle` |
| 1 | `PROGRESS` | K1 TEST ONLY | monotonic test progress, not a product ABI |
| 2 | `YIELD` | CURRENT | voluntary rotation behind equal-priority peers; higher priorities still win |
| 3 | `EXIT` | CURRENT | terminates the calling process in K1 |
| 4 | `CLOSE` | CURRENT | closes `D1` in the caller's handle table |

Unknown syscalls return `BAD_SYSCALL`. Invalid values return an error; they do
not panic. A future ABI query returns supported major/minor versions and feature
bits before additional calls freeze.

The provisional thread-entry register contract is `D2=initial argument`,
`D4=process self handle`, and `D5=thread self handle`; all other general
registers begin at zero. This contract is covered by the K3 target image but is
not frozen until public thread-creation calls enter the NDK.

K3's timed-event syscall is an internal qualification number, not an ABI 0.1
operation. It currently accepts a relative CPU-cycle delay solely to exercise
the one-shot scheduler and returns common result 7 on expiry. A public wait
uses an absolute monotonic nanosecond deadline and a handle-backed object; the
relative-cycle form will not be exported through the NDK.

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
- Every blocking call documents cancellation and peer-death results.
- Wait-multiple accepts 1 through 64 handle/event descriptors and returns the
  lowest input index among simultaneously ready objects.
- Port sends support blocking, nonblocking, and absolute-deadline modes. Full
  queues provide backpressure or `WOULD_BLOCK`; they never grow.

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
