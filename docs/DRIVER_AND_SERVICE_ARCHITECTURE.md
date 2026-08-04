# Astra driver and service architecture

Status: architecture direction with an implemented, provisional Axiom device-
lease substrate. Userspace service protocols remain unimplemented and
unpublished.

This document defines how Astra OS extends hardware and system formats without
turning Axiom into a policy kernel. It preserves the useful Amiga ideas of
named devices, assigns, asynchronous message requests, explicit replies, and
uniform lifecycle while adding protected address spaces, typed handles,
bounded queues, cancellation, deadlines, and process-level fault containment.

Status words are precise:

- **LOCKED**: architecture decision; changing it requires document review.
- **DIRECTION**: required shape for prototypes, subject to measured evidence.
- **OPEN**: unresolved; not an implementation assumption or ABI promise.

## 1. Governing rule

**LOCKED:** Astra has one common driver lifecycle, ownership model, request
envelope, completion model, wait model, and failure model. Each device class
defines a typed protocol over that common mechanism. A block device, display,
input source, and filesystem do not pretend to support the same operations.

Applications normally use typed NDK libraries. They do not issue raw protocol
messages, receive physical addresses, program MMIO, or depend on an internal
driver layout. Names locate services and resources; generation-safe handles
carry authority and lifetime.

There is no generic `ioctl`, shared kernel pointer, C bitfield, compiler-native
enum, or unversioned extension packet in a stable protocol.

## 2. Privilege boundary

### Axiom supervisor mode

**LOCKED:** Axiom contains only mechanisms that require privilege, atomicity,
protection, or bounded interrupt latency:

- exception, trap, interrupt, and context entry/exit;
- scheduling, timers, waits, synchronization, and process teardown;
- PMMU trees, physical pages, mappings, pinning, cache policy, and safe user
  copies;
- typed handles, rights, object lifetime, ports, shared areas, and
  wait-multiple;
- IRQ allocation, minimal acknowledgement/masking, endpoint delivery, storm
  containment, and revocation;
- validated MMIO accessors and cache/device synchronization primitives;
- DMA-buffer validation, accounting, ownership transitions, cancellation, and
  bounds enforcement;
- minimal device timeout, fence, quiesce, and reset mechanisms required to
  reclaim privileged resources after service failure;
- panic, retained tracing, inspection, and the emergency console.

Axiom does not parse FAT, AstraFS, paths, directories, images, fonts, graphics
commands, windows, SD protocols, or application formats. It does not contain a
stable loadable binary driver ABI.

### Arty Linux host

**LOCKED:** On Arty, minimal Linux platform code owns resources that the Astra
guest cannot safely own directly:

- the PL register aperture and physical interrupt connection;
- the reserved graphics arena and ARM/PL cache transitions;
- QEMU-to-PL transport, device generation, reset, and host fault containment;
- host hardware whose physical protocol terminates in Linux.

This layer is transport plumbing, not Astra OS policy. It exposes bounded,
batched operations to the Astra machine backend and does not interpret windows,
filesystems, paths, fonts, or application commands.

### Astra protected userspace

**LOCKED:** Extensible drivers, class services, format handlers, and policy run
in protected Astra userspace. This includes:

- supervisor/registrar and service lifecycle management;
- block-device scheduling and physical storage transports;
- FAT, AstraFS, RAM filesystem, and future filesystem handlers;
- storage/VFS, paths, mounts, volumes, assigns, and notifications;
- display/compositor policy and the graphics device backend;
- font, image, audio, and other format handlers;
- input, audio, network, media, workspace, terminal, and applications.

Anything third-party developers can add runs in userspace. A failed handler
may lose its requests, mount, or service generation, but cannot corrupt Axiom
or another process.

## 3. Driver layers

**LOCKED:** The word *driver* does not collapse these distinct responsibilities:

1. A **physical driver service** controls transport or hardware such as SD,
   SPI, a graphics engine, input hardware, or a network interface.
2. A **class service** presents stable block, display, input, audio, or network
   semantics independent of the physical implementation.
