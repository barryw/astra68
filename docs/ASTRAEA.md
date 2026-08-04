# Astraea — DMA / Drawing / Copper Register Map (v0.4)

> **Legacy ULX3S implementation contract.** This document remains authoritative
> for the implemented ULX3S Astraea v0.4 block and its regression evidence. The
> active Arty Z7-20 graphics architecture is
> [`GRAPHICS_ARCHITECTURE.md`](GRAPHICS_ARCHITECTURE.md), which supersedes these
> formats, address widths, command transport, copper capacity, memory topology,
> and performance limits for the new target. Do not extend this register map
> into the Arty ABI by inference.

Astraea is the Astra 68 "brain" chip (Agnus analog). Four subsystems:

1. **Memory arbiter** — schedules the one 16-bit SDRAM among all masters.
2. **Blitter** — bounded 2D copy, fill, keyed copy, and masked copy DMA.
3. **Draw engine** — clipped geometry, patterns, glyph expansion, and bounded
   scanline flood fill for INDEX8 and RGB565 surfaces.
4. **Copper** — raster coprocessor executing a BRAM instruction list, driving
   Vega/Lyra/Astraea registers synchronized to the beam.

Authoritative contract; `sw/include/astraea.h` is the hand-maintained C mirror.

---

## 1. Addressing & conventions

- **Base:** `ASTRAEA_BASE = 0xFFF10000`.
- 32-bit registers, 4-byte stride, big-endian; RO / RW / RW1C. Supervisor-only.

```
Block map (ASTRAEA_BASE +):
  0x0000  global (id/ver/ctrl/status/irq)
  0x0020  reserved for future arbiter counters
  0x0040  blitter
  0x0080  copper control
  0x0100  draw / glyph / flood frontend
  0x4000  copper instruction RAM (2048 x 8-byte instructions = 16 KB BRAM)

Chipset map: 0xFFF00000 Vesta · 0xFFF10000 Astraea · 0xFFF20000 Vega · 0xFFF30000 Lyra
```

---

## 2. Global (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `ID` | RO | `0x41535452` | "ASTR" |
| 0x0004 | `VERSION` | RO | `0x00040000` | major/minor |
| 0x0008 | `CTRL` | RW | 0 | global control |
| 0x000C | `STATUS` | RO | — | global status |
| 0x0010 | `IRQ_EN` | RW | 0 | `[0]BLIT_DONE [1]COPPER [2]reserved [3]DRAW_DONE` |
| 0x0014 | `IRQ_STAT` | RW1C | 0 | pending; write 1 to clear |
| 0x0018 | `CAPABILITIES` | RO | `0x000000FF` | copy, fill, key, mask, geometry, glyph, flood, copper |

IRQs route to the Vesta interrupt controller.

---

## 3. Memory arbiter

The native controller prioritizes an already-locked CPU read/modify/write,
then the real-time Vega line builder, then Astraea/storage DMA, then ordinary
CPU traffic. A selected owner is held until every response in its transaction
phase retires. Vega framebuffer, tile, and sprite fetches share the real-time
video port and rotate after bounded 32-word bursts. Their independent local
BRAM work overlaps other video clients instead of locking SDRAM for an entire
scanline build. Future Lyra integration adds a separately bounded real-time
port; it must not be hidden behind the opportunistic DMA owner.

Offsets `0x0020..0x003f` are reserved in v0.4. Video deadline state is reported
by `VEGA_STATUS.UNDERRUN`; no undocumented Astraea arbitration/performance
registers are exposed. Future audio integration may define counters here, but
software must currently treat the entire range as reserved and read-zero.

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
| 0x0070 | `BLIT_FENCE` | RW/RO | submitted value while busy; completed value when idle |

