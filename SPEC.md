# Astra 68 — Design Specification (v0.2, living)

A 68030-class fantasy computer for FPGA, targeting the **ULX3S ECP5-85F**.
Clean, modern 68k machine for games, demos, creative tools, and a small
protected-mode graphical OS. Not binary-compatible with any existing platform —
its own thing, in the spirit of Amiga / Atari ST / X68000 but without the legacy
baggage.

> **Status legend:** ✅ VERIFIED on hardware · 🔒 LOCKED decision · 🔧 open/iterating

> **CPU/MMU decision (2026-07-14):** the architecture and RTL tree are locked
> to the TG68K.C 68030-class core with its built-in paged PMMU and caches. The
> older WF68K30L, 68020/no-PMMU TG68K, and first TG030/PMMU imports have been
> removed. CPU acceptance is governed by
> `docs/MC68030_COMPLIANCE.md`; the OS vision is `docs/OS_VISION.md`.

---

## 0. Hardware-verified facts (this board)

| Item | Result | Method |
|---|---|---|
| ✅ FPGA | **LFE5U-85F** (84,480 LUT4, 3744 Kbit EBR, 156 DSP, 4 PLL) | JTAG IDCODE `0x41113043` |
| ✅ SDRAM | **32 MB**, MT48LC16M16, 16-bit — full-chip march clean, **zero errors**, no aliasing | on-board memtest (`fpga/memtest/`) |
| ✅ SDRAM speed | **75 MHz**, 145.06 MB/s write / 143.52 MB/s read in pin-level regression; 32 MiB four-sweep hardware POST completes with the full SoC in ~1.1 s | controller regression + build `0x7FB5A559` |
| ✅ Toolchain | yosys + **ghdl (VHDL)** + nextpnr-ecp5 + trellis + openFPGALoader — full open flow | probed |
| ✅ HDMI 720×480 | POST console proven with TG030+PMMU+SDRAM on this board | build `0x7FB5A559` |

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
| **Vesta** | system glue: identity, interrupt controller, timers, UART, SD, input | Gary/Gayle |

Star/goddess theme (Astra = star). **Lyra** = the lyre (music) and the
constellation containing **Vega** — display + audio pair. (Named Lyra not "Nova"
because the e6502 already uses "Nova" internally.)

---

## 2. CPU 🔒

- **Architecture:** Motorola-compatible MC68030 integer core with built-in
  paged PMMU and instruction/data caches, big-endian with 32-bit logical and
  physical addressing.
- **Sole implementation:** the repaired TG030+PMMU line under
  `fpga/cpu/tg68k_c_030_mmu2/`. Production acceptance still requires the
  fail-closed policy in `docs/MC68030_COMPLIANCE.md` to pass.
- **No FPU:** floating point remains soft-float (§10).
- Platform address maps, cacheability policy, and compatibility behavior live
  in SoC glue, not in the CPU architectural contract.
- Retired CPU implementations and the external Vesta region-MMU are not build
  options and are not present in the active RTL tree.

Not required v0.1: FPU hardware, 040/060 instructions, or cycle-exact external
bus timing. Architecturally correct exception restart, PMMU behavior, cache
control, memory ordering, and bus-visible semantics remain required.

---

## 3. Memory map 🔒 (32 MB)

The authoritative implemented CPU address registry is
[`docs/MEMORY_MAP.md`](docs/MEMORY_MAP.md). Device specifications define
registers only inside ranges allocated there.

The bus currently decodes SDRAM, bootstrap BRAM, a ROM aperture, and MMIO. The
ROM aperture aliases reserved SDRAM backing storage in the production
AstraHost build; stage 0 remains in immutable FPGA BRAM.

```
Physical decode
  SDRAM   32 MB    general RAM plus reserved ROM backing
  BRAM    32 KB    bootstrap stack and scratch
  stage0  8 KB     immutable reset loader
  ROM     256 KB   aliased system-ROM aperture
  MMIO    blocks   Astraea / Vega / Lyra / Vesta
```

