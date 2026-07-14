# Astra Harte gates

This harness runs the architecture-invariant portion of the maintained
SingleStepTests Motorola 68000 vectors on the exact Astra FPGA CPU revision.
It is an integer-core regression gate, not the architectural authority and not
a PMMU test. Motorola documentation remains authoritative for every
disagreement.

## Pinned corpus

- Repository: `https://github.com/SingleStepTests/m68000.git`
- Revision: `64b253116a3de04aaac4346c43680960dc9b67e5`
- Path: `v1`
- Files: 127 binary vector files, 132 MiB

The maintained corpus is generated with MAME's microcoded 68000 core. Its
upstream status marks every family except TAS and TRAPV as verified. The
MC68030 hardware/register gate excludes both; the full MC68000 emulator
diagnostic retains them under explicit upstream-caveat classifications.

Fetch the immutable corpus without adding its 193 MiB checkout to Git:

```sh
bash sw/harte/fetch_vectors.sh
```

The host runner reads the upstream binary format directly and hashes every
input file and every behavioral host source file into its JSON result. A result
therefore identifies the FPGA build ID, runner implementation, run parameters,
and complete vector corpus.

## MC68030 hardware/register scope

The protocol-v2 harness admits sequential, register-only cases that do not use
A7, memory operands, control flow, privileged state, undefined flags, or
68000-specific exception frames. Trace bits are intentionally not loaded: they
cannot alter the compared data result for the admitted instructions, and trace
exception behavior has dedicated MC68030 tests. The runner compares D0-D7,
A0-A6, and all architecturally defined or preserved CCR bits.

The pinned corpus contains 317,500 vectors. The current filter admits 96,103;
the other 221,397 are explicitly classified by skip reason. The filter rejects
the size-`11` system-register and TAS encodings that share opcode bands with
ordinary unary operations.

This scope is deliberately conservative. A 68030 does not have the 68000's
exception frames, prefetch state, or 24-bit external address bus, so blindly
requiring every 68000 vector to match would be incorrect. Memory and exception
coverage must use a 68030-aware execution harness or Motorola-derived tests.

## Hardware run

The default build is the pinned `tg68k030_mmu2` core at 12.5 MHz with SDRAM,
HDMI, caches, and Astraea present. The harness UART runs at 460800 baud and has
a 128-byte RX FIFO. Test loads use FPGA SRAM by default, preserving the known
POST image in SPI flash.

On `nuc`, with the ULX3S attached:

```sh
bash sw/harte/build_flash.sh "full pinned register subset"
VECTORS=$(bash sw/harte/fetch_vectors.sh)
python3 sw/harte/host/harte_run.py \
  --report /tmp/astra68-harte-full.json \
  "$VECTORS"/*.json.bin
```

The runner refuses an unpinned or mismatched FPGA build, validates protocol and
FIFO capabilities, stops after a wedged case instead of timing out through the
rest of the corpus, checkpoints every 1,000 cases, and exits nonzero for any
test or infrastructure failure.

## Shared Musashi run

The same admitted vector IDs can run through the backend-neutral conformance
target backed by Astra's vendored Musashi 68030/PMMU:

```sh
rtk make -C third_party/musashi conformance-target
rtk python3 sw/harte/host/harte_run.py \
  --target musashi-68030 \
  --report /tmp/astra68-harte-musashi.json \
  "$VECTORS"/*.json.bin
```

Each admitted vector is translated into the same backend-neutral
`ConformanceCase` used by checked-in fixtures. Musashi, GHDL RTL, and FPGA UART
replies all return or are decoded into `ExecutionResult`, then use the same
masked comparator. Parsing, scope classification, expected state, flag masks,
failure aggregation, and report format are shared; only execution transport is
different. The Musashi target identity includes hashes of its executable and
behavioral CPU/PMMU sources.

The pinned July 13, 2026 baseline passes all 96,103 admitted vectors on the
vendored Musashi MC68030 target. The remaining 221,397 vectors are reported by
skip reason rather than silently dropped.

## Full MC68000 emulator diagnostic

The companion adapter executes every vector in a persistent Musashi process
configured as an MC68000:

```sh
rtk python3 sw/harte/host/harte_full_run.py \
  --report /tmp/astra68-harte-musashi-68000.json \
  "$VECTORS"/*.json.bin
```

It restores and compares all D/A registers, USP/SSP, architecturally defined
SR bits, and sparse memory. It deliberately does not claim prefetch-queue,
ordered bus-transaction, or cycle-count equivalence. This mode is a complete
corpus diagnostic for the vendored emulator, not an MC68030 hardware gate:
the MC68000 has different exception frames, prefetch behavior, and a 24-bit
external address bus.

The pinned July 13, 2026 baseline attempted all 317,500 vectors:

- ordinary architectural state: 256,894 pass, 0 fail;
- MC68000 address-error cases: 0 pass, 55,606 fail;
- upstream-caveat TAS: 2,500 pass, 0 fail; and
- upstream-caveat TRAPV: 2,500 pass, 0 fail.

The total is 261,894 pass and 55,606 fail. All 55,606 remaining failures are
classified MC68000 address-error cases. They remain visible because inherited
Musashi does not reproduce the MC68000's
microcoded address-error frame, stacked PC, SSW, and partial-side-effect
behavior. That behavior is not part of Astra's MC68030 contract.

The former 1,837 ordinary discrepancies were closed in the emulator by five
root fixes: DIVS/DIVU now clear C on quotient overflow; `LINK A7` stacks the
predecrement SP; CHK refreshes N on every result; ABCD/SBCD/NBCD use the
hardware-observed correction behavior for invalid packed-BCD digits; and the
68040-only MOVE16 decoder can no longer execute on earlier CPUs. The legacy
ABCD/SBCD aggregate tests were updated because their binary countdown loops
also include invalid packed-BCD operands.

Invalid nibbles are not packed-BCD operands in Motorola's architectural
description. Their observed results remain an emulator diagnostic, not an
Astra MC68030 requirement; a shared hardware fixture must not assert them
without a physical MC68030 measurement.

## Legacy corpus

The archived `SingleStepTests/ProcessorTests` million-vector corpus is useful
for stress diagnostics but is not the qualification authority. Its stale shift
expectations include mathematically invalid ASL results and incorrect ASR X/C
values that disagree with Motorola and the maintained corpus. Keep any runs
against it labeled `legacy`.

## Local checks

```sh
python3 -m pytest sw/harte/host/tests -q
make -C conformance test
iverilog -g2012 -o /tmp/tb_uart_rx_fifo \
  fpga/soc/sim/tb_uart_rx_fifo.sv fpga/soc/uart_rx_fifo.sv
vvp /tmp/tb_uart_rx_fifo
bash sw/harte/run_sim.sh
```
