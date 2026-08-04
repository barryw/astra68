# Arty graphics RTL

This directory contains the active Zynq PL implementation of the graphics
contract in `docs/GRAPHICS_ARCHITECTURE.md`. It does not reuse the ULX3S memory
bus or its 720-pixel scanline assumptions.

## Pipeline boundary

The implemented real-time read side is divided into independently testable
stages:

1. The framebuffer, two tile walkers, and sprite engine snapshot one
   scanline's state.
2. Map and source fetchers convert that state into bounded 64-bit AXI bursts.
3. Framebuffer, descriptor, pattern, palette, and sprite stores construct
   complete line slots.
4. The compositor resolves both tile layers, the framebuffer, and front/behind
   sprite planes into RGB888.
5. The HDMI timing path consumes only complete line slots.

The drawing/command engines remain part of the architecture contract but are
not yet implemented by this checkpoint.

`astra_tile_span_walker.sv` is the first retained production component. It
handles signed pixel scroll, independent wrapping, nonwrapped transparent
spans, 8x8 and 16x16 geometry, map indices, and backpressure. It deliberately
does not know about AXI so memory latency cannot alter map semantics.

The walker advances a raw tile coordinate instead of recomputing
`scroll + screen_x` for every span. The exact Beast Vivado 2024.2 OOC route at
200 MHz uses 197 LUTs and 104 flip-flops and has 0.263 ns setup slack. This is
component evidence only; top-level timing and every memory-facing stage remain
separate gates.

The maximum accepted line is bounded at 241 spans for 8x8 tiles and 121 spans
for 16x16 tiles. The fetch stage must preserve span order and may coalesce map
or pattern reads only when doing so produces the same records.

`astra_tile_line_builder.sv` fetches ordered map descriptors and pattern rows,
contains malformed descriptors and AXI failures, and publishes only complete
line slots. Its compositor expands four pixels per 200 MHz build clock into a
quad-wide line store. Under the final test latency profile, a worst-case
1280-pixel INDEX8 line takes 1,346 clocks against the 4,444-clock 720p line
deadline, leaving 3,098 clocks of measured margin. At 200 MHz this is
6.73 microseconds of work in a 22.22-microsecond line period.

The exact complete builder routes out of context on Beast with Vivado 2024.2
at the 5.000 ns build-clock constraint. It has -0.085 ns setup slack on two
endpoints, +0.027 ns hold slack, and uses 1,794 LUTs, 1,428 registers, six
BRAM36s, and no DSPs. The OOC ports deliberately lack final I/O delays and PS
clock placement; this is measured integration risk, not top-level signoff or a
timing waiver. See [`TIMING_CLOSURE.md`](TIMING_CLOSURE.md) for source hashes,
every measured failed cone, final path data, and OOC limitations.

`astra_arty_graphics_top.sv` now connects the production PS DDR/AXI paths and
line pipeline to the qualified 1280x720p60 transport. Exact Beast checkpoint
`full8` fully routes the then-current scanout design at 200 MHz with +0.049 ns
setup and +0.016 ns hold slack. It uses 12,485 total LUTs, 12,677 registers,
29.5 BRAM36-equivalent tiles, and five DSPs. The active Arty boot reserves the
128 MiB graphics arena, writes and verifies the exact RGB565 splash, and
promotes scene generation 1 with zero deferrals. Physical HDMI displays the
exact splash without corruption. See [`TIMING_CLOSURE.md`](TIMING_CLOSURE.md)
for exact source, route, artifact, and boot identities.

Checkpoint `boot-text6` supersedes `full8` as the active scanout release. It
adds a 36x4-cell hardware CP437 plane over a text-free splash. Software writes
the inactive bank and submits an atomic commit; hardware swaps banks only at
vertical blank and clones the visible bank back into shadow storage so later
row-only updates preserve the rest of the panel. The exact complete design
routes at 200 MHz with +0.002 ns setup, +0.019 ns hold, and +0.538 ns
pulse-width slack. It uses 13,096 LUTs, 12,892 registers, 29.5 BRAM36
equivalents, and five DSPs. The active board boot reads back the complete
1,843,200-byte background, publishes four real status rows, and accepts a live
row update through generation 3.

Checkpoint `sprite64-cdc-full2` supersedes `boot-text6` as the active release.
It adds all 64 INDEX8 sprites, 128x128 source images in DDR, sixteen selectable
ARGB palette banks, scaling, signed positioning and complete clipping,
front/behind planes, opacity and alpha, atomic scene promotion, and all-pairs
collision reporting. A bundled-data line-publication mailbox captures slot
metadata one pixel clock after its synchronized toggle; directed skew testing
and physical HDMI qualification prove that stale line tags cannot cause
scanline-wide sprite flicker. The complete route meets timing at +0.024 ns
setup and +0.034 ns hold using 21,954 LUTs, 23,003 registers, 85.5 BRAM36
equivalents and 51 DSPs. Hardware stress, fully off-screen, clipped and aligned
scenes pass with no drops, overflow, AXI errors or deadline errors.

## Boot-text MMIO

The control aperture begins at physical address `0x43c00000`. Boot-cell bits
7:0 select CP437; bits 9:8 select cyan, amber, white, or red. Bits 15:10 must
be zero. The boot text plane is an early-host diagnostic surface, not the
general AFNT glyph engine specified for Astraea.

| Offset | Access | Contract |
|---:|---|---|
| `0x140` | R/W | Shadow enable in bit 0; all other bits zero. |
| `0x144` | R/W | Cell selector 0..143. |
| `0x148` | W | Cell value; successful writes auto-increment the selector without wrapping. |
| `0x14c` | R/W | Read bits 0/1/2 as write-ready, commit-ready, active; write exactly 1 to request vblank commit. |
| `0x150` | R | Commit generation. |
| `0x154` | R | Rows, columns, row pitch, and cell width: `0x04242010`. |
| `0x158` | R | Y/X origin: `0x01f00108` (`x=264`, `y=496`). |

## Test

Run on Beast or NUC with:

```sh
fpga/arty/graphics/run_tests.sh
```

The directed test suite covers aligned, unaligned, positive, negative,
wrapped, independently unwrapped, backpressured, 8x8, 16x16, extreme-scroll,
invalid configuration, malformed descriptors, AXI failures, stable AXI read
responses under backpressure, all tile phases, flips, transparency,
framebuffer byte order and formats, 4 KiB AXI boundaries, palette/alpha
composition, exhaustive sprite scaling, all 64 maximum-size sprites, complete
off-screen clipping, sprite overflow/deadline/AXI containment, collision,
scheduler recovery and publication skew, atomic control promotion,
complete-line publication, the line deadline, boot-text mailbox flow control,
shadow isolation, vblank bank swap, clone preservation, and integrated hardware
glyph composition. The exact top-level PS/AXI design, Linux graphics
reservation, boot loader, and sprite subsystem have routed and board evidence.
Copper, blitter/virtual sprites, geometry/fill, AFNT glyph expansion, command
execution, and their complete-graphics release gates remain pending.

For a routed out-of-context `xc7z020clg400-1` checkpoint at a 200 MHz fabric
target, run on Beast:

```sh
fpga/arty/graphics/run_ooc.sh
```

Set `ASTRA_OOC_OUT` to select a durable output directory. The default
checkpoint measures only the span walker. It is not evidence that the complete
tile engine or graphics complex fits or meets timing.

Route the complete tile-line builder, including AXI fetch state, caches,
four-slot line RAM, and the 74.25 MHz read port, with:

```sh
ASTRA_OOC_COMPONENT=tile-line fpga/arty/graphics/run_ooc.sh
```
