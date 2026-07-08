# HANDOVER — Harte harness (next session)

**Date:** 2026-07-08  **Branch:** `harte-harness`  **HEAD:** `bd4a2fe`
**Rule (Barry):** correct fixes only — no workarounds (they break later). Always know exactly
what bitstream is loaded.

---

## TL;DR

The Harte per-case harness is **functionally complete and simulation-verified**. It boots, does
bidirectional UART, speaks the PING/RUN protocol, and executes a case correctly **in simulation**.
The ONE blocker is a **hardware timing bug**: a `MOVEM` read-burst immediately followed by an
instruction fetch from RAM hangs the CPU on silicon. It does **not** reproduce in functional sim,
so the harness logic is right — the fix is in the **timing domain** (the WF68K30L's negedge
DSACK/read-data paths, already flagged marginal at `astra_soc.sv:27`).

**Start here:** pull the nextpnr timing report for the negedge paths, then fix clock margin /
bus wait-states / core bus-interface retiming and verify on the NUC.

---

## Hardware + workflow (the FTDI nightmare is SOLVED)

The ULX3S now lives on the **NUC** (Ubuntu 24.04, `ssh nuc`). Linux `ftdi_sio` re-binds the serial
port cleanly after openFPGALoader, so **no more power-cycles** between flashes. Everything (m68k-gcc
firmware build, yosys/nextpnr/ecppack bitstream, openFPGALoader flash, pyserial, iverilog/verilator
sim) runs on the NUC. Repo is at `nuc:~/astra68` (keep it in sync with `rsync -a <file> nuc:~/astra68/...`).
Toolchain: `~/oss-cad-suite` (source its `environment`), apt `gcc-m68k-linux-gnu` + `python3-serial`,
udev rule `/etc/udev/rules.d/99-ulx3s.rules` (0403:6015 → plugdev).

### ALWAYS build+flash via the provenance pipeline — never flash a bare .bit
```
ssh nuc 'cd ~/astra68 && bash sw/harte/build_flash.sh "one-line description of this build"'
```
It: builds fw (regenerates `build_id.h` = SHA-1 of ALL RTL+fw source) → builds bitstream → flashes →
**queries the running device (CMD_ID) and hard-fails unless it reports the exact BUILD_ID just built**
→ appends to `sw/harte/BUILD_LOG.md`. So a stale/failed flash can't masquerade as success.

Know what's loaded any time: `ssh nuc 'cd ~/astra68/sw/harte/host && python3 whatsloaded.py'`
→ prints the device BUILD_ID + its BUILD_LOG row (fixes/features).

Fast reset without rebuild (SRAM reload of current astra.bit):
`ssh nuc 'source ~/oss-cad-suite/environment; cd ~/astra68/fpga/soc/oss_flow && openFPGALoader --board ulx3s astra.bit'`
(full mkbit ~5-6 min on the NUC; run it backgrounded — `run_in_background` — and wait for the notify.)

---

## THE BLOCKER — RAM-exec hang = hardware TIMING, not logic

**Symptom:** a RUN case hangs the CPU. Localized (markers + LED bus-state reads) to `harte_exec`
(`sw/harte/harness_exec.S`): the sequence `movem.l regtable,%d0-%d7/%a0-%a6` **then** `jsr codebuf`
(fetch+execute from RAM) hangs — a silent fault-retry loop (plain crt0 `rte` retries the faulting
access forever; there is no bus-error watchdog, so no exception is ever reported).

**Ruled OUT by bisection (all on silicon):** buffer misalignment (4-aligned regtable/resultbuf — no
help), jmp-vs-jsr entry, a-reg values (0 vs nonzero), codebuf content (even a bare `RTS` after the
movem hangs; `jsr` to a RAM `RTS` *without* the preceding movem WORKS). So the trigger is specifically
**MOVEM-read-burst → instruction-fetch-from-RAM**.

**The decisive test:** a Verilator sim of the whole SoC (`fpga/soc/sim/`) runs `harte_exec` to
**completion with the correct result** (`R B A 0` = boot, before/after exec, CCR=0 for 1+2=3). Functional
sim has zero path delays → it passes. **Therefore the logic is correct and this is a hardware TIMING
violation.** It matches the standing warning at `astra_soc.sv:27-32`: the WF68K30L samples DSACK and
latches read data on the **negedge**, giving negedge→posedge paths only half a clock; 10 MHz failed on
HW, 3.125 MHz boots. The movem→fetch sequence stresses one of those negedge paths past margin.

