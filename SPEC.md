# Astra 68 — Design Specification (v0.2, living)

A 68020/030-class fantasy computer for FPGA, targeting the **ULX3S ECP5-85F**.
Clean, modern 68k machine for games, demos, creative tools, and a small
protected-mode graphical OS. Not binary-compatible with any existing platform —
its own thing, in the spirit of Amiga / Atari ST / X68000 but without the legacy
baggage.

> **Status legend:** ✅ VERIFIED on hardware · 🔒 LOCKED decision · 🔧 open/iterating

---

## 0. Hardware-verified facts (this board)

| Item | Result | Method |
|---|---|---|
| ✅ FPGA | **LFE5U-85F** (84,480 LUT4, 3744 Kbit EBR, 156 DSP, 4 PLL) | JTAG IDCODE `0x41113043` |
| ✅ SDRAM | **32 MB**, MT48LC16M16, 16-bit — full-chip march clean, **zero errors**, no aliasing | on-board memtest (`fpga/memtest/`) |
| ✅ SDRAM speed | clean at **50 MHz** (rock-solid) and **75 MHz** (~150 MB/s); command path caps ~80 MHz un-floorplanned | memtest sweep |
| ✅ Toolchain | yosys + **ghdl (VHDL)** + nextpnr-ecp5 + trellis + openFPGALoader — full open flow | probed |
| ✅ HDMI 720×480 | proven on the predecessor e6502 machine (same board) | prior project |

Predecessor project **e6502** (`~/Git/e6502/e6502.FPGA`) is Astra's direct
ancestor: 720×480 HDMI, chunky framebuffer, copper, blitter, sprites, SDRAM
controller — all already running on this board, just 6502 instead of 68k. We
reuse its proven RTL (`sdram.v`, `ecp5pll.sv`, `uart_tx.sv`, HDMI serializer,
constraints) as the foundation.

---

## 1. The Astra chipset

Amiga-style named custom chips. Each is a memory-mapped block with base +
version + status + control registers.

| Chip | Role | Amiga analog |
|---|---|---|
| **Astraea** | DMA + blitter + copper + memory arbiter (the brain) | Agnus |
| **Vega** | video: framebuffer scanout, sprites, palette, backdrop, HDMI | Denise |
| **Lyra** | audio: 16 PCM + 16 wavetable + stereo mixer | Paula |
| **Vesta** | system glue: region-MMU, interrupt controller, timers, UART, SD, input | Gary/Gayle |

Star/goddess theme (Astra = star). **Lyra** = the lyre (music) and the
constellation containing **Vega** — display + audio pair. (Named Lyra not "Nova"
because the e6502 already uses "Nova" internally.)

---

## 2. CPU 🔒

- **Core:** WF68K30L (Wolfgang Förster) — a **complete** 68020/030 integer ISA
  (bitfield ops, 32-bit MUL/DIV, all addressing modes), big-endian, 32-bit.
  GPL VHDL → ingested via the ghdl→yosys path (verified available).
- **No on-chip PMMU / no FPU** in the core — both are intentional:
  - Protection is our own **Vesta region-MMU** (§6), bolted on the bus, driving
    the core's bus-error/fault lines.
  - Floating point is **soft-float** (§12).
- **Compile target:** `m68k-elf`, `-m68020 -msoft-float`, big-endian. The
  complete core runs stock gcc `-m68020` output (the old TG68-subset worry is
  gone).
- Fmax on ECP5 ~40–70 MHz — fine; the machine is SDRAM/arbiter-bound and the CPU
  may take wait states.

Not required v0.1: 68851 PMMU compat, FPU hardware, 040/060 instructions,
cache/cycle accuracy.

---

## 3. Memory map 🔒 (32 MB)

The bus decodes three physical spaces. RAM is the 32 MB SDRAM; ROM and I/O are
separate FPGA decodes (BRAM / register blocks), **not** carved out of SDRAM.

```
Physical decode
  SDRAM   32 MB   general RAM (see software pools below)
  ROM     128–256 KB BRAM   boot ROM / monitor
  MMIO    register blocks    Astraea / Vega / Lyra / Vesta (supervisor-only)
```

**Software pool convention for the 32 MB** (kernel-managed, adjustable). The
jump from 16→32 MB is spent on *resident capacity*, since bandwidth (§17) is
unchanged by capacity:

```
0x0000000  ~16 MB   Kernel + general RAM + user processes (region-MMU mapped)
0x1000000  ~7  MB   Framebuffer / VRAM pool — multiple 720×480×16 buffers +
                    GUI compositor off-screen window buffers
0x1700000  ~5  MB   Audio sample pool (Lyra PCM banks, resident instruments)
0x1C00000  ~3  MB   OS cache / RAM disk / SD block cache
0x1F00000  ~1  MB   Copper lists (mirror), sprite pattern banks, DMA buffers
```

Reset boot overlay: on reset the 68k fetches SP/PC from `$0`/`$4`, so **ROM is
aliased to `$0` at reset** and the overlay is disabled after init (Amiga-OVL
style), remapping RAM low. 🔧 (must be in the boot sequence — §15)

**Why 32 MB matters:** enables a *compositing windowed GUI with caching* — the
qualitative win. Off-screen per-window buffers, resident audio sample libraries,
SD/font/icon caches, more concurrent protected processes. Capacity, not
bandwidth: use it for resident assets and more processes, not more simultaneous
DMA streams.

---

## 4. Vesta — region MMU / protection 🔒

Not a Motorola paged MMU. A simple **region-based protection unit** — enough to
isolate processes without page tables.

- Per process: **8–16 regions**. Each region: `logical_base`, `logical_limit`,
  `physical_base`, `permissions` (R/W/X/U/S), `flags`.
- Every CPU access: logical addr → region match → permission check → physical
  translate → bus cycle, or **fault** (bus error / protection fault) if no match
  or permission fails.
- Process switch: kernel saves CPU state, loads the target's region table.
- Consequence (accepted for v0.1): physically-contiguous regions, no demand
  paging / COW, external fragmentation as processes churn. Paging is a stretch
  goal.

Suggested per-process logical layout:
```
00000000-0000FFFF  null guard
00010000-003FFFFF  text/data
00400000-007FFFFF  heap
70000000-700FFFFF  shared / IPC / mapped resources
7FFF0000-7FFFFFFF  user stack
80000000-FFFFFFFF  kernel / supervisor / device (S-only)
```

---

## 5. Vega — video 🔒

- **Output:** HDMI (GPDI / DVI), **720×480 @ 60** (CEA 480p, 27 MHz pixel /
  270 MHz TMDS). Proven on this board. Note: non-square pixels (PAR 8:9) —
  correct aspect for circles.
- **Framebuffer:** linear chunky **RGB565**, programmable base + pitch, double/
  triple buffering via base swap. One 720×480×16 buffer = 675 KB.
