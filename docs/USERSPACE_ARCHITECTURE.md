# Astra OS userspace architecture

Status: design direction; no userspace service described here is implemented.

This document refines `OS_VISION.md` into a coherent userspace shape. It does
not change the kernel boundary in `KERNEL_SPEC.md`, define a stable service ABI,
or imply that the current K8 kernel can start these services. Exact wire
records land only with implementations and executable protocol tests.

Status markers have the same meanings as in `OS_VISION.md`:

- **LOCKED** - established product or architecture direction.
- **DIRECTION** - design to use for prototypes, still subject to evidence.
- **OPEN** - unresolved and not an implementation assumption.

## 1. Product shape

**LOCKED:** Astra OS is a GUI-first personal operating system with a first-class
terminal and development environment. Its native architecture is neither
AmigaOS nor Unix. The intended character is:

- immediate, compact, and hardware-aware like the best Amiga software;
- protected, preemptive, restartable, and inspectable;
- consistent enough that learning one resource lifecycle transfers to the
  rest of the system;
- useful for games and media without becoming a game console;
- useful for software development without becoming a small Unix clone.

The desktop and shell are peers over the same services. Neither is a privileged
alternate universe. A file opened from a desktop icon, a shell command, an
application menu, or automation resolves to the same object and follows the
same ownership rules.

## 2. System topology

**DIRECTION:** Use a small number of coarse protected services. A 12.5 MHz
single-core CPU should not spend its time crossing hundreds of ornamental
process boundaries.

```text
kernel mechanisms
  VM | scheduling | handles | waits | ports | shared areas | IRQ/time
                              |
                              v
supervisor/registrar
  service lifecycle | discovery | launch | generations | recovery
        |             |             |              |
        v             v             v              v
 block device      display       input          network/media
 storage/VFS       + compositor  routing        services
        |             |             |
        +-------------+-------------+
                      |
                      v
 workspace | terminal | shell | applications | developer tools
```

Candidate initial service boundaries are:

| Service | Responsibility | Must not own |
|---|---|---|
| supervisor/registrar | start, monitor, name, and restart services; launch applications | filesystem parsing, UI policy |
| block device | generic block scheduling, AstraHost protocol, bounded queues, barriers, media generations, timeout and reset | filesystem parsing, names, application policy |
| storage/VFS | read-only FAT-family compatibility, AstraFS, object identity, paths, mounts and notifications | SD electrical protocol, direct SPI, application launch policy |
| display | windows, scenes, composition, Vega/Astraea scheduling and revocation | application behavior, filesystem parsing |
| font | AFNT/import validation, immutable faces, layout and strike caches | window policy, arbitrary framebuffer access |
| input | normalize and route keyboard, pointer, buttons and game controls | widget policy, application callbacks |
| workspace | desktop policy, volumes, launcher, menus, application and document presentation | graphics MMIO, filesystem internals |
| terminal/PTY | terminal sessions, PTYs, termios state and terminal rendering | native process policy, kernel scheduling |
| POSIX personality | file descriptors, Unix process/job semantics and libc adaptation | native kernel ABI or native application identity |
| media | Astra streams/voices, timing, buffering and the ESP audio protocol | application UI policy, direct ESP access |

These names and combinations are provisional. Measurements may justify
co-locating closely coupled services, but service boundaries may not erase
fault containment or allow untrusted parsers into the kernel.

## 3. Bootstrap order

**DIRECTION:** Userspace starts as a dependency graph, not a bag of startup
scripts with accidental ordering.

1. The kernel starts one fixed initial supervisor image with explicit handles.
2. The supervisor publishes the registrar endpoint and starts the minimum
   storage-independent services.
3. Display, font, and input services open ROM resources and protected device
   handles. They can start before the writable filesystem.
4. The block-device service publishes media generations; storage/VFS mounts
   read-only FAT-family volumes and, when provisioned, recovers and mounts
   AstraFS before publishing volume and filesystem notifications.
5. Workspace starts when display, input, font, and one resource namespace are
   ready. Missing writable storage produces a recovery desktop, not a hang.
6. Terminal and a bootstrap command environment become available from the
   workspace and diagnostic path.
7. Optional network, media, package, indexing, and compatibility services start
   without delaying basic interaction.

A service announces `READY` only after its public endpoint can either accept a
request or return a precise bounded error. Publication is generation tagged so
clients never mistake a restarted service for the dead instance.

## 4. Communication rules

**LOCKED:** All services use the kernel's common handle, wait, deadline,
cancellation, and peer-death model.

Astra's ports are the protected descendant of the Amiga message-port idea:
they retain explicit endpoints, queued delivery, replies, and composability,
but never carry shared-address-space pointers or trust a sender's object
lifetime. Port handles carry authority; messages are copied records or refer to
explicit shared areas.

- Small control records use bounded typed messages.
- Bulk bytes, pixels, audio, and directory/query results use shared areas and
  bounded rings.
