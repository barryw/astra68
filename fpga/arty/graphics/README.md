# Vega graphics RTL

This historical path contains the shared production RTL used by the DE25-Nano
Astra shell. The historical Arty design is recorded in
[`docs/GRAPHICS_ARCHITECTURE.md`](../../../docs/GRAPHICS_ARCHITECTURE.md);
[`fpga/de25/TIMING_CLOSURE.md`](../../de25/TIMING_CLOSURE.md) records current source identities,
failed experiments, routed results, release artifacts, and hardware evidence.

The deployed graphics subsystem is hardware-qualified. Tile hardware is
retired from the current source; its replacement SDL rendering work requires
complete regression, route, and hardware gates before release.

## Implemented hardware

### Display and composition

- Fixed 1920x1080 progressive HDMI output at 60 Hz.
- Frame-atomic logical scene modes with hardware nearest-neighbor scaling,
  symmetric crop, letterboxing, integer enlargement, and fractional fit.
- INDEX8, big-endian RGB565, and XRGB8888 framebuffer scanout.
- Pixel-granular horizontal, vertical, and diagonal framebuffer scrolling,
  with independent X/Y wrapping over a virtual framebuffer.
- Ordered composition of framebuffer, boot text, and sprite
  planes into RGB888, followed by exact repeated-line replay and a native
  32x32 ARGB hardware-pointer plane.
- Triple-buffered scene metadata and fenced, atomic frame-boundary promotion.
  Applications do not perform visible-state writes or wait directly for
  vblank.
- Four-line scheduling and complete-line publication so HDMI never consumes a
  partially built line.

### Sprites

- 64 simultaneous hardware sprites.
- Independent INDEX8 sources from 1x1 through 128x128 pixels in reserved DDR.
- Sixteen shared 256-entry ARGB palette banks, independently selected by each
  sprite.
- Signed positioning, complete off-screen rejection, edge clipping,
  nearest-neighbor scaling, X/Y reflection, priority, front/behind placement,
  opacity, and source alpha.
- All-pairs collision reporting.
- All 64 scene sprites remain available when the independent hardware pointer
  is enabled or disabled. Scene sprites scale with the logical display mode;
  the hardware pointer never scales.
- Virtual sprites are bounded groups of ordinary blits into hidden surfaces.
  They inherit normal validation, timeout, completion, reset, and fence rules.

### Command processor and blitter

- Bounded submission and completion rings with monotonic fences,
  backpressure, validation, finite deadlines, diagnostics, cancellation, and
  engine reset.
- Shared clipped pixel writer for INDEX8, RGB565, XRGB8888, and ARGB8888
  sources where the operation permits them.
- Fill and overlap-safe copy.
- Independent X/Y scaling and reflection.
- Color-keyed and MASK1-masked copies.
- Straight-alpha source-over (SDL BLEND) with per-command opacity.
- All sixteen two-input Boolean raster operations.
- Supported source/destination format conversions and palette expansion.
- Latency-tolerant transport: commands are fetched up to 32 per burst into
  on-chip RAM; a surface descriptor the previous command also referenced is
  reused without a DDR read (valid only within one doorbell; see
  `docs/GRAPHICS_ARCHITECTURE.md`); pixel writes are posted and a command's
  completion record is written once its writes are answered, so no command
  waits on a memory round trip. `perf/run_ordering.sh` checks the ordering
  against a memory whose writes land late and whose responses reorder.
- Fills and plain copies stream through one burst mover with eight source
  chunks read ahead, about three RGB565 pixels per clock at any DDR latency.
  Unscaled format-converting and source-over BLITs (RGB565, XRGB8888,
  ARGB8888) stream through the same mover at one pixel per clock; a chunk of
  opaque source pixels at opacity 255 skips its destination read.

### Geometry and fonts

- Clipped lines and outlined or filled rectangles, circles, and ellipses.
- Transparent and opaque 8x8 pattern fills.
- Bounded scanline flood fill using caller-provided, validated workspace;
  exhaustion reports `WORK_OVERFLOW` without corrupting adjacent memory.
- Hardware AFNT glyph expansion for MASK1, A4, A8, INDEX4, and INDEX8 glyph
  data through the same writer, blend, completion, timeout, and reset paths.
- Texture engine (`TRIANGLES`, docs/TEXTURE_ENGINE.md): exact top-left
  rasterization, affine u/v/color interpolation, nearest and bilinear
  sampling of RGB565, XRGB8888, ARGB8888, INDEX8 and A8, color modulation,
  and NONE/BLEND/ADD/MOD/MUL into RGB565, XRGB8888 and ARGB8888. Its oracle
  is `sw/userspace/graphics/src/texture_reference.c`.
- ARGB8888 is a destination for every operation.
- The four-row CP437 boot-text plane remains available for diagnostics before
  the general command processor is usable. It uses the generated true 8x16
  Spleen rescue strike and is separate from the AFNT command path.

### Copper

