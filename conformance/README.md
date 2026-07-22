# Astra68 shared architectural conformance

This directory owns architectural cases that must produce the same observable
result in the vendored Musashi emulator, RTL simulation, and retained FPGA
hardware. Motorola documentation remains the authority; agreement between two
implementations does not override the documented result.

The fixture, not a C test or VHDL bench, is the source of truth. A target
adapter receives a `ConformanceCase` and returns an `ExecutionResult` containing
CPU state and requested memory observations. Target-specific timing and
internal implementation state do not enter the shared oracle.

## Current targets and coverage

The production matrix has two canonical targets:

- `musashi-68030`, a persistent native process identifying itself as
  `vendored-musashi-68030-pmmu-no-fpu`; and
- `rtl-tg68k030-mmu2`, the synthesizable TG68K.C 68030/PMMU RTL behind a file-driven
  sparse-memory testbench.

`musashi` and `rtl` remain command-line aliases. With no `--target`, the runner
executes both canonical production targets and writes one matrix report. A
target can be selected explicitly, and repeated `--target` options construct a
custom matrix without changing the suite.

Both support:

- reset followed by masked CPU-register initialization;
- one-instruction execution;
- bounded program execution;
- completion on a masked 32-bit physical-memory marker; and
- requested physical-memory observations;
- explicit absence of an FPU; and
- fallible physical PMMU table reads and writes.

`initial.reset` has real MC68030 reset semantics: it disables translation but
does not flush the ATC. Independent PMMU fixtures must execute `PFLUSHA` before
depending on a newly installed table, just as software and hardware must.

The checked-in fixtures prove the shared path for integer register/flag state,
DIVS/DIVU overflow flags, `LINK A7`, CHK format-2 frames and N semantics, valid
packed-BCD arithmetic, rejection of the MC68040-only `MOVE16`, the F-line
vector taken when no FPU is installed, and encoded MC68030 `PMOVE`, translated
instruction fetch and data write, `PLOAD`, `PTEST`, MMUSR transfer, descriptor
U/M history, `PFLUSHA`, ATC-backed execution, physical table-search bus
failure, and format-B restart of faulted `MOVES.B` SFC/DFC transfers with
separate SRP/CRP roots and a translated supervisor stack. Invalid packed-BCD
digits remain excluded because Motorola does not define them as packed-BCD
operands.

The Tom Harte register-only subset is converted into `ConformanceCase` objects
by `sw/harte/host/musashi.py` and compared by `conformance.model.compare_result`.
Musashi and RTL therefore receive the same initial state, instruction bytes,
masks, and expected state. The FPGA UART reply is decoded into the same
`ExecutionResult`, so hardware uses the same comparator as simulation.

External CPU-cycle bus-error injection, ordered transaction traces,
interrupts, and cache observations are deliberate next protocol capabilities.
PMMU table-walk bus failure is already represented by absent sparse physical
pages. Tests requiring the remaining features stay in their focused suites
until the shared model can express them without losing information.

## Running both gates

From the repository root:

```sh
rtk make -C conformance test
```

This builds both targets, runs the Python unit/integration tests, runs every
JSON fixture under `conformance/cases` against the production matrix, and runs
the same Harte smoke vector through both adapters. The combined architectural
report is `/tmp/astra68-conformance-matrix.json`; target-specific Harte reports
are also written under `/tmp` by default.

`pmmu/unaligned-data-fault-format-b` is the first fixture extracted directly
from the RTL qualification suite. It preserves the bench's program, page-table
layout, result marker, 92-byte frame size, format/vector word, masked SSW, and
logical fault address. The RTL adapter should consume this fixture in place of
maintaining a second architectural oracle in VHDL.

`pmmu/moves-sfc-dfc-fault-restart` runs four independently faulted byte
transfers: absolute and postincrement DFC writes, a predecrement DFC write,
and a postincrement SFC read. It requires four vector-2 entries, an unmodified
format-B `RTE` restart after each descriptor repair, exact address-register
updates, copied data, and the final fault frame. The complementary RTL bench
also counts accepted target bus cycles because duplicate physical transfers
are not observable in the portable final-state model.

`cpu/fpu-absent-line-f-vector-11` makes Astra's no-FPU contract executable.
`pmmu/table-bus-fault-status` follows a valid indirect descriptor into an
absent physical table page and requires MMUSR `B|I`; returning a fabricated
zero descriptor therefore cannot pass.

To run only architectural fixtures:

```sh
rtk make -C third_party/musashi conformance-target
rtk python3 -m conformance.runner conformance/cases \
  --target musashi-68030 \
  --report /tmp/astra68-musashi-conformance.json
rtk make -C conformance/rtl
rtk python3 -m conformance.runner conformance/cases \
  --target rtl-tg68k030-mmu2 \
  --report /tmp/astra68-rtl-conformance.json
```

To run a pinned Harte corpus against Musashi rather than the board:

```sh
rtk python3 sw/harte/host/harte_run.py \
  --target musashi-68030 \
  --report /tmp/astra68-harte-musashi.json \
  /path/to/pinned-vectors/*.json.bin
```

For the separate all-vector MC68000 emulator diagnostic, use
`sw/harte/host/harte_full_run.py`; its scope and current discrepancy inventory
are documented in [`sw/harte/README.md`](../sw/harte/README.md).

## Fixture schema

[Schema version 1](schema/case-v1.schema.json) contains:

- stable ID, description, requirements, and manual citations;
- reset choice and initial CPU register values;
- sparse physical-memory segments;
- execution mode and bounded completion condition;
- requested memory ranges; and
- masked CPU and memory expectations.

Numbers may be JSON integers or strings such as `"0xff001234"`. Memory content
is a big-endian hexadecimal byte string. Expected masks are explicit so
undefined or reserved fields are never compared accidentally.

Every report hashes the fixture, worker executable, and all behavioral Musashi,
PMMU, worker, and configuration sources.

## RTL adapter contract

The RTL adapter implements `conformance.target.ConformanceTarget`:

1. Load the same sparse physical-memory segments.
2. Reset and apply the same explicitly supplied architectural registers.
3. Execute until the same completion condition or bound.
4. Return the complete normalized CPU state and requested memory ranges.
5. Identify the exact RTL, simulator executable, and source revisions in its
   manifest.

It may generate VHDL packages, memory files, or simulator commands internally.
Those are backend artifacts and must not contain a second copy of the expected
result. RTL-only clock, handshake, cache-array, and lockup assertions continue
to live beside the RTL and complement this shared architectural gate.

## Test ownership boundary

Portable architectural results belong here. This includes instructions,
defined SR bits, exceptions and frames, PMMU/cache-visible behavior, memory
ordering, and external bus ordering once the case schema can represent them.
No Musashi C test, VHDL bench, ROM self-test, or Harte transport may retain a
second expected architectural result after its case is expressible here.

Implementation-local tests remain necessary for internal invariants such as
micro-state progress, PMMU walker handshakes, cache-array replacement, clock
enables, synthesis SCCs, and timing closure. Musashi has no corresponding
internal signals, so those benches are complementary white-box gates rather
than cross-target tests. SoC boot, SDRAM, HDMI, UART, and hardware stress tests
also remain integration gates, but reusable CPU-visible expectations should be
promoted into shared fixtures instead of copied between benches.
