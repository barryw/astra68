# Vega — Video Chip Register Map (v0.5)

Vega is the Astra 68 display chip: framebuffer scanout with pixel-granular
two-axis viewport scrolling, 16 hardware sprites, palette, backdrop, and the
raster/beam interface the copper (Astraea) and CPU use for raster effects.

This document is the authoritative register contract. `sw/include/vega.h` is the
hand-maintained C mirror; keep them in sync.

---

## 1. Layer / compositor model

Per output pixel, Vega composites bottom → top:

```
1. BACKDROP        single color, copper-animatable per scanline (gradients)
2. BEHIND sprites  sprites with CTRL.BEHIND=1, by priority
3. FRAMEBUFFER     chunky RGB565 or INDEX8; optional color-key transparency
4. FRONT sprites   sprites with CTRL.BEHIND=0, by priority (then index)
```

Every layer is independently enableable. Games and the graphical OS use a
virtual framebuffer wider and/or taller than the display and change `FB_VIEW`
instead of copying visible pixels to scroll.

- **Backdrop:** copper rewrites `BACKDROP` per scanline for gradients; shown in
  the border and through color-keyed framebuffer holes.
- **Framebuffer:** chunky RGB565 or INDEX8, opaque unless `FB_COLORKEY_EN` and
  the pixel equals `FB_COLORKEY` (then transparent → lower layers show).
- **Sprites:** 4bpp indexed; `BEHIND` composites a sprite below the framebuffer.
  Order among sprites by `PRIORITY`, ties by index.
- Sprite pixels are transparent where their pattern index equals the
  descriptor's transparent index. Color = `PAL[bank*16 + index]`.

Framebuffer and sprites are fetched into **double-buffered per-scanline line
buffers** one line ahead (never per-pixel SDRAM lookups). Both clients pipeline
requests into the SDRAM controller's two-entry FIFO, drain
responses, and yield at bounded 32-word boundaries. Sprite pixels/line are
admitted in descending priority and then ascending descriptor index. A sprite
is accepted in full or dropped in full; overflow sets `STATUS.SPR_OVERFLOW`.

---

## 2. Addressing & conventions

- **Base:** `VEGA_BASE = 0xFFF20000` (provisional chipset MMIO map below).
- **Registers are 32-bit, 4-byte stride, big-endian.** Access with `move.l`.
  Narrow values sit in the low bits and reserved fields must be written zero.
  Configuration registers retain malformed reserved/address bits for diagnostic
  readback and assert `STATUS.CONFIG_ERROR`; they are never silently truncated
  into an SDRAM request. No 16-bit HI/LO address splits are used.
- **Access:** RO, RW, RW1C (write-1-to-clear). MMIO is **supervisor-only**.

```
MMIO block map (within Vega, VEGA_BASE +):
  0x0000  global (id/ver/ctrl/status/irq/mode)
  0x0020  display / raster (beam, raster compare, active res)
  0x0030  backdrop / scene submission dependencies
  0x0040  framebuffer, viewport, and atomic presentation
  0x0080  reserved (retired tile-layer aperture)
  0x0400  palette (256 x RGB888)
  0x0800  sprite global (enable/budget/collision)
  0x1000  sprite table (16 x 32 bytes)
  0x2000  bootstrap POST text plane (2700 byte cells)

Chipset map (provisional):
  0xFFF00000 Vesta   0xFFF10000 Astraea   0xFFF20000 Vega   0xFFF30000 Lyra
```

---

## 3. Global registers (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `VEGA_ID` | RO | `0x56454741` | "VEGA" |
| 0x0004 | `VEGA_VERSION` | RO | `0x00050000` | [31:16] major, [15:0] minor |
| 0x0008 | `VEGA_CTRL` | RW | 0 | see below |
| 0x000C | `VEGA_STATUS` | RO | — | see below |
| 0x0010 | `VEGA_IRQ_EN` | RW | 0 | IRQ enable mask |
| 0x0014 | `VEGA_IRQ_STAT` | RW1C | 0 | IRQ pending; write 1 to clear |
| 0x0018 | `VEGA_MODE` | RW | 0 | display mode select |
| 0x001C | `VEGA_CAPS` | RO | — | implemented capability bits |

**`VEGA_CTRL`** `[0]DISPLAY_EN [1]FB_EN [2]SPR_EN [3]FB_COLORKEY_EN
[4]BACKDROP_EN`.

**`VEGA_STATUS`** `[0]VBLANK [1]HBLANK [2]SCENE_LOCKED [3]SPR_OVERFLOW
[4]DISPLAY_READY [5]UNDERRUN [6]CONFIG_ERROR [7]FETCH_BUSY`.

- `DISPLAY_READY` is the synchronized display/HDMI-ready input.
- `UNDERRUN` is sticky and means a requested scanline was not ready when the
  compositor needed it. Write one to status bit 5 to clear it.
