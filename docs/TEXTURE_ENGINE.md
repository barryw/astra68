# Astraea texture engine: triangles, modulation, blend modes, ARGB targets, readback

Status: contract accepted 2026-09-26. RTL for §1–§7 implemented and
simulation-verified against the reference model; a full DE25 production
route closes every clock with it (`fpga/de25/TIMING_CLOSURE.md`,
2026-09-26, 98 % ALMs). Not yet run on hardware. §8 is software. This
completes the SDL2 2D renderer in hardware (`docs/MANAGED_GRAPHICS.md` §7).

One new engine, not one per SDL feature. Everything SDL needs beyond the
existing blitter is an affine textured, color-interpolated triangle:

| SDL feature | Lowering |
|---|---|
| `SDL_RenderGeometry` | triangles as given |
| `SDL_RenderCopyEx`, any angle | two triangles |
| texture color modulation | two triangles, vertex color = modulation |
| ADD, MOD, MUL blend (copies and fills) | two triangles with that blend |
| linear scale mode | two triangles, `FILTER_LINEAR` |

Plain opaque/alpha copies keep using the blitter's faster path.

## 1. TRIANGLES command (`ASTRA_RENDER_OP_TRIANGLES = 768`)

Common 64-byte command header (words 0–7) as every opcode. Words 8–15:

| Word | Meaning |
|---:|---|
| 8 | destination surface descriptor |
| 9 | source surface descriptor, or 0 for untextured |
| 10 | arena offset of the vertex array, 32-byte aligned |
| 11 | triangle count, 1..4096 |
| 12 | options (below) |
| 13–15 | zero |

Command flags (16 bits) must be zero. Options word 12:

| Bits | Field |
|---:|---|
| 2:0 | blend: 0 NONE, 1 BLEND, 2 ADD, 3 MOD, 4 MUL; 5–7 rejected |
| 3 | `FILTER_LINEAR` (textured only; must be 0 when untextured) |
| 31:4 | zero |

The vertex array holds `3 × count` vertices, 32 bytes each, big-endian:

| Word | Meaning |
|---:|---|
| 0 | x, signed 24.8 fixed-point destination pixels |
| 1 | y, signed 24.8 |
| 2 | u, signed 16.16 fixed-point source texels (0 when untextured) |
| 3 | v, signed 16.16 |
| 4 | color `0xAARRGGBB`, straight alpha |
| 5–7 | zero |

The whole array must lie inside one validated allocation (the batch data
arena). Validation happens before the first DMA; a rejected command writes
nothing (`BAD_RANGE`, `BAD_FLAGS`, `BAD_DESCRIPTOR`, `UNSUPPORTED`).

Vertex `x` and `y` must lie in `[-2^23, 2^23)` (±32768 destination pixels);
this bound keeps every edge function and attribute exact in 52-bit and
88-bit hardware arithmetic. The rejection order and codes are:

| Check | Status | Where | Fault detail |
|---|---|---|---|
| command flags ≠ 0 | `BAD_FLAGS` | command processor | word 1 |
| options bits 31:4, blend 5–7, `FILTER_LINEAR` untextured, words 13–15 ≠ 0 | `BAD_FLAGS` | command processor | `0x00030005` |
| count 0 or > 4096, offset not 32-byte aligned, array past the arena | `BAD_RANGE` | command processor | `0x00030004` |
| a descriptor is misplaced or invalid; INDEX8 source without palette | `BAD_DESCRIPTOR` | command processor | descriptor offset |
| array overlaps a ring, descriptor, destination, source, palette or protected range; texture overlaps the destination | `BAD_RANGE` | command processor | `0x00060001` / `0x00060002` |
| destination not RGB565/XRGB8888/ARGB8888, source not RGB565/XRGB8888/ARGB8888/INDEX8/A8 | `UNSUPPORTED` | engine, before any DMA | `0x000c0001` |
| a vertex outside the range, reserved words 5–7 ≠ 0, or u/v ≠ 0 untextured | `BAD_RANGE` | engine prepass, before any write | `0x000c4000` + vertex index |

The engine reads the whole vertex array once to check it before the pixel
writer starts, then again per triangle while drawing.

## 2. Rasterization

- Pixel (px, py) is covered when its center (px+0.5, py+0.5) is inside the
  triangle. Edge functions are evaluated exactly in integer arithmetic on the
  24.8 coordinates.