**`BLIT_OP`**
```
[2:0]  MODE   0=COPY 1=FILL 2=COPY_KEY 3=COPY_MASK
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

Start a blit: program registers, then write `BLIT_CTRL = BLIT_START`. Poll
for `BLIT_STATUS.DONE && !BLIT_STATUS.BUSY` or take the done IRQ. `DONE` is
sticky and START clears it; software must not require observing `BUSY`, because
short operations can complete between two CPU reads.

### RTL status

All four modes support 8-, 16-, and 32-bit elements, two-dimensional pitches,
arbitrary byte alignment, and exact destination byte preservation. COPY/FILL
operate in bounded 16-element phases. COPY_KEY compares complete elements.
COPY_MASK consumes most-significant-bit-first 1bpp rows with an independent
mask pitch. Reverse Y is supported by every applicable mode; COPY reverse X/Y
provides `memmove` overlap semantics. Invalid encodings report error 1 and an
internal state-machine fault reports error 2. A destination overlapping Vega's
active or submitted framebuffer reports error 5. `DONE` and the error field
clear on the next accepted `START`.

All address and pitch registers are retained at their full 32-bit MMIO width
until START validation. Reserved high bits, a range crossing the 32 MiB SDRAM
aperture, or a wrapped final row fail with error 1 before DMA ownership is
taken. No source is narrowed silently to the native 25-bit memory port.
Protection validates the complete two-dimensional destination range before
the first DMA request, so a rejected command performs no partial write.

`BLIT_FENCE` is captured with an accepted START and becomes the completed value
only after every destination write retires. Vega presentation can depend on
that value without polling transient BUSY state.

The engine holds the native DMA grant through every chunk. CPU SDRAM requests
stall while it owns memory, MMIO and ROM remain available for polling, and the
TG wrapper caches remain invalid for the operation and completion boundary.
The Astraea-local arbiter registers either blitter or draw ownership for a
complete transaction phase, so responses cannot cross-route when both engines
request memory. Pin-level simulation at 60 MHz currently measures 39.54 MB/s
for aligned copy and 92.88 MB/s for aligned fill.

### Draw, glyph, and flood frontend (0x0100)

The draw engine is an immediate-mode SDRAM master sharing Astraea's registered
local memory owner with the blitter. It supports exact unaligned INDEX8 and
big-endian RGB565 accesses. All commands are clipped before address generation;
an invalid or overflowing SDRAM address terminates the command with an error.

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0100 | `DRAW_DST` | RW | [24:0] destination SDRAM byte address |
| 0x0104 | `DRAW_DST_PITCH` | RW | [15:0] destination bytes/row |
| 0x0108 | `DRAW_FORMAT` | RW | 0=INDEX8, 1=RGB565 |
| 0x010C | `DRAW_CLIP_MIN` | RW | signed `(y << 16) | x`, inclusive |
| 0x0110 | `DRAW_CLIP_MAX` | RW | signed `(y << 16) | x`, exclusive |
| 0x0114 | `DRAW_P0` | RW | first point, center, destination, or flood seed |
| 0x0118 | `DRAW_P1` | RW | second point or unsigned glyph source `(y,x)` |
| 0x011C | `DRAW_RADII` | RW | ellipse `[31:16] ry [15:0] rx`; circle uses `rx` |
| 0x0120 | `DRAW_FG` | RW | foreground/fill pixel in destination format |
| 0x0124 | `DRAW_BG` | RW | opaque pattern/mask background pixel |
| 0x0128 | `DRAW_PATTERN_HI` | RW | upper half of 8x8 1bpp pattern |
| 0x012C | `DRAW_PATTERN_LO` | RW | lower half of 8x8 1bpp pattern |
| 0x0130 | `DRAW_ORIGIN` | RW | signed pattern origin `(y,x)` |
| 0x0134 | `DRAW_SRC` | RW | [24:0] glyph bitmap base |
| 0x0138 | `DRAW_SRC_PITCH` | RW | [15:0] glyph bitmap bytes/row |
| 0x013C | `DRAW_SRC_SIZE` | RW | single glyph `[31:16] height [15:0] width` |
| 0x0140 | `DRAW_PALETTE` | RW | [24:0] RGB565 palette base for indexed glyphs |
| 0x0144 | `DRAW_WORK` | RW | descriptor array or flood LIFO base |
| 0x0148 | `DRAW_WORK_ENTRIES` | RW | 16-bit descriptor count/LIFO capacity |
| 0x014C | `DRAW_OP` | RW | operation and flags (below) |
| 0x0150 | `DRAW_CTRL` | RW | `[0]START [1]IRQ_EN` |
| 0x0154 | `DRAW_STATUS` | RO | `[0]BUSY [1]DONE [15:8]ERROR` |
| 0x0158 | `DRAW_FENCE` | RW/RO | submitted value while busy; completed value when idle |

`DRAW_OP[7:0]` selects the operation:

| Value | Operation | Parameters |
|---:|---|---|
| 0 | line | `P0` to `P1`, both endpoints included |
| 1 | rectangle outline | opposite inclusive corners `P0`, `P1` |
| 2 | filled rectangle | opposite inclusive corners `P0`, `P1` |
| 3 | circle outline | center `P0`, radius `RADII.rx` |
| 4 | filled circle | center `P0`, radius `RADII.rx` |
| 5 | ellipse outline | center `P0`, radii `RADII` |
| 6 | filled ellipse | center `P0`, radii `RADII` |
| 7 | 8x8 pattern fill | inclusive rectangle `P0`, `P1`; origin `ORIGIN` |
| 8 | MASK1 glyph run | `FG`; bit 7 is the leftmost pixel |
| 9 | A4 glyph run | RGB565 only; high nibble is the leftmost coverage |
| 10 | INDEX4 glyph run | 16-entry RGB565 palette |
| 11 | INDEX8 glyph run | 256-entry RGB565 palette |
| 12 | bounded flood fill | seed `P0`; replacement `FG`; caller LIFO in `WORK` |

Bit 8 requests opaque `BG` writes for zero pattern/MASK1 bits; otherwise those
pixels are transparent. For INDEX4/INDEX8, bits `[23:16]` select the transparent
palette index. Other flag bits are reserved and must be zero.

A single glyph uses `SRC`, `SRC_PITCH`, unsigned source origin `P1`, signed
destination `P0`, and `SRC_SIZE`. For a batch, `WORK_ENTRIES` is nonzero and
`WORK` points at 16-byte big-endian descriptors:

```c
struct AstraeaGlyphDesc {
    uint32_t source_offset;  /* added to DRAW_SRC */
    uint32_t source_yx;      /* unsigned y:16, x:16 */
    uint32_t dest_yx;        /* signed y:16, x:16 */
    uint32_t size_hw;        /* unsigned height:16, width:16 */
};
```

INDEX4/INDEX8 palettes are loaded into a 256-entry on-chip RGB565 cache once per
command. A4 blends native 5/6/5 channels as `(fg*a + dst*(15-a) + 7) / 15`.
Text layout remains software's responsibility: the font service maps Unicode,
kerns and shapes text, then submits positioned glyph rectangles. The AFNT and
resident-font contract is `docs/FONTS.md`.

Flood fill uses a bounded scanline algorithm. `WORK` names a caller-owned array
of packed signed `(y,x)` 32-bit seeds and `WORK_ENTRIES` gives its capacity.
Exhausting that capacity stops cleanly with error 3; no write occurs outside the
mandatory clip rectangle. Errors are 1=invalid configuration, 2=internal state,
3=work overflow, 4=address range, and 5=protected range. Every command validates
its complete clipped destination before its first write. Flood fill also
validates the complete caller LIFO range, so neither pixels nor workspace are
partially modified when protection fails. `DONE` is sticky until the next
accepted START. The submitted fence is returned only after all writes complete
and can be used to retire asynchronous draw-list resources.

Directed simulation covers every primitive, line octants, clipping, degenerate
ellipses, transparent/opaque patterns, all glyph formats, exact A4 blending,
batched descriptors, both destination formats, arbitrary alignment, bounded
flood overflow, malformed commands, fences, and concurrent blitter arbitration.
Standalone ECP5 synthesis reports 4,722 LUT4, 1,243 CCU2C, 3 MULT18X18D,
2 DP16KD, and 3,390 flip-flops for the draw engine. Counting two LUT primitives
per carry cell gives a 7,208-primitive draw budget. The exact A4 divide-by-15
step is a registered 1,024-entry quotient ROM; this adds one DP16KD and removes
the former single-cycle combinational divider from the SDRAM domain.

The complete Astraea chip, including blitter, draw engine, copper RAM, MMIO,
and local arbitration, synthesizes standalone to 7,693 LUT4, 1,755 CCU2C,
5,314 flip-flops, 18 DP16KD, and 6 MULT18X18D. That is 11,203 pre-pack LUT
primitives. Integrated routed utilization and timing remain release gates and
are tracked with the complete chipset rather than treated as fixed sums of
standalone blocks.

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

With `VBL_RESTART`, the copper arms at vertical blanking and restarts at
`COP_START` as the beam enters the next active frame. Visible-line `WAIT`s
therefore cannot collapse while the beam is at a vblank line. Swap `COP_START`
between two lists for tear-free copper double-buffering.

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

The copper executes entirely from BRAM and writes MMIO; it is not an SDRAM
master. Display fetch has priority over Astraea DMA. The blitter and draw engine
share one registered Astraea-local owner and can saturate the bandwidth display
leaves; CPU SDRAM accesses stall behind an active DMA transaction while ROM and
MMIO remain available. A full 720x480 RGB565 surface is 675 KiB. Submit
independent work asynchronously and retire it through IRQs/fences rather than
polling `BUSY` in a hot loop. The complete graphics regression verifies both
tile layers plus 32 unrelated-row sprites at the 720-pixel scanline deadline;
the format-dependent sprite ceilings and measured margins are in
`docs/VEGA.md` section 12.

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

1. Add the shared software reference-model adapter for draw-list pixel
   comparisons; directed RTL coverage and malformed-command tests exist now.
2. Blitter **A/B/C/D channel** minterm ROP (full Amiga-style) — COPY_KEY +
   COPY_MASK cover v0.1; add later if needed.
3. Copper **target-range guard** (restrict `MOVE` away from Vesta) — hardening.
4. Copper per-instruction **conditional** beyond SKIP (compare register)?
5. DMA channels for SD/audio sample streaming — or leave to Lyra/Vesta?
