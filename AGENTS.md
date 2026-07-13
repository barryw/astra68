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
  `rtk proxy ssh` and `rtk proxy scp`.
