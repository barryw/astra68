# Motorola-corrected tests

These benches are derived from pinned upstream tests only where an upstream
expectation conflicts with Motorola documentation. The upstream originals stay
unchanged in `../upstream/`.

`tb_stack_frame_push_motorola.vhd` corrects the MMU configuration case to the
six-word format 2 frame required by MC68030 User's Manual Table 8-6. The stacked
PC is the next instruction, the additional longword is the PMOVE instruction
address, and the vector offset is vector 56 (`$0E0`).

`tb_pmmu_reg_comprehensive_motorola.vhd` corrects MMUSR readback to the `$EE47`
field mask shown in Figure 9-38 instead of treating hardwired-zero fields as
register state.

`tb_t1_trace_motorola.vhd` and `tb_sysreg_frame_capture_motorola.vhd` verify
that undefined SR/CCR bits read as zero after writes and RTR restoration. The
upstream versions incorrectly require those bits to persist.

`tb_unaligned_pmmu_fault_motorola.vhd` verifies the MC68030 data-alignment rule:
an unaligned longword is performed as bus transfers, not rejected as an address
error. If translation of a constituent transfer finds an invalid descriptor,
the operation takes the normal vector-2 MMU/bus-error path and must not retire.
It also checks the 46-word format-B stack delta, `$B008` format/vector word,
defined SSW bits, and logical data-fault address.

`tb_mmu_captured_badfeed_dispatch_motorola.vhd` corrects the upstream bench's
explicit dependence on an 88-byte format-B frame. MC68030 User's Manual Table
8-6 defines that frame as 46 words (92 bytes), so SSP `$40079B74` must become
handler A7 `$40079B18`, not `$40079B1C`.

`tb_interrupt_master_dual_frame_motorola.vhd` verifies the section 8.1.9
master-mode interrupt contract. A real level-7 interrupt accepted with M=1
must create a format-0 frame on MSP and a format-1 throwaway frame on ISP. The
throwaway saved SR retains M=1 while forcing S=1, so its `RTE` continues on the
already post-incremented MSP. The bench interrupts a multiword instruction,
runs a multiword handler, inserts clock-enable stalls, and proves that the
restored PC fetches its opcode rather than decoding an extension word.