- **Backdrop color register** 🔒 — a per-scanline-writable background color
  (behind sprites/framebuffer). This is how copper "gradients" work on a
  chunky (non-palettized) framebuffer — the copper writes the backdrop reg per
  line. (Palette tricks can't repaint direct-color framebuffer pixels.)
- **Palette:** 256 entries, RGB888/666 internal → RGB565 out; used by sprites,
  backdrop, and optional indexed layers. Copper-writable mid-frame.
- **2 scrolling tilemap layers** 🔒 — 8×8/16×16 tiles, 4bpp (shared palette
  banks), 16-bit map entries, pixel-granular hardware scroll, pow2 wrap. Each
  layer sits above or below the sprite/FB group (`ABOVE` bit) — foreground +
  parallax background for platformers. Console-style games use tiles+sprites and
  skip the framebuffer (bandwidth).
- Additional modes (via scanout timing / scaling): 320×240, 640×480, etc.
- Scanout via line FIFO + SDRAM burst reads; deterministic priority (§15).
- Full register map: `docs/VEGA.md` + `sw/include/vega.h` (offsets verified).

---

## 6. Copper (in Astraea, drives Vega) 🔒

Lightweight raster coprocessor, Amiga-style.
- Ops: `WAIT y,x` · `MOVE reg,val` · `SKIP` · `END` · `IRQ`.
- **Copper list lives in BRAM** 🔒 (small, deterministic, no SDRAM arbitration
  race).
- **Full access to all Vega/Astraea registers** 🔒 — palette, backdrop, scroll,
  framebuffer base (mid-frame = split screen / page flip), sprite regs, mode,
  blitter triggers, IRQ. 🔧 define which registers are safe to write mid-scanline
  vs racy.

---

## 7. Astraea — blitter 🔒

2D DMA bus master. V0.1 ops: copy rect, fill rect, masked copy, color-key
(transparent) copy, optional line draw. Registers: SRC/DST addr+pitch, W, H, op,
color, mask_color, control, status, IRQ. Yields to video/audio/sprite DMA; runs
opportunistically in blanking/idle. Completion IRQ. Deferred: scaling, rotation,
alpha, arbitrary ROPs.

---

## 8. Vega — sprites 🔒

- **32 hardware sprites.**
- **4bpp indexed** 🔒 (16 colors) + per-sprite **palette bank**.
- **Per-sprite transparent-index register** 🔒 (names which of the 16 indices is
  see-through — more flexible than a fixed index 0).
- **Streamed from SDRAM** 🔒 (patterns in RAM, not BRAM) → **line-buffer
  compositor** (fetch per scanline into a line buffer, merge with scanout — never
  per-pixel SDRAM lookups).
- **Per-scanline sprite pixel budget** 🔒 — hard cap on sprite pixels fetched per
  line (like Amiga/NES). At 4bpp, 32×64px/line ≈ 1 KB/line, which fits the
  bandwidth budget; excess sprites drop per-line. Bounds worst-case bandwidth.
- Per sprite: X/Y (signed), W, H, base, pitch, palette bank, priority, flags
  (enable, flip X/Y, visible). Reserve **1 sprite as the hardware mouse cursor**
  for the GUI.
- Collision detection: optional, later.

---

## 9. Lyra — audio 🔒

Two engines into a common stereo mixer, **48 kHz** stereo out.
- **16 PCM voices:** sample playback from the SDRAM audio pool. Per voice: start/
  cur/loop-start/loop-end/length addr, step (rate), vol L/R, format, ctrl,
  status. 8-bit unsigned / 16-bit signed, looping + one-shot, panning, completion
  IRQ, FIFO/DMA fetch. Optional: interpolation, ping-pong, reverse.
- **16 wavetable voices:** phase-accumulator oscillators, wave RAM in BRAM
  (~64 KB target). Per voice: waveform base/len, phase, phase step, vol L/R,
  ctrl, optional envelope.
- **Mixer:** time-multiplexed (not 32 parallel) — 50 MHz / 48 kHz ≈ 1000 cyc per
  sample for 32 voices serially. Voice → vol multiply → stereo accumulate →
  saturate → FIFO → output.
- **Output path:** sigma-delta on the 3.5 mm jack for v0.1 (easy) 🔧; HDMI audio
  later.

---

## 10. FPU — soft-float 🔒

- **Soft-float only.** `-msoft-float` → libgcc IEEE 32/64-bit in software. Zero
  hardware. Most game/demo math is fixed-point anyway.
- **Future (stretch):** a memory-mapped IEEE-754 coprocessor (add/sub/mul/div/
  sqrt, ~2–5K LUT + a few DSP) — *not* 68881-compatible, its own MMIO ABI. If
  built, it becomes a named coprocessor. The e6502's `rtl/math_copro.sv` is a
  working MMIO-math-coprocessor template to reuse. **Not** the real 68881 F-line
  coprocessor (80-bit, transcendentals, FSAVE/FRESTORE) — months of work, no open
  core, not worth it.

---

## 11. Vesta — interrupts, timers, I/O 🔒

- **Interrupt controller** muxing sources onto the 68k's 7 IPL levels (vectored):
  vblank, raster compare, copper, blitter done, audio, timer0/1, keyboard/input,
  UART, SD, protection-fault/bus-error.
- **Timers:** periodic + one-shot, microsecond/scanline resolution.
- **Input:** UART console (mandatory v0.1); keyboard + gamepad board-specific
  (PS/2, USB-via-ESP32, GPIO, or the e6502's USB-HID host path).
- **Storage:** SD card — raw block read, boot-monitor load/run; FAT + filesystem
  later. SD read cache in the OS-cache pool.
- **UART:** 115200 8N1 on the FTDI serial (proven by the memtest).

---

## 12. Boot 🔧

1. Reset → CPU fetches SP/PC from `$0`/`$4` — **ROM boot overlay** maps ROM to
   `$0` initially.
2. Boot ROM: SDRAM init, exception vectors, device reset, UART, disable overlay
   (RAM low), video init, optional logo.
3. **ROM monitor:** examine/modify memory, registers, load binary/S-record over
   UART, load from SD, run, reset, diagnostics. First real environment.

ROM: 128–256 KB BRAM.

---

## 13. Toolchain 🔒

**gcc, not vbcc, for C** — but use **vasm** for assembly:

| Tool | Use | Why |
|---|---|---|
| **m68k-elf-gcc** | primary C compiler | best optimizer, newlib, C++, GDB, OS-grade; the complete 020 core runs its `-m68020` output |
| **vasm** (Motorola syntax) | assembler | the superior 68k assembler — macros, Motorola syntax, from the vbcc/vasm suite |
| **vlink** | linker | flexible output formats, pairs with vasm |
| vbcc | optional/secondary | fine for quick Amiga-style bare-metal, but gcc's optimizer + ecosystem win for an OS |

So: **gcc for C, vasm/vlink for hand asm.** Not either/or. Flags: `-m68020
-msoft-float`, big-endian ELF. Define an Astra ABI (crt0, linker script, syscall
via `trap`, hardware headers, newlib subset).

---

## 14. OS direction 🔒 (kernel start-from decided)

Phased: ROM monitor → single-task → cooperative → preemptive → **protected user
processes (region-MMU)** → filesystem → GUI/windowing (compositing desktop, which
32 MB now makes comfortable).

**Filter:** our region-MMU is custom → full paged-MMU Unix (Linux / NetBSD /
OpenBSD m68k) is **OUT** — all mandate a real 68851 PMMU for 020/030. uClinux
(no protection at all) and AROS (flat trusted memory baked into its ABI) **fight**
our protection model. So **the region-MMU protection layer is ours to build no
matter what** — nothing off-the-shelf targets it.

**Start-from strategy:**

- **Foundation → crib `rosco_m68k`** (MIT, active — firmware 2.42 Jan 2025, has a
  **68030 edition**, uses our exact `m68k-elf-gcc` + vasm/vlink toolchain): take
  its ROM monitor, `crt0.S`, linker scripts, SD/IDE boot, serial — swap the MMIO
  drivers for Astraea/Vega/Lyra/Vesta. (`tomstorey/m68k_bare_metal` = second
  minimal crt0/linker reference.)
- **Scheduler → build it** (small for a single 020: one timer IRQ + one
  context-switch routine + ready queue), using **FreeRTOS** primitives (MIT) as
  the template. This is the exact layer that reloads region-MMU descriptors on
  every context switch, so we own it. FreeRTOS has no official 020 port
  (ColdFire only) — but that's just one context-switch `.S` to write.
- **Worked reference → study `Computie`/`Gloworm`** (transistorfet, 68010/020/030,
  active): a readable Unix-like build of the exact cooperative→preemptive→syscall→
  MINIX-VFS→FS→TCP arc we target. GPL-3 → **read, don't paste.**
- **GUI → study EmuTOS's GEM** (VDI + AES + desktop; GPL-2, active — 1.4 Jun 2025,
  builds with our toolchain) for windowing. Skim AROS Intuition for concepts.
- **Optional → RTEMS** (permissive GPL-2+exception, explicit 020, explicitly
  flat/MMU-free) *only* if we later want POSIX threads + filesystem for free and
  accept writing a custom BSP — it's heavy for a fantasy console.

**Protection bolt-on:** rosco / FreeRTOS / RTEMS / Computie all run **flat +
supervisor**, so a region unit adds cleanly — a "load N regions from the process
control block" step in the context switch + bus-error / privilege trap handlers.

---

## 15. Memory bandwidth — the binding constraint 🔧

One 16-bit SDRAM: ~130–180 MB/s realistic sustained (150 MB/s proven at 75 MHz).
Everything shares it. Budget (approx, deterministic masters first):

```
priority:  video scanout > audio FIFO > sprite line fetch > CPU > blitter > storage
video 720×480×16@60   ~54 MB/s during active lines
sprites 32×64@4bpp    ~1 KB/line, per-line-budget capped
audio 32 voices       ~1-2 MB/s (trivial)
CPU + blitter         leftover (may stall)
```
Video and audio must never underrun (line/FIFO buffers + guaranteed slots). CPU
and blitter may stall. 100 MHz SDRAM (→200 MB/s) needs the floorplanned Astraea
controller + `sdram_clk` phase tuning (memtest proved the chip fine; it's an FPGA
timing-closure task).

---

## 16. FPGA resource budget (85F: 84,480 LUT4, 3744 Kbit EBR, 156 DSP)

| Block | ~LUT4 |
|---|---|
| WF68K30L 030 core | **16,772 (20%)** — measured (Synplify), 0 BRAM/DSP, ~12 MHz |
| Astraea (DMA/blit/copper/arbiter) | ~5K (est) |
| Vega (video/sprites/tilemaps/palette/HDMI) | ~6K (est) |
| Lyra (audio 32-voice) | ~3K (est) + few DSP |
| Vesta (MMU/IRQ/timers/UART/SD) | ~4K (est) |
| SDRAM ctrl + TMDS + glue | ~3K (est) |
| **Total** | CPU measured, chips estimated → realistically **~55–65K (~66–78%)** |

EBR: wave RAM 64 KB (14%) + line buffers + copper BRAM + FIFOs < 40%. Sprites
stream from SDRAM and the core has no cache → both save EBR. "Use every bit" —
there's headroom for bigger caches, more wave RAM, tilemap layers, etc.

---

## 17. MVP + open items

**Hardware MVP:** ✅ 85F + 32 MB SDRAM verified · WF68K30L core bring-up · boot
ROM · UART · timer · IRQ controller · basic RGB565 framebuffer · MMU
identity/off.

**Software MVP:** ✅ linker script + crt0 + hello-over-UART (`sw/boot/`, builds
with gcc-m68k-linux-gnu — vectors/SP/PC verified) · UART monitor · SDRAM init ·
framebuffer test — TODO.

**✅ Done — full chipset register layer:** Vega, Astraea, Vesta, Lyra — every
register map (`docs/*.md`) + C header (`sw/include/*.h`) + `astra.h` umbrella,
all offsets and macros compile-verified. Hardware: 85F + 32 MB SDRAM verified.

**Open (🔧) — RTL + boot phase:**
1. Reset boot overlay (ROM@$0) — into boot sequence.
2. Copper mid-scanline-safe vs racy set — drafted `docs/VEGA.md §10`; finalize.
3. ✅ WF68K30L cost **measured** (Diamond hybrid, `fpga/cpu/`). **Synplify: 16,772
   LUT4 (20%), 3192 FF, 0 BRAM/DSP, ~11.6 MHz.** LSE: 24,898 (30%), ~9.9 MHz →
   use Synplify. ~12 MHz ceiling = deep opcode-decode→exception path (48 levels,
   routing-bound), inherent to this area-optimized core. Fits (20%); ~12 MHz
   workable (chipset-first; CPU takes wait states vs 50–75 MHz SDRAM ≈ Amiga-500
   CPU class w/ full 030 ISA). Open ghdl flow crashes on the core; Diamond is it.
   Faster needs core surgery (pipeline that path) or a faster FPGA — later call.
4. 100 MHz SDRAM: floorplan Astraea controller + phase-tune.
5. Audio out: sigma-delta jack (v0.1) vs HDMI audio.
6. ✅ crt0 + linker script + boot hello (`sw/boot/`, gcc-m68k-linux-gnu on beast).
   Next: ROM monitor (examine/modify/load-over-UART/run), SDRAM init, overlay.
7. **SoC skeleton — IN PROGRESS** (`fpga/soc/astra_soc.sv` + `fpga/cpu/wf68k_wrap.vhd`):
   WF68K30L + boot ROM + BRAM stack + Vesta UART, single 10 MHz clock, 68030
   async bus FSM (AS/DS/DSACK), boots `astra_boot.bin` → banner over UART. RTL
   written + parses clean in Diamond; `rom_init.hex` generated. **Blocker:**
   Synplify auto-selects top=`wf68k_wrap` not `astra_soc` (trims to 118 LUT) —
   need to force the Synplify top (`prj_impl option top` isn't reaching it; try
   `prj_strgy`/`-top` or a dummy top constraint). Then: full fit → PAR + ULX3S
   `.lpf` → flash → observe banner. Bus FSM still needs HW/sim validation.
   Build reproducible on beast (`~/astra_soc/prj/build_soc.tcl`).
8. Implement each chip (Astraea/Vega/Lyra/Vesta) against its register contract.

---

## 18. Non-goals

Not an Amiga / Atari ST / Mac / NeXT clone, not Linux-first, not cycle-accurate,
not a 68030 PMMU, not a GPU-heavy 3D machine. Astra 68 is its own platform: a
clean, understandable, powerful 68k machine with enough custom hardware to be fun.