3. A **policy or format handler** understands filesystems, image formats,
   fonts, codecs, windows, and other higher-level structures.

FAT and AstraFS are filesystem handlers, not SD drivers. The display service
is graphics policy, not an application-visible PL register wrapper. Layering
may be co-located only when measurements justify it and failure containment is
not silently weakened.

## 4. Common lifecycle

### Implemented Axiom lease substrate

The kernel implementation in `sw/kernel/device.c` is the narrow privileged
foundation for this lifecycle. It does not implement driver requests or device
class policy.

- Boot may register at most 8 physical devices, then seals the registry.
- At most 8 exclusive leases exist system-wide and 2 per process.
- Acquisition is a trusted bootstrap operation, not a public syscall.
- Device handles carry explicit read/query, transfer, and administer rights.
- Reset advances generation; revoked leases report peer-dead.
- Process death revokes owned leases, quiesces and resets each target, then
  closes handles. Failed recovery contains the target as `FAILED`.
- Storage is fixed; no heap, physical pages, or duplicate I/O queue is added.

The provisional trap ABI is `DEVICE_QUERY=33`, `DEVICE_RESET=34`, and
`DEVICE_REVOKE=35` at revision `0x00010006`. Requests reuse existing ports,
shared areas, rings, waits, and handle transfer. Discovery and class protocols
remain open.

**DIRECTION:** Every driver service follows the same externally visible state
model:

```text
DISCOVERED -> BOUND -> READY -> QUIESCING -> STOPPED
                         |
                         +-> FAILED -> RESETTING -> READY
```

The common operation families are:

```text
discover/open
query identity and capabilities
create a session
submit a typed request
poll or wait for completion
cancel a request
inspect status and counters
close a session
reset or recover, with explicit authority
```

A service is published as `READY` only when it can accept a request or return a
precise bounded error. Every restart advances its generation. Existing clients
wake with peer-dead/device-lost and never reconnect silently to a new
generation.

Service death performs, in bounded order: reject submissions, mask interrupts,
stop or fence device work, reset when required, terminate old-generation
requests exactly once, revoke mappings and leases, reclaim buffers, then permit
a replacement service to bind.

## 5. Requests and completion

**LOCKED:** Small control records use bounded message ports. Bulk bytes,
pixels, audio, directory results, and command streams use shared areas and
bounded producer/consumer rings. Handles may be transferred to convey
authority. Raw pointers never cross an address-space boundary.

**DIRECTION:** Every protocol request begins with a common fixed-width,
four-byte-aligned header containing at least:

```text
total size
protocol and version
operation code
flags
transaction ID
absolute monotonic deadline, when applicable
reserved zero fields
```

The exact wire structure and integer widths remain **OPEN** until compiled ABI
probes and protocol tests fix them. Every accepted asynchronous request has
exactly one terminal completion. Common terminal results include success,
cancelled, timeout, peer/device lost, media changed, bad request, no resources,
and I/O failure.

Submission is atomic: either the complete request, transferred authority, and
queue charge become visible, or none do. Queue limits exist in both entries and
bytes. Full queues apply backpressure or return would-block. Cancellation,
timeout, close, process death, service death, reset, and late hardware
completion cannot complete or release one request twice.

## 6. Performance contract

**LOCKED:** Userspace isolation must not imply one syscall, IPC message, or
copy per byte, sector, pixel, glyph, primitive, or directory entry.

```text
control and ownership     bounded message ports
bulk data                 shared areas
continuous transfer       bounded rings
completion                fences/events
readiness                 wait-one/wait-multiple
```

Required batching examples:

- storage submits extents or scatter/gather vectors rather than sectors;
- directory enumeration returns bounded batches rather than one entry per IPC;
- graphics submits draw lists and scene generations rather than primitives;
- sprite state is committed as one validated scene generation;
- file data may move through shared cache pages without intermediate copies;
- persistent sessions reuse preallocated queues and mappings.

