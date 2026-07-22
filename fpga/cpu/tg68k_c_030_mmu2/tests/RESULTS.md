# Questa strict-run results

Source revision: `4adbd1a1bec152c9493e4c1b53bb7961f6e27c64`

Questa Lattice OEM 2024.2 produced this restart-qualified strict summary:

```text
STRICT_RESULT total=137 clean=111 compile_failures=3 simulation_failures=18 unscored=5
```

The 18 raw simulation failures are classified below. `Test defect` means the
expectation or bench implementation is demonstrably wrong; it is not a waiver
of CPU behavior. `Candidate RTL defect` still requires a reduced
Motorola-cited reproducer before a source fix is accepted.

| Bench | Classification | Reason |
| --- | --- | --- |
| `tb_basic_cputest_exact_suite0` | Candidate RTL defect | Unresolved JMP record 37 low-memory side effects. |
| `tb_basic_cputest_exact_suite3` | Candidate RTL defect | Same focused JMP behavior as suite 0. |
| `tb_basic_cputest_exact_suite7` | Candidate RTL defect | Unresolved runtime020 CHK2 frame state and JMP side effects. |
| `tb_default_div_rtr_exact` | Test defect | Requires exact undefined divide flags and incorrect RTR stack behavior. |
| `tb_div_rtr_frame_probe` | Test defect | Uses faulting DIV PC instead of next PC, expects wrong signed-result N bit, and advances RTR SP by two instead of six. |
| `tb_jmp_record37_ori_tail` | Test defect | Modeled memory ends at `$42001FFF`, but the failing check is at `$4204FEFF`. |
| `tb_mmu_captured_badfeed_dispatch` | Test defect | Explicitly requires an 88-byte format-B frame; Motorola Table 8-6 defines 46 words/92 bytes. |
| `tb_moves_validation` | Stale mock | Mock times out; maintained MOVES mode, privilege, and PC benches pass. |
| `tb_pmmu_bus_verify` | Test defect | Expects PMOVE postincrement and predecrement, which are not control-alterable EAs. |
| `tb_pmmu_reg_comprehensive` | Test defect | Expects reserved MMUSR fields to retain ones; corrected `$EE47` test passes 48/48. |
| `tb_pmove_comprehensive` | Test defect | Starts with invalid PMOVE Dn forms and then scores the resulting exception-loop cascade. |
| `tb_pmove_crp_a7_postinc` | Test defect | PMOVE postincrement is not a legal control-alterable EA. |
| `tb_pmove_crp_mem_to_mmu_postinc` | Test defect | PMOVE postincrement is not a legal control-alterable EA. |
| `tb_pmove_pc_all_regs` | Racy bench | Samples unchanged PC before retirement, reports failure, then reports the same case passing. |
| `tb_stack_frame_push` | Test defect | Expects format 0 and reads the format-2 frame at the wrong SP. |
| `tb_sysreg_frame_capture` | Test defect | Expects undefined CCR bit 5 to survive; corrected bench passes. |
| `tb_t1_trace` | Test defect | Expects undefined SR/CCR bits to survive; corrected bench passes 11/11. |
| `tb_whichamiga_mmu` | Unclassified diagnostic | Older diagnostic fails while the maintained MMU detection and WhichAmiga tests pass; its oracle still needs audit. |

Fixed by Astra compliance patches:

| Bench | Result |
| --- | --- |
| `tb_stack_frame_push_motorola` | Clean: format 2, vector 56, next PC, and PMOVE instruction address pass. |
| `tb_addr_error_pmmu_data` | Clean: an unaligned access to an invalid page is translated and takes vector 2 rather than retiring or taking vector 3. |
| `tb_unaligned_pmmu_fault_motorola` | Clean: independent Motorola-derived check of the same vector-2 behavior. |
| `tb_mmu_captured_badfeed_dispatch_motorola` | Clean: user-mode format-B frame is 46 words/92 bytes and preserves the expected vector, PC, and fault address. |
| `tb_mmu_restart_netbsd` | Clean: all five faulted instructions restart under an unmodified plain RTE, including mid-transfer MOVEM and user mode. |
| `tb_mmu_restart_stack_walk` | Clean: user data recovery succeeds while exception entry uses an SRP-translated supervisor stack. |
| `tb_mmu_user_data_fault_recovery` | Clean: user execution resumes after the handler repairs translation and executes RTE. |
| `tb_mmu_badfeed_fault_frame` | Clean: executing-access PC, format-B state, MMUSR, logical fault address, and Motorola-defined SSW `$0141` pass; the old `$0341` oracle set reserved bit 9. |
| `tb_mmu_badfeed_softfix_recovery` | Clean: handler-cleared DF plus a repaired data-input buffer completes the faulted read through `RTE`. |
| `tb_mmu_restart_moves_dfc` | Clean: four separately faulted `MOVES.B` SFC/DFC forms restart through plain `RTE`, preserve auto-modification, and perform exactly one target transfer each. |
| `tb_mmu_stacking_walk_fault` | Clean: a cold table walk for the translated supervisor exception stack completes without a second fault or CPU halt. |

The restart-qualified manifest has 111 clean variants. The four intended
status changes above account for the complete delta from the prior 107-clean
baseline; every other compile, simulation, and unscored classification is
unchanged.

Compile failures:

| Bench | Reason |
| --- | --- |
| `tb_bug95_pmove_ea_pc` | Uses removed kernel cache-control port names. |
| `tb_cache_ctrl_030` | Uses removed cache controller burst/snoop port names. |
| `tb_moves_instruction` | Stale mock does not compile as a current top-level bench. |

Unscored benches are `tb_exceptions`, `tb_jmp_bus_trace`, `tb_jmp_debug`,
`tb_rte_ccr_restore`, and `tb_sysreg_trap_capture`. They ran without a parsed
failure but do not provide a reliable positive completion oracle. They are not
counted as passes.
