# Astra68 Agent Instructions

@/Users/barry/.codex/RTK.md

## Execution Topology

- Use `nuc` or `beast` for builds, synthesis, simulation, and FPGA tooling.
- The ULX3S board is physically attached to `nuc` (`barry@192.168.1.2`).
- Run flashing, serial/FTDI access, HDMI capture, and all hardware tests through
  `nuc`.
- If a toolchain component is unavailable on the local Mac, continue on `nuc`
  or `beast`; do not treat the local installation as a project blocker.
- Preserve the required `rtk` command prefix, including remote access through
  `rtk proxy ssh`; transfer build artifacts with `rtk rsync`.
- Closing a local tool/SSH session does not prove that its remote CAD process
  exited. Before launching or accepting a remote build, check `pgrep -af
  nextpnr`/`yosys` on that host and terminate only stale, identified jobs.
- When a command runner returns a live session ID, retain it and poll that exact
  session through completion. A wrapper returning without visible output is not
  proof that its remote child completed or failed; confirm both the session exit
  status and the expected artifact/log before starting a replacement run.

## FPGA Timing Closure

- Read `docs/CURRENT_STATE.md` before project work. It is the current
  continuation map for the selected CPU, architectural authority, shared test
  ownership, storage/transport boundaries, build topology, resource policy,
  active integration blocker, and release gate. Historical handovers and old
  audit resource tables are not current status.
- Read `fpga/soc/oss_flow/TIMING_CLOSURE.md` before changing timing-sensitive
  RTL, placement constraints, or the production build flow. It records the
  measured checkpoints, exact failing cones, approaches that worked and failed,
  regression gates, reproducibility hazards, and hardware release procedure.
- A usable production bitstream must pass the exact 12.5 MHz CPU and 75 MHz
  SDRAM constraints with the complete production feature set. Do not trade
  correctness, architecture, graphics features, or clock rates for routing.
- Placement estimates and reduced-feature builds are not acceptance evidence.
  Require a full route of the exact release ROM and nonzero build identity,
  followed by repeated POST, SDRAM, and HDMI checks on the board attached to
  `nuc`.
- Keep the continuation record current. After every retained RTL, synthesis,
  placement, route, or hardware checkpoint, record the exact source identity,
  tool/host, resources, constrained-clock result, failed cone, and disposition
  in `fpga/soc/oss_flow/TIMING_CLOSURE.md`; update `docs/CURRENT_STATE.md` and
  `docs/FPGA_RESOURCE_BUDGET.md` whenever the active blocker or capacity
  changes. Failed measured experiments are part of the record and must not be
  erased when a later checkpoint succeeds.