- Two BRAM-backed banks of 4096 instructions each.
- Beam WAIT and SKIP.
- Validated MOVE through a register whitelist.
- IRQ generation and render-command dispatch.
- Hardware-enforced pixel-boundary, next-scanline, and next-vblank register
  timing classes.
- Baseline restoration at the frame boundary before execution of the next
  copper list.

## Pipeline and memory boundary

The real-time scanout side is divided into bounded stages:

1. Framebuffer and sprite engines snapshot one scanline's scene state.
2. Map and source fetchers issue bounded 64-bit AXI bursts into the reserved
   graphics arena.
3. Framebuffer, descriptor, palette, and sprite stores build
   complete line slots.
4. The compositor resolves the ordered layers and sprite planes into RGB888.
5. The scaler maps the fixed physical beam to logical source coordinates and
   replays completed composed lines when vertical enlargement repeats a row.
6. The native hardware-pointer plane composites after scaling.
7. HDMI consumes the aligned RGB/timing bundle at 1920x1080p60.

A compiled window-scene line is fetched as a stream, not span by span: the
line's span records arrive in bursts into a 16-entry descriptor ring, an issue
walker requests each source span's pixels under the beat-FIFO credit, and the
writer fills solid spans and writes source spans behind it. A line costs its
write cycles (960 at two pixels a clock for 1920 pixels) plus about three
memory round trips, whatever its span count. The serial form paid two round
trips a span; on the DE25's LPDDR4B (~90-100 build cycles a round trip under
render load) eleven spans overran the 2,442-cycle line period and the line
scheduler blacked out every line until the spans thinned out.
`tb_astra_framebuffer_scene_latency` holds an eleven-span line to its budget
against a pipelined 100-cycle memory.

Linux reserves the contiguous 128 MiB physical range
`0x18000000..0x1fffffff` as `no-map` graphics memory. Framebuffers, sprite
pixels, render surfaces, command data, and AFNT data
live there rather than in PL block RAM. PL RAM is reserved for bounded line
working sets, metadata banks, queues, palettes, boot text, and copper stores.

The Linux host owns the PL aperture, IRQs, reserved memory, and ARM/PL cache
transitions. The Astra display service owns graphics policy. Guest
applications receive validated handles and never receive physical addresses or
writable mappings of active scanout resources.

## Boot-text MMIO

The control aperture begins at physical address `0x43c00000`. Boot-cell bits
7:0 select CP437; bits 9:8 select cyan, amber, white, or red. Bits 15:10 must
be zero. This plane is an early-host diagnostic surface, not the AFNT engine.

| Offset | Access | Contract |
|---:|---|---|
| `0x140` | R/W | Shadow enable in bit 0; all other bits zero. |
| `0x144` | R/W | Cell selector 0..143. |
| `0x148` | W | Cell value; successful writes auto-increment without wrapping. |
| `0x14c` | R/W | Read bits 0/1/2 as write-ready, commit-ready, active; write exactly 1 to request vblank commit. |
| `0x150` | R | Commit generation. |
| `0x154` | R | Rows, columns, row pitch, and cell width: `0x04243018`. |
| `0x158` | R | Y/X origin: `0x02e8018c` (`x=396`, `y=744`). |

The complete register, descriptor, command, scene, and timing-class contracts
are defined in the architecture and generated public headers. Do not infer a
software ABI from internal RTL signals or historical checkpoint offsets.

## Verification

Run the complete directed suite on Beast:

```sh
fpga/arty/graphics/run_tests.sh
```

The suite covers framebuffer formats and byte order, AXI boundaries and
backpressure, framebuffer scrolling, scene promotion, all 64 sprites,
every legal sprite source width and height, clipping and rejection, scaling,
reflection, blending, collisions, CDC publication skew, command validation,
ring backpressure, completion and reset, the complete blitter, virtual-sprite
groups, geometry, pattern and bounded flood operations, every AFNT format,
copper execution and timing classes, and integrated pipeline behavior.

Component routes remain useful for attribution but are not release evidence:

```sh
fpga/arty/graphics/run_ooc.sh
ASTRA_OOC_COMPONENT=sprite-line fpga/arty/graphics/run_ooc.sh
```

Render-engine cycle accounting under a DDR-like latency model (per-command
cost, fill/copy per pixel, one SDL testdraw2 frame) and Quartus area per
engine:

```sh
fpga/arty/graphics/perf/run_perf.sh 100          # read/write latency 100
fpga/arty/graphics/perf/run_ooc_q.sh /tmp/render-ooc
```

Set `ASTRA_OOC_OUT` to a durable output directory. Generated simulation,
synthesis, route, and test artifacts must not be committed.

## Qualified release

Only the exact DE25 build that passes complete simulation, full-route timing,
artifact hashing, deployment, repeated boot, and physical HDMI checks is a
release. Current identities and capacity are recorded in
[`fpga/de25/TIMING_CLOSURE.md`](../../de25/TIMING_CLOSURE.md), not duplicated
here.
