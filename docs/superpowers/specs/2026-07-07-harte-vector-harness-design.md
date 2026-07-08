# Astra68 — Tom Harte vector test harness (design)

**Date:** 2026-07-07
**Status:** Approved (brainstorming) → ready for implementation plan
**Goal:** Exhaustively verify the WF68K30L core's instruction execution against the
Tom Harte 680x0 ProcessorTests (SingleStepTests/ProcessorTests, 68000/v1), on **real
silicon** (ULX3S ECP5), as a permanent regression net beyond the algebraic self-test ROM.

---

## 1. Context & constraints

- Harte vectors are **68000** per-instruction cases (~thousands per opcode). Each case:
  `initial`/`final` = {d0–d7, a0–a6, usp, ssp, sr, pc, prefetch[2], ram[[addr,byte]…]}
  plus `length` and cycle `transactions`.
- Our core is a **68030-class** WF68K30L. It agrees with the 68000 on data-processing
  results but diverges on exception stack frames, privileged ops, prefetch-exact state,
  and some **undefined-flag** behaviors. Cycle timing differs entirely (3-word pipe).
- The core has **no register load/scan port**. State must be established by executing
  instructions (a per-case setup routine), not by a debug bus.
- **Memory-map wall:** Harte assumes flat RAM from address 0 with random 32-bit `An`
  values dereferenced anywhere. Our SoC has 32 KB RAM at `0x01FF8000`, ROM at
  `0xFFE00000`, UART at `0xFFF00500`. Harte's memory-operand addresses do **not** map to
  writable RAM. → **Memory-operand cases are out of scope until SDRAM (or a sim variant).**

**Comparison philosophy:** FINAL-STATE only (regs + CCR), never cycle counts or prefetch.

---

## 2. Scope

**In scope (Phase 1–2): register-only data-processing cases.**
Instructions whose operands are all registers/immediates — no memory addressing modes.
ADD/SUB/MOVE/AND/OR/EOR/NOT/NEG/NEGX/CMP/CLR/EXT/SWAP/shifts/rotates/Scc/MOVEQ/ADDQ/
SUBQ/ADDI/SUBI/ANDI/ORI/EORI/CMPI/TST/… (register/immediate forms only).

These touch **no data memory** (only the instruction stream, which the harness relocates
into its own code space). This is where deep ALU/flag bugs live (the DIVS.L remainder-sign
bug found this session was register-only). Thousands of operand+flag combinations/opcode.

**Excluded (Phase 1–2), filtered by the host driver:**
- Any memory addressing mode (`(An)`, `(An)+`, `-(An)`, `d(An)`, `d(An,Xn)`, absolute, PC-rel).
- Cases where the instruction references **A7** as an operand (harness owns A7/stack).
- Privileged instructions and exception-generating cases (TRAP, illegal, /0, CHK, …).
- Cases with **T (trace) set** in `initial.sr` (would trace-trap the test instruction).
- Per-opcode **undefined CCR flags** (masked out of comparison via a defined-flags table).

**Deferred (Phase 3, separate decision): memory-operand cases** — unlock via ULX3S 32 MB
SDRAM bring-up (maps the full 68000 space; also a needed SoC feature) **or** a sim testbench
(flat sparse memory is trivial there).

---

## 3. Architecture — streaming harness

```
  Mac host (Python)            ULX3S (one bitstream)
  ┌──────────────┐   UART TX   ┌───────────────────────────┐
  │ parse Harte  │ ─────────►  │ resident harness ROM       │
  │ JSON, filter │             │  poll RX → decode case     │
  │ stream case  │             │  set regs+CCR, place instr │
  │ recv result  │ ◄─────────  │  single-step, dump regs+CCR│
  │ compare/report│  UART RX    │  → TX result               │
  └──────────────┘             └───────────────────────────┘
```

One flash, thousands of cases streamed. Host owns JSON parsing, filtering, and comparison
(reusing the robust serial layer from `check_boot.py`, **115740 baud**). The FPGA runs a
small fixed test-executive.

**New SoC scope:** the current SoC is TX-only. Add **`uart_rx`** (8N1, 3.125 MHz/115200)
and map an RX-data + RX-ready register into the existing UART block (`0xFFF005xx`). Modest,
standard, makes the console bidirectional (generally useful).

---

## 4. Per-case execution mechanism

Harness runs in **supervisor mode** with A7 = its own writable supervisor stack (in the
32 KB RAM). Because register-only, non-A7 cases never touch the stack, A7/USP/SSP are
harness-owned and **excluded from comparison**.

Per case received over UART:
1. **Decode** payload → a RAM table of {d0–d7, a0–a6}, the CCR byte, and the instruction bytes.
2. **Place** the instruction bytes at a fixed `harness_pc` in RAM, followed immediately by
   a **`JMP dump`** (no stack use — avoids any frame; simpler than TRAP for register-only).
   *(final.pc is not read from a frame; it is `harness_pc + length`, verified implicitly by
   correct decode. TRAP-based capture is the fallback if we later need exact final.pc.)*
