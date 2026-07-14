# Astra68 MC68030 PMMU model

This directory contains the independently implemented PMMU used by the
vendored Musashi emulator. It is a host-side architectural test rig for Astra68
software and a differential oracle for the RTL; the Motorola manuals and
verified hardware behavior remain authoritative.

## Implemented behavior

- TC, CRP, SRP, TT0, TT1, and MMUSR formats and write masks;
- TC and root-pointer configuration validation;
- 22-entry fully associative ATC with function-code tags;
- CPU-space bypass and both transparent-translation registers;
- CRP/SRP selection, optional function-code lookup, and up to five table
  levels plus indirection;
- short and long table/page descriptors;
- early termination, root termination, indirect descriptors, and signed
  modulo-32-bit page-offset addition;
- upper/lower limits, supervisor-only pages, write protection, cache inhibit,
  and U/M history updates;
- normal translation, `PLOAD`, `PTEST` levels 0-7, `PFLUSHA`, and selective
  `PFLUSH`;
- physical table-walk callbacks with optional begin/end hooks for indivisible
  descriptor searches;
- reset behavior that clears TC/TT enables without flushing the ATC;
- MC68030 16-word format-A and 46-word format-B PMMU fault frames, including
  SSW, logical fault address, DOB/DIB, stage-B address, and version field;
- format-A deferred-final-write replay and format-B instruction/data restart;
- handler-completed reads and writes through a cleared DF bit and stacked
  DIB/DOB;
- user-to-supervisor stack switching across a fault and `RTE`; and
- replay of completed pre-fault data cycles so a restarted multi-transfer
  instruction does not repeat an MMIO read or write.

The Musashi adapter decodes only `PMOVE`, `PFLUSH`, `PLOAD`, and `PTEST`.
MC68851-only commands deliberately raise the F-line unimplemented exception.
The Astra build also disables Musashi's FPU opcode routing, so every FPU
instruction takes the same vector-11 path as no-FPU hardware.

## Tests

`tests/test_pmmu030.c` tests the PMMU without a CPU engine. It covers register
validation, transparent translation, ATC hits/refills, U/M updates, write
protection, root and long-descriptor limits, SRP selection, FCL, early
termination, indirection, `PLOAD`, `PTEST`, selective flushes, and injected
table-bus failures.

`tests/test_musashi_pmmu.c` executes encoded 68030 instructions through
Musashi. It proves translated instruction fetch and data write, `PMOVE`,
`PLOAD`, `PTEST`, MMUSR transfer, `PFLUSHA`, TT matching, the vector 56 format-2
configuration frame, a long write split across discontiguous physical pages,
and rejection of a 68851-only command. It also checks every defined format-A/B
field byte-for-byte, 32/92-byte frame sizing, format-B version rejection,
mapping-repair restart, DIB software completion, user stack swapping, pending
format-A write replay, instruction-fetch faults, and exactly-once `MOVEM`
stores across a mid-instruction fault.

Run the complete focused suite from the parent directory:

```sh
make clean test-pmmu
```

The backend-neutral architectural fixtures and Harte adapter live in the
repository-level `conformance/` directory. They build a persistent target from
this exact vendored source tree and return normalized observations suitable for
later comparison with RTL and FPGA targets:

```sh
rtk make -C conformance test
```

Expected results remain in the common fixtures, not in the Musashi worker.
The shared table-bus case proves that an absent physical descriptor page sets
MMUSR `B|I` instead of being misread as a zero descriptor.

## Known conformance work

These are explicit gaps, not behavior for Astra OS to depend upon:

- The new MC68030 path applies to faults raised by Astra's PMMU adapter.
  Host-injected external BERR still enters Musashi's inherited legacy path.
- Format-A generation currently recognizes the complete `MOVE`-to-memory
  family, whose destination write is unambiguously final. Other final-write
  opcode families conservatively receive a valid format-B frame until their
  last-cycle classification has focused tests.
- Architecturally internal frame words and pipe images are deterministic, not
  a cycle-level model of the physical 68030 pipeline. Software may not depend
  on those internal values; defined offsets and fields are tested exactly.
- One pending PMMU restart record is retained in the CPU context. A page-fault
  handler and its supervisor stack must therefore remain wired; nested PMMU
  faults in the handler are not yet a supported recovery path.
- Faults while `RTE` reloads a format-A/B frame, or while it retries a still
  unresolved format-A pending write, do not yet model the MC68030's validation
  and double-fault phases cycle-for-cycle.
- The generic PMMU reports cache-inhibit state, but the Musashi host API has no
  cache-inhibit callback yet.
- Physical table reads and U/M writes now use fallible host callbacks. The
  generic PMMU can also bracket an indivisible walk, but the Musashi host API
  does not yet expose a separate external bus-lock signal.
- Musashi does not currently label every memory read/write pair as an RMC
  operation. This only affects TT matching when RWM is clear; standalone tests
  cover the correct PMMU rule.
- The ATC replacement cursor is deterministic round-robin. Motorola documents
  a pseudo-LRU policy but does not expose its precise history algorithm.
- Instruction and table-walk cycle timing is not cycle-accurate.
- The result's cache-inhibit bit is not connected to a modeled 68030 cache.

Those gaps should become focused tests before the corresponding behavior is
used as a hardware oracle.
