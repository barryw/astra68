# TG68K 030 MMU2 audit

> **Historical snapshot:** This audit records the initial July 2026 core
> qualification and pre-graphics SoC measurements. Its resource table is not
> the current integrated-machine budget. See
> [FPGA_RESOURCE_BUDGET.md](FPGA_RESOURCE_BUDGET.md) for enforced limits and
> exact current utilization, and [the shared Harte gates](../sw/harte/README.md)
> for the current two-adapter architectural result.

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

After the restart and combinational-loop repairs, the pinned Astra strict
runner produced 137 bounded VHDL runs, including all five exact BASIC variants
and six Motorola-corrected derivatives:

- 107 ran clean;
- 3 failed to compile against the current RTL interface;
- 22 emitted raw simulation failures;
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

The imported July RTL contained a MiSTer filesystem trapdoor that bypassed PMMU
translation for `$00DD4000-$00DD5FFF`. The Astra candidate removes that address
exception. `TG68K_PMMU_030.vhd` also documents that descriptor walks were not
made atomic against Amiga chip-bus DMA because page tables normally resided in
fast RAM. Astra arbitration must provide the descriptor-search/update
indivisibility required by the MC68030 without weakening display or DMA
behavior inside the CPU contract.

## Candidate core defects

The restart-qualified candidate now passes focused Motorola-cited reproducers
for unmodified format-B RTE recovery, including postincrement reads,
predecrement writes, arithmetic/CCR state, MOVEM store, mid-transfer MOVEM
load, user-mode faults, and a translated supervisor stack. The following
results remain candidate RTL defects pending focused minimal reproducers or
final classification:

- the older BADFEED software-fix diagnostic and MOVES/DFC restart diagnostic
  still fail even though the maintained plain-RTE user-data recovery benches
  pass;
- a valid supervisor stack page that needs a table walk during exception entry
  can cascade into a double fault and halt;
- WinUAE-derived exact JMP/CHK2 cases still report unresolved low-memory side
  effects and exception-state differences.

The five primary demand-paging restart failures no longer block acceptance.
The remaining fault-on-stacking and MOVES/DFC diagnostics are still relevant to
a protected-memory operating system and cannot be waived as Amiga behavior.

## Open-flow qualification

Finite ALU shift/rotate chains, complete combinational defaults, and explicit
kernel operand/address/PC calculations eliminate all 11 inherited TG68K
combinational SCCs. Standalone gates for the ALU, PMMU, cache, kernel, and
TG68K top report zero SCCs. The fail-closed full-SoC gates also report zero both
before and after ECP5 synthesis.

An earlier restart-qualified ULX3S seed-3 build routed at 38,705 of 83,640
TRELLIS_COMB (46%), 9,396 flip-flops (11%), 136 of 208 DP16KD blocks (65%), and
13 multipliers (8%). Its post-route timing reached 12.92 MHz for the constrained
12.5 MHz CPU clock and 76.55 MHz for the 75 MHz SDRAM clock. Focused arithmetic,
system-control, PMMU, restart, exception, RMC, IRQ, and SDRAM-replay tests
passed, full Astra coretest passed, and the strict 137-variant result was
unchanged from the accepted restart-qualified baseline. The current-tree
measurements below supersede those utilization and timing numbers.

### Current resource budget

A clean current-tree synthesis used the production configuration:
`tg68k030_mmu2`, integrated PMMU, external instruction/data cache store, SDRAM,
HDMI/POST console, Astraea, a 460800-baud UART with 128-byte RX FIFO, and the
256 KiB system ROM. Yosys 0.64 reports zero SCCs before and after ECP5 mapping.

The mapped design contains 37,269 pre-pack LUT primitives. The attribution
below uses retained synthesized hierarchy names and counts one primitive per
ordinary LUT4, two per CCU2C carry cell, and six per TRELLIS_DPR16X4 distributed
RAM cell. That definition exactly matches nextpnr's pre-pack total. Cross-module
optimization can move a small amount of glue between rows, so these are design
budget numbers rather than source-line accounting.