### Correct-fix directions (timing domain — NOT the harness, NO NOP padding)
1. `cd fpga/soc/oss_flow && cat pnr_*.log` — read nextpnr's timing report; find the marginal
   negedge path(s) in the core's bus interface (`wf68k30L_bus_interface.vhd`) / DSACK/data mux.
2. Candidate correct fixes: more bus wait states (`astra_soc.sv` bus FSM `waitc`, give the negedge
   latch more settle time — legit for an async bus); slower clock (`clkdiv`, currently /8=3.125 MHz);
   register/retime the read-data path so it's posedge-stable well before the core's negedge sample;
   or constrain the negedge paths in the flow. Verify on the NUC (cheap now).
3. Can't be caught in functional sim — needs HW verify or post-PnR timing (SDF) sim.

---

## Second bug — UART RX overrun (also correct-fix, smaller)

`uart_rx`/SoC RX has **no FIFO** — a single `rx_data` register. At full baud the NUC delivers bytes
back-to-back and long frames (the 66-byte RUN frame) drop bytes → `getc` blocks. macOS's slow FTDI hid
this; the NUC exposed it. Host currently paces bytes (`probe.py` sends byte-by-byte with 1 ms gaps) as a
stopgap for debugging. **Correct fix: add a small RX FIFO** (a few bytes) in `fpga/soc/uart_rx.sv` /
the SoC RX register block so full-baud frames don't overrun. Needed before the real Harte sweep (pacing
every byte is too slow for thousands of cases).

---

## What is DONE + verified (do not redo)

- **Phases 0+1 verified on silicon:** boot, TX/RX, PING/PONG (`harness.c`). Earlier commits.
- **RUN handler (Phase 2) logic:** `harness.c` `run_case` (frame checksum-validated, drops corrupt
  frames) + `harness_exec.S` `harte_exec` (movem-load regs+CCR, jsr codebuf, capture regs+CCR to
  aligned buffers). **Sim-verified correct.** Blocked only by the HW timing bug.
- **Provenance:** `build_id.h`/CMD_ID, `build_flash.sh`, `whatsloaded.py`, `BUILD_LOG.md`.
- **Sim:** `fpga/soc/sim/{mkcore.sh,tb_soc.sv,sim_harness.c}` + `astra_soc.sv` `RST_MAX` param.
  Verilator: `cd fpga/soc/sim && bash mkcore.sh && verilator --binary -j 0 --top-module tb_soc -Wno-fatal
  -Wno-lint --timing tb_soc.sv ../astra_soc.sv ../uart_tx.sv ../uart_rx.sv wf68k_core.v && ./obj_dir/Vtb_soc`
  (needs `rom_init.hex` = sim_harness ROM in cwd; build it with m68k-gcc, see mkcore/Makefile pattern).

## Debug aids kept in-tree
- `sw/harte/crt0_diag.S` — crt0 with per-vector (2/3/4) emit handlers (NOTE: didn't boot cleanly —
  emit-at-boot is fragile if a spurious boot exception fires before UART is usable; a fault-COUNTING
  handler that `rte`s until N faults then reports would be more robust if you need the vector).
- `sw/harte/host/probe.py` / `probe_seq.py` — single paced RUN probe (overrun-safe).
- LED map `astra_soc.sv`: `{hb[23], ~as_n, ~rw_n, uart_busy, adr[3:0]}` — read at a hang to see
  stuck-cycle (as_n) + address nibble. (A hang-address latch+cycle-out LED mode was used and reverted;
  git has it if needed.)

## Next steps (in order)
1. **Fix the movem→fetch timing bug** (directions above). Verify on NUC. This unblocks Phase 2.
2. **Add the RX FIFO** to `uart_rx`/SoC.
3. **Task 6:** host driver `sw/harte/host/harte_run.py` — parse/filter Harte JSON (register-only,
   non-A7, non-trace, defined-flags), stream ADD/MOVE/ASL cases, compare regs+CCR. Spec §5–6,
   plan Task 6. Harte data on beast `~/astra_soc/harte/` or fetch SingleStepTests/ProcessorTests.

## Pointers
- Spec: `docs/superpowers/specs/2026-07-07-harte-vector-harness-design.md`
- Plan: `docs/superpowers/plans/2026-07-07-harte-vector-harness.md`
- Memory (basic-memory, project claude-memory): the "Astra68 — Harte harness" note (has all 3
  sessions' findings appended).
- Ledger: `.superpowers/sdd/progress.md`.
