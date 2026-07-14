# Embedded boot ROM

`astra_boot.bin` is the unchanged Astra 68 boot ROM consumed by both hardware
and AstraVM. It is embedded in `astravm-machine` so the desktop reference
machine boots without requiring an m68k cross-compiler at runtime.

- Source commit: `12b784e55f6e7d5178154072040d996d15559631`
- Source epoch: `1783980086` (`2026-07-13T22:01:26Z`)
- ROM version: `0.3`
- CPU configuration: `tg68k030_mmu2`, 12.5 MHz (`CPU_CLK_DIV_BIT=0`)
- Reset SP: `0x02000000`
- Reset PC: `0xFFE00400`
- SHA-256: `29569e80544d7543401cb405108a62cd9a57fa01c3d2eb868f8e7a24eff086e7`

Rebuild on `beast` or `nuc` with deterministic provenance:

```sh
rtk env SOURCE_DATE_EPOCH=1783980086 \
  ASTRA_ROM_GIT_REVISION=12b784e55f6e7d5178154072040d996d15559631 \
  make -C sw/boot CPU_CORE=tg68k030_mmu2 CPU_CLK_DIV_BIT=0 ROM_VERSION=0.3
```

Copy the resulting `sw/boot/astra_boot.bin` here after verifying its vectors
and digest. For development builds, set `ASTRA68_BOOT_ROM=/path/to/image.bin`
when launching AstraVM; the emulator validates the reset vectors before use.
