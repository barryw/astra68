# TG68K 030 MMU2 Astra core

This directory began as an unmodified RTL snapshot from the source revision
used by the test repository linked from the TG68K.C `030_mmu` README. Original
source hashes are retained in `SHA256SUMS.upstream`; explicit Astra repairs are
listed in `CHANGES.astra.md`.

- Repository: `https://github.com/apolkosnik/Minimig-AGA_MiSTer`
- Branch: `030_mmu2`
- Commit: `4adbd1a1bec152c9493e4c1b53bb7961f6e27c64`
- Imported path: `rtl/tg68k/*.vhd`
- Test path: `tests/upstream/` from `tests/tg68k_030/` at the same commit

This is Astra's sole supported RTL CPU revision. Do not silently mix files from
other TG68K branches into this pinned source set.

## Status

This snapshot is integrated as Astra's development CPU, but production
acceptance remains fail-closed under `docs/MC68030_COMPLIANCE.md`:

- it compiles under Questa Lattice OEM 2024.2;
- upstream tests are enumerated independently by Astra's strict runner instead
  of trusting the upstream fail-open aggregate;
- Motorola-derived PMMU, exception-frame, trace, and restart checks are retained
  under `tests/motorola/`;
- the shared `conformance/` framework runs the same architecture cases against
  RTL and the Musashi Astra model;
- the maintained Harte hardware corpus and full SoC boot/SDRAM tests are release
  gates, not substitutes for the Motorola contract.

The repaired ALU/kernel and complete SoC report zero combinational SCCs before
and after ECP5 synthesis. Canonical ULX3S builds constrain a 12.5 MHz CPU clock
and the accepted 60 MHz SDRAM domain; exact utilization, seed, timing, and
retained hardware identity belong in build reports because the surrounding
chipset is still changing.

See [`../../../docs/TG68K_030_MMU2_AUDIT.md`](../../../docs/TG68K_030_MMU2_AUDIT.md)
and [`../../../docs/MC68030_COMPLIANCE.md`](../../../docs/MC68030_COMPLIANCE.md).
The historical strict-run classification is in [`tests/RESULTS.md`](tests/RESULTS.md).

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
must be tested against the Motorola contract before production acceptance.

## License

The inherited TG68K kernel files carry LGPL-3.0-or-later headers. This snapshot
comes from a repository distributed with the included GPL-3.0 license, and some
newer or headerless files do not state a narrower per-file license. Preserve all
notices and treat the snapshot under the repository license unless the upstream
authors clarify otherwise.