- `CONFIG_ERROR` is asserted when an enabled framebuffer or sprite
  descriptor is malformed. The invalid source is skipped rather than allowing
  a wrapped SDRAM request.
- `FETCH_BUSY` covers framebuffer and sprite scanline construction.
- `SCENE_LOCKED` means a submitted scene is pending or Vega is completing its
  bounded metadata-bank copy. CPU visual-state writes are rejected while set.

**`VEGA_IRQ_EN`/`STAT`** `[0]VBLANK [1]RASTER [2]COLLISION`.

**`VEGA_CAPS`** `[0]POST_TEXT [1]FRAMEBUFFER [2]PALETTE [3]TILEMAP
[4]SPRITE [5]INDEX8 [6]FB_SCROLL`. `TILEMAP` reads zero in v0.5;
`FB_SCROLL` reads one. Software must test capability bits rather than infer
features from a version number.

**`VEGA_MODE`** `0=720x480@60 (primary) 1=640x480 2=320x240 3=320x200
4=400x300 5=640x400`.

### Bootstrap POST text plane

`VEGA_BASE + 0x2000` exposes 2700 byte-addressed cells arranged as 90 columns
by 30 rows. Each byte is an ASCII/CP437 character. The locked target renders a
true 8x16 Astra Rescue Mono bank, exactly filling the 720x480 active area. The
current bring-up image renders an older 8x8 bank with doubled rows until the
replacement passes visual and hardware acceptance. Character RAM and font ROM
are BRAM-backed and do not depend on SDRAM, so memory-test failures remain
visible.

This plane is a ROM/POST facility, not the OS text API. Its final BRAM bank is
the 8x16 Astra Rescue Mono face and remains visible without SDRAM. The OS uses
AFNT strikes and Astraea hardware glyph expansion into normal Vega surfaces;
Vega does not parse fonts or render glyphs. See `docs/FONTS.md`.

---

## 4. Display / raster (0x0020)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0020 | `VEGA_BEAM` | RO | [31:16] beamY, [15:0] beamX (live) |
| 0x0024 | `VEGA_RASTER_CMP` | RW | [15:0] scanline; RASTER IRQ when beamY == this |
| 0x0028 | `VEGA_ACTIVE` | RO | [31:16] active height, [15:0] active width |

---

## 5. Backdrop and scene dependencies (0x0030)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0030 | `VEGA_BACKDROP` | RW | [23:0] RGB888 backdrop / border color (copper-animate per line) |
| 0x0034 | `VEGA_SCENE_GENERATION` | RW | caller's nonzero scene generation |
| 0x0038 | `VEGA_DRAW_FENCE` | RW | required completed Astraea draw fence; zero means none |
| 0x003C | `VEGA_BLIT_FENCE` | RW | required completed Astraea blitter fence; zero means none |

---

## 6. Framebuffer and presentation (0x0040)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0040 | `VEGA_FB_BASE` | RW | 0 | [24:0] SDRAM byte address in the editable scene |
| 0x0044 | `VEGA_FB_PITCH` | RW | 1440 | [15:0] bytes per scanline |
| 0x0048 | `VEGA_FB_FORMAT` | RW | 0 | [2:0] format: 0 RGB565, 1 INDEX8 |
| 0x004C | `VEGA_FB_COLORKEY` | RW | 0 | RGB565 [15:0] or INDEX8 [7:0] transparent value |
| 0x0050 | `VEGA_PRESENT_CTRL` | WO | 0 | write bit 0 to submit the complete editable scene |
| 0x0054 | `VEGA_PRESENT_STATUS` | RO/RW1C | 0 | presentation state and sticky errors below |
| 0x0058 | `VEGA_PRESENT_COMPLETED_GENERATION` | RO | 0 | last completely promoted generation |
| 0x005C | `VEGA_PRESENT_COMPLETED_FRAME` | RO | 0 | frame counter associated with completion |
| 0x0060 | `VEGA_PRESENT_RETIRED_FB` | RO | 0 | framebuffer base replaced by the last promotion |
| 0x0064 | `VEGA_FRAME_COUNTER` | RO | 0 | increments once per vblank |
| 0x0068 | `VEGA_FB_VIEW` | RW | 0 | [31:16] viewport Y, [15:0] viewport X, in pixels |
| 0x006C | `VEGA_FB_VIRTUAL` | RW | 0 | [31:16] virtual height, [15:0] virtual width; a zero field selects the active dimension |
| 0x0070 | `VEGA_FB_WRAP` | RW | 0 | [0] wrap X, [1] wrap Y; all other bits reserved |

`VEGA_PRESENT_STATUS` is `[0]PENDING [1]COPY_BUSY [2]DONE [3]INVALID
[4]SHADOW_WRITE_REJECTED [5]COPY_DEADLINE [6]WAIT_WRITERS`.

