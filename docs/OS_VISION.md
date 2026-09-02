# Astra OS vision and architecture principles

Version 0.3 — living design document

Astra OS is the working name for the native operating system of Astra 68; the
final product name remains open. This document defines what the operating
system is meant to be, the values that guide it, and the architectural traps it
must avoid. It is not yet a syscall specification, an ABI promise, or a detailed
implementation plan.

The evolving privileged-core contract and its research gates are maintained in
`docs/KERNEL_SPEC.md`. That specification must implement this vision rather
than silently redefining it.

Focused userspace direction is maintained in:

- `docs/USERSPACE_ARCHITECTURE.md`;
- `docs/DESKTOP_AND_UI.md`;
- `docs/TERMINAL_AND_POSIX.md`;
- `docs/APPLICATION_AND_KIT_MODEL.md`;
- `docs/USERSPACE_BUDGET.md`;
- `docs/RESOURCE_MODEL.md`.

Those documents refine this vision. They do not claim that the current kernel
can yet start the described services or that their protocols are stable.

Status markers used here:

- **LOCKED** — direction explicitly established by the project owner.
- **DIRECTION** — strong working design that should guide prototypes but still
  requires a focused design and acceptance decision.
- **OPEN** — intentionally unresolved.

## 1. North star

**LOCKED:** Astra OS is a single-owner, fully preemptive, protected,
media-first personal operating system, co-designed with Astra 68 hardware.

It should combine:

- the responsiveness, consistency, developer friendliness, and media focus
  people valued in BeOS;
- the unusually close hardware/software fit that made the Amiga feel like one
  coherent computer;
- process isolation, fault containment, defensive interfaces, and testing
  practices learned over the following four decades.

It is not an Amiga clone, a classic Mac clone, a BeOS reimplementation, or a
small Unix workstation. Proven ideas may be adopted, but Astra's native object
model, services, user experience, and relationship with its chipset must form a
coherent system of its own.

The intended feeling is immediate and understandable: the computer belongs to
one person, boots into a useful environment quickly, remains responsive under
load, exposes its abilities cleanly to developers, and survives broken
applications without losing the session.

A useful one-sentence product thesis is:

> Astra is a protected, inspectable, multimedia-native creative computer where
> hardware resources are first-class system objects.

## 2. Product principles

### 2.1 One person owns the computer

**LOCKED:** Astra is a personal computer for one local owner, not a timesharing
host or server with multiple local users.

The native system does not need Unix-style user IDs, groups, `root`, `sudo`,
setuid executables, login sessions, or permissions designed to separate several
people sharing one machine. The owner can inspect, modify, recover, and develop
for the whole system.

Single-owner does **not** mean single trust domain. Applications, services,
downloaded code, network peers, file parsers, and DMA engines can be faulty or
hostile. They remain isolated from one another even though all ultimately serve
the same person.

### 2.2 Preemption and protection are foundational

**LOCKED:** There is no cooperative-multitasking phase in the native OS. The
first real scheduler is preemptive, and the first application environment is
protected.

Multitasking, address-space isolation, guarded kernel state, and recoverable
application faults are not features to bolt on after a single-task system has
defined unsafe APIs. Bring-up code may run alone, but it must lead directly to
the protected execution model.

### 2.3 The system is networked

**LOCKED:** Network connectivity is a first-class product capability.

Networking must include standard protocols and useful local-development flows,
not an Astra-only island. Network input also makes defensive parsing,
authentication, resource limits, and service isolation mandatory from the
beginning.

### 2.4 Hardware and software fit together

**LOCKED:** Astra OS targets Astra 68 rather than an open-ended universe of
generic computers. It should exploit the actual chipset and coprocessor instead
of hiding Vega, Astraea, Vesta, and AstraHost behind abstractions designed for
unrelated hardware.

This does not authorize ordinary applications to manipulate unrestricted MMIO.
The OS exposes meaningful native resources—surfaces, scenes, sprites, copper
programs, audio voices, DMA buffers, and raster or media events—through safe,
versioned interfaces.

### 2.5 Developer experience is a product feature

**LOCKED:** A coherent SDK, excellent documentation, useful diagnostics, fast
deployment, and understandable behavior are part of the operating system, not
optional polish.

A developer should be able to discover the system, write a small native
application without learning historical accidents, deploy it over the network,
inspect its resource use, and understand a crash from the report alone.

### 2.6 Performance is measured behavior

**LOCKED:** The OS is designed for responsiveness and media work on modest
hardware. Performance claims require measurements and budgets.

The system must measure interrupt latency, wakeup latency, context switches,
IPC, input-to-display latency, audio jitter, queue depth, memory pressure, cache
behavior, and DMA bandwidth. A machine that benchmarks well but becomes
unresponsive under concurrent load has failed this goal.

### 2.7 Responsiveness is a correctness property

**LOCKED:** No untrusted, remote, removable, or failure-prone component may
impose an unbounded wait on another component. Waiting is sometimes necessary,
but it is explicit, interruptible where possible, bounded by a deadline, and
confined to the thread or request that chose to wait.

"Busy" is local, never contagious. A slow file operation may suspend its
worker; it must not freeze the application's event loop, the filesystem
service, the compositor, input routing, or the desktop. A stalled application
may be identified on its own window and terminated as a protected process;
there is no system-wide beachball or busy cursor.

The input path, compositor, service supervisor, cancellation path, and system
inspector retain independent forward progress under CPU saturation, full
queues, storage errors, network loss, and an unresponsive application or
service. Responsiveness budgets are acceptance criteria, not optional tuning.

### 2.8 One coherent personal environment

**LOCKED:** Astra is GUI-first and has a first-class terminal, shell, and
development environment. The desktop and command line are peers over the same
native resources and services; neither is a privileged alternate system.

The environment should preserve the immediacy, compactness, hardware/software
fit, visible resources, and conceptual consistency that made the Amiga feel
coherent without copying Workbench or inheriting AmigaOS's flat address space,
cooperative dependencies, raw pointers, or unrestricted chipset access.

