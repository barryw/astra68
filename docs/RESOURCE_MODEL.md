# Astra 68 Resource Model

The Astra NDK presents one ownership and completion model across hardware and
operating-system services. Applications do not coordinate by writing shared
MMIO registers or by agreeing informally on channel numbers.

This is an architectural contract. Individual subsystem APIs may add stronger
rules, but they use the same lifecycle and terminology.

## Lifecycle

1. Discover or open a service.
2. Acquire, create, or import a process-owned handle.
3. Configure the object or submit work through that handle.
4. Poll, wait for, or receive completion and state-change events.
5. Release or close the handle.

Handles are opaque 32-bit values scoped to one process. The kernel validates
the object type, generation, rights, and owner on every operation. Process exit
closes all remaining handles, cancels or detaches outstanding work according to
the subsystem contract, and releases every lease.

The bare-metal NDK uses lightweight in-library handles and direct MMIO. The OS
NDK backend uses kernel objects and system calls. Application source and the
public subsystem API remain the same.

## Automatic cleanup in C

The target toolchain supports GCC's `cleanup` attribute. NDK headers provide
typed `ASTRA_AUTO_*` declarations that initialize a handle and invoke its close
function whenever normal control leaves the scope, including an early `return`
or `goto`. Acquisition and submission functions are marked `warn_unused_result`
so silently discarding an error is a compile-time warning.

```c
ASTRA_AUTO_FRONT_PANEL_LED_LEASE(leds);
AstraResult result = astra_front_panel_acquire_leds(0xff, 0, &leds);
if (result != ASTRA_OK)
    return result;                    /* no lease yet */

result = astra_front_panel_set_leds(&leds, 0x55);
if (result != ASTRA_OK)
    return result;                    /* lease automatically released */
```

Cleanup attributes do not execute after process termination, a crash, or a
non-local jump that bypasses the scope. The kernel process-handle table is the
non-optional backstop and releases everything in those cases. Debug builds also
report handles that survived until process teardown.

## Resource classes

| Class | Examples | Sharing policy |
|---|---|---|
| Observation | buttons, switches, clocks, counters | Many readers; subscriptions receive changes |
| Partitioned lease | LED bits, audio voices, timer channels | Disjoint units can have different exclusive owners |
| Scheduled engine | blitter, DMA, storage queues | Many clients submit jobs; kernel/driver schedules fairly |
| Memory object | surfaces, audio buffers, DMA buffers | Handles carry mapping and access rights |
| Service-owned global | display mode, scanout, mixer policy | One service owns hardware; clients request logical objects |
| Exclusive device | diagnostic UART, recovery SPI | One owner unless a device-specific multiplexer exists |

Hardware divisibility does not imply that software sees raw channels. For
example, applications receive audio voices or streams, graphics surfaces, and
blit jobs. The audio and graphics services decide how those objects map onto
physical channels over time.

## Common behavior

- Acquisition is explicit and reports `BUSY`, `TIMEOUT`, `PERMISSION`, or
  `NO_RESOURCES`; stealing is never implicit.
- A lease has defined granularity. The front panel leases individual LED bits,
  while button and switch state is shared read-only information.
- Mutating calls require a handle carrying the necessary rights.
- Long operations are asynchronous. Submission returns a job or request handle;
  completion is delivered through the common event/wait mechanism.
- Handles use generation checks so a stale value cannot address a new object.
- Drivers bound queue depth, execution time, and priority. A process cannot
  monopolize DMA, blitter, audio, or storage service by flooding requests.
- The PMMU protects device pages. Direct user MMIO is an explicit privileged
  lease, not the normal application interface.
- Resource state is inspectable for diagnostics: owner, rights, queue depth,
  progress, and last error.

## Front-panel application

Buttons and switches are shared observations. LED bits are partitioned leases:
two processes may own disjoint masks, but overlapping acquisition fails. LED
writes are masked by the lease, and releasing a lease returns those LEDs to the
hardware diagnostic display. The OS may reserve selected LEDs permanently.

The initial bare-metal backend implements the same rules in `libastra`; the
future OS backend associates the lease with the calling process and performs
automatic cleanup on process death.

## Work still to specify

The kernel ABI must still lock down the generic wait set, event record, handle
transfer, cancellation, deadlines, and priority inheritance. Those details
will be defined before asynchronous graphics, audio, DMA, or storage APIs are
published. Subsystem APIs must not invent private alternatives in the meantime.
