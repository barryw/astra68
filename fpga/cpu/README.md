# Astra 68 CPU

Astra has one supported RTL processor: the repaired TG68K.C MC68030-compatible
integer core with integrated PMMU under `tg68k_c_030_mmu2/`, adapted by
`tg68k030_mmu2_wrap.vhd`.

The SoC and all simulation/synthesis scripts compile this implementation
directly. There is no selectable CPU build parameter. CPU identity is fixed to
MC68030 / `TGM2` / PMMU + 32-bit data + 32-bit address in `astra_soc.sv`.

Upstream provenance, Astra changes, strict Questa tests, Motorola-derived
architectural checks, and retained regression results live with the core. The
shared `conformance/` framework compares this RTL target with the Musashi Astra
machine model.

Retired WF68K30L, 68020/no-PMMU TG68K, and first-generation TG030/PMMU imports
were removed on 2026-07-14 after their useful integration tests migrated.