Applications, menus, keyboard bindings, the command palette, shell tools, drag
and drop, and automation should share commands and object identity where they
represent the same action. A user should learn one ownership, completion,
error, and cancellation model and recognize it across the machine.

**LOCKED:** Program size, resident memory, launch time, and hot-path latency are
measured continuously. Small programs should obtain capability from shared
system Kits and hardware-backed services rather than embedding private copies
of the operating system.

## 3. Explicit non-goals

- No multi-user local account model as the native security architecture.
- No cooperative application scheduler.
- No flat, mutually trusted application address space.
- No permanent ROM-resident application toolbox.
- No Linux, Windows, macOS, Amiga, classic Mac, or BeOS binary compatibility as
  the native identity.
- No requirement that the native kernel present POSIX as its internal object or
  process model.
- No raw chipset access for ordinary applications.
- No generic hardware portability layer that erases Astra's useful hardware
  features.
- No demand paging, swap, dynamic kernel modules, or shared-library ABI as
  prerequisites for the first useful system.
- No promise that every development ABI survives unchanged before 1.0.

POSIX source compatibility, a BSD-sockets adapter, emulation, or other
compatibility environments may be valuable later. They must remain layers over
the native design rather than defining it.

## 4. Current hardware assumption

**LOCKED:** Astra OS sees one big-endian MC68030-class CPU with its paged PMMU
and caches. The active Arty target executes that machine through QEMU TCG on
the Zynq ARM processing system. This changes the implementation boundary, not
the guest architecture or ABI. An external Vesta region MMU is not an alternate
OS target.

The QEMU implementation must continue to earn acceptance against Motorola
MC68030 semantics and independent system tests. No emulator-specific semantic
may become part of the Astra ABI.

The active hardware baseline is:

- one big-endian MC68030-class virtual CPU with an integrated PMMU;
- the Arty Z7-20's 512 MiB DDR divided into 128 MiB of Astra guest RAM, 128 MiB
  of graphics RAM, and 256 MiB for Linux and host services;
- full supervisor/user separation and paged address translation;
- no hardware FPU, with soft-float where needed;
- Arty Vega/Astraea graphics as frozen by `GRAPHICS_ARCHITECTURE.md`, including
  1920x1080p60, two-axis ring scrolling, two independently scrolling tile
  layers, and exactly 64 hardware sprites. Sprite 0 is the optional desktop
  pointer and becomes an ordinary sprite when the pointer is disabled;
- Linux-owned SD, host integration, and board services exposed through explicit
  Astra device contracts rather than inherited Nova behavior;
- a separately specified audio subsystem and fixed-point game-math coprocessor;
- enough immutable PL/boot state to report failure before normal services run.

## 5. System structure

### 5.1 Proposed topology

**DIRECTION:** Use a compact hybrid kernel with microkernel discipline rather
than either a policy-heavy monolith or a purist microkernel.

```text
ROM firmware
  POST · recovery monitor · boot selection · image validation
                              │ BootInfo
                              ▼
Kernel mechanisms
  exceptions · VM/PMMU · scheduler · IPC · handles · IRQ/time
  cache/DMA control · minimal device transports
                              │ messages + shared areas
                              ▼
Protected system services
  filesystem · display · media · input · network · settings
  launcher/workspace · package service · debugger/inspector
                              │ native service protocols
                              ▼
Application kits and applications
```

Keep operations in the kernel when they require privilege, precise atomicity,
or extremely low latency. Keep complex policy, parsers, and restartable
subsystems in protected services.

The initial kernel is expected to own:

- exception, trap, and interrupt entry;
- physical and virtual memory management;
- processes, threads, scheduling, and synchronization;
- typed capability handles and IPC;
- monotonic clocks and timers;
- low-level MMIO, DMA, cache-maintenance, and device-reset mechanisms;
- the minimum console and storage path needed for boot and recovery.

Protected services are expected to own:

- filesystem policy and on-disk parsing;
- the window server and compositor;
- media graphs, mixing policy, and device allocation;
- network endpoint brokering, access policy, and application protocols;
- input routing, settings, packages, and the user workspace;
- developer-facing inspection and debug coordination.

The exact boundary is **OPEN** and must be tested against latency, memory use,
restart behavior, and the supervisor-only nature of current MMIO.

### 5.2 Failure-containment ladder

"The kernel does not crash" is an engineering objective, not a literal proof
that defects cannot exist. Astra turns failures into the narrowest possible
recovery action:

```text
application fault  -> terminate or debug that application
service fault      -> restart the service and let clients reconnect
device fault       -> stop DMA, reset the device, restart its service
kernel invariant   -> preserve a useful crash record and reboot safely
hardware mismatch  -> recovery ROM or known-good image
```

User faults, malformed syscalls, invalid pointers, and resource exhaustion must
never be kernel panic conditions. A panic is reserved for corrupted kernel
state or a violated invariant that makes safe continuation impossible.

### 5.3 Kernel sourcing decision

**DIRECTION:** Build the purpose-designed Axiom kernel instead of adapting a
general-purpose Unix kernel, a legacy 68k operating system, or a
single-address-space RTOS.

This is not a commitment to invent every operating-system component. It follows
from the unusually small mechanism kernel created by Astra's other decisions:
the ESP owns SD protocol plus TCP/UDP, while filesystems, networking policy,
graphics, media, settings, packages, and most device policy live in protected,
restartable services. The remaining privileged core is specifically the part
that must match Astra's 68030 PMMU, caches, DMA behavior, capability handles,
bounded IPC, scheduling, and failure model.

The candidate landscape has no implementation that matches both the processor
and the intended architecture:

- NetBSD and Linux have real classic-m68k MMU support, but adopting either also
  adopts a generic Unix kernel architecture, process model, VFS, networking,
  compatibility surface, and much larger policy footprint. NetBSD is the
  strongest fallback and 68k implementation reference.
- FreeMiNT contains useful 68k experience but carries TOS/MiNT compatibility
  and legacy shared-system assumptions that Astra explicitly rejects.
