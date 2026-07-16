# Vega — Video Chip Register Map (v0.3)

Vega is the Astra 68 display chip (Denise analog): framebuffer scanout, **2
scrolling tilemap layers**, 32 hardware sprites, palette, backdrop, and the
raster/beam interface the copper (Astraea) and CPU use for raster effects.

This document is the authoritative register contract. `sw/include/vega.h` is the
hand-maintained C mirror; keep them in sync.

---

## 1. Layer / compositor model

Per output pixel, Vega composites bottom → top:

```
1. BACKDROP        single color, copper-animatable per scanline (gradients)
2. BG tile layers  TILE[*] with CTRL.ABOVE=0   (TILE1 under TILE0)
3. BEHIND sprites  sprites with CTRL.BEHIND=1, by priority
4. FRAMEBUFFER     chunky RGB565 or INDEX8; optional color-key transparency
5. FRONT sprites   sprites with CTRL.BEHIND=0, by priority (then index)
6. FG tile layers  TILE[*] with CTRL.ABOVE=1   (foreground overlays)
```

Every layer is independently enableable — a console-style game uses tile layers +
sprites and skips the framebuffer; a framebuffer game skips tiles. This keeps the
per-scanline SDRAM budget (§12) under control by only enabling what's on screen.

- **Backdrop:** copper rewrites `BACKDROP` per scanline for gradients; shown in
  the border and through color-keyed framebuffer holes.
- **Tile layers:** each is a scrolling tilemap (§7). `ABOVE=0` = background (below
  sprites/FB — parallax); `ABOVE=1` = foreground overlay (above sprites — e.g.
  grass the player walks behind). Within a group, TILE0 is nearer the top.
- **Framebuffer:** chunky RGB565 or INDEX8, opaque unless `FB_COLORKEY_EN` and
  the pixel equals `FB_COLORKEY` (then transparent → lower layers show).
- **Sprites:** 4bpp indexed; `BEHIND` composites a sprite below the framebuffer.
  Order among sprites by `PRIORITY`, ties by index.
- Tile & sprite pixels are transparent where their pattern index equals the
  layer's transparent index. Color = `PAL[bank*16 + index]`.

Tiles and sprites are fetched into **double-buffered per-scanline line buffers**
one line ahead (never per-pixel SDRAM lookups). Framebuffer, tile, and sprite
clients pipeline requests into the SDRAM controller's two-entry FIFO, drain
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
  0x0030  backdrop
  0x0040  framebuffer (base/pitch/format/colorkey)
  0x0080  tilemap layers: TILE[0] @0x080, TILE[1] @0x0A0
  0x0400  palette (256 x RGB888)
  0x0800  sprite global (enable/budget/collision)
  0x1000  sprite table (32 x 32 bytes)
  0x2000  bootstrap POST text plane (2700 byte cells)

Chipset map (provisional):
  0xFFF00000 Vesta   0xFFF10000 Astraea   0xFFF20000 Vega   0xFFF30000 Lyra
