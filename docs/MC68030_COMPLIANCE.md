# MC68030 compliance policy

## Architectural authority

The Astra CPU contract is the Motorola MC68030 architecture, in this order:

1. [MC68030 User's Manual, third edition](https://www.nxp.com/docs/en/reference-manual/MC68030UM.pdf)
2. Published Motorola/Freescale errata and the M68000 Family Programmer's
   Reference Manual
3. Observations from physical MC68030 hardware when the manuals leave behavior
   undefined

WinUAE, Amiga software, upstream TG68K tests, and Astra tests are regression
references. They do not override Motorola-defined behavior. Platform-specific
address maps, cacheability rules, and compatibility workarounds must remain in
SoC glue outside the CPU, PMMU, and cache architecture.

The core must not add Astra-specific opcodes, exception meanings, status bits,
address exceptions, or privileged-register behavior. Undefined behavior may be
resolved only from Motorola errata or measurements of physical MC68030 parts;
it must not be chosen merely to satisfy one operating system or emulator. This
keeps compiler, assembler, debugger, and operating-system assumptions aligned
with a real MC68030.

Software compatibility is a consequence of implementing that contract, not a
source of alternate CPU semantics. A compiler-generated sequence that is valid
for an MC68030 must execute with Motorola-defined results and exception state;
passing one existing binary never justifies changing those rules.

## Astra scope

Required:

- MC68030 integer instruction and addressing-mode behavior
- architecturally defined status flags, privilege checks, and trace behavior
- MSP/ISP/USP operation and all MC68030 exception-frame formats
- bus error, address error, restart, and read-modify-write semantics
- full MC68030 PMMU register, ATC, table-walk, fault, and instruction behavior
- MC68030 instruction/data cache controls and cache-inhibit behavior
- 32-bit logical and physical addressing

Explicitly out of scope:

- FPU implementation
- cycle-exact instruction or external bus timing
- MC68851-only features that the MC68030 manual requires software to avoid or
  emulate after an F-line unimplemented-instruction exception
- MC68040 and MC68060 behavior

The absence of cycle accuracy does not relax instruction results, exception
state, memory ordering, PMMU/cache semantics, or externally observable bus
ordering.

## Test acceptance

A core revision is accepted only when all of these are true:

- the exact RTL revision and exact upstream test revision are pinned together;
- every available upstream bench is enumerated and run, not only headline
  aggregates;
- testbench assertions produce a failing process status;
- tests that conflict with Motorola are corrected and documented;
- genuine architecture failures are fixed in the core, not waived for an OS;
- Astra coretest, Harte vectors, PMMU/cache tests, synthesis, and hardware stress
  all pass against the same retained revision.

## Shared architectural fixtures

Portable CPU/PMMU expectations live under [`conformance/`](../conformance/),
outside both the Musashi and RTL implementations. The versioned fixture owns
the program bytes, sparse physical memory and page tables, initial
architectural state, bounded completion condition, manual citations, masks for
undefined fields, and expected architectural observations.

Each target returns a normalized CPU/memory result. Musashi, RTL simulation,
and FPGA adapters may have different loaders and transports, but may not embed
a second expected result. Reports hash the common runner, fixture, target
adapter, executable, and behavioral implementation sources. Missing target
capabilities and unknown fixture fields fail closed.

The shared matrix includes a fixture extracted directly from the
Motorola-corrected RTL unaligned-PMMU-fault bench. It checks vector 2 rather
than vector 3, the 46-word/92-byte format-B frame, format/vector word, defined
SSW bits, and logical fault address. Separate fixtures cover DIV overflow C/V,
`LINK A7`, CHK format-2/N behavior, valid packed-BCD arithmetic, rejection of
the MC68040-only `MOVE16`, an F-line vector 11 when no FPU is present, and
MMUSR `B|I` when a physical table read fails. Run the current shared gate with:

```sh
rtk make -C conformance test
```

Tom Harte vectors are converted to the same `ConformanceCase` model and use the
same result comparator. The MC68030 gate
retains the conservative architecture-invariant 68000 filter; a separate
MC68000-mode diagnostic executes the full corpus and reports every mismatch.
Neither mode is a PMMU or MC68030 exception-frame authority.

The July 13, 2026 Musashi baseline passes all 96,103 vectors admitted by the
MC68030-compatible gate. Its separate full-state diagnostic also passes all
256,894 ordinary vectors. The only remaining full-corpus discrepancies are
55,606 MC68000-specific address-error cases, whose microcoded frame and
partial-side-effect behavior are outside Astra's MC68030 contract.

## Upstream audit

The `TG68K.C/030_mmu` export at commit
`9876ecd89c798c6aa6515621aaafc7e0f902a00a` is older than the RTL used by the
linked `Minimig-AGA_MiSTer/030_mmu2` suite at commit
`4adbd1a1bec152c9493e4c1b53bb7961f6e27c64`. They must not be described as one
tested revision.

Initial execution of the July suite under Questa 2024.2 found:

- a stale test expecting reserved SR bits to survive a write even though the
  MC68030 manual requires undefined SR bits to read as zero and be written zero;
- an upstream test that expected the wrong format-0 frame for invalid TC data;
  a Motorola-corrected format-2 test showed that vector 56 and the stacked PC
  were correct but found the instruction-address field using the next PC. The
  Astra candidate now latches the PMOVE opcode PC and passes that corrected
  test;
- Makefile references to missing benches and assertion failures that do not
  propagate to `make`;
- an Amiga-specific `cpu_wrapper.v` that does not compile standalone under
  modern Questa and is not an acceptable generic Astra integration boundary.

An exhaustive invocation of every declared `test-*` Make target still loaded
only 97 of the 129 checked-in top-level testbench entities, plus one generated
bench. Thirty-two checked-in benches were never elaborated. A separate strict
run of the omitted VHDL benches exposed stale benches that no longer compile,
missing data-path assumptions, and additional unresolved failures in PMOVE
addressing, MMU fault restart, fault-on-stacking, odd-address handling,
DIV/RTR exception state, and the WinUAE-derived JMP/CHK2 cases. Two exact BASIC
sub-suites passed, while three reported failures. These are not accepted as
core defects or waived as test defects until each expected result is checked
against Motorola documentation.

The July RTL also contains two platform-specific behaviors that cannot be part
of the Astra CPU contract:

- logical addresses `$00DD4000-$00DD5FFF` bypass PMMU translation for a MiSTer
  filesystem trapdoor;
- the PMMU walker relies on Amiga page tables residing in fast RAM instead of
  requiring exclusive arbitration while descriptors are read and updated.

The address bypass must be absent from a generic core. Astra's bus arbiter must
also guarantee the indivisibility required by Motorola for descriptor update
sequences; display or DMA timing is not permitted to weaken CPU semantics.

These findings are an audit baseline, not a waiver list.