- seL4 and Genode are valuable references for capabilities, isolation, and
  component structure, but neither currently supplies an m68k base; creating
  one would still be major kernel-port work, and a new seL4 architecture would
  not inherit proofs for a different verified configuration.
- RTEMS and FreeRTOS provide proven real-time scheduling and embedded device
  patterns, but their normal single-address-space execution model cannot be
  Astra's protected application foundation. FreeRTOS remains the correct
  choice on the ESP coprocessor.
- Haiku is a source of application, media, and user-interface ideas, not a
  suitable 68030 kernel base or a product identity to inherit.

**LOCKED reuse rule:** Astra should write its kernel, not its universe. Copy
good ideas without importing an entire foreign architecture. Existing code,
algorithms, tests, tools, and file-format implementations should be reused when
they fit Astra and their licenses fit the project's still-open licensing
policy. In particular, mature 68k trap, context-switch, PMMU, cache,
soft-float, and fault-handling implementations should be treated as references
and test oracles rather than ignored.

A feature belongs in the kernel only when privilege, atomicity, protection, or
a measured latency bound requires it there. Complex policy, parsers, and
anything that can be restarted belong in a service. This admission rule is the
guardrail that keeps a custom kernel tractable.

The choice remains evidence-reversible. The first vertical slice must prove
PMMU isolation, preemption, bounded IPC, cache-safe device completion, fault
containment, and service restart within explicit memory and latency budgets. If
that cannot be achieved without the kernel absorbing general filesystems,
network stacks, UI policy, or other large subsystems, reopen this decision;
NetBSD is the default fallback for an honest m68k Unix port rather than a
half-hidden compatibility substrate.

## 6. Firmware and boot boundary

### 6.1 Firmware responsibilities

ROM firmware should:

1. establish a diagnostic-safe machine state;
2. initialize the POST console and recovery UART;
3. run POST without relying on untested SDRAM;
4. read validated persistent boot settings;
5. provide a recovery monitor and boot selection;
6. locate, validate, and load a kernel image;
7. construct a versioned `BootInfo` record;
8. disable the reset overlay and transfer control.

ROM should not become a permanent BIOS service layer for filesystems, windows,
audio, networking, or application libraries. Once control is transferred, the
kernel owns the machine.

### 6.2 BootInfo contract

**DIRECTION:** Define a small, versioned, extensible handoff containing facts,
not policy:

- magic, total size, protocol version, and checksum;
- RAM ranges and all reserved physical ranges;
- ROM location and firmware/build identity;
- machine, CPU, and instantiated-chip capability descriptors;
- boot device and selected image information;
- diagnostic console and initial display state;
- NVRAM generation and reset reason;
- kernel/modules placement and optional command line;
- a bounded copy of the firmware log.

The initial loader may copy an embedded kernel into SDRAM. SD and network boot
can follow without changing the kernel entry contract.

### 6.3 Update and recovery rule

**DIRECTION:** FPGA personality, ROM, and OS images require version manifests,
atomic installation, a known-good recovery path, and rollback after interrupted
or incompatible updates. A checksum detects corruption; authentication of
network-delivered updates requires cryptographic signatures or an owner key.

## 7. Processes, threads, and scheduling

### 7.1 Native execution objects

- A **process** owns an address space, capability table, resource accounting,
  and failure boundary.
- A **thread** is the independently scheduled execution object within a
  process.
- A **service** is a process registered under one or more versioned protocols.
- A **shared area** is an explicitly mapped memory object used for bulk data.

Names remain provisional; semantics matter more than copying BeOS or Unix
terminology.

### 7.2 Preemptive scheduler

**LOCKED:** Native threads are preemptively scheduled from the first protected
multitasking milestone.

**DIRECTION:** The initial uniprocessor scheduler should use bounded,
constant-time ready selection—such as fixed priority queues plus a bitmap—and
support:

- interactive, normal, background, and time-sensitive/media classes;
- round-robin quanta within an ordinary priority;
- per-process CPU accounting and limits that prevent thread-count amplification
  from defeating ordinary scheduling fairness;
- capability-controlled access to time-sensitive priorities so an ordinary
  application cannot promote itself above critical system work;
- blocking waits rather than polling;
- priority inheritance for contended locks;
- high-resolution deadlines and sleeps;
- tickless idle where practical;
- short, measured non-preemptible kernel sections;
- tiny hard-interrupt handlers with work deferred to schedulable context.

Hard real-time guarantees, deadline scheduling, exact priority counts, and
quantum policy are **OPEN**. API contracts should not gratuitously prevent a
future multicore machine, but the first kernel should be optimized and reasoned
about as one CPU rather than carrying speculative SMP complexity.

### 7.3 Process creation and termination

**DIRECTION:** Native process creation is explicit `spawn`, not `fork`:

- executable or bundle identity;
- arguments and environment values;
- explicitly inherited capability handles;
- initial memory and resource limits;
- requested service permissions.

Asynchronous Unix signals are not the native notification model. CPU faults are
synchronous exceptions; ordinary notifications are queued events; cancellation
occurs at defined safe points. A thread is never forcibly destroyed while it
may own process locks. When unconditional termination is required, the process
is destroyed as one protected unit and the kernel reclaims its handles.

## 8. Virtual memory and protection

### 8.1 Initial PMMU use

**DIRECTION:** Use the MC68030 PMMU for isolation before using it for clever
virtual-memory features.

The first implementation needs:

- a permanently mapped supervisor kernel and MMIO space;
- a distinct user mapping root per process;
- an unmapped null region;
- guarded user and kernel stacks;
- read-only kernel and executable mappings where supported;
- explicit shared areas;
- reliable user-pointer copy/validation helpers;
- deterministic process destruction on an unrecoverable user fault.

The K1 qualification image uses 4 KiB pages and remains the comparison oracle.
The stable default targets 8 KiB pages with 4,096 physical frames, half the
frame-metadata cost, and twice the 22-entry ATC reach. A 4 KiB build remains
supported. The 8 KiB target becomes the default only after its table geometry,
fault behavior, internal fragmentation, ATC misses, mapping costs, workload,
route, and board results pass the same gates as the retained 4 KiB image.

