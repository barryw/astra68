# Astra application, bundle, and Kit model

Status: design direction; no loader, bundle manager, dynamic linker, or shared
Kit ABI described here is implemented or stable.

## 1. Native application identity

**DIRECTION:** A native application is an inspectable bundle containing code,
resources, metadata, protocol declarations, and requested capabilities. The
workspace presents the bundle as one application while the shell and developer
tools can inspect its ordinary contents.

A candidate layout is:

```text
Example.app/
  Manifest
  bin/m68k-astra/Example
  resources/
  icons/
  locales/
  licenses/
```

The exact names and manifest encoding remain **OPEN**. The manifest eventually
records at least:

- application identity, version, display name, and executable;
- supported document/content types and commands;
- minimum OS, hardware, service-protocol, and Kit versions;
- requested capabilities and resource limits;
- immutable resource hashes and package provenance;
- launch policy, single/multiple-instance behavior, and update identity.

Installation scripts do not receive ambient privilege. Package installation is
transactional and rollback-capable.

## 2. Launch contract

The registrar launches a bundle through the loader service using explicit:

- executable identity and validated image;
- arguments and environment;
- initial thread entry and stack policy;
- inherited handles selected by type and rights;
- application endpoint and startup transaction;
- memory, handle, thread, IPC, and pinned-page limits;
- granted service capabilities.

Launch either publishes one valid process/application instance or returns a
complete failure with no leaked mappings, handles, service registration, or
desktop placeholder. Startup has a deadline. A slow application may continue
starting in the background, but it cannot freeze the workspace.

## 3. Application protocol

**DIRECTION:** Every GUI application has one application endpoint and one or
more windows. The endpoint receives lifecycle, open-document, command,
settings, and quit requests. Window endpoints receive bounded UI events.

- Opening a document transfers a document/file handle plus descriptive
  metadata; the path alone is not authority.
- Requests have transaction IDs and explicit reply/defer/cancel behavior.
- Quit is cooperative until the owner explicitly terminates the whole process.
- Application and window commands use the shared command model in
  `USERSPACE_ARCHITECTURE.md`.
- Hidden nested event loops are forbidden by the SDK.
- An application can use worker threads, but UI-object affinity and blocking
  behavior are visible rather than magical.

## 4. Kits

**DIRECTION:** Astra Kits provide the small, coherent developer surface that
Amiga libraries and well-designed application frameworks made possible, while
keeping process boundaries and service protocols explicit.

Candidate Kits are:

- Core: handles, errors, time, waits, memory and process operations;
- Application: launch, lifecycle, commands, messaging and settings;
- Interface: windows, views, controls, layout, clipboard and drag/drop;
- Graphics: surfaces, draw lists, sprites, raster resources and fences;
- Storage: files, directories, attributes, queries and notifications;
- Media: audio streams, voices, clocks and synchronization;
- Network: asynchronous endpoints and name resolution;
- POSIX: libc, file descriptors, paths, PTYs, jobs and compatibility.

The durable boundary remains a C-compatible ABI plus versioned service
messages. C++ and other language wrappers may provide move-only ownership,
RAII, containers, and UI classes without exposing compiler object layouts,
exceptions, RTTI, or name mangling across a process or Kit boundary.

## 5. Shared code direction

Static linking is acceptable during bring-up. It is not the desired permanent
answer for dozens of small applications on a 32 MiB machine.

**DIRECTION:** After the native ABI is stable enough to deserve sharing, the
loader maps immutable versioned Kit text and read-only data into each process.
Writable globals, thread-local state, heaps, and service handles remain
process-local.

- Kit versions are immutable once installed.
- Packages request an ABI major plus a compatible minimum minor version.
- Resolution never searches the current directory.
- An update installs a new version beside the old one and switches manifests
  atomically; running applications keep their mapped version.
- Identical read-only pages are physically shared where VM support permits.
- Service protocol negotiation is separate from client-library versioning.
- A small import veneer is preferred over embedding large generated stubs in
  every binary.

The exact ELF dynamic-linking model, relocation set, PLT/GOT design, and
function-table alternative are **OPEN** and require m68k size/startup
benchmarks. Shared libraries are not a prerequisite for the first protected
userspace service.

## 6. Small-binary rules

**LOCKED:** Binary and resident-memory size are measured continuously. Astra
does not accept growth as an invisible byproduct of convenience.

- Build with function/data sections and garbage collection.
- Compare `-Os`, targeted `-O2`, and LTO using both size and cycle evidence.
- Keep resources, translations, icons, and debug data out of executable text.
- Store full symbols in a development symbol package; ship stripped images.
- Avoid C++ exceptions, RTTI, static constructors, and heavyweight runtime
  dependencies in base-system programs unless measured value justifies them.
- Prefer shared Kit code over private copies after the ABI is stable.
- Keep service client stubs small and batch operations rather than generating
  broad object proxies.
- Record text, read-only data, writable data, BSS, relocations, startup cycles,
  committed pages, and peak stack for every shipped binary.

Base command-line tools may use a multicall executable when it materially saves
storage and shared memory, while retaining individual command names, help,
exit behavior, and documentation. Frequently used shell operations can also be
zsh builtins, but native commands remain independently scriptable.

## 7. Development model

Every build emits:

- stripped bundle and executable sizes;
- section and relocation reports;
- dependency and protocol-version manifests;
- cold and warm launch cycles;
- initial and peak committed memory;
- startup IPC/message counts;
- ABI and package compatibility checks;
- detached symbols for crash reports and debugging.

The SDK should make a tiny correct application easy without hiding allocation,
blocking, ownership, or service failure. Generated examples are tested against
both direct bring-up and OS backends only where their documented semantics are
actually equivalent.

## 8. Application acceptance

Before declaring the model stable, demonstrate:

1. a tiny CLI utility with no unnecessary runtime payload;
2. a GUI application using shared Kits and a private window surface;
3. open-document launch by handle from desktop and shell;
4. application crash with complete automatic resource reclamation;
5. old and new Kit versions installed concurrently;
6. package update and rollback while an older application remains running;
7. reproducible size, startup, dependency, and ABI reports.
