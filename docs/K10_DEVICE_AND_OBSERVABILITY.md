# K10 device and observability contract

Status: **normative contract for the implemented pre-route K10 candidate**.
`STATUS.md` remains the authority for what is qualified and promoted.

K10 establishes the bounded supervisor mechanisms required by protected
device services. It does not put device policy, protocol parsing, filesystems,
graphics policy, or service restart policy in Axiom.

## Fixed limits and memory cost

The first implementation uses these compile-time limits. Increasing one
requires measured workload evidence and an updated `MEMORY_BUDGET.md` entry.

| Resource | Limit | Backing |
|---|---:|---:|
| Vesta interrupt sources | 32 | hardware-defined |
| simultaneously bound IRQ endpoints | 16 | 16 x 128 B = 2 KiB maximum |
| pending records per endpoint | 4 | embedded in the endpoint |
| endpoints owned by one process | 8 | no separate allocation |
| claimed IRQs awaiting deferred dispatch | 32 | 32 x 16 B = 512 B static ring |
| deferred device classes | 8 bitmap bits | worker state only |
| FTDI command line | 80 bytes including terminator | static |
| monitor response fragment | 128 bytes | static |
| trace storage | 64 KiB total | `.kernel_trace`, allocation-free |
| trace record | 32 bytes | 2,047 records plus 32-byte header |

No K10 hard-interrupt, fault-classification, trace, process-death, endpoint
revocation, or monitor path performs an allocation. Endpoint records are
charged to the endpoint owner when the endpoint is bound because their storage
is embedded in the typed endpoint cache.

## IRQ endpoint object

An endpoint is named only by a process-private generation handle of type
`KERNEL_OBJECT_IRQ`. Diagnostic source numbers and endpoint slots are not
authority. The endpoint structure contains:

```text
wait queue                 12 bytes
four IRQ records           64 bytes
owner/generation           8 bytes
delivery counters and next
  sequence                 16 bytes
reference count             2 bytes
source/state/trigger/IPL,
  vector/queue/flags        10 bytes
reserved                    8 bytes
storm-window timestamp      8 bytes
                           --------
maximum                    128 bytes
```

Each 16-byte IRQ record is `{timestamp_hi, timestamp_lo, status, sequence}`.
Sequence zero is invalid. Sequence reuse is protected by the endpoint handle
generation and is not accepted as an acknowledgement for a replacement
endpoint.

Endpoint states are:

```text
FREE --bind--> MASKED --arm--> ARMED --interrupt--> PENDING
                      ^                       |
                      +---------ack-----------+

MASKED|ARMED|PENDING --revoke--> REVOKING --last reference--> FREE
```

- `FREE`: no source, owner, waiter, record, or live handle.
- `MASKED`: configured and owned, but the Vesta source is disabled.
- `ARMED`: enabled and able to publish a record.
- `PENDING`: at least one record is readable. A level source remains masked.
- `REVOKING`: no new record can be published; all waiters have been woken with
  `ASTRA_SYSCALL_PEER_DEAD`; final handle release returns the slot to `FREE`.

Only these rights are valid:

| Right | Meaning |
|---|---|
| `READ` | inspect the oldest pending record and endpoint counters |
| `WAIT` | use wait-one or wait-multiple for readable/error state |
| `SIGNAL` | acknowledge exactly the oldest returned sequence |
| `ADMINISTER` | arm, mask, or revoke the endpoint |
| `TRANSFER` | move or duplicate a reduced-right handle |

Binding is an internal capability-grant operation. There is no syscall that
lets an unprivileged process claim an arbitrary hardware source. A future
service supervisor installs an already-authorized endpoint handle.

## Hard claim and deferred order

Vesta source configuration uses common vector 80. The common entry decodes the
snapshotted `IRQ_CURRENT` source and vector. Reading that register atomically
claims and masks exactly one selected source. Timer 0 follows its dedicated
bounded scheduler route. Every other source is copied into one 16-byte record
in a fixed 32-entry ring, the IRQ-dispatch worker bit is published, and the hard
handler returns. It performs no device read, protocol work, endpoint delivery,
or waiter wakeup. Because a claimed source cannot be selected again until it is
explicitly re-enabled, one ring slot per Vesta source is sufficient; exhaustion
is an internal controller/queue invariant failure, not a reason to overwrite a
record.