Demand paging, swap, copy-on-write, and memory-mapped files are deferred. They
may be added after basic mapping, faults, scheduling, and I/O are demonstrably
correct.

### 8.2 Honest memory accounting

**DIRECTION:** Astra does not overcommit anonymous memory. An allocation or
reservation succeeds only when its backing can be guaranteed.

- Critical kernel and recovery paths retain protected reserves.
- Services and applications have visible budgets.
- Reclaimable caches are distinguished from committed application memory.
- Queues are bounded and apply documented backpressure.
- Memory pressure is reported before exhaustion.
- The kernel does not later kill a random process to honor an allocation it
  previously claimed was safe.

### 8.3 Execute protection limitation

**OPEN:** The MC68030 PMMU has no modern no-execute page permission. Read-only
code and software W^X policy are still useful, but they do not provide hardware
NX for stacks and data.

Before the memory architecture is frozen, evaluate an Astra-specific execute
permission check in SoC glue using the CPU program/data function codes. If that
is not practical, document the limitation explicitly and design network-facing
services with that threat model in mind. Do not modify the CPU's Motorola
architectural behavior merely to invent a private PMMU descriptor bit.

## 9. DMA, caches, and device protection

### 9.1 The PMMU is not an IOMMU

**DIRECTION:** Treat every DMA master as a separate protection concern. Vega,
Astraea, AstraHost transport DMA, OHCI, and any future master can access
physical memory without passing through the CPU PMMU.

Before untrusted services can influence DMA, the platform needs one of:

- per-master physical base/limit fences;
- a small central DMA-region table;
- a trusted kernel submission path that validates every address and length,
  combined with hardware overflow and aperture checks.

Per-master fences are preferred as inexpensive defense in depth. Address-plus-
length arithmetic must detect wraparound. A device fault must identify the
master and offending address, stop further transfers, and raise a recoverable
interrupt.

Only the kernel programs raw DMA registers. A protected graphics, audio,
storage, or network service receives safe queues and memory objects rather than
unrestricted physical addresses.

### 9.2 Cache-coherency contract

**DIRECTION:** Every DMA buffer has explicit ownership and cacheability:

- map it cache-inhibited, or
- transfer ownership through defined clean/invalidate operations.

Never create simultaneous cacheable and non-cacheable aliases of the same
physical memory. Never rely on timing or incidental cache eviction. The kernel
owns cache-maintenance operations, and the SDK exposes safe buffer-transition
operations rather than raw CACR manipulation.

### 9.3 Device lifetime

Device access is leased through kernel-owned handles. On service death the
kernel can revoke submissions, disable interrupts, stop DMA, reset the engine,
reclaim buffers, and start a replacement service. Completion records carry
generation identifiers so stale interrupts cannot complete a newer request.

Loadable third-party kernel drivers are not required for the initial fixed
hardware platform.

## 10. Capabilities, handles, and IPC

### 10.1 Single-owner capability model

**DIRECTION:** Authority belongs to typed handles, not global identity, string
paths, or ambient process privilege.

A handle contains or references:

- an object type;
- explicit rights such as read, write, map, send, configure, or delegate;
- a generation that detects stale reuse;
- kernel-owned lifetime and accounting state.

Applications receive only the handles they need. Opening a document grants a
handle to that document; selecting a directory may grant a directory handle;
ordinary applications do not inherit raw disk, kernel, MMIO, or all-files
authority.

The owner must be able to inspect and override grants without suffering a
prompt for every ordinary action. The exact application-manifest, downloaded-
code quarantine, and consent UI are **OPEN**.

### 10.2 IPC rules

**DIRECTION:** Use bounded message ports/channels for control and shared areas
for bulk transfer.

- Messages are typed, length-delimited, versioned, and pointer-free.
- Queues define capacity and backpressure behavior.
- Backpressure is local to the producer and channel that exhausted its credit;
  it does not stop an unrelated client or shared global dispatcher.
- Sending authority is a transferable handle, not a global name.
- Timeouts use a monotonic clock.
- Every asynchronous request has an identity; cancellation, timeout, and peer
  death have explicit results, and late completions are harmless.
- Services do not invoke arbitrary client callbacks while holding internal
  locks.
- Kernel and service code do not block on IPC while holding unrelated locks.
- Cross-process interfaces prefer asynchronous requests and completions over
  nested synchronous call chains.
- A synchronous IPC operation is permitted only when its implementation and
  dependency path have a documented short bound; it cannot hide disk, network,
  device, or further synchronous service work.

Shared memory never means shared ownership is implicit. Buffer state and the
party allowed to mutate it are part of each protocol.

### 10.3 Native syscall boundary

**DIRECTION:** Keep the kernel syscall surface small and mechanism-oriented. A
single reserved 68k `TRAP` entry with fixed register and memory conventions is a
likely implementation, but the exact calling convention is **OPEN**.

System calls should create, map, wait on, transfer, query, and close kernel
objects. Filesystem paths, widgets, network protocols, and media policy belong
to services rather than an ever-growing syscall table.

There is no generic untyped `ioctl` escape hatch. Extensible operations use
named, versioned messages with explicit sizes and validation.

## 11. Native application model

### 11.1 Kits and protocols

**DIRECTION:** Present developers with a small family of coherent native kits,
in the spirit of BeOS consistency but with Astra's own API and protocols.
Candidate areas include:

- Application and messaging;
- Interface and graphics;
- Storage and queries;
- Media and timing;
- Network;
- Devices and system inspection.

The detailed application, bundle, and shared-code direction is
`docs/APPLICATION_AND_KIT_MODEL.md`. Shared immutable Kit text is a later size
and launch optimization, not a prerequisite for the first protected service.

The stable binary boundary is C-like syscalls and serialized service messages.
C and C++ SDKs may provide ergonomic source-level wrappers. C++ object layout,
name mangling, exception ABI, and virtual tables do not cross process or kernel
boundaries.

### 11.2 Application bundles

**DIRECTION:** A native application is a self-contained, inspectable bundle
containing its executable, resources, metadata, declared protocols,
dependencies, and requested capabilities.

