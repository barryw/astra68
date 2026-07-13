# Astraea — DMA / Blitter / Copper / Arbiter Register Map (v0.1)

Astraea is the Astra 68 "brain" chip (Agnus analog). Three subsystems:

1. **Memory arbiter** — schedules the one 16-bit SDRAM among all masters.
2. **Blitter** — 2D DMA engine (copy / fill / color-key / masked copy).
3. **Copper** — raster coprocessor executing a BRAM instruction list, driving
   Vega/Lyra/Astraea registers synchronized to the beam.

Authoritative contract; `sw/include/astraea.h` is the hand-maintained C mirror.

---

## 1. Addressing & conventions

- **Base:** `ASTRAEA_BASE = 0xFFF10000`.
- 32-bit registers, 4-byte stride, big-endian; RO / RW / RW1C. Supervisor-only.

```
Block map (ASTRAEA_BASE +):
  0x0000  global (id/ver/ctrl/status/irq)
  0x0020  arbiter (status / perf)
  0x0040  blitter
  0x0080  copper control
  0x4000  copper instruction RAM (2048 x 8-byte instructions = 16 KB BRAM)

Chipset map: 0xFFF00000 Vesta · 0xFFF10000 Astraea · 0xFFF20000 Vega · 0xFFF30000 Lyra
```

---

## 2. Global (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `ID` | RO | `0x41535452` | "ASTR" |
| 0x0004 | `VERSION` | RO | `0x00010000` | major/minor |
| 0x0008 | `CTRL` | RW | 0 | global control |
| 0x000C | `STATUS` | RO | — | global status |
| 0x0010 | `IRQ_EN` | RW | 0 | `[0]BLIT_DONE [1]COPPER [2]ARB_UNDERRUN` |
| 0x0014 | `IRQ_STAT` | RW1C | 0 | pending; write 1 to clear |

IRQs route to the Vesta interrupt controller.

---

## 3. Memory arbiter (0x0020)

Mostly fixed-function. Priority (SPEC §15), highest first:
`video scanout > audio FIFO > sprite/tile line fetch > CPU > blitter > storage/DMA`.
Video and audio never underrun (line/FIFO buffers + guaranteed slots); the CPU
and blitter stall first.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0020 | `ARB_CTRL` | RW | reserved (fixed priority in v0.1) |
| 0x0024 | `ARB_STATUS` | RO | `[0]VIDEO_UNDERRUN [1]AUDIO_UNDERRUN` (sticky, debug) |
| 0x0028 | `ARB_PERF` | RO | rolling SDRAM utilization counter (debug) |

Underrun bits should never set in normal operation — they flag a
mis-provisioned scene (too many layers/sprites for the bandwidth budget).

---

## 4. Blitter (0x0040)

A DMA bus master. Yields to video/audio/sprite fetch; runs opportunistically in
blanking and idle bus time. Completion raises `IRQ_STAT.BLIT_DONE`.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0040 | `BLIT_SRC` | RW | [24:0] source SDRAM byte address |
| 0x0044 | `BLIT_DST` | RW | [24:0] dest SDRAM byte address |
| 0x0048 | `BLIT_MASK` | RW | [24:0] mask base (COPY_MASK mode) |
| 0x004C | `BLIT_SRC_PITCH` | RW | [15:0] source bytes/row |
| 0x0050 | `BLIT_DST_PITCH` | RW | [15:0] dest bytes/row |
| 0x0054 | `BLIT_MASK_PITCH` | RW | [15:0] mask bytes/row (1bpp) |
| 0x0058 | `BLIT_DIM` | RW | [31:16] height (rows), [15:0] width (elements) |
| 0x005C | `BLIT_OP` | RW | mode + element size + direction (below) |
| 0x0060 | `BLIT_COLOR` | RW | fill / constant value (element-sized) |
| 0x0064 | `BLIT_KEY` | RW | color-key transparent value (COPY_KEY) |
| 0x0068 | `BLIT_CTRL` | RW | `[0]START (write 1) [1]IRQ_EN` |
| 0x006C | `BLIT_STATUS` | RO | `[0]BUSY [1]DONE [15:8]ERROR` |

**`BLIT_OP`**
```
[2:0]  MODE   0=COPY 1=FILL 2=COPY_KEY 3=COPY_MASK 4=LINE(deferred)
[5:4]  ELEMSZ 0=8-bit 1=16-bit 2=32-bit
[8]    REV_X  blit right-to-left (overlapping copies)
[9]    REV_Y  blit bottom-to-top (overlapping copies)
[31:10] reserved
```

