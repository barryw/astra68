# Arty graphics RTL

This directory contains the production Zynq PL implementation of the Astra 68
version-1 graphics architecture for the Arty Z7-20. The normative behavioral
contract is [`docs/GRAPHICS_ARCHITECTURE.md`](../../../docs/GRAPHICS_ARCHITECTURE.md);
[`TIMING_CLOSURE.md`](TIMING_CLOSURE.md) records exact source identities,
failed experiments, routed results, release artifacts, and hardware evidence.

The complete graphics subsystem is implemented and hardware-qualified. It is
feature-frozen except for correctness fixes. New graphics features require a
new architecture decision and complete regression, route, and hardware gates.

## Implemented hardware

### Display and composition

- Fixed 1280x720 progressive HDMI output at 60 Hz.
- INDEX8, big-endian RGB565, and XRGB8888 framebuffer scanout.
- Pixel-granular horizontal, vertical, and diagonal framebuffer scrolling,
  with independent X/Y wrapping over a virtual framebuffer.
- Two independently scrolling INDEX4/INDEX8 tile layers using 8x8 or 16x16
  patterns, per-tile palette selection, reflection, transparency, and
  independent X/Y wrapping.
- Ordered composition of tile layers, framebuffer, boot text, and sprite
  planes into RGB888.
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
- Sprite 0 may be reserved by system policy as the hardware pointer; hardware
  does not prevent applications with exclusive scene ownership from using it.
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
- Premultiplied source-over composition with per-command opacity.
- All sixteen two-input Boolean raster operations.
- Supported source/destination format conversions and palette expansion.

### Geometry and fonts

- Clipped lines and outlined or filled rectangles, circles, and ellipses.
- Transparent and opaque 8x8 pattern fills.
- Bounded scanline flood fill using caller-provided, validated workspace;
  exhaustion reports `WORK_OVERFLOW` without corrupting adjacent memory.
- Hardware AFNT glyph expansion for MASK1, A4, A8, INDEX4, and INDEX8 glyph
  data through the same writer, blend, completion, timeout, and reset paths.
- The four-row CP437 boot-text plane remains available for diagnostics before
  the general command processor is usable. It is separate from AFNT.

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

1. Framebuffer, tile, and sprite engines snapshot one scanline's scene state.
2. Map and source fetchers issue bounded 64-bit AXI bursts into the reserved
   graphics arena.
3. Framebuffer, descriptor, pattern, palette, tile, and sprite stores build
   complete line slots.
4. The compositor resolves the ordered layers and sprite planes into RGB888.
5. HDMI consumes only a complete, correctly tagged line slot.

Linux reserves the contiguous 128 MiB physical range
`0x18000000..0x1fffffff` as `no-map` graphics memory. Framebuffers, sprite
pixels, tile maps and patterns, render surfaces, command data, and AFNT data
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
| `0x154` | R | Rows, columns, row pitch, and cell width: `0x04242010`. |
| `0x158` | R | Y/X origin: `0x01f00108` (`x=264`, `y=496`). |

The complete register, descriptor, command, scene, and timing-class contracts
are defined in the architecture and generated public headers. Do not infer a
software ABI from internal RTL signals or historical checkpoint offsets.

## Verification

Run the complete directed suite on Beast or NUC:

```sh
fpga/arty/graphics/run_tests.sh
```

The suite covers framebuffer formats and byte order, AXI boundaries and
backpressure, tile phases and scrolling, scene promotion, all 64 sprites,
every legal sprite source width and height, clipping and rejection, scaling,
reflection, blending, collisions, CDC publication skew, command validation,
ring backpressure, completion and reset, the complete blitter, virtual-sprite
groups, geometry, pattern and bounded flood operations, every AFNT format,
copper execution and timing classes, and integrated pipeline behavior.

Component routes remain useful for attribution but are not release evidence:

```sh
fpga/arty/graphics/run_ooc.sh
ASTRA_OOC_COMPONENT=tile-line fpga/arty/graphics/run_ooc.sh
```

Set `ASTRA_OOC_OUT` to a durable output directory. Generated simulation,
synthesis, route, and test artifacts must not be committed.

## Qualified release

The clean from-source Vivado 2024.2 production flow routes the exact complete
design with no failed, unrouted, partially routed, or overlapping nets and
generates a valid bitstream. The retained result has:

| Gate | Result |
|---|---:|
| Setup slack | +0.036 ns |
| Hold slack | +0.016 ns |
| Slice LUTs | 37,534 / 53,200 (70.55%) |
| LUT as memory | 5,025 / 17,400 (28.88%) |
| Slice registers | 44,655 / 106,400 (41.97%) |
| Physical slices | 13,036 / 13,300 (98.02%) |
| BRAM36 | 118 / 140 (84.29%) |
| DSP48E1 | 83 / 220 (37.73%) |

Ten consecutive Arty hardware certifications pass the copper, renderer, and
sprite suites. The FPGA manager reports `operating`; HDMI, splash readback,
scene promotion, renderer output, sprite output, and copper behavior are
verified on the physical board.

Only 264 physical slices and 22 BRAM36 blocks remain. BRAM is the primary
future capacity limit and placement is tight, so nominal unused LUTs are not
evidence that another PL feature will route. See
[`TIMING_CLOSURE.md`](TIMING_CLOSURE.md) and
[`docs/FPGA_RESOURCE_BUDGET.md`](../../../docs/FPGA_RESOURCE_BUDGET.md) for
the exact release identity and capacity history.