- Ties use the top-left rule: a center exactly on a top edge or a left edge is
  covered; on a bottom or right edge it is not. Shared edges are therefore
  drawn exactly once, which matters for BLEND/ADD. With y growing downward,
  a top edge is horizontal with the interior below it and a left edge has the
  interior to its right.
- Winding is irrelevant (both orientations draw). Zero-area triangles draw
  nothing.
- Coverage is limited to the command clip ∩ destination bounds; the engine
  never visits pixels outside it (bounding box first, then clipped).

## 3. Attribute interpolation

Affine (screen-space) barycentric interpolation at the pixel center of u, v
and each color channel. Barycentric weights are computed from the same exact
edge functions; interpolated values are rounded to nearest (u, v keep 16.16
precision internally; color channels become 8-bit). Exactly:
`floor((w0·a0 + w1·a1 + w2·a2) / A + 1/2)` with `A = w0 + w1 + w2 > 0`, so
ties round toward +∞.

## 4. Sampling (textured)

Texel (tx, ty) covers source [tx, tx+1) × [ty, ty+1). Addressing clamps to the
whole source surface (`0 ≤ tx < width`, `0 ≤ ty < height`).

- Nearest: tx = floor(u), ty = floor(v), then clamp.
- Linear: sample at (u − 0.5, v − 0.5); x0 = floor, fx = 8-bit fraction;
  clamp each of the four taps independently; blend per channel as
  `((a·(256−fx) + b·fx)·(256−fy) + (c·(256−fx) + d·fx)·fy + 32768) >> 16`.

Source formats: RGB565, XRGB8888 (alpha = 255), ARGB8888, INDEX8 with its
palette (as the blitter), A8 (RGB = 255, alpha = coverage). Direct formats
expand to 8 bits by bit replication.

## 5. Modulation

`src = tex × color` per channel with `(a·b + 127) / 255` exact rounding;
untextured `src = color`.

## 6. Blend (straight alpha, exact /255 rounding, saturate at 255)

Let `m(a,b) = (a·b + 127) / 255`.

| Mode | RGB | Alpha (ARGB8888 destinations) |
|---|---|---|
| NONE | `src.rgb` | `src.a` |
| BLEND | `m(src.rgb, src.a) + m(dst.rgb, 255 − src.a)` | `src.a + m(dst.a, 255 − src.a)` |
| ADD | `min(255, m(src.rgb, src.a) + dst.rgb)` | `dst.a` |
| MOD | `m(src.rgb, dst.rgb)` | `dst.a` |
| MUL | `min(255, m(src.rgb, dst.rgb) + m(dst.rgb, 255 − src.a))` | `dst.a` |

These are SDL2's definitions. RGB565 and XRGB8888 destinations read dst.a as
255 and store no alpha (XRGB8888 writes its X byte as 0xFF). Destination
conversion rounds to nearest (RGB565 as `(c·31 + 127)/255`,
`(c·63 + 127)/255`). INDEX8 destinations are rejected.

Overlapping triangles in one command blend against each other: before a
triangle that reads the destination (every mode except NONE), the engine
waits on a writer barrier for all earlier pixel writes.

## 7. ARGB8888 render targets

ARGB8888 becomes a valid destination for every op:

- FILL, LINE and the geometry ops write the full canonical `AARRGGBB` value.
- BLIT copy writes source alpha (sources without alpha write 255).
- BLIT with `FLAG_BLIT_ALPHA` uses the BLEND row above, including the alpha
  equation, with `src.a` replaced by `m(src.a, opacity)`. This is straight
  alpha for every destination: it replaces the former premultiplied
  source-over, so ARGB8888 and palette sources whose alpha is below 255 now
  blend as SDL does. Opaque sources are unchanged.
- GLYPH_RUN coverage blending keeps the destination alpha on ARGB8888:
  `out.a = c + m(dst.a, 255 − c)`, where `c` is A8 coverage, A4 coverage
  × 17, or the palette alpha; RGB is unchanged.
- Flood fill compares and replaces the full 32-bit ARGB value.

Surfaces with `DRAW_TARGET` may then be ARGB8888 in the display service and
NDK.

## 8. Readback (software path; no RTL)

