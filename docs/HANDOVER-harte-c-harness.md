# HANDOVER — Harte harness C rewrite (next session)

**Date:** 2026-07-08
**Branch:** `harte-harness`
**Guiding principle (from Barry):** simplest thing that works. No cleverness. Reuse the
**proven** 68k patterns already in this repo. 45+ years of 68k wisdom exists — lean on it.

---

## TL;DR for next session

The CPU core is **done and solid on silicon** (5/5 `SELFTEST: PASS`). We're building the Harte
vector test harness. Phase 0 hardware plumbing (UART RX in the SoC + host framing library) is
built and the **SoC is confirmed good on silicon**. The ONE blocker: the harness ROM was written
in **bare 68k assembly** and won't boot on hardware. The fix is not to debug the bare-asm — it's
to **rewrite the harness in C, reusing the exact scaffolding the working selftest already uses**
(`crt0.S` + `astra_st.ld`). That path is proven to boot and do UART. Start there.

---

## What is DONE and verified (do not redo)

- **git**: repo initialized; vendored WF68K30L core flattened in (one repo); branch `harte-harness`.
  Commits: `e3bb2f9` uart_rx, `c17e284`+`7d5711f` SoC RX wiring, `7b9b4ab` build infra, `4ba578e` harness skeleton+proto.
- **Task 1 — `fpga/soc/uart_rx.sv`**: 8N1 receiver, iverilog-tested. Good.
- **Task 2 — SoC RX wired** into `fpga/soc/astra_soc.sv`: read regs
  `0xFFF00508` bit0 = `rx_ready`, `0xFFF0050C` = RX byte (reading it clears `rx_ready`).
  TX unchanged (`0xFFF00500` write, `0xFFF00504` bit0 = TX ready). **Confirmed good on silicon**
  (the known-good selftest prints `PASS sum=574D530E` on this exact SoC).
- **Task 3 code — `sw/harte/host/proto.py`** (+ passing pytest), **`ping.py`**: host-side framing. Good.
- **Build flow self-contained**: `fpga/soc/oss_flow/mkbit.sh <rom.hex> <tag>` builds the canonical
  repo SoC → `astra.bit`. Confirmed loop-free (nextpnr rc=0) and boots.

## The BLOCKER (why we stopped)

`sw/harte/harness.S` (bare 68k asm, linked with `sw/harte/harness.ld`) **does not boot on hardware** —
it freezes right after its first peripheral read (LEDs: heartbeat blinks, `uart_busy` off, address
bus frozen at `…8`, i.e. halted). The gcc-C selftest (`crt0.S` + `astra_st.ld`) boots fine on the
same SoC. So it is a bare-asm-vs-C problem, not a SoC problem. Leading theory: a spurious early
exception hits the bare-asm's `STOP` handler (vectors 2–255 → `_halt` = `STOP #0x2700`), which
**freezes**; the selftest's crt0 handler does `RTE` and survives. **Do not rabbit-hole on this.**

---

## THE PLAN: rewrite the harness in C (simplest proven path)

Reuse the selftest's scaffolding **verbatim**. The selftest lives in `sw/boot/` and is known to
boot + do UART on this exact hardware. Copy its structure.

### Files to create (`sw/harte/`)
- **Reuse as-is** (copy from `sw/boot/`, do not modify): `crt0.S`, `astra_st.ld`. These give you a
  working reset vector table, SSP setup, `.data`/`.bss` init, `jsr kmain`, and an `RTE`
  `_default_handler` for spurious exceptions — the whole reason C boots and bare-asm didn't.
- **`sw/harte/harness.c`** — `kmain()`: the whole harness in C. Structure it exactly like
  `sw/boot/selftest.c`'s UART helpers (proven):
  ```c
  #include "vesta.h"
  static void putc(char c){ while(!(VESTA->UART_STATUS & UART_TX_READY)){} VESTA->UART_DATA=(uint8_t)c; }
  static uint8_t getc(void){ while(!(VESTA->UART_RXSTATUS & 1)){} return VESTA->UART_RXDATA; }  // add RXSTATUS/RXDATA to vesta.h
  ```
  (Add `UART_RXSTATUS` @ offset 0x08 and `UART_RXDATA` @ 0x0C to `sw/include/vesta.h` — mirror the
  existing UART_STATUS/UART_DATA fields.)