- `DONE`, `INVALID`, `SHADOW_WRITE_REJECTED`, and `COPY_DEADLINE` are sticky
  RW1C bits. `PENDING`, `COPY_BUSY`, and `WAIT_WRITERS` are live.
- `WAIT_WRITERS` means the requested fences have retired but an Astraea or
  AstraHost memory writer still owns SDRAM.
- Fence comparison is modulo 32 bits with the usual half-range ordering. A
  required fence of zero disables that dependency.

All CPU writes to visual registers, palette RAM, and sprite descriptors update
an editable shadow scene. Reads return that editable state. `PRESENT_SUBMIT`
captures the generation and fence values and locks the scene. At the first
vblank for which both fences are complete and every render/host writer is idle,
Vega atomically promotes scalar state and swaps the framebuffer base. Palette
and descriptor metadata are copied between three on-chip banks during the
bounded blanking-time copy; framebuffer pixels are never copied.

After `PRESENT_COMPLETED_GENERATION` reaches the submitted value, software may
reuse `PRESENT_RETIRED_FB`. A second submission while locked, malformed shadow
configuration, or visual write while locked is rejected explicitly; no request
is silently merged into an immutable generation.

The active framebuffer and a submitted framebuffer are hardware-protected
write ranges. A CPU store to either range receives a 68030 bus error before an
SDRAM transaction starts. Astraea blit/draw/flood commands whose complete
destination or flood workspace overlaps either range finish with error 5 before
their first write. AstraHost boot or `BLOCK_PUSH` writes are rejected with
protocol status `0x09`. Rendering is therefore performed into an unprotected
back page, followed by one scene submission and a page swap.

Copper MOVE remains the intentional raster-time exception: it writes active
Vega registers during scanout. Every vblank restores the committed baseline
before copper runs the next frame, so raster effects do not mutate the CPU's
editable scene. Sprite image data referenced from SDRAM is a published resource
rather than a mutable surface; update a private copy and submit the new pointer
instead of modifying an active resource in place.

Framebuffer base and pitch must be 32-bit aligned. Pitch must be at least
`virtual_width*2` for RGB565 or `virtual_width` for INDEX8. RGB565 virtual width
must be even; INDEX8 virtual width must be divisible by four. The complete
virtual surface, including row pitch, must remain within the 32 MiB SDRAM
aperture. Unsupported formats and malformed addresses reject presentation and
suppress framebuffer requests.

`FB_VIEW` selects the active display-sized rectangle within that virtual
surface. X and Y are pixel-granular. Each virtual dimension must be at least the
corresponding active dimension and each viewport coordinate must be inside the
virtual surface. With wrapping disabled, the complete viewport must fit. With
wrapping enabled, a viewport crossing the selected edge continues at coordinate
zero. X and Y wrap independently, so software can use a double-width surface,
a double-height surface, or a two-axis ring surface without moving visible
pixels.

CPU writes to `FB_VIEW`, `FB_VIRTUAL`, and `FB_WRAP` are part of the editable
scene and take effect atomically at presentation vblank. Copper writes affect
the active copies during scanout; the committed values are restored at the next
vblank before the new copper frame begins.

---

## 7. Reserved aperture (0x0080)

The former tile-layer aperture at `0x0080..0x00BF` is reserved in v0.5. Reads
return zero, writes have no effect, and `VEGA_CAPS.TILEMAP` is clear. Hardware
scrolling is provided by the framebuffer viewport registers and does not depend
on a tile engine.

---

## 8. Palette (0x0400)

256 entries, `VEGA_PAL[i]` at `0x0400 + i*4`, `0x00RRGGBB`. Used by sprites
and INDEX8 framebuffer scanout; RGB565 framebuffer pixels bypass it. Sprite
color = `PAL[bank*16 + index]`. Copper-writable mid-frame.

---

## 9. Sprite global (0x0800)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0800 | `VEGA_SPR_CTRL` | RW | 0 | [0] global sprite enable |
| 0x0804 | `VEGA_SPR_BUDGET` | RW | 1024 | [15:0] requested sprite pixels/scanline |
| 0x0808 | `VEGA_SPR_COLLISION` | RO | 0 | [31:0] per-sprite collision bitmap |

The effective admission budget is `min(SPR_BUDGET, format limit)`. The
hardware-enforced limits are 1024 pixels/line for INDEX8 or framebuffer-disabled
composition and 512 pixels/line while an RGB565 framebuffer is active. Readback
returns the requested value. If active sprites exceed the effective budget,
lower-priority sprites are omitted for that line and `STATUS.SPR_OVERFLOW` is
asserted. This is a deterministic bandwidth limit, not a descriptor-count
limit: all 16 descriptors may be visible when their clipped widths fit.