| Block | LUT primitives | FFs | DP16KD | MULT18X18D |
|---|---:|---:|---:|---:|
| TG68K wrapper, integer core, and PMMU | 27,994 | 5,544 | 0 | 10 |
| External TG68K cache store | 1,895 | 434 | 0 | 0 |
| SDRAM controller, bridge, BIST, and glue | 2,157 | 1,176 | 0 | 0 |
| Astraea blitter/DMA | 1,148 | 1,387 | 0 | 3 |
| HDMI encoder and POST console | 1,115 | 128 | 6 | 0 |
| System ROM | 787 | 5 | 114 | 0 |
| UART TX/RX/FIFO | 382 | 82 | 0 | 0 |
| Scratch RAM and top-level glue | 1,791 | 652 | 16 | 0 |
| **Total before packing** | **37,269** | **9,408** | **136** | **13** |

Within the 27,994-primitive TG68K row, retained hierarchy attributes 7,114 to
the PMMU, 6,452 to the ALU, 10,110 to the remaining kernel, and 4,318 to the
wrapper and bus/cache interface. The core plus its external cache therefore
costs about 29,889 pre-pack LUT primitives, not 23.7K, in the current Astra
integration. The PMMU is about one quarter of that CPU subsystem and avoids a
second translation unit elsewhere in the SoC.

Nextpnr packs the complete design into 38,061 of 83,640 TRELLIS_COMB (45.5%),
9,408 FFs (11.2%), 136 of 208 DP16KD blocks (65.4%), and 13 of 156 multipliers
(8.3%). This leaves 45,579 nominal combinational cells, 72 block RAMs, and 143
multipliers. A routable production budget should not plan to consume all
45,579 cells: holding the device to 75-80% TRELLIS_COMB leaves approximately
24,700-28,900 additional cells for Vega, the Astraea copper, Lyra, and future
integration growth.

Unconstrained current-tree seeds 3 and 4 routed physically but missed the
12.5 MHz CPU requirement at 12.02 and 12.45 MHz respectively. The critical
path crosses the TG68K kernel, PMMU, external instruction/data cache, bus
completion logic, and back into the kernel. Constraining only the much smaller
external cache beside the PMMU/bus interface removes that placement accident
without restricting the 30K-cell CPU core or changing the netlist. The cache
region contains 2,080 TRELLIS_COMB, 536 FF, and 112 TRELLIS_RAMW cells while
using only 19.9%, 5.1%, and 8.6% of the corresponding regional capacities.

With that cache-only constraint, current-tree seed 3 routes with zero warnings
at 12.98 MHz CPU and 82.47 MHz SDRAM against 12.5 and 75 MHz requirements. The
video pixel and shift clocks also pass at 69.83 and 239.98 MHz against 27.00 and
135.01 MHz. A timing-weighted global-placement comparison also passes at 12.90
MHz CPU, but the cache-only constraint is retained because it provides the
better CPU margin without special nextpnr tuning.

Block RAM is the immediate reclaim opportunity. The current 256 KiB system ROM
uses 114 DP16KD blocks, the 32 KiB scratch RAM uses 16, and the POST console
uses 6. A small FPGA stage-0 loader that reads the system image from SD into
SDRAM can recover most of the ROM's 114 blocks. Some recovered blocks can then
hold caches, copper RAM, audio FIFOs, and line buffers instead of implementing
those memories in LUT fabric. The full Vega, copper, and Lyra implementations
still need isolated and integrated synthesis budgets before the remaining
capacity can be called sufficient.

## Acceptance status

The July core is valuable development work and passes substantial instruction,
PMMU, Amiga software, and WinUAE-derived coverage. It is not currently a clean,
complete, generic MC68030 implementation, and the supplied harness cannot prove
that claim as written.

Keep the known Astra hardware bit as the integration baseline. Treat the July
RTL as a pinned repair candidate, correct invalid tests, reduce each remaining
valid failure to a Motorola-cited reproducer, and require full Harte and
hardware qualification before promoting it. SCC elimination, fail-closed
synthesis, full-SoC routing, and Astra coretest are no longer blockers.