Every hot path gets a target-representative baseline, cycle budget, queue-depth
measurement, bytes-copied count, and regression gate before higher layers
depend on it. Code moves into Axiom only when measurements prove all of these:

1. transition cost materially violates a required workload;
2. batching, shared memory, rings, or co-location cannot recover the cost;
3. the operation requires privilege or bounded interrupt latency; and
4. the kernel implementation remains small, bounded, and independently tested.

Convenience and speculative speed are not sufficient reasons to add kernel
policy. Assembly is appropriate for measured Axiom or service hot paths when
generated MC68030 code demonstrates a material gain.

## 7. Storage stack

**DIRECTION:** Storage uses these replaceable layers:

```text
application
    |
storage/VFS: paths, mounts, volumes, assigns, object identity
    |
filesystem handler: FAT | AstraFS | RAM filesystem | future format
    |
generic block protocol
    |
physical block driver: SD | memory image | future network/device
```

The generic block class provides media identity and generation, logical and
physical block sizes, capacity, read-only/removable state, batched reads and
writes, flush and durability barriers, media notification, cancellation,
timeout, and reset. Exact operation records, queue limits, ordering, and
durability semantics remain **OPEN** for the block protocol specification.

Filesystem handlers implement one node-oriented contract: mount/unmount,
lookup relative to a directory handle, open/create, batched read/write,
directory enumeration, attributes, timestamps, rename/link/unlink,
synchronization, volume information, and notifications. VFS owns paths,
assigns, permissions, mount policy, and open-object identity. A handler owns
on-disk parsing and transactions.

Native logical locations may include `SYS:`, `WORK:`, `HOME:`, `RAM:`, and
`APP:`. Assigns resolve transactionally and are inspectable. An already-open
object remains bound to its handle when an assign changes. The POSIX
personality is another view of these objects, not a second mount authority.

## 8. Graphics stack

**DIRECTION:** Graphics uses these replaceable layers:

```text
application
    |
NDK graphics objects and display protocol
    |
display/compositor service
    |
standard graphics backend protocol
    |
Arty Vega/Astraea backend | desktop emulator backend | future backend
```

Applications receive handles for surfaces, palettes, sprite sets, tile
layers, draw lists, copper programs, scenes, fences, and frame notifications.
The display service owns physical graphics authority, validation,
presentation, arbitration, and recovery. Exclusive fullscreen changes policy
ownership but does not grant raw MMIO or remove validation.

The Arty hardware and desktop emulator consume the same logical work. Backend
capabilities are discoverable, but normal application semantics do not depend
on register addresses or host implementation details. The exact backend wire
protocol and reconnection behavior remain **OPEN** until implementation and
cross-backend tests exist.

## 9. Discovery and naming

**DIRECTION:** The registrar discovers typed, generation-tagged services using
names such as `block.sd0`, `display.0`, `input.keyboard0`, and
`input.pointer0`. Names confer no authority; opening a service returns a typed
session handle with explicit rights.

Volumes and assigns such as `SYS:` and `WORK:` belong to the storage namespace.
Arbitrary devices do not masquerade as filesystems. A compatibility `/dev`
entry maps to a typed adapter handle and never exposes raw MMIO.

## 10. Acceptance before publication

No driver or handler ABI is stable until it has executable tests for:

- version and size negotiation, reserved fields, and big-endian encoding;
- queue saturation, backpressure, deadlines, and cancellation races;
- malformed records, bad handles, stale generations, and invalid shared areas;
- client death, service death, restart, reset, and late completion;
- allocation failure at every request transition;
- bounded memory accounting and return to baseline after teardown;
- representative latency, throughput, copies, context switches, and CPU cost;
- behavior parity across at least two implementations where replacement is a
  stated goal, such as Arty and desktop graphics backends.

The first specifications derived from this architecture should be the generic
driver-service envelope, generic block protocol, filesystem-handler protocol,
and graphics-backend protocol. This document intentionally does not assign
wire opcodes, binary layouts, or final queue sizes before that work is measured.