- **COPY** — `dst = src`.
- **FILL** — `dst = BLIT_COLOR` (clear framebuffer / tilemap fast).
- **COPY_KEY** — transparent copy: source elements equal to `BLIT_KEY` leave the
  destination unchanged (sprite/UI blits with a transparent color).
- **COPY_MASK** — 1-bit-per-pixel mask at `BLIT_MASK` selects which source
  elements are written.
- Width is in **elements**; `ELEMSZ` picks 8/16/32-bit (RGB565 fb = 16-bit,
  4bpp packed = 8-bit two-px, 32-bit palette entries, etc.). Pitches are bytes.
- `REV_X`/`REV_Y` set the traversal direction so overlapping src/dst regions
  copy correctly (`memmove` semantics).

**LINE** is deferred (§10); the reserved mode keeps the encoding stable.

Start a blit: program registers, then write `BLIT_CTRL = BLIT_START`. Poll
for `BLIT_STATUS.DONE && !BLIT_STATUS.BUSY` or take the done IRQ. `DONE` is
sticky and START clears it; software must not require observing `BUSY`, because
short operations can complete between two CPU reads.

### Current RTL status

The first hardware implementation supports `COPY` and `FILL` for 8-, 16-,
and 32-bit elements, two-dimensional pitches, arbitrary byte alignment, and
both reverse directions. Aligned rows transfer 16 native 32-bit words per
chunk. Unaligned rows use exact byte-enable writes, preserving bytes outside
the destination rectangle. Reverse traversal has directed overlap coverage and
provides `memmove` semantics.

`COPY_KEY`, `COPY_MASK`, and `LINE` are reserved but not implemented yet.
Starting one of those operations completes with `BLIT_STATUS.ERROR=1`; it does
not modify SDRAM. Error 2 reports an internal state-machine fault. `DONE` and
the error field are cleared by the next accepted `START`.

The engine holds the native DMA grant through every chunk. CPU SDRAM requests
stall while it owns memory, MMIO and ROM remain available for polling, and the
TG wrapper caches remain invalid for the operation and completion boundary.
The shared BIST/blitter port registers one owner for the complete operation, so
request and response routing cannot change while transactions are outstanding.
Pin-level simulation at 75 MHz measures 51.65 MB/s for aligned copy and
119.25 MB/s for aligned fill. Build `0x7E17F8DC` passed three cold ULX3S boots
at 51.40 MB/s copy and 118.36 MB/s fill, including MMIO launch, completion
polling, and cache-maintenance boundaries.

---

## 5. Copper (0x0080 control + 0x4000 RAM)

A tiny coprocessor that executes an instruction list in on-chip **BRAM**
(deterministic — no SDRAM arbitration in the raster path) and writes chipset
registers synchronized to the beam. This is how you change palette, backdrop,
scroll, framebuffer base, sprite regs, etc. per scanline without the CPU.

### Control registers (0x0080)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0080 | `COP_CTRL` | RW | `[0]ENABLE [1]VBL_RESTART` |
| 0x0084 | `COP_START` | RW | [10:0] entry instruction index (double-buffer lists here) |
| 0x0088 | `COP_STATUS` | RO | `[10:0] PC`, `[16]RUNNING`, `[17]WAITING` |
| 0x008C | `COP_STROBE` | WO | write 1 = restart now (PC ← `COP_START`) |

With `VBL_RESTART`, the copper restarts at `COP_START` every vblank (Amiga
model) — swap `COP_START` between two lists for tear-free copper double-buffering.

### Instruction RAM (0x4000)

2048 instructions, 8 bytes each (`COP[i].w0`, `COP[i].w1`), CPU-writable,
copper-readable (dual-port BRAM).

```
w0: [31:29] OPCODE   [28:0] A-field
w1: [31:0]  B-field
```

| Op | Name | A-field (w0[28:0]) | B-field (w1) | Effect |
|---|---|---|---|---|
| 0 | `END` | — | — | halt until next restart |
| 1 | `MOVE` | register offset [17:0] | 32-bit value | write value → `0xFFF00000 + offset` |
| 2 | `WAIT` | wait Y [15:0] | wait X [15:0] | stall until beam ≥ (Y,X) this frame |
| 3 | `SKIP` | Y [15:0] | X [15:0] | skip next instruction if beam ≥ (Y,X) |
| 4 | `IRQ` | source bits [3:0] | — | raise `IRQ_STAT.COPPER` |
| 5 | `JUMP` | instruction index [10:0] | — | PC ← index |

`MOVE` targets any chipset register (offset from `0xFFF00000`, 256 KB range →
all four chips). So the copper can drive Vega (gradients, scroll, flips), Lyra
(audio sync), and even trigger the blitter (`MOVE BLIT_CTRL, BLIT_START`).

