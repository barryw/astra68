# Astra changes

The initial source checksums from upstream commit
`4adbd1a1bec152c9493e4c1b53bb7961f6e27c64` are retained in
`SHA256SUMS.upstream`.

Local changes must cite a Motorola requirement or isolate platform integration;
Amiga, MiSTer, and WinUAE behavior alone is not sufficient justification.

## 2026-07-12 generic compliance repairs

- Removed the MiSTer `$00DD4000-$00DD5FFF` PMMU translation bypass. Platform
  I/O must be mapped by the system's normal MMU/TTR/address-decoder policy.
- Corrected the instruction-address field in format-2 MMU configuration frames
  to use the latched PMOVE opcode PC. The stacked next PC remains unchanged.
- Preserved PMMU faults that arrive while the main sequencer enable is
  suppressed for a split misaligned data operand. The captured logical address,
  function code, read/write direction, instruction/data classification, and
  access size feed the existing MC68030 vector-2 bus-error path when sequencing
  resumes. This does not create an odd-data address error; MC68030 data operands
  may be misaligned.
- Prevented the format-B fill counter from advancing during the user-to-
  supervisor stack swap, when no frame data is written. User-mode format-B
  frames now consume the 46 words/92 bytes defined by MC68030 User's Manual
  Table 8-6 instead of 44 words/88 bytes.

Questa strict run v3 completed 135 variants with 103 clean, 3 stale compile
failures, 24 raw upstream-test failures, and 5 unscored diagnostics. Relative to
the unpatched v2 run, all existing results were unchanged except
`tb_stack_frame_push_motorola`, which now passes format 2, vector 56, next PC,
and PMOVE instruction-address checks.

Questa strict run v4 completed 136 variants with 105 clean, 3 stale compile
failures, 23 raw upstream-test failures, and 5 unscored diagnostics. Relative to
v3, `tb_addr_error_pmmu_data` is the only existing result that changed, moving
from failure to clean, and the new `tb_unaligned_pmmu_fault_motorola` also ran
clean. All other 134 statuses are unchanged.

Questa strict run v6 completed 137 variants with 105 clean, 3 stale compile
failures, 24 raw upstream-test failures, and 5 unscored diagnostics. The
strengthened unaligned-data test verifies the complete 92-byte format-B frame,
`$B008` format/vector word, defined SSW fields, and logical fault address. The
only v4-to-v6 existing-status change is
`tb_mmu_captured_badfeed_dispatch`, whose source explicitly expects the old
88-byte frame; its Motorola-corrected derivative expects 92 bytes and passes.

## 2026-07-13 restart and external-bus repairs

- Prevented faulted data accesses from committing placeholder bus data or
  address-register side effects before vector-2 dispatch.
- Preserved the faulting opcode, logical address, function code, access size,
  CCR state, and unfinished MOVEM mask needed to resume a format-B frame with
  an unmodified RTE.
- Added plain-RTE restart coverage for postincrement reads, predecrement writes,
  arithmetic/CCR updates, MOVEM stores, mid-transfer MOVEM loads, user-mode
  faults, and translated supervisor-stack exception entry.
- Exported core-owned RMC state for TAS, CAS, and CAS2 instead of reconstructing
  atomicity in SoC glue.
- Captured external BERR metadata at first fire, including the original logical
  address when a 32-bit SoC write is combined from two 16-bit core transfers.
- Implemented Motorola format-A/format-B RTE write replay from the stacked data
  output buffer at `SP+$18`, using the SSW size/function-code fields and data
  fault address at `SP+$10`.
- Made level-7 interrupt recognition transition-sensitive while retaining its
  nonmaskable behavior.

The final focused combined-SDRAM-write BERR test, focused exception/RMC/IRQ
tests, all five NetBSD restart cases, and translated-stack restart test pass.
Full Astra coretest also passes. The strict manifest is unchanged from the
restart-qualified baseline: 137 total, 107 clean, 3 stale compile failures,
22 raw simulation failures, and 5 unscored diagnostics.

## 2026-07-22 PMMU restart and stacking closure

- Restricted the short format-A data-write frame to positively identified
  final `MOVE.B/W/L` destination transfers. `MOVES`, CAS, MOVEM, MOVEP, and
  every unclassified store retain the complete format-B restart state required
  by MC68030 User's Manual section 8.1.2.
- Captured the live PMMU data-output buffer and format eligibility at first
  fault. Register-sourced `MOVES` writes bypass the older temporary write-data
  register, so delayed vector dispatch must not reconstruct their payload.
- Added an idle RTE replay-setup state. The final frame longword now retires
  completely before the saved fault address, function code, size, and payload
  launch a replay transfer; setup and wait cycles hold those values stable.
- Restarted faulted `MOVES` instructions from the stacked PC instead of
  synthesizing a separate write replay. This preserves SFC/DFC selection and
  postincrement/predecrement updates while performing exactly one target
  transfer.
- Removed reserved SSW bit 9 from software-completed format-B read recovery.
  Handler-cleared DF plus the repaired data-input buffer now completes the
  supported no-extension read form through `RTE`.
- Strengthened the translated-stack and `MOVES` benches with exact target-cycle
  counts and four independent SFC/DFC addressing forms. Added the same
  architecture-visible program and expectations to the shared Musashi/RTL
  conformance matrix.

Questa Lattice OEM 2024.2 on Beast now reports 137 total variants, 111 clean,
3 stale compile failures, 18 classified simulation failures, and 5 unscored
diagnostics. The four changed statuses are `tb_mmu_badfeed_fault_frame`,
`tb_mmu_badfeed_softfix_recovery`, `tb_mmu_restart_moves_dfc`, and
`tb_mmu_stacking_walk_fault`; all moved to clean and no existing case
regressed. The first case had a stale `$0341` oracle that set reserved SSW bit
9; its PC, frame, MMUSR, fault address, and corrected `$0141` SSW all pass.

The shared matrix now passes all 15 fixtures on both production adapters (30
executions), including the four-fault `MOVES.B` case. This is a simulation
qualification checkpoint. Synthesis, strict routing, and repeated ULX3S boot
promotion remain required before this source replaces the persistent hardware
release.

## 2026-07-13 combinational-loop and open-flow closure

- Replaced the ALU's self-referential rotate/sign chains with finite,
  size-complete combinational decodes and explicit defaults.
- Reworked kernel operand, address, PC-delta, condition, divide-error, and PMMU
  read combinational logic so every output is derived without self-feedback.
- Retained the converged signed three-bit non-branch PC-delta behavior; focused
  system-control and full coretest coverage guard this synthesis-sensitive
  detail.
- Removed an unreachable SDRAM BIST error-counter saturation cone. Two read
  passes over the 25-bit, 32 MiB port cannot overflow the 32-bit byte-error
  count; clean and injected-fault BIST tests retain exact behavior.

Standalone ALU, PMMU, cache, kernel, and TG68K-top SCC gates all report zero.
The complete SoC also reports zero SCCs both before and after ECP5 synthesis.
The final seed-3 ULX3S route uses 38,705 of 83,640 TRELLIS_COMB (46%), 9,396
flip-flops (11%), 136 of 208 DP16KD blocks (65%), and 13 multipliers (8%). It
closes the 12.5 MHz CPU clock at 12.92 MHz and the 75 MHz SDRAM clock at
76.55 MHz; the worst routed endpoint has +0.269 ns slack. The retained
`fpga/soc/oss_flow/astra_scc-zero-bist-seed3.bit` SHA-256 is
`3dd73d9becebd9250eca31c74c117ab5c99c82e898e98e4b5e64bd6c63ade278`.