Use ordinary big-endian m68k ELF for executable plumbing unless a concrete need
demands another format. Originality belongs in the application and service
model, not in needlessly replacing a mature object format.

Static linking is acceptable during bring-up. Shared libraries and shared text
pages come after the native ABI is stable enough to deserve sharing.

### 11.3 Interface server

**DIRECTION:** A protected display service owns desktop composition and Vega/
Astraea policy.

`docs/GRAPHICS_ARCHITECTURE.md` and `docs/PRESENTATION.md` define the required
hardware-backed shadow scene, fenced back-surface rendering, active-surface
write guard, vblank promotion, and copper exception.

- Applications render to shared surfaces or submit validated scene commands.
- Drawing submission is batched and asynchronous.
- A font service validates native AFNT and imported fonts, owns immutable
  strikes and caches, performs Unicode layout/shaping, and submits positioned
  glyph runs for Astraea hardware expansion. Applications do not rasterize
  glyph pixels or hand font-file pointers to hardware.
- Damage and presentation are synchronized to display events.
- The compositor never waits indefinitely for an application.
- Moving, exposing, closing, or identifying a stalled window never requires
  cooperation from that application's event thread.
- The compositor retains the last completed surface while an application is
  stalled; any busy indication is local to that application, not global.
- Ordinary windows and an explicit exclusive-fullscreen mode coexist.
- A failed application cannot corrupt the display server.
- A restarted display service has a documented client-reconnection path.

Main-thread or object-owner affinity may be useful, but hidden reentrancy and
nested event loops are not. The SDK should make message ownership and blocking
behavior obvious.

`docs/DESKTOP_AND_UI.md` defines the Amiga-inspired but non-clone workspace,
window, scene, command, and responsiveness direction. Exact chrome, palette,
icons, menu activation, and the final workspace name remain **OPEN** and must be
selected from measured prototypes on the physical 1920x1080 output.

## 12. Graphics and media as native services

### 12.1 Graphics resources

Astra should expose chipset-aware protected objects such as:

- INDEX8, RGB565, and XRGB8888 drawing and presentation surfaces;
- composited windows and fullscreen scenes;
- pixel-scrolled and wrapped framebuffer surfaces;
- validated sets for the production Vega limit of 64 sprites, plus sixteen
  independently selectable 256-entry sprite palette banks;
- font faces, designed bitmap strikes, positioned glyph runs, and resident ROM
  fallback faces;
- validated copper programs;
- blitter command buffers and fences;
- vertical-blank, raster, and presentation events;
- cache-safe DMA buffer areas.

The native API should preserve the character and efficiency of the hardware
without exposing register addresses as the application ABI.

### 12.2 Audio and media

**DIRECTION:** A protected Astra media service owns application-facing media
timing, stream/voice allocation, buffer scheduling, and mixing policy. On the
Arty, a Linux host service performs the production backend work through
bounded, preallocated queues and feeds the single HDMI PCM boundary; no
application talks directly to the host service or raw audio MMIO.

It should expose both conventional streams and native Astra concepts such as
PCM voices, wavetable instruments, synchronized triggers, and media clocks.
Real-time paths use preallocated buffers, bounded queues, and no filesystem I/O,
unbounded allocation, or ordinary-priority blocking.

Audio continuity while the CPU, UI, storage, and network are busy is a primary
system acceptance test, not merely an audio-driver test.

The hosted MC68030/PMMU vCPU is isolated on ARM core 1. Linux IRQs, QEMU I/O,
audio mixing and synthesis, and the fixed-point game-math worker remain on
core 0. Guest-visible audio and math devices enqueue bounded asynchronous work;
they do not perform expensive operations synchronously on the vCPU thread.

## 13. Networking

**LOCKED:** Astra is connected, but networking does not turn it into a
multi-user server.

Use established Ethernet/IP protocols and interoperable formats. Do not invent
private replacements for IP, TCP, UDP, DHCP, or DNS merely to be distinctive.

**LOCKED:** The Linux host owns link setup and the IP implementation. Astra
does not duplicate a TCP/IP stack in the kernel.

A protected Astra network broker owns application-facing policy and object
lifetime. It maps per-process capability handles onto generation-tagged host
endpoint handles and exposes asynchronous operations for interface status,
scan/connect/disconnect, name resolution, TCP connect/listen/accept, UDP
bind/send/receive, shutdown, close, cancellation, and completion events. Host
socket descriptors, native structures, pointers, `errno` values, and vendor ABI
details never cross the machine interface.

Each endpoint has bounded receive and transmit credit. Control, storage, and
network traffic use separate logical channels and queue budgets so a bulk
transfer on one cannot cause unbounded
head-of-line blocking on the others. A host-backend reset changes the transport
generation, invalidates every endpoint, and produces explicit peer-loss events;
handles are never silently reused after reconnect.

The native API supports asynchronous endpoints and completion events. A
BSD-sockets compatibility kit may map onto that API without defining the native
model. Raw packet access is not required for the first system and, if added,
requires an explicit capability.

Placement of TLS, certificate/private-key custody, multicast policy, SoftAP,
IPv6 requirements, and any raw diagnostic interface remains **OPEN**. HTTP,
remote deployment, file transfer, and other application protocols remain
protected Astra services or libraries above the endpoint interface.

Security defaults:

- no network service listens merely because a cable is connected;
- remote development and administration authenticate the owner's key;
- network-facing host code and Astra application-protocol parsers are bounded
  and fuzzed on the host;
- listening sockets, raw packets, and privileged protocols require explicit
  capabilities;
- network failure and offline operation never freeze the desktop.

The first network features should help build the rest of the machine: remote
deployment, logs, debugger attachment, a terminal, and file transfer.

## 14. Storage, settings, and packages

### 14.1 Host storage-controller boundary

**LOCKED:** Linux owns the SD-card electrical and protocol layer. Astra receives
a versioned raw multi-sector block device and never receives host descriptors,
pointers, filesystem structures, or native error layouts.

