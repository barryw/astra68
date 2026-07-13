# Musashi vendor record

## Upstream identity

- Project: [Musashi](https://github.com/kstenerud/Musashi)
- Upstream commit: `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`
- Upstream commit date: 2026-03-08
- Imported: 2026-07-13
- License: permissive MIT-style license in `readme.txt` and the source headers

The source was imported without upstream Git metadata. Generated files
`m68kops.c` and `m68kops.h` are intentionally not checked in; `m68kmake`
regenerates them.

## PMMU exclusion and provenance boundary

Upstream's root `m68kmmu.h` and its `example/m68kmmu.h` link were excluded
from the import. The file now named `m68kmmu.h` is Astra68 integration code,
and everything under `astra/` is Astra68-authored code.

The replacement was written from these architecture specifications:

- [MC68030 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68030UM.pdf), section 9;
- [M68000 Family Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf), the `PFLUSH`, `PLOAD`, `PMOVE`, and `PTEST` entries.

No source text was copied from upstream Musashi's PMMU implementation. This is
an independent rewrite in the practical engineering sense, not a formal legal
clean-room process: an Astra68 agent inspected the upstream PMMU while
evaluating whether it was usable before this rewrite was chosen. We therefore
must not claim that the author was unexposed to the old implementation.

## Astra68 changes to upstream files

- `Makefile` builds the independent PMMU module and exposes `test-pmmu`.
- `m68kcpu.h` stores PMMU and restart state in the CPU context, translates
  program/data/cross-page accesses, and builds MC68030 format-A/B frames.
- `m68kcpu.c` initializes and resets PMMU/fault state and takes instruction
  boundary snapshots used for restart.
- `m68kmmu.h` decodes the MC68030 PMMU instruction set and captures exact fault
  cycle metadata.
- `m68k_in.c` accepts and validates format-A/B frames in `RTE`, including
  mapping-repair restart and handler-completed DIB/DOB cycles.
- `example/Makefile` links the replacement module; the example links back to
  the Astra68 integration header and module.

All other imported files should remain as close to the pinned upstream commit
as practical. Future updates must repeat the explicit exclusion of upstream
`m68kmmu.h`, review license changes, regenerate the opcode tables, and run both
PMMU suites before the vendor revision changes.

## Reproduction and verification

The import was made from the pinned checkout with the equivalent of:

```sh
rsync -a --exclude=.git --exclude=m68kmmu.h UPSTREAM/ third_party/musashi/
```

Project builds and simulations run on `nuc`. The focused verification command
is:

```sh
cd third_party/musashi
make clean test-pmmu
```

This builds Musashi, runs the standalone PMMU model suite, and executes real
68030 PMMU command words through Musashi's generated opcode engine.