**Software pool convention for the 32 MB** (kernel-managed, adjustable). The
values below are SDRAM-relative offsets, not current CPU bus addresses.
The implemented CPU aperture begins at `0x02000000`; adding a low SDRAM mapping
is a separate memory-map revision. The jump from 16→32 MB is spent on
*resident capacity*, since bandwidth (§17) is unchanged by capacity:

```
0x0000000  ~16 MB   Kernel + general RAM + protected user processes
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

## 4. MC68030 PMMU / protection 🔒

Process isolation uses the CPU's built-in paged PMMU. The kernel maintains a
per-process user address space plus permanently mapped supervisor kernel state,
changes the user translation root during a process switch, and performs the
required ATC/cache maintenance.

Initial OS use is intentionally conservative:

- unmapped null region and guard pages;
- supervisor-only kernel, ROM, and MMIO mappings;
- explicit read-only code and shared-memory mappings;
- wired exception vectors, page tables, interrupt stack, and active kernel
  stacks;
- no memory overcommit, demand paging, or swap in the first system;
- an invalid user access terminates or debugs that process without harming the
  kernel.

Page size, table geometry, virtual layout, and any external execute-protection
extension remain open in `docs/OS_VISION.md`. The CPU PMMU does not constrain
chipset DMA, so DMA fencing and cache ownership are separate platform
requirements.

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

2D DMA bus master and shared drawing backend. V0.1 ops: copy rect, fill rect,
masked copy, and color-key (transparent) copy. The required next frontend adds
Bresenham lines, rectangle outline/fill, midpoint circle/ellipse outline/fill,
repeating pattern fill, and batched hardware glyph expansion. Geometry emits
points or spans into the existing clipped, word-coalescing DMA writer; it is not
a second memory engine. Flood fill is software-assisted and deferred because an
arbitrary region requires an unbounded work list and hostile SDRAM access.

Native AFNT bitmap strikes support hardware `MASK1`, anti-aliased `A4`, and
indexed color glyphs. Unicode mapping, shaping, fallback, and layout remain in
the font service; Astraea receives positioned glyph runs and performs the pixel
work. See `docs/ASTRAEA.md` and `docs/FONTS.md`. Deferred: scaling, rotation,
general alpha compositing, arbitrary ROPs, and hardware flood fill.

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
2. Boot ROM starts UART and the SDRAM-independent **720x480 HDMI POST console**,
   then reads Vesta's machine identity and instantiated-personality table.
3. POST verifies SDRAM data lines, byte/word/long access paths, address lines,
   and every declared RAM location with a position-dependent full-range pattern.
   The stack and POST state remain in BRAM so all 32 MB can be tested destructively.
4. Load versioned, CRC-protected persistent configuration; invalid or absent
   NVRAM selects documented defaults without preventing diagnostics.
5. Disable the reset overlay, establish the low RAM map, initialize devices,
   verify/load the selected OS image, and transfer control.
6. **ROM monitor:** examine/modify memory, registers, load binary/S-record over
   UART, load from SD, run, reset, diagnostics. First real environment.

ROM: 128–256 KB BRAM.

The 90x30 POST text plane uses the fixed 8x16 **Astra Rescue Mono** bitmap face
in BRAM. It remains available when SDRAM, SD, or the font service has failed.
The normal ROM image also carries proportional **Astra Sans** and monospaced
**Astra Mono** AFNT strikes, rendered into Vega surfaces by Astraea's glyph
frontend. They do not depend on the writable filesystem. See `docs/FONTS.md`.

---

## 13. Toolchain 🔒

**gcc, not vbcc, for C** — but use **vasm** for assembly:

| Tool | Use | Why |
|---|---|---|
| **m68k-elf-gcc** | primary C compiler | optimizer, newlib, C++, GDB, and an established big-endian m68k ELF toolchain |
| **vasm** (Motorola syntax) | assembler | the superior 68k assembler — macros, Motorola syntax, from the vbcc/vasm suite |
| **vlink** | linker | flexible output formats, pairs with vasm |
| vbcc | optional/secondary | fine for quick Amiga-style bare-metal, but gcc's optimizer + ecosystem win for an OS |

So: **gcc for C, vasm/vlink for hand asm.** Not either/or. The native ABI is
big-endian m68k ELF with soft-float; the final MC68030 user-code flags and ABI
baseline must be locked with the toolchain. Define crt0, linker scripts, the
native `trap` syscall convention, hardware/service headers, and a libc subset.

---

## 14. OS direction 🔒

The authoritative product and architecture direction is
`docs/OS_VISION.md`.

Locked principles:

- one local owner without a Unix multi-user administration model;
- network connectivity as a first-class capability;
- preemptive scheduling from the first native multitasking milestone;
- protected processes using the built-in MC68030 PMMU;
- application and service failure containment;
- BeOS-like responsiveness and developer coherence without cloning BeOS;
- chipset-native graphics, media, and DMA resources behind safe interfaces;
- no cooperative or flat-address-space compatibility foundation.

The immediate software milestone is a firmware-to-kernel handoff followed by a
protected vertical slice: enable permanent PMMU mappings, enter user mode,
perform a native trap syscall, preempt between user tasks, contain a deliberate
user fault, and complete a cache-safe fenced DMA operation.

---

## 15. Memory bandwidth — the binding constraint 🔧

One 16-bit SDRAM: ~130–180 MB/s realistic sustained (143-145 MB/s measured at
75 MHz with refresh active).
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

The old WF68K30L component estimate is retired. Resource planning now starts
from integrated TG030+PMMU builds and is remeasured as each chipset block lands.

The retained 75 MHz SDRAM baseline in `docs/SDRAM.md` contains the TG030 with
PMMU and caches, HDMI POST console, SDRAM subsystem, and Astraea paths. It uses
36,935 packed LUTs, 9,147 FFs, 136 DP16KD blocks, and 13 multipliers. This is a
measured integration baseline, not an estimate of the completed Vega/Lyra/
storage/network system.

Every major integration must report LUTs, FFs, EBRs, DSPs, CPU Fmax, SDRAM
Fmax, and retained hardware-test identity. Optional features are admitted only
after the protected OS, deterministic display/audio paths, and recovery
facilities fit with timing margin.

---

## 17. MVP + open items

**Hardware MVP:** ✅ 85F + 32 MB SDRAM verified · TG68K.C 68030+PMMU at 12.5
MHz · boot ROM POST · UART · 720x480 HDMI text console. Basic RGB565
framebuffer, timer, and IRQ controller remain open.

**Software MVP:** ✅ linker script + crt0 + hello-over-UART (`sw/boot/`, builds
with gcc-m68k-linux-gnu — vectors/SP/PC verified) · UART monitor · SDRAM init ·
framebuffer test — TODO.

**✅ Done — full chipset register layer:** Vega, Astraea, Vesta, Lyra — every
register map (`docs/*.md`) + C header (`sw/include/*.h`) + `astra.h` umbrella,
all offsets and macros compile-verified. Hardware: 85F + 32 MB SDRAM verified.

**Open (🔧) — hardware and firmware foundation:**
1. Promote one TG030+PMMU RTL revision only after the complete acceptance policy
   in `docs/MC68030_COMPLIANCE.md` passes in simulation and retained hardware.
2. Complete the reset overlay and recovery monitor. Versioned `BootInfo`, the
   separately linked kernel image, verified loader, and kernel handoff are
   implemented at ABI 0.1.
3. Define and implement DMA fencing/fault reporting before protected services
   can influence chipset DMA.
4. Finish the interrupt/timer, framebuffer, input, storage, and audio hardware
   needed by the first protected OS vertical slice.
5. Finalize copper mid-scanline safety and every chipset capability/version
   contract exposed to software.
6. Keep 75 MHz SDRAM as the accepted baseline; treat 100 MHz as a stretch goal
   requiring fresh timing, cache, DMA, PMMU, and full-memory stress acceptance.

---

## 18. Non-goals

Not an Amiga / Atari ST / Mac / NeXT / BeOS clone, not Linux-first, not
cycle-accurate, not FPU-dependent, and not a GPU-heavy 3D machine. Astra 68 is
its own platform: a clean, understandable, protected 68k computer with enough
custom hardware to be distinctive and fun.