New display operation `ASTRA_DISPLAY_FRAME_READ_SURFACE`: the helper copies a
rectangle of one validated Media RAM surface out of the graphics arena into
the mailbox payload; on completion QEMU copies the payload into the guest DMA
buffer named by the request. The display service then copies it into the
client's staging area (`ASTRA_GUI_GRAPHICS_SURFACE_READ`), and the NDK exposes
`astra_surface_read()`. `SDL_RenderReadPixels` uses it, converting format in
SDL only when the caller asks for a different one.

## 9. Reference model and gates

`sw/userspace/graphics/src/texture_reference.c` is the bit-exact C model of
§2–§6 (coverage, interpolation, sampling, modulation, blending, conversion).
It is the oracle for:

- RTL simulation: directed and randomized triangles, every blend × format ×
  filter, shared-edge tests (no double coverage), clip tests, rejected
  commands write nothing.
- The DE25 certification tool (`fpga/arty/linux/astra_render_certify.c`),
  comparing arena memory after real execution.

A production route must close every clock with the full feature set; timing
and resources are recorded in `fpga/de25/TIMING_CLOSURE.md`.

## 10. Hardware implementation

`fpga/arty/graphics/astra_render_texture.sv` is the engine; the command
processor validates TRIANGLES exactly as it does GLYPH_RUN (the vertex array
takes the glyph descriptor array's place in every range check) and routes the
engine through the shared pixel writer, the shared HP2 read path (AXI ID 7)
and the writer barrier.

- **Coverage.** Three biased edge functions (52-bit) are evaluated once per
  triangle with a 27×27 DSP multiplier and stepped by addition across the
  clipped bounding box, one pixel per four cycles; the top-left rule is a −1
  bias on non-top-left edges, so coverage is three sign bits.
- **Interpolation without division.** Each attribute carries
  `(Q, R)` with `2N + A = Q·2A + R`, `0 ≤ R < 2A`, stepped Bresenham-style
  (50-bit remainders). Setup costs three 88-cycle divisions per attribute that
  varies across the triangle; a constant attribute costs none. Q is kept
  modulo 2³² (u, v) or 2⁸ (color), which is exact at every covered pixel.
- **Pixel pipeline.** One covered pixel at a time: up to four texel taps
  (zero-weight taps are skipped, so nearest is one tap), a two-entry texel
  beat cache (one per tap row), a one-entry palette cache, and a one-entry
  destination cache that merges its own writes. Modulation, blending and the
  bilinear taps share one bank of eight 8×17 DSP multipliers and the exact
  `(x + 127)/255` divider used by the glyph engine.

Measured cost (simulation, `tb_astra_render_texture +texture_perf`, AXI
model with a few cycles of read latency) for a 20×14-pixel sprite quad drawn
as two triangles, including setup and the visited but uncovered half of each
bounding box:

| Configuration | Cycles/pixel | At 165 MHz |
|---|---:|---:|
| untextured, NONE, XRGB8888 | 19 | 8.7 Mpixel/s |
| nearest, NONE, XRGB8888 | 47 | 3.5 Mpixel/s |
| bilinear, NONE, XRGB8888 | 74 | 2.2 Mpixel/s |
| bilinear, BLEND, ARGB8888 | 77 | 2.1 Mpixel/s |

Per covered pixel the pipeline is about 14 (untextured), 38 (nearest) and
65 (bilinear) cycles; each uncovered bounding-box pixel costs 4, RGB565
destinations add 3, and every texel, palette or destination beat that misses
its cache adds one DDR read round trip on hardware. Triangle setup is about
100 cycles plus about 300 per attribute that varies across the triangle (a
textured quad with constant color: ~700 cycles per triangle). This is a
correctness-first sequential engine: plain and alpha copies should stay on
the blitter's faster path, and many tiny triangles pay setup. The upgrade
path is a texel cache with several outstanding reads.

`tb_astra_render_texture` replays 194 model-generated cases (every blend ×
destination format × filter × source format, randomized and directed:
shared edges, fans, overdraw, clipping, zero area, ±32768-pixel and 16.16
limit coordinates, slivers, overlapping blended triangles under long write
stalls, and rejections) and compares every destination byte and the whole
memory image with `texture_reference.c`. `tb_astra_render_command_processor`
covers TRIANGLES validation and ARGB8888 destinations for FILL, LINE, BLIT
copy, BLIT alpha and A8 glyphs. `fpga/arty/linux/astra_render_certify.c`
repeats the model comparison on hardware (`ASTRA_TEXTURE PASS`).
