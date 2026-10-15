# Astra68 Agent Instructions

@/Users/barry/.codex/RTK.md

## Execution Topology

- Use `beast` for builds, synthesis, simulation, QEMU, and FPGA tooling.
- The DE25-Nano is attached to and reachable through `beast`; the Arty Z7-20 is
  the rollback platform. Run deployment, serial/JTAG access, HDMI capture, and
  all hardware tests through `beast`.
- The sole MC68040 implementation is QEMU TCG running on the DE25's AArch64 HPS.
  The FPGA fabric implements graphics and peripherals, not the CPU.
- If a toolchain component is unavailable on the local Mac, continue on
  `beast`; do not treat the local installation as a project blocker.
- Preserve the required `rtk` command prefix, including remote access through
  `rtk proxy ssh`; transfer build artifacts with `rtk rsync`.
- Source syncs must be exact mirrors outside generated directories: exclude
  every `build/` directory, delete removed remote sources, and use
  checksum-based, non-mtime-preserving transfer:
  `rtk rsync -az --checksum --no-times --delete --exclude .git --exclude build
  --exclude '*/build' ./ HOST:/path/to/astra68/`. This prevents local products
  from replacing host-built products, removes renamed/deleted source artifacts,
  and makes every changed source newer than the host objects that depend on it.
  Transfer selected finished artifacts in a separate explicit command.
- Closing a local tool/SSH session does not prove that its remote process
  exited. Before launching or accepting a remote build, check the relevant
  QEMU, Vivado, filesystem-stress, or test processes and terminate only stale,
  identified jobs.
- When a command runner returns a live session ID, retain it and poll that exact
  session through completion. A wrapper returning without visible output is not
  proof that its remote child completed or failed; confirm both the session exit
  status and the expected artifact/log before starting a replacement run.
