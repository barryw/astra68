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
