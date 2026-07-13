# TG68K.C 030 MMU import

This directory contains an unmodified source import from:

- Repository: `https://github.com/apolkosnik/TG68K.C`
- Branch: `030_mmu`
- Commit: `9876ecd89c798c6aa6515621aaafc7e0f902a00a`

This is a source-only candidate import. It is intentionally separate from
`../tg68k_c/`, which tracks upstream no-MMU TG68K.C.

Current integration status:

- Not wired into `astra_soc.sv` yet.
- The branch replaces the multi-CPU upstream modes with a 68030-oriented core
  selected as `CPU => "10"`.
- The kernel includes the 030 PMMU, but the top-level cache fill and PMMU
  walker memory paths need an Astra-specific arbiter before this can be treated
  as a real hardware candidate.
- Keep upstream files unmodified; put Astra integration changes in wrappers or
  clearly documented local patches.

Licensing note:

- The VHDL file headers state LGPL-3-or-later licensing.