**Security:** the copper runs with hardware privilege and `COP` RAM lives in
supervisor MMIO — only the kernel authors copper lists. A user process cannot
reach the copper to bypass the MMU. (A configurable target-range guard is a
possible future hardening; §10.)

---

## 6. Copper ↔ Vega interaction

Use `docs/VEGA.md §11` for which Vega registers are safe to `MOVE` mid-scanline
(palette, backdrop, colorkey, tile scroll, sprite regs, raster compare) vs
blanking-only (mode, pitch, format). `WAIT` pairs with Vega's `VEGA_BEAM`; the
copper's beam compare and Vega's `VEGA_RASTER_CMP` are the same raster position.

---

## 7. Programming sketches

**Full-screen backdrop gradient (copper list)**
```c
int n = 0;
for (int y = 0; y < 480; y++) {
    ASTRAEA->COP[n].w0 = COP_OP_WAIT | (y & 0xFFFF);       // WAIT line y
    ASTRAEA->COP[n].w1 = 0;                                 //   x = 0
    n++;
    ASTRAEA->COP[n].w0 = COP_OP_MOVE | COP_OFF(&VEGA->BACKDROP);
    ASTRAEA->COP[n].w1 = sky_gradient[y];                  // RGB888 for this line
    n++;
}
ASTRAEA->COP[n].w0 = COP_OP_END; ASTRAEA->COP[n].w1 = 0;

ASTRAEA->COP_START = 0;
ASTRAEA->COP_CTRL  = COP_ENABLE | COP_VBL_RESTART;
```

**Clear the framebuffer with the blitter**
```c
ASTRAEA->BLIT_DST       = fb;
ASTRAEA->BLIT_DST_PITCH = 720 * 2;
ASTRAEA->BLIT_DIM       = BLIT_DIM_(720, 480);   // w x h
ASTRAEA->BLIT_OP        = BLIT_MODE_FILL | BLIT_ELEM16;
ASTRAEA->BLIT_COLOR     = 0x0000;                 // black RGB565
ASTRAEA->BLIT_CTRL      = BLIT_START;
while (ASTRAEA->BLIT_STATUS & BLIT_BUSY) ;
```

**Transparent sprite/tile blit into an off-screen buffer**
```c
ASTRAEA->BLIT_SRC = spr; ASTRAEA->BLIT_SRC_PITCH = spr_pitch;
ASTRAEA->BLIT_DST = buf; ASTRAEA->BLIT_DST_PITCH = buf_pitch;
ASTRAEA->BLIT_DIM = BLIT_DIM_(w, h);
ASTRAEA->BLIT_KEY = 0xF81F;                        // magenta = transparent
ASTRAEA->BLIT_OP  = BLIT_MODE_COPY_KEY | BLIT_ELEM16;
ASTRAEA->BLIT_CTRL = BLIT_START | BLIT_IRQ_EN;     // done via IRQ
```

---

## 8. Bandwidth / arbitration

The blitter and copper are SDRAM masters (copper only for `MOVE`s that target
SDRAM-backed things — normally it touches MMIO registers, not SDRAM, so it is
cheap and deterministic). The blitter can saturate whatever bandwidth video/
audio/sprites leave; it stalls first. A full-screen 720×480×16 fill ≈ 675 KB —
at best-case leftover bandwidth a few ms; it runs in the background, so pipeline
draws and don't block on `BLIT_BUSY` when you can take the IRQ.

---

## 9. Decisions

- 2×32-bit copper instructions — `MOVE` carries a full 32-bit value (our regs are
  32-bit: `FB_BASE`, RGB888 palette, etc.), no HI/LO split.
- Copper list in BRAM (2048 insns) — deterministic raster path, enough for a
  full-screen per-scanline effect (960 insns) with headroom.
- Copper `MOVE` reaches the whole chipset (incl. blitter triggers) — Amiga-style
  power; kernel-authored lists keep it safe.
- Blitter width in elements + `ELEMSZ` — one engine serves 8/16/32-bit data.

## 10. Open

1. Blitter **LINE** mode (Bresenham) — reinterpret SRC/DST/DIM as endpoints, or
   add x0/y0/x1/y1 regs? Deferred.
2. Blitter **A/B/C/D channel** minterm ROP (full Amiga-style) — COPY_KEY +
   COPY_MASK cover v0.1; add later if needed.
3. Copper **target-range guard** (restrict `MOVE` away from Vesta) — hardening.
4. Copper per-instruction **conditional** beyond SKIP (compare register)?
5. DMA channels for SD/audio sample streaming — or leave to Lyra/Vesta?