### Phase 0 first — prove the C harness boots + bidirectional UART (SMALL, do this before anything else)
`kmain()` = a PING/echo loop:
```c
void kmain(void){
    putc('R');                         // boot marker (proves C harness boots + TX)
    for(;;){
        uint8_t b = getc();            // blocks for a host byte (proves RX)
        putc(b);                       // echo it
    }
}
```
Build (same recipe as selftest, on beast `~/astra_st`):
`m68k-linux-gnu-gcc -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib -I ../include -T astra_st.ld -o h.elf crt0.S harness.c && objcopy -O binary h.elf h.bin && python3 bin2hex.py h.bin rom_harness.hex`
→ pull hex → `mkbit.sh rom_harness.hex harness` → flash → power-cycle → host sends bytes, expects echo + sees 'R'.
**If 'R' streams and bytes echo, the C harness works — the whole bare-asm problem is gone.**

### Then the protocol (proto.py already exists, host side done)
Implement the wire protocol from the spec in C:
- **PING** `0x55 0x03 0x02 <byte> cksum` → **PONG** `0xAA 0x03 0x80 <byte> cksum`.
  ⚠️ **Known bug to avoid:** the old bare-asm sent PONG `LEN=0x02`; it MUST be **`0x03`** (CMD+byte+cksum
  = 3 bytes after LEN). `proto.py`'s `parse()` requires the correct LEN.
- Keep it dead simple: read byte-by-byte, match `0x55`, read LEN/CMD/payload, dispatch.

### Then Task 5 — per-case execution (the only part that needs asm, keep it minimal)
Do the register load + single-step in ONE small inline-asm block inside a C function (the
established 68k way — don't hand-roll a whole asm program):
- Host sends a case (regs, CCR, instruction bytes) — see spec §5 wire protocol.
- C writes the instruction bytes + a trailing `JMP dump` into a RAM buffer at a fixed `harness_pc`.
- Inline asm: `movem.l regtable,%d0-%d7/%a0-%a6` ; `move.b ccr,%d0 ; move.w %d0,%ccr` (set CCR LAST) ;
  `jmp harness_pc`. The test instruction runs, falls into `JMP dump`.
- `dump`: `movem.l %d0-%d7/%a0-%a6,(resultbuf).l` FIRST (absolute dest, no reg clobber), then read SR,
  send regs+CCR to host. Host (`harte_run.py`, to be written) compares d0-7/a0-6 + CCR (masked).
- Full detail: `docs/superpowers/plans/2026-07-07-harte-vector-harness.md` Task 5 (the asm sequence is
  correct there; just host it in C instead of a standalone .S).

### Scope (unchanged, approved)
Register-only data-processing Harte 68000 cases (ADD/MOVE/ASL first, then the DP set). No memory
operands / A7 / privileged / trace / undefined-flags. Memory cases = Phase 3 (SDRAM or sim). Compare
final d0-7, a0-6, CCR (masked by per-opcode defined-flags). Skip prefetch + cycle counts.

---

## Delete / supersede
- `sw/harte/harness.S` + `sw/harte/harness.ld` (bare-asm) — **superseded by the C rewrite**. The plan's
  Task 3/5 "bare-asm" wording is superseded by this doc. `sw/harte/host/proto.py`, `ping.py`, tests stay.
- Scratchpad `echo.S`/`echo2.S` were throwaway debug ROMs (session temp, gone).

## Hardware workflow (hard-won — follow exactly)
1. Build firmware on **beast** (`ssh beast`, `~/astra_st/`, `m68k-linux-gnu-gcc`), pull the `.hex`.
2. `cd fpga/soc/oss_flow && bash mkbit.sh <rom.hex> <tag>` → `astra.bit` (expect `nextpnr rc=0`).
3. `openFPGALoader --board ulx3s -f astra.bit` (~3 min, writes SPI flash).
4. **Power-cycle the board** (unplug/replug USB) — mandatory, resets the FT231X to UART mode.
5. Host serial: **115740 baud** (NOT 115200) on `/dev/cu.usbserial-D01457`. macOS FTDI VCP is flaky —
   use a retry loop. NEVER "start a reader then unplug" — the reader crashes (Errno 6) on the unplug.
6. Liveness oracle when UART is silent: LEDs = `{hb[23] heartbeat, ~AS, ~RW, uart_busy, adr[3:0]}`.

## Pointers
- Memory (basic-memory, project claude-memory): note "Astra68 — Harte harness Phase 0 in progress…"
  and the prior "RESOLVED" note. Ledger: `.superpowers/sdd/progress.md`.
- Spec: `docs/superpowers/specs/2026-07-07-harte-vector-harness-design.md`.
- Plan: `docs/superpowers/plans/2026-07-07-harte-vector-harness.md` (Task 5 asm sequence is the reference).
- Working reference to copy: `sw/boot/{crt0.S,astra_st.ld,selftest.c,Makefile}`, `sw/include/vesta.h`.