Astra's writable native filesystem runs in a protected Astra service over the
raw-block interface. Linux and Astra never mount the same partition writable
at the same time. A filesystem nested inside a large FAT file may be useful for
temporary bring-up, but it is not a shipping storage architecture.

The block protocol includes request identifiers, version and feature
negotiation, bounded queues, explicit backpressure, multi-sector transfers,
completion status, media generation/removal state, write protection, timeout
and reset behavior, and a precisely defined flush/barrier operation. An ESP
reset fails outstanding requests and changes the device generation; it must not
hang or crash Axiom.

Because the same ESP owns Wi-Fi and SD, they share a hardware and firmware
failure domain. Wi-Fi/lwIP faults, watchdog resets, malformed network traffic,
and firmware restarts are treated as sudden storage-controller loss. Native
filesystem crash-consistency tests must include resets induced during hostile
and saturated network workloads. Separate FreeRTOS tasks, buffers, queues, and
transport credits prevent ordinary network load from directly consuming the
storage budget, but the ESP firmware remains part of the trusted storage base.

FreeRTOS supplies the fixed-priority preemptive scheduling mechanism, but that
alone is not a performance guarantee. The ESP storage implementation follows
these real-time rules:

- one task owns each stateful peripheral driver and receives work through
  bounded queues rather than contended ad hoc calls;
- latency-critical tasks, queues, stacks, and DMA buffers are statically
  allocated before service begins, using internal DMA-capable memory where
  required;
- interrupt handlers acknowledge hardware, capture minimal status, and wake a
  task; parsing, retries, logging, and filesystem work do not run in the ISR;
- critical sections are bounded and contain no blocking calls;
- storage completion and transport progress are covered by watchdogs and an
  Astra-visible heartbeat;
- firmware update, flash erase, diagnostics, and verbose logging cannot run in
  a storage-critical path and require an explicit quiesced mode when they can
  disturb deadlines;
- on a multicore ESP target, deadline-critical work has explicit core affinity
  and is isolated from network protocol work where the hardware permits; exact
  affinity remains a measured platform choice;
- equal-priority time slicing is never used as a deadline guarantee.

**RULE:** No latency or throughput promise becomes part of the platform
contract until its worst case is measured with concurrent Astra requests,
Wi-Fi/lwIP traffic on the same ESP, card errors and retries, queue saturation,
watchdogs, and recovery paths active. Deadline misses and queue high-water
marks are observable counters, not silent behavior.

### 14.2 Filesystem direction

**LOCKED:** Astra OS reads FAT-family boot and exchange volumes through a
protected filesystem service. Initial support is read-only and must include the
format actually used by the existing 256 GB card without formatting,
repartitioning, or modifying unrelated files. FAT is a compatibility and
recovery format, not Astra's writable system filesystem.

**LOCKED:** The native filesystem is **AstraFS**. It occupies a separate GPT
partition and runs in a protected user-space filesystem service over the
generic raw-block service. Neither Axiom nor AstraHost parses AstraFS. An ESP
reset, filesystem-service crash, malformed volume, or failed transaction must
produce bounded errors and recovery, never kernel corruption or an indefinite
desktop stall.

The first on-disk design starts with 4 KiB filesystem blocks, independent of
the PMMU page-size choice, and provides:

- explicit crash-consistency and durability semantics;
- a checksummed write-ahead metadata journal with transaction sequence numbers,
  replay that is idempotent, ordered data-before-metadata publication, and
  explicit flush/barrier use;
- redundant, generation-tagged superblocks and checksummed metadata;
- atomic replace and rename plus defined `fsync`/volume-sync completion;
- extent-based allocation and indexed directories;
- versioned typed metadata/attributes;
- indexing and live queries where they materially improve the personal
  workspace;
- 64-bit block numbers, file sizes, file offsets, and timestamps from its first
  on-disk version;
- stable object identity independent of a mutable path;
- change notification with sequence numbers and a resynchronization path;
- orphan recovery, online read-only fallback after unrecoverable damage, and
  an offline verify/repair tool; and
- power-loss and controller-reset injection at every transaction transition.

Filename case rules, Unicode normalization, removable-volume behavior, open-
file deletion semantics, data checksums, indexing scope, journal sizing, and the
exact on-disk structures are **OPEN** and require a dedicated `ASTRAFS.md`
contract before any tool commits user data to the native format.

Creating an AstraFS partition is always an explicit provisioning operation.
The system must preserve the existing FAT/exFAT game collection and may use the
remaining card capacity only after showing the exact proposed partition change
and receiving confirmation.

Pathnames are a discovery convenience, not a security token. Services resolve
a path atomically into a handle, and subsequent operations use the handle.

### 14.3 Settings

There is no opaque global registry. Settings are versioned, inspectable,
transactional objects owned by applications or services. System-wide settings
may be coordinated by a service without becoming one hidden database on which
unrelated programs mutate each other's state.

### 14.4 Packages and dependencies

**DIRECTION:** Installation is atomic and rollback-capable. Packages and
application bundles declare exact dependencies and do not run unrestricted
privileged install scripts.

Avoid global in-place library replacement and current-directory library
search. Immutable versioned system kits and application-local dependencies
prevent DLL hell; identical code may still share physical read-only pages when
the VM system eventually supports it.

## 15. API and ABI discipline

### 15.1 Compatibility is earned

Before 1.0, interfaces may change deliberately. After a stable release, the
project must state which boundaries are durable and how deprecation works.

Every durable binary or wire interface includes:

- a protocol or structure version;
- a total byte size;
- fixed-width integer types;
- explicit byte order for stored or transmitted data;
- defined alignment and reserved-zero fields;
- precise ownership, cancellation, timeout, and error semantics.

Never persist or transmit a compiler-native structure. Never place kernel
pointers or C++ object layouts in an ABI.

### 15.2 Compatibility layers remain layers

POSIX calls, Unix signals, BSD sockets, a shell environment, or emulation may
be useful to port software. They should translate into native processes,
handles, messages, and services. They do not gain privileged shortcuts that
weaken the native safety model.

