# Astra application, bundle, and Kit model

Status: the protected-process loader, versioned shared Kit ABI, version-1
bundle format, `Terminal.app`, and the Graphics, Filesystem, Interface, Events,
and Messaging Kits are implemented. Private-library resolution and
transactional installation remain design direction; discovery deliberately
needs no registrar.

## 1. Native application identity

A native application is an inspectable bundle containing code,
resources, metadata, protocol declarations, and requested capabilities. The
workspace presents the bundle as one application while the shell and developer
tools can inspect its ordinary contents.

The version-1 layout is:

```text
Example.app/
  manifest
  bin/m68k-68030/Example
  resources/
  icons/
  locales/
  licenses/
```

The exact names and manifest encoding are now locked by
`docs/BUNDLE_FORMAT.md`. The manifest
records at least:

- application identity, version, display name, and executable;
- supported document/content types and commands;
- minimum OS, hardware, service-protocol, and Kit versions;
- requested capabilities and resource limits;
- immutable resource hashes and package provenance;
- launch policy, single/multiple-instance behavior, and update identity.

Installation scripts do not receive ambient privilege. Transactional package
installation and rollback are not implemented yet.

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

Astra Kits provide the small, coherent developer surface that
Amiga libraries and well-designed application frameworks made possible, while
keeping process boundaries and service protocols explicit.

Implemented Kits are:

- Graphics: `graphics.library` and `font.library`;
- Filesystem: `filesystem.library`;
- Interface: `interface.library` and `input.library`;
- Events: `events.library`;
- Messaging: `messaging.library`.

The remaining planned Kits are:

- Core: handles, errors, time, waits, memory and process operations;
- Application: launch, lifecycle, commands and settings;
- Interface extensions: views, controls, layout, clipboard and drag/drop;
- Media: audio streams, voices, clocks and synchronization;
- Network: asynchronous endpoints and name resolution;
- POSIX: libc, file descriptors, paths, PTYs, jobs and compatibility.

The durable boundary remains a C-compatible ABI plus versioned service
messages. C++ and other language wrappers may provide move-only ownership,
RAII, containers, and UI classes without exposing compiler object layouts,
exceptions, RTTI, or name mangling across a process or Kit boundary.

`messaging.library` ABI 1.0 exposes the existing bounded ports, timed send and
receive, handle duplication, and capability-transfer primitives. It is direct
process messaging, not an in-process substitute for system event publication.
A future event broker can build pub/sub on these primitives when there is a
real system service to own subscriptions, filtering, and backpressure.

`events.library` ABI 1.0 exposes the existing diagnostic event emitter, log,
trace reader, and event catalog. It records observable system events; it does
not replace interprocess messaging.

## 5. Shared code direction

Static linking remains available for small runtime veneers. Graphics and font
implementations are now loaded separately rather than copied into each client.
The Filesystem Kit follows the same rule: `filesystem.library` is the shared,
assign-aware client layer over the existing VFS rather than another storage
implementation. Its native-to-POSIX boundary is recorded in
`FILESYSTEM_KIT.md`.

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

**IMPLEMENTED:** the image builder validates `.kit` manifests and library
identities, then writes the newest compatible provider for each `(name, ABI)`
under `LIBS:.providers/`. `OpenLibrary(name, ABI)` first asks Axiom to attach a
compatible resident identity; on a miss it reads that bounded provider record,
with a manifest sweep retained for older images. `CloseLibrary()` releases the
process reference. Axiom maps immutable pages at a common process-local address
and shares their physical frames; writable pages are copied privately from the
cached initial image. Libraries are constrained big-endian
ELF32/m68k images with fixed metadata and export-table offsets, eager
`R_68K_RELATIVE` relocation, and no PLT or lazy binding. Process teardown is the
hard cleanup boundary, so a missed `CloseLibrary()` cannot pin mappings after
the process dies. The bounded global cache reclaims entries with no process
mapping.

### 5.1 Version identity and resolution

**DIRECTION:** Final bundle resolution cannot depend on whichever compatible
file happens to be installed when an application starts. A bundle records each resolved Kit
by name, ABI major, exact version, target architecture, and content digest.
Launch either maps that identity or fails with the missing identity; it never
silently substitutes another version.

Shared Kit bundles already live side by side beneath `LIBS:`. An application may eventually carry
the exact Kit in its own `libs/` directory. Resolution checks the bundle first
and then `LIBS:`; it never checks the working directory, an environment search
path, or an unversioned global alias. Private bundle lookup is not implemented
yet. A private Kit will affect only its bundle.
The system namespace owns `LIBS:` with read/write authority so an installer can
publish and retire versions. Ordinary applications receive a read-only grant;
installation is an explicit capability, not an ambient right of every process.

The loader cache key is the complete resolved identity, including the content
digest. The same immutable read-only pages may therefore be shared even when
identical Kit bytes came from separate bundles, while different versions or
different digests remain distinct. Reusing a name and version for different
bytes is an installation conflict, not an update.

Updates install a new version beside the old one and atomically replace only
the dependency records that opt into it. Running processes retain their old
mappings. A version may be collected only when no running process maps it and
no installed bundle records it. This is the rule that permits multiple Kit
versions without a process-wide or machine-wide DLL conflict.

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
