# TG68K 030 MMU2 repair candidate

This directory began as an unmodified RTL snapshot from the source revision
used by the test repository linked from the TG68K.C `030_mmu` README. Original
source hashes are retained in `SHA256SUMS.upstream`; explicit Astra repairs are
listed in `CHANGES.astra.md`.

- Repository: `https://github.com/apolkosnik/Minimig-AGA_MiSTer`
- Branch: `030_mmu2`
- Commit: `4adbd1a1bec152c9493e4c1b53bb7961f6e27c64`
- Imported path: `rtl/tg68k/*.vhd`
- Test path: `tests/upstream/` from `tests/tg68k_030/` at the same commit

It is intentionally separate from `../tg68k_c_030_mmu/`, which is the older
TG68K.C export used by the current Astra wrapper and retained hardware bit.
Do not silently mix files between these revisions.

## Status

This snapshot is a repair candidate, not an accepted Astra CPU:

- it compiles under Questa Lattice OEM 2024.2;
- the advertised small regression target passes 8 of 8;
- the full repository harness is fail-open and does not run every checked-in
  bench;
- Astra's fail-closed runner currently reports 107 clean of 137 variants, with
  3 stale compile failures, 22 raw simulation failures, and 5 unscored benches;
- all five NetBSD-style plain-RTE demand-paging restart cases and the translated
  supervisor-stack restart case pass, including mid-transfer MOVEM recovery;
- unresolved exception-stacking, MOVES/DFC, and WinUAE-derived integer-test
  diagnostics still require Motorola-based classification or repair;
- several supplied tests themselves contradict Motorola and must be corrected.

The repaired ALU/kernel and the complete SoC now report zero combinational SCCs
both before and after ECP5 synthesis. The canonical ULX3S build routes at 38,705
TRELLIS_COMB and 9,396 flip-flops; seed 3 closes the 12.5 MHz CPU clock at
12.92 MHz and the 75 MHz SDRAM clock at 76.55 MHz, with positive worst-case
slack. This removes the open-flow blocker, but does not by itself promote the
candidate: the unresolved diagnostics above, full Harte qualification, and
hardware validation still remain.

See [`../../../docs/TG68K_030_MMU2_AUDIT.md`](../../../docs/TG68K_030_MMU2_AUDIT.md)
and [`../../../docs/MC68030_COMPLIANCE.md`](../../../docs/MC68030_COMPLIANCE.md).
The raw strict-run classification is in [`tests/RESULTS.md`](tests/RESULTS.md).

The upstream test files are also unmodified. Their Makefile is retained as
evidence, not as Astra's pass/fail authority.

`tests/run_questa_strict.sh MODELSIM_ROOT OUT_DIR` enumerates every checked-in
VHDL bench independently, runs the exact BASIC generic variants, treats compile
and simulation diagnostics as failures, and reports benches with no pass/fail
oracle as unscored. The caller must provide the Questa license environment.

## Integration boundary

The imported snapshot contained:

- a MiSTer-specific PMMU bypass for `$00DD4000-$00DD5FFF`;
- an Amiga-specific descriptor-walker arbitration assumption;
- `TG68K_CacheCtrl_030.vhd`, whose Z2/Z3 cacheability policy is platform glue.

None of those behaviors may be used by the generic Astra integration. Required
changes are recorded in `CHANGES.astra.md` or isolated in an Astra wrapper and
must be tested against the Motorola contract before this candidate replaces the
baseline.

## License

The inherited TG68K kernel files carry LGPL-3.0-or-later headers. This snapshot
comes from a repository distributed with the included GPL-3.0 license, and some
newer or headerless files do not state a narrower per-file license. Preserve all
notices and treat the snapshot under the repository license unless the upstream
authors clarify otherwise.
