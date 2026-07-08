# Astra 68 — Session Handover

Resume point for a fresh session. Authoritative design lives in `SPEC.md`; the
four chip contracts in `docs/*.md`; this file is the "where we are / how to
resume" map.

Astra 68 = a 68020/030-class fantasy computer for the ULX3S ECP5-85F. Chipset-
first (Amiga philosophy): custom chips do the heavy lifting, CPU coordinates.

---

## 1. Current status (one glance)

| Area | State |
|---|---|
| FPGA | ✅ **LFE5U-85F** verified (JTAG IDCODE `0x41113043`) |
| SDRAM | ✅ **32 MB** verified on real HW — memtest clean at 50 & 75 MHz (`fpga/memtest/`) |
| Chipset contracts | ✅ Vega, Astraea, Vesta, Lyra — `docs/*.md` + `sw/include/*.h` (offsets compile-verified) |
| CPU | ✅ **WF68K30L** cost measured — 16.8K LUT (20%), ~11.6 MHz (Synplify), 0 BRAM/DSP |
| Boot software | ✅ linker+crt0+hello builds (`sw/boot/`), vectors verified |
| **SoC skeleton** | 🔧 **IN PROGRESS** — boots+banners in QuestaSim (source AND gate-level functional); real HW silent = negedge timing (see §5) |

Next milestone: get the SoC skeleton to boot and print the banner over UART
("it's alive"). The DESIGN IS FUNCTIONALLY CORRECT — it boots and prints
"ASTRA 68..." in QuestaSim (both source sim and post-PAR *functional* gate sim).
The remaining blocker is **timing**: the WF68K30L's negative-clock-edge bus logic
fails on real silicon. SDF (timing) gate sim reproduces the HW silence; functional
gate sim boots. Full detail in basic-memory note "Astra68 SoC — RESET bug fixed
(HALT+RESET)...". Three root causes were found this session; two are fixed.

---

## 2. Repo layout (`/Users/barry/Git/astra68`)

```
SPEC.md                     authoritative living design (READ FIRST)
docs/{VEGA,ASTRAEA,VESTA,LYRA}.md   chip register maps
sw/include/*.h              verified C SDK (astra.h umbrella) — offsets _Static_assert'd
sw/boot/                    boot ROM: astra.ld, crt0.S, boot.c, Makefile (builds ✅)
fpga/memtest/               SDRAM memtest (open flow, runs on the Mac) ✅
fpga/cpu/                   WF68K30L vendored + Diamond build scripts + wf68k_wrap.vhd
  synth.sh                  open-flow (ghdl) attempt — BLOCKED (documents why)
  diamond_build.tcl         LSE synth (Diamond)
  diamond_build_synplify.tcl Synplify synth (Diamond) — the one to use
  wf68k_wrap.vhd            generic-free VHDL wrapper for mixed-lang SV instantiation
fpga/soc/astra_soc.sv       SoC skeleton top (WF68K30L + ROM + RAM + UART)
```

---

## 3. Environment / infrastructure (CRITICAL — non-obvious)

**Mac** (`darwin`, this repo, ULX3S attached):
- Open FPGA flow: `/opt/homebrew/oss-cad-suite/bin` (yosys, nextpnr-ecp5, openFPGALoader, ghdl). Used for the memtest.
- vasm+vlink present at `~/amiga-cc/vbcc/bin` (Motorola-syntax asm; not currently used).
- No `m68k-elf-gcc`. ULX3S serial: `/dev/cu.usbserial-D01457` (115200). Flash: `openFPGALoader --board ulx3s <bit>` (SRAM, temporary).
- Serial reader: `fpga/memtest/read_serial.py <dev> <seconds>`.