```

---

## 3. Global registers (0x0000)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0000 | `VEGA_ID` | RO | `0x56454741` | "VEGA" |
| 0x0004 | `VEGA_VERSION` | RO | `0x00030000` | [31:16] major, [15:0] minor |
| 0x0008 | `VEGA_CTRL` | RW | 0 | see below |
| 0x000C | `VEGA_STATUS` | RO | — | see below |
| 0x0010 | `VEGA_IRQ_EN` | RW | 0 | IRQ enable mask |
| 0x0014 | `VEGA_IRQ_STAT` | RW1C | 0 | IRQ pending; write 1 to clear |
| 0x0018 | `VEGA_MODE` | RW | 0 | display mode select |
| 0x001C | `VEGA_CAPS` | RO | — | implemented capability bits |

**`VEGA_CTRL`** `[0]DISPLAY_EN [1]FB_EN [2]SPR_EN [3]FB_COLORKEY_EN
[4]BACKDROP_EN` (tile layers enable per-layer in `TILE_CTRL`).

**`VEGA_STATUS`** `[0]VBLANK [1]HBLANK [2]FLIP_PENDING [3]SPR_OVERFLOW
[4]DISPLAY_READY [5]UNDERRUN [6]CONFIG_ERROR [7]FETCH_BUSY`.

- `DISPLAY_READY` is the synchronized display/HDMI-ready input.
- `UNDERRUN` is sticky and means a requested scanline was not ready when the
  compositor needed it. Write one to status bit 5 to clear it.
- `CONFIG_ERROR` is asserted when an enabled framebuffer, tile layer, or sprite
  descriptor is malformed. The invalid source is skipped rather than allowing
  a wrapped SDRAM request.
- `FETCH_BUSY` covers framebuffer, tile, and sprite scanline construction.

**`VEGA_IRQ_EN`/`STAT`** `[0]VBLANK [1]RASTER [2]COLLISION`.

**`VEGA_CAPS`** `[0]POST_TEXT [1]FRAMEBUFFER [2]PALETTE [3]TILEMAP
[4]SPRITE [5]INDEX8`. Software must test capability bits rather than infer
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

## 5. Backdrop (0x0030)

| Offset | Name | Acc | Description |
|---|---|---|---|
| 0x0030 | `VEGA_BACKDROP` | RW | [23:0] RGB888 backdrop / border color (copper-animate per line) |

---

## 6. Framebuffer (0x0040)

| Offset | Name | Acc | Reset | Description |
|---|---|---|---|---|
| 0x0040 | `VEGA_FB_BASE` | RW | 0 | [24:0] SDRAM byte address (latched at vblank) |
| 0x0044 | `VEGA_FB_PITCH` | RW | 1440 | [15:0] bytes per scanline |
| 0x0048 | `VEGA_FB_FORMAT` | RW | 0 | [2:0] format: 0 RGB565, 1 INDEX8 |
| 0x004C | `VEGA_FB_COLORKEY` | RW | 0 | RGB565 [15:0] or INDEX8 [7:0] transparent value |

**Page flip:** `FB_BASE` writes stage and latch at the next vblank (tear-free);
`STATUS.FLIP_PENDING` high until latched; `VBLANK` IRQ signals done.

Framebuffer base and pitch must be 32-bit aligned. Pitch must be at least
`width*2` for RGB565 or `width` for INDEX8. Unsupported formats and malformed
addresses set `STATUS.CONFIG_ERROR` and suppress framebuffer requests.

---

## 7. Tilemap layers (0x0080) — 2 layers

Two scrolling tile layers, `TILE[0]` @ `0x0080`, `TILE[1]` @ `0x00A0`, 32 bytes
(8 longs) each.

| +Off | Name | Acc | Description |
|---|---|---|---|
| 0x00 | `TILE_CTRL` | RW | control (below) |
| 0x04 | `TILE_MAP` | RW | [24:0] SDRAM byte addr of the tilemap (16-bit entries) |
| 0x08 | `TILE_SET` | RW | [24:0] SDRAM byte addr of the tileset (4bpp patterns) |
| 0x0C | `TILE_SIZE` | RW | [3:0] map width log2 (tiles), [7:4] map height log2 |
| 0x10 | `TILE_SCROLL` | RW | [31:16] scrollY, [15:0] scrollX (pixels) |
| 0x14 | reserved (×3) | — | future: row-scroll table ptr, affine, etc. |

**`TILE_CTRL`**
```
[0]      ENABLE
[1]      TRANSP_EN   0 = opaque fill; 1 = index==TRANSP_IDX is transparent
[2]      TILE16      0 = 8x8 tiles, 1 = 16x16
[3]      WRAP_X      1 = wrap horizontally; 0 = blank outside map width
[4]      ABOVE       0 = background (below sprites/FB); 1 = foreground (above)
[5]      WRAP_Y      1 = wrap vertically; 0 = blank outside map height
[7:6]    reserved
[19:16]  TRANSP_IDX  transparent 4bpp index when TRANSP_EN
[31:20]  reserved
```

**Tilemap** (in SDRAM, 16-bit big-endian entries, row-major of `2^log2W` ×
`2^log2H`): entry address = `MAP + ((ty & (H-1)) * W + (tx & (W-1))) * 2`.
```
tilemap entry:
[9:0]   tile index (0..1023)
[12:10] palette bank (0..7)
[13]    reserved (future per-tile priority)
[14]    flip X
[15]    flip Y
```

**Tileset** (in SDRAM): tile `n` pattern at `SET + n * tile_bytes`, 4bpp linear
(2 px/byte, high nibble = left). `tile_bytes` = 32 (8×8) or 128 (16×16).

Scroll is pixel-granular → smooth hardware scrolling. Map dims are powers of two
so wrap is a mask. `TILE_SCROLL` is copper-safe per scanline → per-line scroll
offsets give parallax bands / wobble effects without CPU.

---

## 8. Palette (0x0400)

256 entries, `VEGA_PAL[i]` at `0x0400 + i*4`, `0x00RRGGBB`. Used by tiles,
sprites, and INDEX8 framebuffer scanout; RGB565 framebuffer pixels bypass it.
Tile/sprite color = `PAL[bank*16 + index]`. Copper-writable mid-frame.

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
limit: all 32 descriptors may be visible when their clipped widths fit.

---

## 10. Sprite table (0x1000)

32 sprites, 32 bytes each. Sprite `i` at `0x1000 + i*0x20`.

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
| `TILE_SCROLL` (line-scroll parallax) | `VEGA_FB_BASE` (auto-latches @vblank) |
| `SPR_*`, `VEGA_RASTER_CMP` | `TILE_MAP`/`TILE_SET`/`TILE_SIZE`, `TILE_CTRL` size bits |

---

## 12. Bandwidth note

Each **active** layer fetches per scanline from the one 16-bit SDRAM (150 MB/s
raw at 75 MHz). A 720x480 output line provides 2383 SDRAM clocks at the locked
75 MHz memory and 27 MHz pixel clocks. Rough per-scanline payload at 720 wide:
- RGB565 framebuffer = 1440 B; INDEX8 framebuffer = 720 B.
- One 8×8 tile layer is approximately 400–550 B with map/pattern reuse.
- Sprites are bounded by `SPR_BUDGET` source pixels per line.

The full CPU/pin-level-SDRAM/HDMI regression measures the following simultaneous
worst-case workloads. Its 32 sprites use unrelated SDRAM rows to prevent cache
locality from hiding command overhead.

| Composition | Effective sprite pixels | Accepted 32px sprites | Worst line | Margin |
|---|---:|---:|---:|---:|
| INDEX8 FB + both tile layers | 1024 | 32 | 2274 clocks | 109 clocks |
| RGB565 FB + both tile layers | 512 | 16 | 2022 clocks | 361 clocks |

Normal diagnostic composition completes in 1346 clocks. The testbench rejects
any line at or beyond the 2383-clock deadline. Video has priority; draw,
blitter, and CPU clients stall before scanout is allowed to miss its
reservation. `STATUS.UNDERRUN` remains a sticky fault indicator, while
`STATUS.SPR_OVERFLOW` reports deterministic sprite admission pressure.

---

## 13. Programming sketches

**Scrolling tile background + page-flipped framebuffer are mutually optional.**

```c
// A parallax background: TILE1 far, TILE0 near, both below sprites.
for (int l = 0; l < 2; l++) {
    VEGA->TILE[l].SET  = tileset_addr;
    VEGA->TILE[l].MAP  = map_addr[l];
    VEGA->TILE[l].SIZE = TILE_MAPSIZE(7, 6);          // 128 x 64 tiles, pow2
    VEGA->TILE[l].CTRL = TILE_ENABLE | TILE_WRAP;     // ABOVE=0 background, 8x8
}
VEGA->CTRL = VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_SPR_EN;

// Per frame: scroll TILE0 at camera speed, TILE1 at half (parallax).
VEGA->TILE[0].SCROLL = TILE_SCROLL(cam_x,      cam_y);
VEGA->TILE[1].SCROLL = TILE_SCROLL(cam_x >> 1, cam_y >> 1);
```

```c
// Build a tilemap entry in SDRAM.
map[ty * mapw + tx] = TILE_ENTRY(tile_id, /*bank*/2, /*flipx*/0, /*flipy*/0);
```

Foreground overlay layer: set `TILE_ABOVE` in its `CTRL`.

---

## 14. Baseline and extension points

**Decided:** 32-bit regs (atomic) · FB color-key→backdrop (copper gradients) ·
sprite `BEHIND` + tile `ABOVE` (layer ordering without a full priority sort) ·
16-bit tilemap entries, 8×8|16×16 tiles, pow2 wrap, pixel scroll · 2 tile layers.

The v0.3 baseline implements framebuffer scanout, palette, two tile layers, 32
sprites, page flipping, collision bitmap, raster state, and copper writes.
Reserved extension points are per-tile priority (entry bit 13), row-scroll
tables, affine tile transforms, and sprite scaling. They are not part of the
v0.3 contract and software must not rely on them.
