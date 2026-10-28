# Astra application, bundle, and Kit model

Status: the protected-process loader, versioned shared Kit ABI, version-1
bundle format, `Terminal.app`, and the Graphics, Filesystem, Interface, Events,
and Messaging Kits are implemented. Private-library resolution is implemented;
transactional installation remains design direction. Discovery deliberately
needs no registrar.

The locked package transport and universal shared-library direction are in
`PACKAGE_FORMAT.md` and `SHARED_LIBRARY_FORMAT.md`.

## 1. Native application identity

A native application is an inspectable bundle containing code,
resources, metadata, protocol declarations, and requested capabilities. The
workspace presents the bundle as one application while the shell and developer
tools can inspect its ordinary contents.

The version-1 layout is:

```text
Example.app/
  manifest
  bin/m68k-68040/Example
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

Launch accepts an absolute `.app` path on any mounted volume. `APP:` and the
initial resource directory are rooted at that bundle's actual volume path;
shared Kit lookup remains in `LIBS:` after private bundle providers. Process
identity records the full bundle path, including volume, so single-instance
launch applies to a copied app independently of its installed source. A native
`cp -R` command uses Filesystem Kit's tree copy and publishes the destination
bundle only after copied files have been synced. A failed or interrupted copy
attempts to remove its private `.astra-copying-HEX` sibling on failure.
If media disappears during cleanup, a later copy uses a fresh sibling and
can still publish the destination; the orphan cannot launch as an app.

Reference review: Haiku's `src/kits/app/Roster.cpp` launches an `entry_ref`
without requiring a central application directory, and Tracker
`src/kits/tracker/FSUtils.cpp` separates folder traversal from file copying.
Astra adapts those patterns to explicit VFS paths, volume grants, and a shared
Filesystem Kit copy operation. Haiku's `src/system/kernel/fs/vfs.cpp` keeps a
mount's root stable while referenced and separates mount lifetime from path
lookup. Astra uses an exact catalog epoch/id/generation to acquire a temporary
supervisor grant, then derives APP/CWD under that provider root. The app never
receives the supervisor's grant. Supervisor retains the parent session until
app process exits, so provider retirement cannot revoke a running app's APP:
tree. Supervisor's launch path now owns that exact-volume lease through
acquisition, temporary namespace binding, process handoff, and rollback;
process exit closes it. Bundle source authority is passed explicitly through
grant construction. The focused host gate covers fixed volumes, removable
handoff, stale media, and failed acquisition steps. Haiku checkout remained
read-only.

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

Implemented Kit bundles are:

- Runtime: `compiler.library`, `runtime.library`, `system.library`, and the
  process interpreter `loader.library`;
- Streams: `streams.library`;
- Graphics: `graphics.library` and `font.library`;
- Filesystem: `filesystem.library`;
- Interface: `interface.library`;
- Network: `network.library`;
- Configuration: `config.library`;
- C/POSIX: `libc.library` and `terminfo.library`.

Messaging and event emission remain NDK API groups, not alias DSOs. Port and
resource operations resolve through `system.library`; structured event, log,
trace, and event-catalog operations resolve through `runtime.library`. There
is one implementation and one eager-loader path for each operation.

The remaining planned Kits are:

- Core: handles, errors, time, waits, memory and process operations, plus a
  structured `AstraTask` API for background work. Tasks use native threads and
  Messaging Kit ports, support cooperative cancellation, completion results,
  waiting, and owner-scoped cleanup, and deliver UI-facing progress or
  completion back to the UI thread rather than mutating controls from workers;
- Application: launch, lifecycle, commands and settings;
- Interface extensions: remaining views and controls, and drag/drop;
- Media: audio streams, voices, clocks and synchronization;
- POSIX extensions beyond the implemented libc, file-descriptor, path, signal,
  threading, and terminal foundation.

The durable boundary remains a C-compatible ABI plus versioned service
messages. C++ and other language wrappers may provide move-only ownership,
RAII, containers, and UI classes without exposing compiler object layouts,
exceptions, RTTI, or name mangling across a process or Kit boundary.

The Messaging API group exposes the existing bounded ports, timed send and
receive, handle duplication, and capability-transfer primitives. It is direct
process messaging, not an in-process substitute for system event publication.
A future event broker can build pub/sub on these primitives when there is a
real system service to own subscriptions, filtering, and backpressure.

The Events API group exposes the existing diagnostic event emitter, log, trace
reader, and event catalog through `runtime.library`. It records observable
system events; it does not replace interprocess messaging.

`interface.library` ABI 5.2 exposes the retained control context, TextSurface,
typed clipboard, per-document undo/redo, the shared UTF-8 piece-table model,
and retained fields through the
public NDK. Label, Button, and the other primitive controls share intrinsic
measurement, deterministic nested flex layout, hit testing, focus/capture,
interaction state, damage, clipping, and Graphics Kit rendering. Controls and
their hierarchy remain caller-owned; the Interface Kit retains the compiled
layout description and iteratively reflows every descendant when a parent
extent changes. The alert implementation and `InterfaceGallery.app` consume
that same code; applications do not own parallel widget painters, coordinate
tables, resize handlers, or input state machines.

The same retained hierarchy owns tab strips and conditional pages. Tabs reuse
the shared choice/action model, while `ASTRA_CONTROL_COLLAPSED` removes a page
container and all descendants from layout, rendering, hit testing, focus, and
animation through one state transition.

Animated controls retain their own state and active-list membership while
sharing the display-refresh epoch. The
display service supplies a coalescing per-window vblank wait handle, and the UI
thread calls the Interface Kit once per pulse. Vblank and ordinary input return
the same semantic action type, including pointer-held numeric-stepper repeats.
Only active controls are visited; controls do not require application timers,
polling, a second callback path, or background UI mutation.

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
under `LIBS:.providers/`. Every normal executable names its direct dependencies
with `DT_NEEDED`. The supervisor resolves only `PT_INTERP`; the one user-space
loader resolves and maps the exact installed dependency closure, resolves
versioned C symbols eagerly, establishes TLS, seals RELRO, and runs
constructors. Both use the same provider-index resolver, and neither scans Kit
directories at launch. There is no parallel library-open API and no typed
function-table ABI.

Axiom shares immutable physical frames for an exact library identity while
keeping writable pages, TLS, and loader state process-local. A process owns one
reference to each member of its resolved closure; process teardown releases the
entire closure even after an abnormal exit. Libraries are constrained
big-endian ELF32/m68k `ET_DYN` images with explicit SONAMEs, dependencies,
symbol versions, and eager relocations. Lazy binding and unresolved imports are
rejected.

### 5.1 Version identity and resolution

**DIRECTION:** Final bundle resolution cannot depend on whichever compatible
file happens to be installed when an application starts. A bundle records each resolved Kit
by name, ABI major, exact version, target architecture, and content digest.
Launch either maps that identity or fails with the missing identity; it never
silently substitutes another version.

Shared Kit bundles already live side by side beneath `LIBS:`. An application
may declare private `provides` entries and carry their exact library files in
its own `libs/` directory. The loader checks those declarations and
payloads first, then resolves remaining identities through `LIBS:`. It never
checks the working directory, an environment search path, or an unversioned
global alias. A private library affects only its declaring bundle. The shared
native bundle policy selects private providers and matches required versions
for both runtime loader and host bundle tool. Removing a shared Kit counts
only other shared Kits as alternatives; another app's private payload cannot
satisfy its dependents. An app with its own private provider does not depend
on that shared Kit. The host build checker verifies the same scope.
The standard image installs `SDL.kit` in `LIBS:`. Applications linked against
its SDL2 libraries use that shared Kit without carrying private copies.
Haiku reference: `src/system/runtime_loader/runtime_loader.cpp`
(`open_executable()` checks application locations before system paths) and
`src/system/runtime_loader/elf_load_image.cpp` (loaded-image reuse). Astra
adapts that order to declared private payloads and exact provider records.
The system namespace owns `LIBS:` with read/write authority so an installer can
publish and retire versions. Ordinary applications receive a read-only grant;
installation is an explicit capability, not an ambient right of every process.

The current loader cache key is name, release version, ABI, and build ID. A
private payload is fed to the kernel before reuse; the kernel compares its
pages with any cached entry of that identity and rejects conflicting bytes.
Distinct builds must carry distinct build IDs. Identical read-only pages may
therefore be shared across bundles, while different versions remain distinct.
Reusing one exact identity for different bytes is an installation conflict,
not an update.

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