**DIRECTION:** Upstream zsh is the primary modern interactive-shell port target
and Vim is an early full-screen terminal target. Their Unix process, file-
descriptor, PTY, signal, and job-control requirements are supplied by a
userspace POSIX personality over native Astra mechanisms. They do not redefine
the native spawn, capability, messaging, or resource model. The required
process-clone/fork mechanism remains an explicit design gate in
`docs/TERMINAL_AND_POSIX.md`, not an implied kernel feature.

### 15.3 Developer-facing quality

The SDK should include:

- concise API references and complete small examples;
- symbolic, actionable errors rather than unexplained numeric failures;
- a debugger that understands processes, threads, exceptions, and messages;
- a profiler and system-wide trace viewer;
- resource, handle, queue, memory-map, DMA, sprite, and audio inspection;
- reproducible builds and network deployment;
- crash reports containing registers, exception frames, mappings, thread
  states, handles, and recent system activity.

Whether Astra eventually supports native compilation on the machine is
**OPEN**. Cross-development must nevertheless feel like developing for one
coherent computer rather than operating a collection of disconnected tools.

## 16. Responsiveness, time, concurrency, and backpressure

Public operations are classified as immediate, asynchronous, or explicitly
blocking. An innocent-looking getter never hides IPC, filesystem, device, or
network work. UI/event threads use immediate operations and asynchronous
requests; development builds diagnose forbidden blocking calls from those
threads.

- Wall-clock time is for calendar display and timestamps; monotonic time is for
  timeouts and scheduling; media clocks are for sample/frame synchronization.
- Public time values are 64-bit from the beginning.
- Every wait has defined interruption, timeout, and peer-death behavior.
- Every queue is bounded and defines block, reject, coalesce, or drop behavior.
- Worker pools and in-flight request counts are bounded; overload produces
  backpressure or an explicit error rather than unlimited thread creation.
- Cancellation propagates toward the slow dependency, releases reservations,
  and makes any unavoidable late completion safe to discard.
- There are no nested event loops or synchronous service call chains whose
  duration depends on an application, filesystem, network peer, or device.
- No kernel or service lock is held while invoking untrusted code.
- No unrelated lock is held across blocking IPC.
- Lock ordering is documented and mechanically checked where possible.
- The first system performs no demand paging or swap I/O, so a memory access
  cannot secretly block on storage and memory pressure cannot create thrashing.
- Interrupt handlers acknowledge and bound the source before deferring work.
- Interrupt storms are detected, rate-limited, and visible to inspection tools.
- Resource ownership is released automatically when its process dies.
- Long operations publish progress or a bounded next deadline. Lack of progress
  is observable independently of whether a process has technically crashed.

## 17. Reliability, recovery, and testing

### 17.1 Defensive implementation

- All syscall arguments and user pointers are hostile until validated.
- Arithmetic involving addresses, sizes, strides, pitches, and counts is
  checked for overflow.
- Kernel stacks have unmapped guard pages.
- Kernel allocators provide deterministic failure behavior and diagnostics.
- Critical interrupt and fault paths do not depend on pageable or reclaimable
  memory.
- Complex externally supplied data is parsed outside the kernel where
  practical.
- Assertions protect development invariants; release behavior never silently
  continues through known kernel corruption.

### 17.2 Recovery

- A service supervisor starts services in dependency order and restarts those
  whose protocols support recovery.
- Stall recovery escalates without blocking clients indefinitely: cancel the
  request, revoke its handles and DMA, terminate/restart the protected service,
  then reset the device if required.
- An unresponsive application is contained to that application; the compositor
  and workspace remain usable and offer termination or debugging.
- The kernel owns enough device state to stop and reset hardware after service
  death.
- A hardware watchdog recovers genuine hangs.
- Panic information survives soft reset and is rendered by recovery firmware.
- Boot failure counters select a known-good image or recovery monitor.
- Storage writes during panic are avoided unless a separately designed
  crash-safe path exists.

### 17.3 Fail-closed verification

The CPU/MMU audit already demonstrated a project-wide lesson: a test that
prints an assertion but exits successfully is not a test authority.

- Every reported failure produces a failing process status.
- Every checked-in test is enumerated or explicitly classified as unscored.
- A timeout is a failure, not a missing result.
- An unscored run is never counted as a pass.
- Host builds use sanitizers and fuzzing for portable kernel/service code.
- Syscalls, filesystem inputs, packages, media data, and network parsers receive
  malformed-input tests.
- Allocation, I/O, power-loss, device-reset, dropped-interrupt, full-queue, and
  peer-death failures are injected deliberately.
- Simulation, retained hardware builds, and on-board stress use the same
  revision and reproducible manifests.
- Performance regressions fail budgets rather than becoming folklore.

## 18. Footgun register

These are architectural red lines learned from earlier systems:

| Footgun | Astra rule |
|---|---|
| Flat trusted address space | Every ordinary application runs in a protected address space. |
| CPU MMU assumed to protect DMA | DMA is fenced, validated, and resettable independently. |
| Universal `root` or ambient privilege | Authority is carried by typed capability handles. |
| Huge permanent syscall surface | Kernel ABI stays small; policy uses versioned services. |
| Generic untyped `ioctl` | Extensible operations are typed, sized messages. |
| Kernel pointers or structs in public APIs | Interfaces use fixed-width values, IDs, and offsets. |
| C++ ABI across system boundaries | C-like ABI plus serialized protocols; C++ remains an SDK layer. |
| `fork` as native creation | Explicit spawn with explicit handle inheritance. |
| Asynchronous Unix signals | Synchronous faults and queued events with safe cancellation. |
| Forced thread termination | Cancel safely or terminate the whole protected process. |
| Synchronous callbacks under locks | Asynchronous messages; never call untrusted code while locked. |
| Nested synchronous service calls | Request/completion protocols with deadlines and cancellation. |
| Hidden I/O in ordinary API accessors | APIs declare immediate, asynchronous, or blocking behavior. |
| Unbounded queues | Capacity, accounting, and backpressure are mandatory. |
| Unbounded worker/thread creation | Fixed pools, admission limits, and explicit overload behavior. |
| Global busy cursor or app-controlled desktop | Compositor and input remain independent; stalls are local to one app. |
| Unrestricted real-time priority | Elevated scheduling classes require explicit capability and budgets. |
| Memory overcommit | Commit only backed memory; fail predictably at allocation time. |
| Complex drivers/parsers in kernel | Keep privileged transports small; isolate restartable policy. |
| Raw MMIO as application API | Expose safe chipset-native objects and command streams. |
| Implicit cache coherence | DMA ownership and cache transitions are explicit. |
| Global mutable registry | Versioned, inspectable, transactional per-service settings. |
| Global in-place libraries | Immutable versions, explicit manifests, atomic packages. |
| Native structs written to disk/wire | Explicit formats with size, version, endian, and bounds. |
| Wall time used for deadlines | Monotonic clocks drive waits; media uses a media clock. |
| Vague persistence guarantees | Atomicity and durability have precise documented meanings. |
| Compatibility before design maturity | Development interfaces may break until intentionally stabilized. |
| Fail-open tests | Failure, timeout, and missing coverage are visible and nonzero. |
| OS and FPGA updated independently | Versioned manifests, atomic pairing, recovery, and rollback. |