---

## 10. Sprite table (0x1000)

16 sprites, 32 bytes each. Sprite `i` at `0x1000 + i*0x20`.

| +Off | Name | Description |
|---|---|---|
| 0x00 | `SPR_CTRL` | attributes (below) |
| 0x04 | `SPR_POS` | [31:16] Y (s16), [15:0] X (s16) |
| 0x08 | `SPR_SIZE` | [31:16] height, [15:0] width (px) |
| 0x0C | `SPR_BASE` | [24:0] SDRAM byte addr of 4bpp pattern |
| 0x10 | `SPR_PITCH` | [15:0] bytes per pattern row (≥ width/2) |
| 0x14 | reserved ×3 | collision/scaling, future |

**`SPR_CTRL`** `[0]ENABLE [1]VISIBLE [2]FLIPX [3]FLIPY [4]BEHIND [5]COLLIDE_EN
[11:8]PRIORITY(0-15) [15:12]PAL_BANK(0-15) [19:16]TRANSP_IDX(0-15)`.

---

## 11. Copper safety

| Safe per-scanline (copper effects) | Blanking-only |
|---|---|
| `VEGA_PAL[*]`, `VEGA_BACKDROP` | `VEGA_MODE`, `VEGA_CTRL` |
| `VEGA_FB_COLORKEY` | `VEGA_FB_PITCH`, `VEGA_FB_FORMAT` |
| `VEGA_FB_VIEW`, `VEGA_FB_WRAP` | `VEGA_FB_BASE` (auto-latches @vblank) |
| `SPR_*`, `VEGA_RASTER_CMP` | `VEGA_FB_VIRTUAL` |

Vega prepares scanlines one line ahead. A copper viewport change affects a
subsequently prepared scanline rather than the line currently leaving the line
buffer; copper lists must schedule line-scroll effects with that pipeline in
mind. CPU viewport changes use scene presentation and need no beam timing.

---

## 12. Bandwidth note

Each active source fetches per scanline from the one 16-bit SDRAM (120 MB/s raw
at 60 MHz). A 720x480 output line provides about 1906 SDRAM clocks at the
locked 60 MHz memory and 27 MHz pixel clocks. Payload at 720 pixels is:
- RGB565 framebuffer = 1440 B; INDEX8 framebuffer = 720 B.
- Sprites are bounded by `SPR_BUDGET` source pixels per line.

The framebuffer fetcher splits a wrapped X line into at most two contiguous
bursts; Y wrap only changes the selected row. Therefore scrolling adds address
generation and at most one burst boundary, not another full-frame copy or an
extra display layer. Video has priority; draw, blitter, and CPU clients stall
before scanout is allowed to miss its reservation. Regression rejects a line
that reaches its scanout deadline. `STATUS.UNDERRUN` remains a sticky fault
indicator, while `STATUS.SPR_OVERFLOW` reports deterministic sprite admission
pressure.

---

## 13. Programming sketches

**Pixel-granular horizontal scrolling on a double-width RGB565 surface:**

```c
VEGA->FB_BASE    = surface_addr;
VEGA->FB_PITCH   = 1440u * 2u;
VEGA->FB_FORMAT  = VEGA_FMT_RGB565;
VEGA->FB_VIRTUAL = VEGA_FB_VIRTUAL_(1440, 480);
VEGA->FB_VIEW    = VEGA_FB_VIEW_(camera_x % 1440u, 0);
VEGA->FB_WRAP    = VEGA_FB_WRAP_X;
VEGA->SCENE_GENERATION = generation;
VEGA->PRESENT_CTRL = VEGA_PRESENT_SUBMIT;
```

Use a double-height surface and `VEGA_FB_WRAP_Y` for vertical streaming, or a
surface larger in both dimensions with `VEGA_FB_WRAP_XY`. Active-surface
protection covers the complete published virtual surface, including pixels
outside the viewport. Continuous streaming therefore uses two ring surfaces:
software, Astraea, or the blitter updates newly exposed strips in the back ring,
then scene presentation swaps rings at vblank. This keeps tear prevention a
hardware invariant rather than relying on application beam timing.

---

## 14. Baseline and extension points

**Decided:** 32-bit registers, chunky RGB565/INDEX8 scanout, atomic scene
presentation, pixel-granular two-axis framebuffer scrolling and wrap, backdrop,
palette, color key, 16 priority sprites with `BEHIND`, and copper raster writes.

The v0.5 baseline deliberately removes tile layers to preserve routing and
timing margin for the complete system. It retains framebuffer scanout, palette,
16 sprites, atomic scene presentation, write-protected page flipping, collision
bitmap, raster state, and copper writes. Reserved extension points include
sprite scaling and additional pixel formats. They are not part of the v0.5
contract and software must not rely on them.
