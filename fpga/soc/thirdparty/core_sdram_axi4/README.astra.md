# core_sdram_axi4 physical core

This directory vendors `src_v/sdram_axi_core.v` from:

- Repository: https://github.com/ultraembedded/core_sdram_axi4
- Commit: `c4becd60f0ce9ada991d894e36036b9429446922`
- Upstream license: GPL-2.0-or-later in the source header; the repository also
  ships the included GPL-3.0 `LICENSE` file.

The upstream AXI and PMEM adapters are intentionally not included. Astra uses
the core's masked 32-bit request interface behind its own CPU/DMA arbiter. The
vendored Verilog file is unmodified; board adaptation belongs in Astra wrapper
modules so upstream provenance remains byte-exact.