The guarded worker consumes at most four claimed records per pass. It performs
the endpoint or internal-service route below with interrupts enabled, writes
the retained trace, and immediately republishes its work bit when records
remain.

For an edge source:

1. read device status through its registered kernel capture callback;
2. acknowledge the device condition when its contract requires it;
3. write Vesta `IRQ_ACK` for the source;
4. append one fixed record and wake endpoint waiters;
5. leave the source armed unless its record ring is full or its storm budget
   is exhausted.

For a level source:

1. retain the mask established by the atomic claim;
2. read the bounded device status snapshot;
3. append one fixed record and wake endpoint waiters;
4. leave it masked until the owner acknowledges the exact head sequence;
5. the registered completion callback clears or drains the device condition
   before Vesta is re-enabled.

The scheduler timer uses the same source decoder and acknowledgement ordering,
but its retained internal route rearms timer 0 and requests scheduler work
instead of granting the timer to a user service.

Unknown, unbound, mismatched-vector, malformed, or already-revoking sources are
left masked and controller-acknowledged by deferred dispatch where applicable.
Counters and trace records distinguish each reason. The hard handler never
loops over all sources and claims at most one snapshotted Vesta source per
exception entry.

An endpoint ring that is full does not overwrite unread records. It increments
the saturating dropped counter, masks the source, changes the endpoint to
`PENDING`, and wakes the owner with an overflow indication. Independently, a
per-source budget of 64 deliveries in a 125,000-cycle (10 ms at 12.5 MHz)
rolling burst masks the source as a storm even when the owner is acknowledging
quickly enough to keep the record ring drained. A gap longer than the window
starts a new burst. There is no automatic unmask timer.

## Wait, acknowledge, close, and death

IRQ readiness composes with the existing wait-one and wait-multiple machinery.
Testing readiness and registering a waiter use the endpoint wait-queue sequence
so an interrupt cannot be lost between those operations.

`IRQ_READ` returns the oldest record without consuming it. `IRQ_ACK` accepts
only that record's exact nonzero sequence. The acknowledger and queue removal
complete before a level source is re-enabled. A failed device acknowledgement
leaves the record pending and source masked and returns `IO_ERROR`.

Closing a nonfinal duplicated handle drops one reference. Final close, owner
death, or explicit revocation performs this bounded top-level order:

1. set `REVOKING` and reject new operations;
2. mask the Vesta source;
3. wake every waiter with `PEER_DEAD`;
4. discard all pending records.

The guarded worker then processes at most one revoking endpoint per
`DEVICE_RESET` pass. With interrupts enabled it invokes the nonblocking device
quiesce/reset callback and then acknowledges any latched Vesta edge. Success
detaches the source route and allows the typed cache slot to return after its
last reference; handle generation advances before reuse. Failure leaves the
source masked and route attached and retries on a later worker pass. Device
state, controller pending state, DMA frames, and endpoint slots cannot be
reused before successful quiescence and acknowledgement.

## Deferred device work

The existing guarded MSP worker remains the single kernel deferred-execution
authority. K10 extends its bitmap with fixed device classes. Hard IRQs only OR
a bit and make the worker ready. Each service pass atomically snapshots the
bitmap, enables interrupts, processes a bounded batch, remasks interrupts, and
reschedules itself when work remains.

Every producer updates the pending bitmap, signal counter, ready timestamp,
and worker state in one interrupt-masked critical section. A worker service may
signal itself while interrupts are enabled; that update cannot overwrite a bit
published by an interrupt between the bitmap load and store. The worker tests
this exact self-signal path and verifies restoration of the caller's prior
interrupt state.

Each class has a compile-time maximum batch and a retry bit. One busy device
cannot prevent process reap, monitor input, or another device class from being
visited on the same pass. Protocol parsing and bulk queue draining belong in a
protected service, not this worker.

## Typed MMIO

All Axiom device accesses use:

```c
uint8_t  kernel_mmio_read8(uint32_t address);
uint16_t kernel_mmio_read16(uint32_t address);
uint32_t kernel_mmio_read32(uint32_t address);
void kernel_mmio_write8(uint32_t address, uint8_t value);
void kernel_mmio_write16(uint32_t address, uint16_t value);
void kernel_mmio_write32(uint32_t address, uint32_t value);
void kernel_mmio_cpu_sync(void);
uint32_t kernel_mmio_fence32(uint32_t address);
```