**beast** (`ssh beast` → 192.168.1.3, Ubuntu 24.04, 32-core, 61 GB) — the Diamond + m68k-gcc build host:
- **Lattice Diamond 3.14** at `~/diamond/3.14`. License `~/diamond/3.14/license/license.dat` (free node-locked, HOSTID `bcfce7eb1861` = enp11s0).
- **Diamond quirks that took effort to find (must re-apply each build):**
  - Env: `export bindir=~/diamond/3.14/bin/lin64; source $bindir/diamond_env`
  - **Synplify needs `export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libstdc++.so.6`** (Diamond's bundled libstdc++ lacks GLIBCXX_3.4.30 that system ICU/Qt need).
  - Qt5 runtime libs installed via apt (libqt5webkit5, libqt5*t64, gstreamer-plugins-base) — done, persistent.
- **WF68K30L core**: `~/astra_cpu/wf68k30L` (CERN-OHL). Diamond CPU project: `~/astra_cpu/prj` (LSE) — measured numbers here.
- **SoC build**: `~/astra_soc/` — `src/{astra_soc.sv,wf68k_wrap.vhd,uart_tx.sv,ecp5pll.sv}`, `rom_init.hex`, `prj/build_soc.tcl`.
- **Boot software**: `~/astra_sw/boot/` — builds with `m68k-linux-gnu-gcc` (13.3, apt).
- Note: `src/astra_soc.sv` on beast has an absolute `$readmemh` path (sed'd); the repo copy stays relative. Re-sed after each scp: `sed -i 's|"rom_init.hex"|"/home/barry/astra_soc/rom_init.hex"|' src/astra_soc.sv`.

Device string for Diamond: **`LFE5U-85F-6BG381C`**. Top-level ULX3S pins (from
`fpga/memtest/astra_memtest.lpf`): clk25_mhz=G2, reset_n(btn0)=D6, ftdi_rxd=L4,
leds[7:0]=H3/E1/E2/D1/D2/C1/C2/B2.

---

## 4. Key measured facts & decisions

- **CPU (WF68K30L, Synplify, LFE5U-85F): 16,772 LUT4 (20%), 3192 FF, 0 BRAM/DSP, ~11.6 MHz.** LSE gave 24.9K/9.9 MHz — **use Synplify.** ~12 MHz ceiling = deep opcode-decode→exception path (48 levels, routing-bound), inherent to the core. Workable (chipset-first; run CPU ≤10 MHz, wait-states vs 50–75 MHz SDRAM).
- **ghdl (open flow) crashes on the core** (mixed clocked/comb VHDL). Diamond (Synplify) is the CPU flow. So anything containing the CPU builds on beast, not the Mac open flow.
- CPU has **no MMU/cache/FPU** — matches our design (Vesta region-MMU + soft-float, `gcc -m68020 -msoft-float`).
- Toolchain: gcc-m68k for C, vasm for asm (not vbcc). FPU = soft-float.
- Chipset registers: 32-bit, 4-byte stride, big-endian, supervisor-only MMIO at 0xFFF0_0000 (Vesta) / _1_ (Astraea) / _2_ (Vega) / _3_ (Lyra).
- Budget: CPU 20% + chipset (est ~35–50K) ≈ 66–78% of the 85F. Fits; build chips lean.
- OS plan (SPEC §14): crib **rosco_m68k** for ROM monitor/crt0/boot; build the scheduler (FreeRTOS as template); region-MMU protection is ours; study EmuTOS/GEM for GUI. Linux/*BSD out (need paged MMU).

---

## 5. THE immediate blocker (start here next session)

**The bus-interface handshake in `astra_soc.sv` is wrong.** The CPU boots, fetches
its reset vectors (SSP $0→0x02000000, PC $4→0xFFE00400 both read correctly on HW),
then immediately takes a bus-error exception (reads vector 2 at $8, stacks a frame
near $0) and never runs boot code. Full detail in basic-memory note
"Astra68 SoC bring-up — two bugs fixed, bus-interface handshake is the remaining blocker".

### Two bugs already FIXED this session (both in the repo + on beast)
1. **CPU synth collapse** — Synplify constant-folds the WF68K30L to ~0 LUT if any of
   its 6 bus-control inputs (BERRn/HALT_INn/AVECn/STERMn/BRn/BGACKn) is a compile-time
   constant. Fix: they're now `wf68k_wrap` ports driven from a `(* syn_preserve=1 *)`
   reg in `astra_soc.sv`. (The old HANDOVER "wrong top = 118 LUT" was a MISDIAGNOSIS —
   top was always `astra_soc`; the CPU was being optimized away.)
2. **The .lpf was never applied** — `prj_src add x.lpf` does NOT make it the active
   constraint file; Diamond auto-placed clk25_mhz→B11 (no oscillator→no clock) and
   ftdi_rxd→K20 (not L4→silence). Fix: after `prj_project save`,
   `file copy -force <pins>.lpf [file join [pwd] <project>.lpf]` before PAR. The
   working script is `~/astra_soc/prj/build_soc_full.tcl`. VERIFY in `impl1/*.pad`
   that clk25_mhz shows `G2 ... LOCATED`.

### Verified good on real HW (after those fixes)
PLL@10MHz, clock, UART, 115200 baud, TX pin — a pure-RTL "ASTRA UART OK" hello prints
perfectly. The CPU runs (issues cycles, fetches vectors). So EVERYTHING works except
the bus handshake.

### The bus problem (the WF68K30L contract, from wf68k30L_bus_interface.vhd)
- Core samples DSACK/BERR/HALT on the **NEGATIVE clock edge** ("end of S2"); read data
  latched on the **negedge at T_SLICE=S4**. Async cycle = S0..S5 = 6 half-cycle slices
  (3 clocks). ASn low S0..S4. WAITSTATES=1 only at S3 while DSACK_In="11".
- `BUS_WIDTH <= WORD when DSACKn="01"`, else LONG_32. On HW the core ran WORD-width
  (addresses step by 2) and read data from DATA_PORT_IN[31:16] — my memory returned the
  aligned 32-bit word on all lanes, so odd words read the wrong half → SSP/PC wrong → fault.
- Tried (none booted): posedge FSM (32-bit), combinational DSACK (32-bit), 16-bit port
  (DSACK=01, data on D[31:16]). The last made it worse.

### UPDATE — the above bus-handshake theory was WRONG. Two more root causes found:
- **Reset protocol (FIXED):** the WF68K30L needs RESET_INn AND HALT_INn asserted
  together during reset. `astra_soc.sv` now drives `HALT_INn = ~rst`. With this the
  design BOOTS + prints the banner in QuestaSim (source AND functional gate-level sim).
- **Negedge timing (OPEN, the real blocker):** the core samples DSACK / latches read
  data on the NEGATIVE clock edge → cross-edge paths get half a period. The design is
  functionally correct (gate sim boots) but the SDF *timing* gate sim reproduces the HW
  silence. Real HW silent at 10 MHz / 3.125 MHz / 781 kHz. Next: constrain the derived
  clock properly (it's currently unconstrained — phantom 200 MHz in every report) so the
  tool does setup+HOLD fixing on the negedge paths; re-check with the SDF gate sim.
- QuestaSim flow is set up on beast (~/astra_soc/sim source, ~/astra_soc/gate gate-level);
  full recipe + all details in the basic-memory note. Use it — do NOT debug blind on HW.

### Debug harness (reusable) on beast: `~/astra_soc/dbg/`
`astra_soc_dbg2.sv` = SoC with the CPU+bus, UART owned by an RTL reporter that streams
the first 8 committed bus cycles as `<addr8>:<data8>:<R|W>\r\n`. This is how the boot
sequence was observed. `build2.tcl` builds it (has the lpf fix). dbg3=combinational-32bit,
dbg4=16-bit-port variants.

### Flash + observe loop (macOS)
openFPGALoader grabs the whole FTDI device → can't read serial DURING a flash. Flash
first, THEN `python3 fpga/memtest/read_serial.py /dev/cu.usbserial-D01457 <sec>`. The
design must LOOP its output to be seen post-flash (boot.c now loops the banner).
Sanity control: flash `fpga/memtest/build/astra_memtest.bit` → "RESULT: PASS 32MB".

---

## 6. Next steps (priority order)

1. **Fix the bus-interface handshake** (§5) — the one thing between here and "alive".
   Do it in **simulation** (Diamond/Active-HDL), not build→flash→observe. Testbench =
   `wf68k_wrap` + ROM model; match the negedge-DSACK / S0-S5 / dynamic-sizing contract;
   then port the corrected slave into `astra_soc.sv`. Re-flash → expect the banner.
2. Once alive: clean up (fold the syn_preserve control reg + lpf-copy into the canonical
   `fpga/soc/` build; the working Diamond flow currently lives in `~/astra_soc/` on beast).
3. Add **SDRAM** to the SoC (reuse `fpga/memtest` `sdram.v`) so the full 32 MB + real stack work; add the reset overlay (ROM@$0 then reclaim low RAM).
4. Extend boot → **ROM monitor** (examine/modify/load-over-UART/run).
5. Begin implementing the **chips** (Vega/Astraea/Lyra/Vesta) against their contracts.

**Repo changes made this session:** `fpga/cpu/wf68k_wrap.vhd` (6 control ports),
`fpga/soc/astra_soc.sv` (syn_preserve control reg + wiring), `fpga/soc/astra_soc.lpf`
(new, real pins), `sw/boot/boot.c` (loops the banner), `sw/boot/bin2hex.py` (new,
astra_boot.bin→rom_init.hex). Working Diamond build script on beast:
`~/astra_soc/prj/build_soc_full.tcl` (synth→map→par→bitgen WITH the lpf-copy fix).

---

## 7. Open design questions (deferred, in the docs)

- Vega: add a 2nd indexed playfield vs tiles-only; sprite HW scaling; full programmable timing vs mode enum (`docs/VEGA.md §14`).
- Astraea: blitter LINE mode; copper target-range guard (`docs/ASTRAEA.md §10`).
- Vesta: region count 8 vs 16; double-bank region table for fast switch; SD block-DMA (`docs/VESTA.md §10`).
- Lyra: 64 KB vs 32 KB wave RAM; ADSR packing; PCM interpolation (`docs/LYRA.md §11`).
- Machine-wide: 100 MHz SDRAM (floorplan Astraea + phase-tune); audio out (sigma-delta jack vs HDMI).

---

## 8. How to reproduce the proven pieces

- **Memtest (Mac):** `cd fpga/memtest && ./build.sh flash && python3 read_serial.py /dev/cu.usbserial-D01457 40` → "RESULT: PASS 32MB".
- **CPU cost (beast):** `~/astra_cpu/prj` — `diamondc diamond_build_synplify.tcl` (with env+LD_PRELOAD) → `impl1/*.mrp`.
- **Boot ROM (beast):** `cd ~/astra_sw/boot && make && make verify` → vectors `02000000 ffe00400`.
- **C SDK check (Mac):** the `sw/include/*.h` offsets are `_Static_assert`'d — `cc -I sw/include -c <any test>` compiles clean.