3. **Load** d0–d7, a0–a6 with one `MOVEM.L table, d0-d7/a0-a6`.
4. **Set** CCR last: `MOVE #ccr, CCR` (so the flags are exactly `initial.sr`'s CCR when the
   test instruction executes; MOVEM does not disturb CCR).
5. **Enter** `harness_pc`. Test instruction executes → falls into `JMP dump`.
6. **Dump** routine: `MOVEM.L d0-d7/a0-a6, (buffer).L` **first** (absolute dest — no pointer
   register clobbered, and MOVEM does not disturb CCR), **then** `MOVE SR, d0` and store
   (supervisor → full SR; CCR still intact from the test instruction); TX {d0–d7, a0–a6, ccr}
   to host. Return to the RX poll loop for the next case.
   *(Order matters: capture regs+CCR before any instruction that needs a scratch register or
   touches CCR. Test instruction runs from RAM at `harness_pc`; `JMP` preserves CCR.)*

**Compared:** d0–d7 and a0–a6 (full 32-bit) vs Harte `final`; **CCR** (X,N,Z,V,C) vs low 5
bits of Harte `final.sr`, **masked by the opcode's defined-flags set** (undefined flags
ignored). Not compared: A7/USP/SSP, upper SR (S/T/I), prefetch, cycles.

---

## 5. Wire protocol (framing)

Length-prefixed, checksummed frames; host retries on timeout/bad checksum (FTDI is flaky).

- **Host→FPGA `RUN`:** `SYNC(0x55) LEN CMD=0x01 [d0..d7:32][a0..a6:28][ccr:1][ilen:1][instr:ilen] CKSUM`
- **FPGA→host `RESULT`:** `SYNC(0xAA) LEN CMD=0x81 [d0..d7:32][a0..a6:28][ccr:1] CKSUM`

Throughput: ~90 B/case each way at 115740 → ~50–100 cases/s → ~1–2 min per opcode file.

---

## 6. Components & file layout

- **SoC** `fpga/soc/astra_soc.sv` + `fpga/soc/uart_rx.sv` (new): UART RX + RX register.
- **Harness ROM** `sw/harte/harness.S` (+ minimal C): RX poll, case decode, MOVEM/CCR
  setup, single-step, dump, TX. Built like the selftest (`m68k-linux-gnu-gcc`, `astra_st.ld`,
  ROM_WORDS=2048). Fits 8 KB.
- **Host driver** `sw/harte/host/harte_run.py`: parse Harte JSON (gz), filter to
  register-only + non-A7 + non-trace + defined-flags, stream, compare, report mismatches
  (with the exact failing operands → bug reproduction). Reuses `check_boot.py` serial layer.
- **Defined-flags table** `sw/harte/host/flags.py`: per-opcode CCR mask (which flags are
  architecturally defined). Phase-1 opcodes (ADD/MOVE/ASL) are fully defined → no mask.
- **Harte data**: fetch `680x0/68000/v1/*.json.gz` from SingleStepTests/ProcessorTests to
  the Mac (or copy the subset from beast `~/astra_soc/harte/`).

---

## 7. Phasing

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 0 | UART RX in SoC + harness skeleton; host↔FPGA handshake over the streaming protocol | A ping/echo round-trips reliably on silicon |
| 1 | Full per-case setup→single-step→dump; run **ADD, MOVE, ASL** register-only cases | 100% of those cases PASS (or a real bug is found & fixed) |
| 2 | Expand host filter + defined-flags table to the **full register-only DP opcode set** | All register-only DP opcodes swept; mismatches triaged to real-bug or arch-diff |
| 3 | (Separate decision) memory-operand cases via SDRAM bring-up or sim | — |

---

## 8. Success criteria

- Every in-scope register-only case either **PASSES** or surfaces a **reproducible core bug**
  (exact operands + expected/actual regs+CCR) that we fix in the RTL and re-verify.
- Harness is **re-runnable** as a regression net (one command, streams an opcode set, reports
  pass/fail counts + mismatches).
- Zero false positives: excluded/undefined behavior is filtered, not compared.

## 9. Risks & mitigations

- **68000↔68030 undefined-flag divergence** → per-opcode defined-flags mask; start with
  fully-defined opcodes (Phase 1). Triage the rest as we expand.
- **FTDI/serial flakiness** → checksummed frames + host retries; the same 115740-baud lesson.
- **Harness-instruction interaction** (setup code perturbing state) → CCR set is the *last*
  step before the test instruction; regs loaded via a single MOVEM; `JMP` single-step avoids
  stack frames entirely.
- **Instruction relocation** (using `harness_pc`, not Harte's pc) is sound for register-only
  (position-independent, no PC-relative/absolute modes in scope); pc compared as a delta.
- **Scope honesty** — this verifies the ALU/register datapath exhaustively, NOT memory
  addressing/sequencing (covered by the selftest identities today, and Harte memory cases
  in Phase 3).