Addresses must be in the physical MMIO aperture and naturally aligned for the
width. Accesses are native big-endian and surrounded by compiler memory
barriers. `kernel_mmio_cpu_sync()` emits Motorola's post-write `NOP` with a
memory clobber. It synchronizes the CPU-side external write only. A posted
bridge or engine is complete only after a documented fence/status read or
sequence completion.

Raw volatile device structures may remain in public hardware-description
headers, RTL tests, and boot firmware. Production kernel C may not dereference
them. Host tests bind a semantic register backing store through the same MMIO
API and inject width, alignment, and range failures.

## Physical bus-fault record

Vesta exposes one sticky first-fault record:

| Offset | Register | Contents |
|---|---|---|
| `0x800` | `BUS_FAULT_STATUS` | `[0]VALID [1]TIMEOUT [2]UNMAPPED [3]DEVICE [4]WRITE [6:5]SIZ [10:8]FC` |
| `0x804` | `BUS_FAULT_ADDRESS` | exact physical bus address |
| `0x808` | `BUS_FAULT_TARGET` | decoded target class |
| `0x80c/0x810` | `BUS_FAULT_CYCLES_LO/HI` | coherent first-fault cycle timestamp |
| `0x814` | `BUS_FAULT_LOST` | saturating count of later unrecorded faults |
| `0x818` | `BUS_TIMEOUT_CYCLES` | read-only maximum target deadline, initially 2048 CPU clocks |
| `0x81c` | `BUS_FAULT_ACK` | RW1C valid bit |

The first unacknowledged fault wins. Later faults increment a saturating lost
counter without replacing the diagnostic record. Every CPU bus cycle has a
finite hardware completion deadline. Expiration suppresses any not-yet-issued
local SoC write strobe and asserts `BERR`, but it cannot retroactively cancel a
request already accepted in another clock domain. Each asynchronous target
therefore also requires a bounded target-domain completion, withdrawal, or
reset contract. The USB control bridge withdraws its Wishbone request after
2,048 control clocks, comfortably before the 2,048-CPU-clock outer watchdog at
the production 25 MHz/12.5 MHz clocks. An accepted SDRAM operation is not
cancellable; an SDRAM timeout makes the in-flight write outcome indeterminate
and is classified as a fatal memory-fabric failure rather than an offender-only
user fault.

Target classes are `UNKNOWN=0`, `UNMAPPED=1`, `SDRAM=2`, `USB=3`, `VEGA=4`,
`ASTRAEA=5`, `ASTRAHOST=6`, `UART=7`, `SPI=8`, `PANEL=9`, `BOOT_MEMORY=10`,
`FRAMEBUFFER_GUARD=11`, and `EXTERNAL=12`. `SIZ` and `FC` preserve the raw
MC68030 bus pins. An ACK concurrent with a new fault installs the new record
atomically instead of losing either event.

The vector-2 path classifies faults in this order:

1. active user-copy recovery;
2. PMMU translation/protection status from the Motorola frame;
3. matching valid Vesta physical bus-fault record;
4. unmatched kernel access fault, which is a kernel bug and panics.

A matching recoverable user physical/device fault terminates the offending
process or driver service and begins capability revocation. It never becomes a
fake PMMU translation fault. SDRAM-fabric faults are system-fatal because the
kernel stack, page tables, and process state depend on that memory. A stale or
nonmatching Vesta record cannot classify the current exception. Once PMMU
classification is established, the handler copies the stale record into the
retained trace and acknowledges the hardware record so it cannot hide the next
physical failure.

## Retained trace ring

The trace area is a fixed 64 KiB `.kernel_trace` linker section outside all
allocators. Its 32-byte header contains magic, ABI version, record size,
capacity, next sequence, write index, wrap count, and dropped count. Each
32-byte record contains a commit sequence, 64-bit cycle timestamp, 16-bit event
ID, 16-bit flags, and four 32-bit arguments.

Writers save and raise IPL, write the payload, commit the sequence last, update
the header, and restore the exact prior SR. The ring overwrites the oldest
record after one full wrap; it never waits, allocates, formats text, or invokes
another traced path. Readers reject a record whose commit sequence changed
during the copy.

