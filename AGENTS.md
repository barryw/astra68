# Astra68 Agent Instructions

@/Users/barry/.codex/RTK.md

## Execution Topology

- Use `beast` for builds, synthesis, simulation, QEMU, and FPGA tooling.
- The Arty Z7-20 is attached to and reachable through `beast`. Run deployment,
  serial/JTAG access, HDMI capture, and all hardware tests through `beast`.
- The sole MC68030 implementation is QEMU TCG running on the Arty's Cortex-A9
  processing system. The FPGA fabric implements graphics and peripherals, not
  the CPU.
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

## Kernel Development Discipline

- Before adding a kernel function, type, module, state machine, or test helper,
  search the existing implementation and tests for the same responsibility.
  Extend or consolidate the existing mechanism when it can preserve the
  required contract; record why a new mechanism is necessary when it cannot.
- Treat duplicated policy, state transitions, generation handling, byte
  primitives, queueing, and accounting as defects to remove while the affected
  subsystem is being built. Keep refactors scoped and retain behavior tests.
- Keep the kernel MC68030-specific, compact, and direct. Do not add speculative
  portability layers or abstractions that do not remove measured complexity.
- Establish a target-representative performance baseline before changing a hot
  path and compare it afterward. New scheduler, exception, syscall, VM, IPC,
  and user-copy paths require explicit cycle budgets and automated regression
  gates before they become dependencies of higher layers.
- Inspect generated MC68030 code before replacing C with assembly. Use assembly
  when measurement proves a material improvement, while retaining a clear C
  contract and tests as the behavioral oracle.

## Arty FPGA Timing Closure

- Read `docs/CURRENT_STATE.md` before project work. It is the current
  continuation map for the selected CPU, architectural authority, shared test
  ownership, storage/transport boundaries, build topology, resource policy,
  active integration blocker, and release gate. Historical handovers and old
  audit resource tables are not current status.
- Read `fpga/arty/graphics/TIMING_CLOSURE.md` before changing timing-sensitive
  RTL, constraints, or the production build flow. It records the
  measured checkpoints, exact failing cones, approaches that worked and failed,
  regression gates, reproducibility hazards, and hardware release procedure.
- A usable production bitstream must close every production clock in Vivado
  with the complete feature set. Do not trade correctness or production
  features for routing.
- Placement estimates and reduced-feature builds are not acceptance evidence.
  Require a full route of the exact release ROM and nonzero build identity,
  followed by repeated POST, storage, graphics, and HDMI checks on the Arty.
- Keep the continuation record current. After every retained RTL, synthesis,
  placement, route, or hardware checkpoint, record the exact source identity,
  tool/host, resources, constrained-clock result, failed cone, and disposition
  in `fpga/arty/graphics/TIMING_CLOSURE.md`; update `docs/CURRENT_STATE.md`
  whenever the active blocker or capacity changes. Failed measured experiments
  are part of the record and must not be erased when a later checkpoint
  succeeds.