- Filesystem gates mount the provider image as root, then drop privilege
  and run QEMU as the caller -- QEMU never runs as root, on beast or on
  the board: `tools/astra-qemu-root` (installed as
  `/usr/local/sbin/astra-qemu-root`, allowed by
  `/etc/sudoers.d/astra-qemu-root`) treats the caller as hostile. Root
  opens, creates, mounts and unmounts only under its own
  `mktemp -d /var/tmp/astra-qemu-root.XXXXXX` directory (root, group the
  caller's, 0710), never in a directory the caller can modify. Images
  over 4 GiB are refused; the copy is fallocated under a lock after the
  free-space check, so concurrent runs cannot overcommit `/`. A caller-credential reader
  (`setpriv --reuid/--regid --no-new-privs`) opens the image no-follow,
  refuses anything but a regular file, and sends exactly its fstat size
  into root's `R/img`; that is loop-mounted at `R/mnt`
  (nodev,nosuid,noexec), announced on the helper's first stdout line
  (`ASTRA-QEMU-ROOT-MOUNT R/mnt`, which `test-terminal.Machine` reads),
  and QEMU runs as the caller in its own private mount namespace, its
  pid in `R/qemu.pid`. Afterwards a caller-credential `tee` writes
  `R/img` back to the image, and the mount, loop device and `R` are
  removed, success or not. The mount stays visible system-wide for the
  run: unmounting it once QEMU's private copy existed raced QEMU's
  HostFS init and broke the desktop boot. `R` is on real disk, never
  tmpfs (`/run`): a loop-mounted image backed by a tmpfs file breaks the
  same HostFS path. The image must already contain `work`/`sound`
  (qemu_runtime.py's `root_provider` makes them at `mkfs.ext4 -d` time);
  the helper never creates anything inside the mount. Reinstall it with
  `sudo install -o root -g root -m 0755 tools/astra-qemu-root
  /usr/local/sbin/` after any change; gates refuse a stale copy.

## Kernel Development Discipline

- Before implementing or changing an operating-system mechanism or common
  control, review the corresponding implementation in `~/Git/haiku`. Record
  the relevant source paths and the patterns adopted or deliberately adapted
  to Astra's native contracts. Use Haiku as a read-only reference and respect
  its repository contribution policy; do not modify that checkout.
- Before adding a kernel function, type, module, state machine, or test helper,
  search the existing implementation and tests for the same responsibility.
  Extend or consolidate the existing mechanism when it can preserve the
  required contract; record why a new mechanism is necessary when it cannot.
- Treat duplicated policy, state transitions, generation handling, byte
  primitives, queueing, and accounting as defects to remove while the affected
  subsystem is being built. Keep refactors scoped and retain behavior tests.
- Keep the kernel MC68040-specific, compact, and direct. Do not add speculative
  portability layers or abstractions that do not remove measured complexity.
- Establish a target-representative performance baseline before changing a hot
  path and compare it afterward. New scheduler, exception, syscall, VM, IPC,
  and user-copy paths require explicit cycle budgets and automated regression
  gates before they become dependencies of higher layers.
- Inspect generated MC68040 code before replacing C with assembly. Use assembly
  when measurement proves a material improvement, while retaining a clear C
  contract and tests as the behavioral oracle.

## Compatibility Discipline

- Astra has no external legacy clients during pre-alpha. Prefer the correct,
  coherent contract over preserving obsolete in-tree behavior.
- When a better design requires a breaking change, bump the protocol or library
  ABI major, update every in-tree producer, consumer, test, manifest, and
  document together, and delete the superseded API and compatibility path.
- Do not retain aliases, wrappers, fallback behavior, repurposed private fields,
  or dual wire formats without a current external compatibility requirement.

## Native Substrate and POSIX Personality

- Implement each operating-system mechanism once as an Astra-native facility.
  The scheduler and thread objects, VFS, process lifecycle, IPC, loader, virtual
  memory, clocks, and network stack have no independent POSIX implementation.
- POSIX libraries are compatibility adapters. They may expose standard symbols
  such as `pthread_*`, file descriptors, `errno`, signals, and process groups,
  but must translate them onto the canonical Astra mechanisms underneath.
- Keep POSIX-only policy and bookkeeping in the userspace compatibility layer;
  do not leak it into the kernel or native NDK contracts. Native applications
  must not depend on the POSIX layer, and POSIX applications must not bypass the
  native substrate.
- When a port needs a missing capability, implement the correct Astra-native
  capability first and then adapt the POSIX API to it. Do not add a separate
  POSIX backend, fallback subsystem, or application-specific workaround.
- Linux-side helpers are service providers behind versioned Astra protocols,
  never an alternate API directly visible to applications. Two API spellings
  are acceptable where compatibility requires them; two implementations are
  not.

## FPGA Timing Closure

- Read `docs/CURRENT_STATE.md` before project work. It is the current
  continuation map for the selected CPU, architectural authority, shared test
  ownership, storage/transport boundaries, build topology, resource policy,
  active integration blocker, and release gate. Historical handovers and old
  audit resource tables are not current status.
- Read `fpga/de25/TIMING_CLOSURE.md` before changing timing-sensitive RTL,
  constraints, or the production build flow. It records the
  measured checkpoints, exact failing cones, approaches that worked and failed,
  regression gates, reproducibility hazards, and hardware release procedure.
- A usable production bitstream must close every production clock in Vivado
  with the complete feature set. Do not trade correctness or production
  features for routing.
- Placement estimates and reduced-feature builds are not acceptance evidence.
  Require a full route of the exact release ROM and nonzero build identity,
  followed by repeated POST, storage, graphics, and HDMI checks on the DE25.
- Keep the continuation record current. After every retained RTL, synthesis,
  placement, route, or hardware checkpoint, record the exact source identity,
  tool/host, resources, constrained-clock result, failed cone, and disposition
  in `fpga/de25/TIMING_CLOSURE.md`; update `docs/CURRENT_STATE.md`
  whenever the active blocker or capacity changes. Failed measured experiments
  are part of the record and must not be erased when a later checkpoint
  succeeds.
