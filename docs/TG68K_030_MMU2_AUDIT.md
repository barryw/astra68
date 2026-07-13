# TG68K 030 MMU2 audit

## Scope

This audit covers the RTL and tests linked by the TG68K.C `030_mmu` README.
It does not treat an Amiga boot or an emulator comparison as an architectural
specification.

Authoritative references:

- [MC68030 User's Manual, third edition](https://www.nxp.com/docs/en/reference-manual/MC68030UM.pdf)
- [M68000 Family Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf)

Audited revisions:

- exported TG68K.C `030_mmu`: `9876ecd89c798c6aa6515621aaafc7e0f902a00a`
- linked Minimig `030_mmu2`: `4adbd1a1bec152c9493e4c1b53bb7961f6e27c64`

The revisions are not equivalent. All major CPU/MMU RTL files differ, and the
linked test repository contains substantial fixes newer than the exported
TG68K.C branch.

## Questa execution

The July revision was compiled and run with Questa Lattice OEM 2024.2 on
`beast`. The small advertised regression target reported 8 of 8 passing.
That result is real but is not representative of the complete repository.

Every declared `test-*` Make target was then invoked. The run produced a
2.8 GB transcript and returned failure. Of 129 checked-in top-level bench
entities, the Make targets elaborated only 97, plus one generated bench.
Thirty-two checked-in benches were not loaded.

A separate bounded run compiled or attempted all 28 omitted VHDL benches:

- two benches no longer compile against the current RTL ports;
- one test depended on a relative data path that the Makefile generator failed
  to prepare because it contains a developer-specific absolute pathname;
- after using the checked-in generated data, exact BASIC suites 1 and 2 passed
  with 68/0 and 56/0 results;
- exact BASIC suites 0, 3, and 7 reported 169/9, 45/8, and 124/11;
- several omitted diagnostic benches reported additional failures described
  below.

The upstream Makefile does not fail when VHDL assertions use `severity error`.
It also suppresses compilation failures with `|| true`, references missing
bench files, and includes a target that leaves an orphaned GUI simulator. A
headline target completing therefore cannot be used as a pass result.

After the format-B compliance patch, the pinned Astra strict runner produced
137 bounded VHDL runs, including all five exact BASIC variants and six
Motorola-corrected derivatives:

- 105 ran clean;
- 3 failed to compile against the current RTL interface;
- 24 emitted raw simulation failures;
- 5 ran but had no usable pass/fail oracle.

The raw simulation count intentionally includes contradictory upstream tests.
Manual classification is required, but a classified failure is never converted
into an architectural waiver. Corrected derivatives currently demonstrate:

- MMUSR register coverage: 48 of 48 pass with the Figure 9-38 mask;
- T1 trace coverage: 11 of 11 pass when undefined SR bits are required to zero;
- system-register/RTR frame coverage: all checks pass with undefined CCR bits
  required to zero;
- MMU configuration: format 2, vector 56, stacked next PC, and faulting PMOVE
  instruction address all pass after the local correction.
- unaligned data MMU faulting: an invalid descriptor reached by a split
  misaligned longword takes vector 2, does not take vector 3, and does not let
  the instruction retire. The resulting user-data read frame is the required
  46-word format B with `$B008`, defined SSW value `$0141`, and logical fault
  address `$DFFFFFFD`. Both the original upstream bench and an independent
  Motorola-derived bench pass after making the fault capture persistent across
  the sequencer stall;
- user-mode format-B sizing: the stack-swap cycle no longer consumes a frame
  fill count without writing data. A corrected captured-BADFEED derivative
  passes with the 92-byte frame required by Table 8-6.

## Incorrect tests

The following observed failures conflict with Motorola and must be corrected
in the tests, not copied into the core:

- `tb_t1_trace` expects undefined SR bits to survive a write. The MC68030
  requires undefined SR bits to read as zero and ignores writes to them.
- `tb_sysreg_frame_capture` expects CCR bit 5 to survive. That bit is undefined;
  `$0009`, not `$0029`, is the architecturally valid stacked value.
- `tb_pmmu_reg_comprehensive` expects an all-one MMUSR register-port write to
  read back all sixteen bits. Figure 9-38 defines writable/status mask `$EE47`;
  the zero fields are not MMUSR state.
- `tb_pmmu_bus_verify` expects PMOVE `(An)+` and `-(An)` forms. The PMOVE table
  in the Programmer's Reference Manual permits only control-alterable EAs, so
  those forms must take an F-line unimplemented-instruction exception.
- `tb_div_rtr_frame_probe` expects a divide-trap PC to point at the divide
  instruction. MC68030 section 8.1.4 says the stacked PC points to the following
  instruction; format 2 separately records the instruction address.
- the same bench expects RTR to advance A7 by two bytes. The defined RTR
  operation pops a CCR word and a PC longword, advancing A7 by six bytes.
- `tb_stack_frame_push` correctly expects invalid TC data to use vector 56 but
  incorrectly expects format 0. Table 8-6 assigns MMU configuration exceptions
  the six-word format 2 frame, including the PMOVE instruction address.

Other old PMOVE and DIV/RTR diagnostics inherit these assumptions and need the
same instruction-by-instruction review before their output is scored.

`tb_jmp_record37_ori_tail` also reports a false failure: its modeled high-memory
window ends at `$42001FFF`, but it initializes and checks `$4204FEFF`. The read
there returns the bench's fallback NOP byte `$71`, not memory written by the
CPU. The larger exact BASIC bench maps that address and observes the expected
`$75` update.

`tb_mmu_captured_badfeed_dispatch` is also deliberately contradictory: its
source documents an 88-byte user-fault frame and chooses the initial SSP so
that this malformed frame lands at a captured A7 signature. Table 8-6 defines
format B as 46 words/92 bytes. The unchanged upstream bench now fails at
`$40079B18` because it expects `$40079B1C`; the Motorola-corrected derivative
expects `$40079B18` and passes all other dispatch checks.

## RTL deviations

The July RTL contains platform behavior that is not part of an MC68030:

- `TG68KdotC_Kernel.vhd` bypasses PMMU translation for
  `$00DD4000-$00DD5FFF` to expose a MiSTer filesystem trapdoor.
- `TG68K_PMMU_030.vhd` documents that descriptor walks are not made atomic
  against Amiga chip-bus DMA; it relies on page tables normally residing in
  fast RAM.

A generic Astra candidate must remove the address exception. Astra arbitration
must also provide the descriptor-search/update indivisibility required by the
MC68030, without weakening display or DMA behavior inside the CPU contract.

## Candidate core defects

These results are consistent with Motorola requirements and remain candidate
RTL defects pending focused minimal reproducers:

- user data faults do not reliably resume after an unchanged format A/B frame
  is returned with RTE;
- NetBSD-style read, write, MOVES, and MOVEM page-fault restart sequences fail;
- a valid supervisor stack page that needs a table walk during exception entry
  can cascade into a double fault and halt;
- WinUAE-derived exact JMP/CHK2 cases still report unresolved low-memory side
  effects and exception-state differences.

The restart failures block acceptance for a protected-memory operating system.
They are not timing or Amiga-compatibility issues.

## Acceptance status

The July core is valuable development work and passes substantial instruction,
PMMU, Amiga software, and WinUAE-derived coverage. It is not currently a clean,
complete, generic MC68030 implementation, and the supplied harness cannot prove
that claim as written.

Keep the known Astra hardware bit as the integration baseline. Treat the July
RTL as a pinned repair candidate, remove platform behavior, correct invalid
tests, reduce each valid failure to a Motorola-cited reproducer, and require the
full fail-closed suite plus Astra coretest, Harte, synthesis, and hardware before
promoting it.