Stable numeric events cover exceptions, external IRQ delivery and quarantine,
context switches, wakes, syscalls and result codes, handle lifetime, IPC queue
transitions, mapping changes, PMMU and physical faults, endpoint transitions,
device reset, allocation failure, and monitor commands. The 100 Hz scheduler
timer is represented by counters and context-switch events rather than one
trace record per tick, so it cannot flood the retained ring. Panic output
includes the newest 16 committed records.

## Monitor transports and commands

One parser and command table serve both transports:

- the FPGA FTDI UART works before user services and is handled through the
  UART-RX IRQ plus bounded deferred worker;
- AstraHost carries monitor request/response frames over SPI. The ESP and FPGA
  never use UART to communicate.

Both transports use bounded input/output queues, explicit truncation flags,
and no dynamic allocation. Commands are read-only unless marked privileged.
When a bounded parser batch leaves immediately consumable input, the monitor
signals its worker class again and continues without waiting for the 100 Hz
timer. `RETRY` is reserved for real output-sink backpressure; the timer later
wakes that class so a full SPI response FIFO cannot create a busy loop or stall
other worker classes.
The minimum command set is:

```text
help  build  threads  runq  current  waits
mem   pages  maps     handles ports   irqs
perf  trace  mmu      faults  devices
```

`mmu` reports SRP, CRP, TC, CACR and relevant PMMU state. `trace` reports or
dumps committed numeric records. Monitor operation, including `mem`, `irqs`,
and `trace`, must continue with zero ordinary free pages. A failed output sink
drops a bounded fragment and records the loss; it cannot stall the kernel.

## Failure injection and performance gates

Host and target tests cover:

- every endpoint allocation site failure and handle-publication rollback;
- stale handle and stale acknowledgement after endpoint slot reuse;
- interrupt between readiness test and waiter registration;
- edge/level acknowledgement order and reassertion during acknowledge;
- record-ring overflow and 64-event storm quarantine;
- unknown source, bad vector, spurious IACK, and disabled source;
- owner death with blocked waiters and duplicated handles;
- device quiesce/acknowledge/reset failure;
- MMIO width, alignment, range, ordering, synchronization, and fence behavior;
- unmapped, timed-out, late, overwritten, and unmatched physical faults;
- trace wrap, torn-read rejection, panic snapshot, and zero-memory operation;
- FTDI overrun, command overflow, SPI backpressure, peer reset, and response
  truncation.

K10 adds fixed measured gates without raising any K1-K9 budget:

| Path | Budget at 12.5 MHz |
|---|---:|
| common hard IRQ, no waiter | 1,250 cycles |
| hard IRQ enqueue for an event that later wakes a waiter | 5,000 cycles |
| endpoint read | 15,000 cycles |
| endpoint acknowledge/rearm | 20,000 cycles |
| one deferred-device batch | 50,000 cycles |
| monitor command dispatch excluding output bytes | 125,000 cycles |

The generic instrumentation also reports maximum interrupts-disabled time,
worker dispatch latency, endpoint wake-to-run latency, storm count, spurious
count, and per-source delivery/ack/drop totals.

## K10 release gate

K10 is complete only when all of the following are true for one exact source
identity:

1. timer, vblank/Vega, AstraHost storage, AstraHost input, USB, and Astraea
   sources traverse the common bounded dispatcher and report exact counters;
2. wait-multiple, acknowledgement, close, process death, overflow, storm, and
   stale-generation tests return every object and waiter to baseline;
3. the typed MMIO and physical bus-timeout paths pass host, component RTL, and
   full pin-level fault tests;
4. the retained trace accepts staged records before PMMU/scheduler startup,
   and both FTDI and AstraHost-SPI monitor transports work after guarded-worker
   startup and under zero ordinary free pages;
5. all K1-K9 correctness, size, soak, and cycle gates remain unchanged;
6. shared architecture/Harte coverage remains green;
7. the complete production feature set routes at exact 12.5 MHz CPU and
   60 MHz SDRAM with no waiver; and
8. two independent volatile ULX3S loads pass full POST, SDRAM, HDMI, K1-K10,
   interactive FTDI/SPI monitor checks, IRQ stress, and exact build identity.