- Handle transfer conveys authority; a string name or path does not.
- Requests have transaction IDs. Asynchronous work also has sequence or fence
  IDs, a finite queue, cancellation behavior, and a terminal result.
- No UI event thread blocks on storage, network, another application, or an
  unbounded service dependency.
- No service calls an application synchronously while holding a lock.
- No protocol invents a private wait primitive when the common waitable-object
  model can represent readiness or completion.
- Hot paths batch work. A draw list, directory batch, audio period, or terminal
  output run is not one syscall or IPC message per element.

The intended event-loop shape is:

```text
WAIT_MULTIPLE(input, commands, timers, fences, peer death, cancellation)
  -> drain every ready bounded source
  -> update local model
  -> submit batched work
  -> return to wait
```

This is why `WAIT_MULTIPLE`, ports, shared areas, and handle transfer remain the
kernel prerequisites for real userspace.

## 5. Service lifecycle and failure

Each restartable service follows this conceptual state machine:

```text
STOPPED -> STARTING -> READY -> STOPPING -> STOPPED
                 \-> FAILED -> BACKOFF -> STARTING
```

- The supervisor never hot-loops a failing service indefinitely.
- A service generation changes after every restart.
- Clients waiting on the old endpoint wake with peer-dead, never with a silent
  reconnect to new state.
- Reconnection is explicit. Client libraries may automate it only when the
  operation is demonstrably idempotent.
- Device-owning service death revokes mappings, stops DMA, masks interrupts,
  resets the engine, and completes or cancels every outstanding request.
- Display-service death must leave a recovery console available. Workspace and
  applications redraw after reconnect rather than assuming old graphics
  objects survived.
- Storage-service death fails requests and remounts or repairs according to the
  filesystem contract; it never fabricates successful durability.

Exact retry counts, backoff intervals, and critical-service escalation remain
**OPEN** and require fault-injection measurements.

## 6. One coherent command model

**DIRECTION:** Applications and services publish versioned commands with stable
IDs, current enabled/checked state, a typed argument schema, and an invocation
endpoint. The same command can appear in:

- an application or global menu;
- a toolbar or keyboard binding;
- the workspace command palette;
- shell or automation tooling;
- the system inspector.

This does not turn every shell program into GUI RPC. It prevents applications
from implementing five unrelated paths for the same action. Invocation remains
asynchronous when the command can touch storage, network, or another process.

Drag and drop follows the same rule: the drop carries typed metadata and
transferred handles. A pathname may accompany it for display or compatibility,
but authority and object lifetime do not depend on that string staying valid.

## 7. Dependency rules

- The kernel never depends on a userspace service to complete a fault path.
- The supervisor and emergency console do not depend on the workspace.
- Display and input retain progress when storage or networking stalls.
- Workspace does not synchronously call applications to move, expose, close,
  or identify their windows.
- Filesystem and package parsing do not run in the registrar.
- POSIX compatibility consumes native services; native services do not call
  back into the POSIX personality.
- Service dependency cycles are rejected at manifest validation time.

## 8. Implementation sequence

The userspace implementation begins only after the corresponding kernel
mechanisms are qualified:

1. `WAIT_MULTIPLE`, process/timer waitables, ports and peer-death.
2. Bounded messages, handle transfer, and shared areas.
3. ELF loading, explicit spawn, supervisor/registrar, and service discovery.
4. Display/font/input vertical slice using ROM resources.
5. Workspace and terminal window with no writable-storage dependency.
6. Storage/VFS, PTYs, streams, pipes, and the POSIX personality.
7. zsh, Vim, packages, network deployment, media, and broader applications.

Design work may proceed before these gates. Production code must not replace a
missing kernel primitive with an unbounded polling or global-memory shortcut.

## 9. First userspace acceptance slice

The first meaningful userspace demonstration is:

1. Boot from kernel handoff into the protected supervisor.
2. Start display, font, input, workspace, and terminal components.
3. Show mounted or unavailable volumes without blocking the desktop.
4. Open a terminal from one keyboard command.
5. Move and expose that window smoothly while a separate user process consumes
   all ordinary-priority CPU time.
6. Crash that process and retain pointer, desktop, terminal, and input progress.
7. Restart one noncritical service and show explicit client reconnection or
   peer-death behavior.
8. Report memory, queue, frame, launch, and input-latency measurements.

This is not the completed operating system. It is the first proof that Astra's
native environment has the intended personality and failure boundaries.

## 10. Open decisions

- Final service names and which coarse services share one process.
- Manifest schema and dependency-cycle validation.
- Service restart limits and critical-service recovery policy.
- Clipboard, drag-and-drop, command, and automation wire protocols.
- AstraFS filename case and normalization, open-file deletion, data checksums,
  journal sizing, indexing scope, and exact version-1 on-disk structures.
- Exact boundary between terminal, PTY, and POSIX personality services.
