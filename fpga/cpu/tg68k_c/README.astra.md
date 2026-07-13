# TG68K.C import

This directory started as a source import from:

- Repository: `https://github.com/TobiFlex/TG68K.C`
- Branch: `master`
- Commit: `ade33e396a1e647c2de9daf71ff9d5b3979639b2`

The imported files are used by `../tg68k_wrap.vhd`, which adapts the upstream
16-bit 68K-style bus to Astra's existing 32-bit SoC bus contract.

Notes:

- This is the upstream no-MMU TG68K.C candidate, currently instantiated in
  68020 mode.
- The GitHub repository did not expose a root license file when imported.
- The VHDL file headers state LGPL-3-or-later licensing.
- Local Astra patches:
  - `TG68KdotC_Kernel.vhd` carries a minimal MC68020/030 stack-control patch
    for `MOVEC USP/MSP/ISP` and SR M-bit A7 switching. The upstream master
    implementation accepted MSP/ISP selectors but did not maintain separate
    supervisor stack shadows.
  - Held level-7 interrupts are edge-qualified so a continuously asserted
    `IPL=000` request is not accepted again while SR IPL is already 7.
  - Privileged-opcode decode checks use the stable supervisor-mode latch
    (`SVmode`) rather than the live SR bit during same-instruction SR writes.
  - The debug status output reports the core's current PC for simulation traces.