## 19. First vertical slice

The first meaningful OS milestone is not a desktop. It is proof that every
critical boundary works:

1. ROM completes POST and transfers a versioned `BootInfo` to a separately
   linked kernel.
2. The kernel establishes vectors, supervisor stacks, a UART panic console,
   and a physical-page allocator.
3. The kernel constructs its permanent mappings and enables the PMMU.
4. One user process starts with a guarded stack and unmapped null page.
5. The process invokes one syscall through the native trap convention.
6. A timer preempts between at least two user threads/processes.
7. A deliberate invalid user access kills only the offending process and
   produces a complete report.
8. A user process cannot access kernel memory or chipset MMIO.
9. A cache-safe DMA buffer completes a real device operation without corrupting
   unrelated memory.

Only after this slice is reliable should the kernel grow an executable loader,
general IPC services, storage, networking, or the graphical environment.

## 20. Product-signature demonstration

The north-star integration demonstration is:

1. boot through POST into the native workspace;
2. play uninterrupted audio;
3. run multiple CPU-heavy applications preemptively;
4. move and redraw windows smoothly;
5. deploy another application over the network;
6. saturate CPU, storage, and network queues without losing interactive control;
7. deliberately hang, crash, and corrupt one application;
8. move and close its window while the application remains unresponsive;
9. interrupt an ESP storage/network operation and recover it;
10. keep audio, UI, network, and other applications running;
11. inspect an actionable report showing exactly what stalled or failed.

This proves more of Astra's identity than a static desktop screenshot. It
combines responsiveness, hardware integration, developer experience, and fault
containment in one observable result.

## 21. Phased roadmap

### Phase 0 — firmware handoff

- Recovery monitor and kernel loader.
- Versioned `BootInfo`.
- Embedded/RAM-loaded kernel image, later SD/network loading.
- Hardware/OS compatibility manifest and safe fallback.

### Phase 1 — protected kernel spine

- Complete exceptions and panic console.
- Physical allocator and initial PMMU mappings.
- User entry, syscall trap, fault containment, and guarded stacks.
- Timer interrupt and preemptive scheduler.

### Phase 2 — native object substrate

- Processes, threads, handles, ports/channels, events, shared areas.
- Resource accounting, bounded queues, monotonic timers.
- Explicit spawn and initial ELF loader.
- Service supervisor, registrar, and a bounded bootstrap command/debug
  environment.

### Phase 3 — device and storage substrate

- Safe DMA and cache-maintenance interfaces.
- Kernel device transports and reset/recovery paths.
- Linux-hosted versioned raw-block transport.
- read-only FAT-family compatibility, the SD block service, and the first
  journal/recovery-qualified AstraFS implementation.
- Settings and early package/bundle loading.

### Phase 4 — networked development machine

- ESP Wi-Fi/lwIP endpoint service and protected Astra network broker.
- Authenticated remote deploy, logs, debugger, terminal, and transfer.
- Network SDK and optional BSD-sockets compatibility.

### Phase 5 — native graphical and media system

- Display/compositor service and application event model.
- Surfaces, blitter/draw commands, hardware glyph runs, native AFNT font
  service, presentation, 16-sprite sets, framebuffer viewport scrolling, and
  copper resources.
- Media service, ESP audio streams/voices/instruments, and latency
  instrumentation.
- Launcher/workspace, terminal window, and initial native application Kits.

### Phase 6 — durable personal environment

- Native crash-consistent filesystem decision.
- Attributes, indexing, queries, and workspace persistence.
- Atomic package/update system and rollback.
- Stable 1.0 ABI/protocol declaration only after measured application use.

There is no cooperative-multitasking phase in this roadmap.

## 22. Open design decisions

The following require focused design documents or prototypes:

1. Final OS and user-environment name.
2. Exact hybrid-kernel/service boundary.
3. Page size, table geometry, and virtual address layout.
4. External execute protection versus documented lack of hardware NX.
5. Per-master DMA-fence register design and fault contract.
6. Native syscall number and register convention.
7. Handle encoding, rights, delegation, and revocation.
8. IPC message schema and shared-buffer ownership protocol.
9. Scheduler priority classes, quanta, and media deadline policy.
10. Kernel and SDK implementation languages.
11. Application capability manifests and owner-consent experience.
12. Exact workspace chrome, icon language, menu behavior, and Scene behavior.
13. Native filesystem, case rules, Unicode normalization, and durability API.
14. Shared-library and package-version policy.
15. Network hardware and kernel/user network-stack boundary.
16. Crash-record persistence backend and service reconnection protocols.
17. Bitstream/ROM/OS update bundle and rollback mechanism.
18. POSIX process clone, signal delivery, PTY, and job-control mechanisms needed
    for a correct zsh port.

Each decision should preserve the locked principles in this document. A
familiar implementation mechanism is acceptable; a familiar mechanism that
forces Astra into somebody else's product model is not.
