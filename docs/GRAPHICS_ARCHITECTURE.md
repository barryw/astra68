# Astra 68 graphics architecture

## Status and authority

This document is the normative product and hardware architecture for the
Astra 68 graphics complex on the Arty Z7-20 target. It freezes the version-1
feature set and the invariants that the RTL, Linux host driver, QEMU device,
Axiom display boundary, Astra OS display service, and NDK must preserve.

The logical chip names remain:

- **Vega**: display timing, scanout, palettes, scene state, sprites, collision,
  and frame completion.
- **Astraea**: command processor, blitter, geometry, glyph expansion, flood
  fill, and copper.

They may be implemented as one AXI-connected PL subsystem. Their ownership and
failure boundaries remain distinct even if physical implementation is shared.

`VEGA.md` v0.5 and `ASTRAEA.md` v0.4 describe the implemented ULX3S chipset and
remain historical implementation evidence. They are not the Arty register or
capacity contract. `PRESENTATION.md` remains authoritative for the tear-free
scene invariant where it does not conflict with the dimensions, formats, and
transport defined here.

`ndk/include/astra/graphics.h` now reflects the Arty hardware boundary,
including two tile layers, 64 sprites, independent 1..128 sprite source
extents, the 16-sprite/2048-pixel line budget, palette banks, drawing capabilities,
glyphs, bounded flood fill, raster programs, and fences. It remains a draft
service API until the display service implementation and ABI tests promote it;
the RTL register layout is not the application ABI.

This document freezes architectural behavior, not implementation status. A
feature is not production-ready until it passes the release gates in this
document on the exact routed bitstream.

### Arty direct-HDMI mode decision

Version 1 uses fixed 1280x720p60 output. This replaces the earlier 1080p60
proposal after qualification against the exact `XC7Z020-1CLG400C` SelectIO
limits.

AMD PG230 gives the standard 1080p60 TMDS rate as 1.485 Gb/s per data lane with
a 148.5 MHz pixel/TMDS clock. A 10:1 DDR OSERDESE2 implementation therefore
requires a 742.5 MHz serializer clock. UG471 permits the OSERDESE2 high-speed
clock through BUFIO or through a matched MMCM/PLL clock-buffer arrangement.
DS187 characterizes the `-1` speed grade at 600 MHz maximum for BUFIO and
464 MHz maximum for BUFG. Its published, fully characterized DDR LVDS
transmitter result using OSERDES is 950 Mb/s; it publishes no 1.485 Gb/s
SelectIO TMDS production value for this part. The Arty HDMI source is connected
directly to PL SelectIO and has no external serializer or transceiver that can
remove the clock limit.

Consequently, 1080p60 is 23.75 percent above even the faster valid BUFIO clock
limit and requires a lane rate 56.3 percent above AMD's characterized DDR
OSERDES result. A design may appear to work on one board, but it cannot meet
the project's production reliability requirement. Routing success or a short
hardware test does not override the device data sheet.

The selected 1280x720p60 mode is CEA VIC 4. AMD UG934 and PG235 specify 1280
active pixels by 720 active lines, 1650 total pixels by 750 total lines, and a
74.25 MHz pixel clock. It uses a 742.5 Mb/s TMDS lane rate and a 371.25 MHz DDR
serializer clock. The exact transport shell derives both clocks from the Zynq
100 MHz FCLK0 with one MMCME2:

```text
VCO       = 100 MHz * 37.125 / 5 = 742.5 MHz
TMDS x5   = 742.5 MHz / 2        = 371.25 MHz
pixel     = 742.5 MHz / 10       = 74.25 MHz
```

Vivado 2024.2 fully routes that shell on the exact `xc7z020clg400-1` with
+5.393 ns setup slack, +0.160 ns hold slack, +0.538 ns pulse-width slack, no
failing endpoints, no routing errors, and no methodology findings. Physical
Arty HDMI displays the complete 720p test raster. This qualifies the fixed
timing, clocking, reset, TMDS encoding, serialization, pins, and board output;
it was the transport-only qualification checkpoint.

Historical checkpoint `boot-text6` first connected that transport to the real
PS DDR paths, validated framebuffer and two tile layers, palettes, compositor,
scheduler, frame-boundary scene promotion, counters, and a boot-only CP437
status plane. The promoted version-1 release now also includes all 64 sprites,
bounded command and completion transport, the complete blitter and virtual
sprites, geometry and bounded fill, AFNT glyph expansion, and dual-bank copper.
The clean full-system route and repeated Arty hardware certifications pass.
Exact hashes, resources, timing, failed experiments, and evidence are in
`fpga/arty/graphics/TIMING_CLOSURE.md`.

The boot CP437 plane is deliberately not the version-1 application font
engine. It exists before Astraea command submission is available, uses a fixed
16x16 cell and four-color palette, and accepts only bounded status rows. Normal
Astra OS text continues to use positioned AFNT glyph runs and the hardware
glyph expansion contract in section 8.

Primary references:

- [AMD PG235: tested RGB 720p60 timing](https://docs.amd.com/r/3.1-English/pg235-v-hdmi-tx-ss/Tested-Video-Resolutions-for-RGB-4-4-4-and-YCbCr-4-4-4)
- [AMD UG934: typical video formats](https://docs.amd.com/r/en-US/ug934_axi_videoIP/Typical-Video-Formats)
- [AMD PG230: 1080p60 is 1.485 Gb/s per lane at 148.5 MHz](https://docs.amd.com/r/en-US/pg230-vid-phy-controller/Using-the-Fourth-GT-Channel-as-the-TX-TMDS-Clock-Source)
- [AMD UG471: valid OSERDESE2 clocking arrangements](https://docs.amd.com/api/khub/documents/IbGcnPFe6eF19RHma_Y~IA/content)
- [AMD UG472: 7-series MMCM clock-generation rules](https://docs.amd.com/v/u/en-US/ug472_7Series_Clocking)
- [AMD DS187: XC7Z020 switching characteristics](https://docs.amd.com/api/khub/documents/uaBd8Qxmf_K1~~tiVnOxrw/content)
- [Digilent Arty Z7 reference manual](https://digilent.com/reference/_media/reference/programmable-logic/arty-z7/arty-z7_rm.pdf)

## 1. Fixed product contract

Version 1 provides all of the following:

- fixed 1280x720 progressive output at 60 Hz;
- 24-bit RGB HDMI output;
- INDEX8, RGB565, and XRGB8888 framebuffer scanout;
- pixel-granular horizontal, vertical, and diagonal framebuffer scrolling;
- independently selectable X and Y wrapping over a 2D virtual framebuffer;
- atomic fenced scene presentation without application-visible vblank waits;
- two independently scrolling tile layers with 8x8 or 16x16 packed INDEX4 or
  INDEX8 patterns, per-tile palettes and reflection, and independent X/Y wrap;
- 64 hardware sprites, each with an INDEX8 source up to 128x128 pixels;
- 16 shared 256-entry sprite palette banks, selected independently per sprite;
- nearest-neighbor X/Y sprite scaling and X/Y reflection;
- sprite priority, front/behind placement, alpha, and pixel collision;
- blitter-rendered virtual sprites in the destination surface format;
- copy, fill, keyed copy, masked copy, alpha composition, scaling, and
  two-input Boolean raster operations;
- clipped lines, rectangles, circles, ellipses, fills, monochrome pattern
  fills, and bounded flood fill;
- hardware expansion and composition of positioned AFNT glyph runs;
- a beam-synchronized copper with access to every graphics control through
  defined timing classes;
- bounded command and completion rings, fences, timeouts, reset, diagnostics,
  and frame notifications.

Framebuffer rings and tile maps are complementary. Ring scrolling handles
arbitrary chunky scenes; tile layers provide compact reusable worlds,
pixel-granular parallax, and foreground overlays without redrawing hidden
framebuffer strips.

The required 720p60 mode must work without EDID. EDID and hot-plug state may be
reported for diagnostics, but version 1 does not dynamically negotiate another
mode. A diagnostic lower-resolution mode does not satisfy release acceptance.
The active pixel clock is exactly 74.25 MHz. Horizontal timing is 1280 active
pixels out of 1650 total; vertical timing is 720 active lines out of 750 total.
Output RGB uses full-range channel values from 0 through 255.

## 2. Ownership and software boundary

The protected Astra OS display service is the sole guest owner of graphics
policy. Applications receive handles to surfaces, palettes, tile layers, sprite
sets, command lists, scenes, and fences. They never receive graphics MMIO,
physical addresses, AXI arena offsets, or a writable mapping of an active
resource.

The Linux platform driver owns the PL register aperture, IRQs, reserved memory,
and ARM/PL cache transitions. The QEMU device exposes the Astra hardware
contract to the big-endian MC68030 guest. Host endianness and Linux internals do
not leak into the Astra ABI.

Fullscreen applications may acquire an exclusive Scene through the display
service. This changes policy ownership, not validation, memory bounds, or
failure handling.

## 3. Output and pixel formats

The HDMI pipeline emits eight bits each of red, green, and blue. Surface bytes
have an explicit order independent of ARM host endianness:

| Format | Bytes per pixel | Memory byte order | Use |
|---|---:|---|---|
| INDEX8 | 1 | palette index | scanout, sources, destinations |
| RGB565 | 2 | big-endian `RRRRRGGG GGGBBBBB` | scanout, sources, destinations |
| XRGB8888 | 4 | `XX RR GG BB` | scanout, sources, destinations |
| ARGB8888 | 4 | `AA RR GG BB` | source only |
| MASK1 | 1 bit | most-significant bit first | masks and glyphs |
| A4 | 4 bits | high nibble first | glyph coverage |
| A8 | 1 | coverage | glyph coverage |
| INDEX4 | 4 bits | high nibble first | tile and compact color-glyph source |

ARGB8888 is not a scanout format. The active framebuffer is always logically
opaque after composition. Destination conversion rounds to the nearest
representable channel value.

Base addresses and pitches for scanout and renderable surfaces are 64-byte
aligned. Surface dimensions are at most 4096x4096 pixels. A pitch must cover a
complete row and the final addressed byte must remain inside one validated
graphics allocation.

### Alpha rule

Direct-color composition uses premultiplied source-over:

```text
out.rgb = src.rgb + dst.rgb * (255 - src.a) / 255
```

Public ARGB and palette values use straight alpha. Vega and Astraea may
premultiply metadata internally, but conversion must use exact round-to-nearest
division by 255. A per-object opacity multiplies source alpha and color before
source-over. Opacity zero produces no write; 255 preserves source alpha.

INDEX8 destinations do not perform alpha blending or color quantization. They
support exact-index copy, fill, key, mask, and Boolean raster operations.

## 4. Palettes

Each scene contains:

- one 256-entry framebuffer palette;
- sixteen 256-entry sprite palette banks;
- sixteen 256-entry tile palette banks shared by the two tile layers.

Every entry is logical `0xAARRGGBB`. An INDEX8 framebuffer uses the framebuffer
palette. Every hardware sprite independently selects one sprite palette bank,
and every tile-map entry independently selects one tile palette bank. INDEX4
patterns address entries 0 through 15 of the selected bank; INDEX8 patterns
address all 256 entries. The palette set occupies 33,792 bytes per scene
generation and 101,376 bytes across the three metadata generations.

The framebuffer, sprite, and tile palettes are copper-visible. Copper changes
affect only active raster state. The committed scene palette is restored before
copper starts the next frame.

## 5. Framebuffer scrolling and rings

A framebuffer descriptor contains base allocation, pitch, format, virtual
width and height, viewport X and Y, wrap-X, wrap-Y, color key, palette, and
generation. The active viewport is always 1280x720.

The common scrolling allocation is a 2560x1440 2D circular surface. It is the
chunky equivalent of four adjacent screen-sized name tables. Larger and smaller
virtual surfaces are legal within the 4096x4096 limit.

The address generator applies viewport coordinates modulo the selected virtual
dimension when wrapping is enabled. A horizontally wrapped scanline requires
at most two contiguous AXI fetch regions. Vertical wrapping changes only the
row address. Horizontal, vertical, and diagonal scroll are all pixel-granular.

Version 1 uses two complete ring surfaces for continuously updated content:

1. Vega scans the active ring.
2. Astraea and the CPU update newly exposed strips in the writable back ring.
3. The display service submits render fences and the next viewport scene.
4. Vega swaps ring descriptors atomically at a qualifying vblank.

The complete active and pending virtual surfaces are write-protected. Version
1 does not attempt to prove that a hidden region of the active ring is safe to
modify. A present swaps descriptors; it never copies framebuffer pixels during
vblank.

Memory for one 2560x1440 ring is:

| Format | One ring | Two rings |
|---|---:|---:|
| INDEX8 | 3,686,400 bytes (3.52 MiB) | 7.03 MiB |
| RGB565 | 7,372,800 bytes (7.03 MiB) | 14.06 MiB |
| XRGB8888 | 14,745,600 bytes (14.06 MiB) | 28.13 MiB |

## 6. Scene state machine and tearing invariant

Visual metadata is triple-buffered with exactly these states:

- **ACTIVE**: immutable committed baseline used by scanout;
- **PENDING**: immutable validated generation waiting for fences and vblank;
- **EDITABLE**: the only generation software may change.

When there is no pending generation, submitting EDITABLE validates every
resource, captures required render fences, moves it to PENDING, and assigns the
spare metadata bank as the new EDITABLE generation. At most one PENDING scene
exists. A second submission while PENDING exists returns queue-full without
changing either generation.

At vblank, Vega promotes the complete PENDING scene only when all of these are
true:

1. every required render fence has retired successfully;
2. no engine or CPU owner retains write ownership of a referenced resource;
3. every descriptor and address remains valid;
4. metadata promotion can complete before the line-zero prefetch deadline.

Promotion is all-or-nothing. If any condition is false, Vega displays ACTIVE
again for the complete next frame. It never exposes a partial scene. Invalid
or failed dependencies complete the pending scene with an error instead of
retrying forever.

Applications and normal display-service code never call or wait for vblank to
maintain correctness. They submit work, a scene generation, and fences at any
time. Optional frame notifications exist for animation pacing and diagnostics.

Every completion identifies the presented generation, actual frame number,
retired surface, monotonic hardware timestamp, and any defer or failure reason.

## 7. Compositor and sprites

Vega composites bottom to top:

1. RGB888 backdrop;
2. tile layers configured below the framebuffer, layer 1 then layer 0;
3. sprites marked behind the framebuffer;
4. framebuffer, including optional color key or INDEX8 palette alpha;
5. sprites marked in front of the framebuffer;
6. tile layers configured above the framebuffer, layer 1 then layer 0.

Within a sprite layer, larger priority values are above smaller values. Equal
priorities are resolved by larger descriptor index above smaller index. Palette
alpha and per-sprite opacity use the source-over rule in section 3. A sprite's
transparent index always forces alpha zero regardless of its palette entry.

### Descriptor contract

There are exactly 64 descriptors numbered 0 through 63. Each descriptor has:

- enable and visibility;
- signed destination X and Y;
- source width and height from 1 through 128;
- destination width and height from 1 through 1024;
- a validated 64-byte-aligned INDEX8 source allocation and pitch;
- nearest-neighbor source stepping;
- X and Y reflection;
- front/behind selection;
- priority;
- palette bank 0 through 15;
- transparent index;
- global opacity;
- collision enable, class, and mask.

The internal hardware descriptor is exactly eight 32-bit words. It is not a
public application structure; the display service builds it from validated
resource handles and writes it through the privileged control interface.

| Word | Bits | Meaning |
|---:|---:|---|
| 0 | 0 | enabled |
| 0 | 1 | visible |
| 0 | 2 | reflect X |
| 0 | 3 | reflect Y |
| 0 | 4 | in front of framebuffer |
| 0 | 5 | collision enabled |
| 0 | 7:6 | reserved, zero |
| 0 | 15:8 | priority |
| 0 | 19:16 | palette bank |
| 0 | 27:20 | transparent index |
| 0 | 31:28 | reserved, zero |
| 1 | 15:0 | signed destination X |
| 1 | 31:16 | signed destination Y |
| 2 | 7:0 | source width |
| 2 | 15:8 | source height |
| 2 | 23:16 | global opacity |
| 2 | 31:24 | reserved, zero |
| 3 | 10:0 | destination width |
| 3 | 15:11 | reserved, zero |
| 3 | 26:16 | destination height |
| 3 | 31:27 | reserved, zero |
| 4 | 31:0 | validated physical source base |
| 5 | 12:0 | source pitch in bytes |
| 5 | 31:13 | reserved, zero |
| 6 | 15:0 | collision mask |
| 6 | 31:16 | collision class |
| 7 | 31:0 | reserved, zero |

Destination coordinates use the complete signed 16-bit range. A descriptor
remains valid when part or all of its destination rectangle lies outside the
1280x720 active raster. Hardware clips every edge before source addressing; a
fully clipped sprite performs no source read, admits no pixel, and can be used
as an off-screen parked object without changing its enable state. Clearing
visibility is the cheaper way to hide an object that does not need to retain a
positioned active descriptor.

Validation derives the horizontal nearest-neighbor step and collision
compatibility matrix. Software cannot provide either derived value.

### Sprite image storage

A sprite's visual shape is its INDEX8 image in DDR. There is no on-chip sprite
pixel store and no hidden fixed-shape ROM. Source pixels are row-major, one
byte per pixel. The transparent index always suppresses a source pixel;
otherwise the selected palette entry supplies ARGB and the descriptor opacity
scales its alpha.

Every source allocation obeys these rules:

- base is aligned to 64 bytes and lies wholly inside the graphics arena;
- width and height are each 1 through 128 pixels;
- pitch is a multiple of 64 bytes and is at least the source width;
- the allocation is at least `pitch * height` bytes;
- all bytes fetched by the final row remain inside the same allocation;
- CPU mappings are not writable while ACTIVE or PENDING references exist.

The minimum pitch is therefore 64 bytes for widths through 64 and 128 bytes
for widths 65 through 128. One maximum `128x128` image occupies 16 KiB. A
complete set of 64 distinct maximum images occupies exactly 1 MiB; simultaneous
maximum ACTIVE and PENDING generations require 2 MiB. Output scaling does not
increase source-image storage.

The arena allocator guarantees 2 MiB of sprite-image capacity so a complete
maximum scene can be replaced atomically. This is a reservation policy, not a
hard address partition. Sprite images may borrow all otherwise-unpinned arena
capacity, and unused sprite reserve may satisfy only allocations that can be
reclaimed before the next scene submission. Animation frames and atlases are
ordinary image resources; an atlas frame base must still be 64-byte aligned.

Assets originate in AstraFS, FAT, a shared host directory, or ordinary process
memory. The display service allocates an arena resource, copies or decodes the
INDEX8 pixels into it, cleans the non-coherent cache range, and returns an
opaque generation-bearing image handle. Applications submit that handle plus
geometry and presentation state. Only the display service resolves it to the
physical base and pitch above. Closing a handle defers reclamation until every
render fence and ACTIVE/PENDING scene reference has retired.

There is no separate collision-shape bitmap in version 1. A pixel participates
when collision is enabled, its index is not the transparent index, and its
palette alpha multiplied by sprite opacity rounds to a nonzero effective
alpha. Collision class and mask select eligible sprite pairs; they do not
provide pixel storage.

The hardware retains 64 global descriptors and admits at most 16 complete
sprite spans and 2,048 destination pixels on one scanline. Scaling remains
available, but wider destination spans consume more of the pixel budget.
Sprites are admitted in descending priority and ascending descriptor order; a
non-admitted lower-priority span is omitted for that line and reported in a
64-bit overflow bitmap. A span is never rendered partially because either
budget expired.

### Cursor policy

There is no dedicated cursor plane and no cursor-specific RTL. Sprite
descriptor 0 is an ordinary descriptor.

While the desktop pointer is enabled, the display service conventionally owns
sprite 0, places it in front at the highest priority, and disables its collision
participation. Disabling the pointer releases descriptor 0 for ordinary use.
Fullscreen software can therefore use all 64 sprites, while the normal desktop
uses sprite 0 as its hardware cursor and has 63 remaining sprite descriptors.

The pointer follows the same shadow-scene and automatic vblank promotion rules
as every other sprite. Applications still do not write descriptor 0 directly.

### Collision contract

Each descriptor has a 16-bit collision class and 16-bit collision mask. A pair
is tested only when both descriptors enable collision and each object's class
intersects the other object's mask. Transparent pixels do not collide.

Vega records an exact symmetric 64x64 pair matrix for the completed frame and
the frame number associated with it. The display service reads a stable
previous-frame bank while hardware builds the current matrix. Cursor-policy
use of sprite 0 forces its collision enable off.

## 8. Tile layers

Vega provides exactly two tile layers. Each layer is independently enableable
and selects:

- 8x8 or 16x16 square tiles;
- packed INDEX4 or INDEX8 pattern storage;
- a 32-bit tile map and one validated tile-set allocation;
- power-of-two map width and height;
- signed 32-bit pixel scroll X and Y;
- independent wrap-X and wrap-Y;
- transparent-index enable and an 8-bit transparent index;
- below-framebuffer or above-framebuffer placement;
- one 8-bit layer opacity.

Map dimensions are between 1 and 512 entries per axis. An 8x8 layer may use up
to 512x512 entries; a 16x16 layer may use up to 256x256 entries so the logical
world remains within 4096x4096 pixels. Width and height must each be powers of
two. The display service normalizes wrapped scroll values before submission;
hardware nevertheless performs signed 33-bit intermediate arithmetic so an
extreme valid scroll cannot overflow address validation.

### Tile-map entry

Tile maps are row-major arrays of 32-bit big-endian entries. Logical bits are:

| Bits | Field | Meaning |
|---|---|---|
| 31:16 | `TILE_INDEX` | pattern number 0 through 65,535 |
| 15:12 | `PALETTE_BANK` | tile palette bank 0 through 15 |
| 11 | `FLIP_X` | reflect the selected pattern horizontally |
| 10 | `FLIP_Y` | reflect the selected pattern vertically |
| 9:0 | reserved | must be zero in version 1 |

The bytes in memory are therefore tile-index high, tile-index low, attributes,
and reserved-low. A scene containing a nonzero reserved bit, an out-of-range
tile index, an invalid map extent, or an address outside the retained graphics
allocation is rejected before it can become PENDING.

Pattern storage is tile-major and row-major with no hidden row padding. INDEX4
stores the left pixel in the high nibble and occupies 32 bytes for an 8x8 tile
or 128 bytes for a 16x16 tile. INDEX8 occupies 64 or 256 bytes respectively.
Tile-set and map bases are 64-byte aligned. Allocation bounds, not the 16-bit
index field alone, determine the largest legal tile index.

The configured transparent index forces alpha zero before palette lookup.
Otherwise the selected tile-palette entry supplies straight RGBA and the layer
opacity participates in the same source-over rule as sprites. Disabling
transparent-index matching does not disable palette alpha.

### Scrolling, copper, and line construction

Scroll is pixel-granular in both axes. Wrapped axes use the power-of-two map
extent as a torus; an unwrapped coordinate outside the map produces a
transparent span. Horizontal, vertical, diagonal, and negative scrolling are
required. No software redraw, coarse-scroll boundary, or application-visible
vblank wait is involved.

Tile scroll, wrap, enable, placement, opacity, and transparent index are
next-scanline copper controls. Map base, tile-set base, dimensions, tile size,
and pattern format are next-vblank structural controls. This allows independent
horizontal bands, parallax, and raster effects while preventing a partially
prefetched line from observing mixed structural state.

Map descriptors and pattern rows are fetched ahead into on-chip line storage.
The implementation must support an unaligned 1280-pixel line, which touches at
most 161 8x8 tiles or 81 16x16 tiles per layer. Tile pixels are never dropped
selectively to recover a missed deadline. A missing complete output line uses
the common scanout-underrun containment rule in section 13.

## 9. Astraea rendering engines

All Astraea operations target non-active validated graphics allocations. They
are asynchronous, clipped, fenced, and cancelable through an engine reset.
Each command validates its complete source, destination, workspace, pitch, and
integer arithmetic before its first DMA request. A rejected command performs
no partial write.

### Blitter and virtual sprites

The blitter supports:

- one-to-one and scaled copy;
- solid fill;
- source color key;
- MASK1 copy;
- premultiplied source-over and constant opacity;
- nearest-neighbor X/Y scaling;
- X/Y reflection;
- overlap-safe same-format one-to-one copies;
- all sixteen two-input source/destination Boolean raster operations.

It converts INDEX8 sources through a selected palette and converts among
INDEX8, RGB565, XRGB8888, and ARGB8888 where the destination can represent the
result. Direct-color to INDEX8 quantization is not provided.

The 16-bit command-flags field and 32-bit `OPTIONS` word have one exact v1
layout. Unknown or reserved bits reject the command before DMA:

| Command flag | Meaning |
|---:|---|
| bit 0 | reflect source X |
| bit 1 | reflect source Y |
| bit 2 | enable source color key |
| bit 3 | apply the auxiliary MASK1 surface |
| bit 4 | source-over with constant opacity |
| bit 5 | expand INDEX8 through its validated ARGB palette |
| bit 6 | enable the Boolean ROP in bits 11:8 |
| bit 7 | reserved, zero |
| bits 11:8 | four-bit source/destination truth table |
| bits 15:12 | reserved, zero |

ROP truth-table bit `{S,D}` selects the output for that source/destination bit;
therefore `0x0` clears, `0x6` is XOR, `0x8` is AND, `0xc` copies the source,
`0xe` is OR, and `0xf` sets. With ROP disabled, the operation is a source copy.
ROP and source-over are mutually exclusive.

For BLIT, `OPTIONS[31:24]` is constant opacity and `OPTIONS[23:0]` is the
source-key value. INDEX8 compares its low 8 bits, RGB565 its low 16 bits, and
XRGB8888/ARGB8888 compare RGB in all 24 bits. Fields unused by the selected
flags must be zero. FILL continues to use the complete `OPTIONS` word as the
canonical destination-format color.

Nearest-neighbor sampling is exactly
`floor(destination_offset * source_extent / destination_extent)` before any
requested reflection. The complete source rectangle must lie inside its
validated source surface; destination and command clipping never permit an
out-of-range source DMA. Same-surface overlap is supported only for the
same-format, unscaled, unreflected source-copy case, where traversal has
`memmove` semantics. Other aliased operations reject before DMA.

Application draw lists expose that operation as a same-surface rectangular
copy. It is the shared retained-pixel primitive for terminals, editors, word
processors, and other text controls: software moves the preserved pixels once,
then emits glyph runs and fills only for newly exposed or otherwise damaged
cells. The draw-list replay path lowers it to the ordinary overlap-safe BLIT;
there is no terminal-only renderer or text-specific copy engine.

A virtual sprite is a blitter command or command group rendered into a hidden
surface. It has no descriptor-count limit. Its practical limits are the
bounded command ring, destination clip, graphics memory, and render fence
deadline. Virtual sprites use the destination framebuffer's format and may use
the same scale, reflection, key, alpha, and mask operations as other blits.

Virtual-sprite groups use consecutive ordinary `BLIT` commands in the bounded
submission ring; `BLIT_BATCH` remains reserved and is not accepted by version
1 hardware. Every command retains its own nonzero sequence, validation,
deadline, completion, and reset behavior. The sequence of the final submitted
BLIT is the group fence. The graphics service retains every referenced
resource until that fence retires and records the first failed completion, if
any.

All commands in a group target a hidden surface. Earlier commands may modify
that surface even if a later command fails, times out, or is canceled. The
graphics service must present or reuse the hidden surface only after every
completion through the group fence reports success. Failure discards the
hidden result; it never exposes a partially rendered group. Ring capacity and
the service's per-frame command budget bound the group, while multiple groups
may be submitted over time. There is no fixed virtual-sprite descriptor count
in hardware.

### Geometry, pattern, and flood operations

The hardware geometry path provides:

- one-pixel clipped line segments with both endpoints included;
- rectangle outline and fill;
- circle outline and fill;
- ellipse outline and fill;
- 8x8 MASK1 repeating pattern fill with foreground, optional background, and
  signed pattern origin;
- bounded scanline flood fill using a caller-supplied validated workspace.

Geometry operates on INDEX8, RGB565, and XRGB8888 destinations. Version 1
geometry is exact aliased rasterization. Wide strokes, joins, caps,
antialiased geometry, arbitrary polygons, and color texture fills are outside
the version-1 contract. They may be added without changing the required
primitive semantics.

Flood fill terminates with a work-overflow error when its bounded workspace is
exhausted. It never grows hidden storage or writes outside its clip rectangle.

Geometry commands use the common command header and destination descriptor.
Their opcode-specific words are fixed as follows; coordinates are packed
signed `x:16, y:16`, radii are unsigned `rx:16, ry:16`, and colors are the
canonical value for the validated destination format.

| Word | Geometry meaning |
|---:|---|
| 8 | destination surface descriptor |
| 9 | pattern bits 63..32, otherwise zero |
| 10 | pattern bits 31..0, or flood workspace descriptor |
| 11 | P0: first point, center, or flood seed |
| 12 | P1: second inclusive corner, otherwise zero |
| 13 | radii, or signed pattern origin |
| 14 | opaque pattern background, otherwise zero |
| 15 | foreground, fill, or replacement color |

Flag bit 0 requests a filled rectangle, circle, or ellipse. Flag bit 1 makes
zero pattern bits write the background color; otherwise they are transparent.
Flags not meaningful for the selected opcode must be zero. Pattern bits are
row-major with bit 63 at pattern coordinate `(0,0)`. Flood workspace is a
read/write XRGB8888 auxiliary descriptor used as a four-byte-aligned byte
container. Its data is an array of four-byte packed signed seed coordinates;
its validated byte size divided by four is the hard queue capacity. The
workspace must not overlap either ring, either flood descriptor, destination
storage, or a protected range. Words 9 and 12 through 14 are zero for flood
commands. Workspace exhaustion completes with `WORK_OVERFLOW` and may leave a
partially modified hidden destination, which must not be presented.

### Fonts and glyphs

AFNT remains the native hardware-ready font format. TTF and OTF are import and
installation formats parsed by the user-space font service, never by RTL. Text
layout, Unicode mapping, shaping, fallback, kerning, and glyph positioning stay
in software.

Astraea accepts positioned glyph runs using MASK1, A4, A8, INDEX4, or INDEX8
glyph sources. MASK1 writes a selected color. A4 and A8 provide coverage for
source-over into RGB565 or XRGB8888. INDEX4 and INDEX8 support color glyphs
through a validated palette. INDEX8 destinations accept exact indexed glyphs
or MASK1 selection but do not alpha-quantize direct color.

The CPU builds glyph descriptors; Astraea expands and writes every destination
pixel. CPU framebuffer glyph loops are not part of the Astra graphics path.

The protected runtime transport preserves that split. A render batch contains
the hardware's native big-endian command ring, surface descriptors, positioned
glyph descriptors, masks, and source data. The kernel bounds and pins the DMA
buffer; QEMU copies only that declared byte range; the Linux display owner
validates the batch envelope and places it unchanged in the reserved graphics
arena. Astraea validates each command and performs the pixel work. The NDK and
font service may decode UTF-8, select strikes, position glyphs, clip, schedule,
and wait on fences, but production code must not rasterize a UI glyph or shape
into a framebuffer on the 68030.

Framebuffer output is double-buffered. A batch targets the allocation that is
not ACTIVE, and the Linux display owner changes `FB_BASE` only after every
command in that batch completes successfully. The next batch targets the
other allocation. Both the current ACTIVE range and a pending scanout range
remain protected from renderer writes.

The current window server gives each live window one fixed graphics-arena
cache. Chrome or content changes rebuild that cache with Astraea commands.
Movement, z-order changes, activation, and exposure reuse the cache with
clipped masked `BLIT`; the MC68030 only updates state and emits commands. A
bounded union damage rectangle is retained for each alternating scanout, so a
frame is repaired before it can become ACTIVE. Rounded corners require no
special CPU repair: the cache mask suppresses corner pixels while lower
windows and the desktop are repainted through the same damage rectangle.

`GLYPH_RUN` uses the common command header and clip rectangle. Word 8 is the
write-capable destination surface descriptor; word 9 is the read-capable AFNT
strike surface descriptor; word 10 is the arena-relative offset of the
positioned-glyph array; word 11 is its nonzero descriptor count; words 12 and
13 are foreground and optional opaque-background colors; word 14 bits 7:0 are
the transparent index for INDEX4/INDEX8; word 15 is zero. The command flag bit
0 requests opaque background for zero MASK1 pixels. Other flags and option
bits are zero. A run contains at most 4,096 descriptors and the entire array
is validated before glyph execution.

Each positioned glyph is one 16-byte big-endian record, preserving the proven
Astraea descriptor layout:

```text
word 0  source byte offset relative to the strike surface
word 1  unsigned source x:y (x in bits 31:16, y in bits 15:0)
word 2  signed destination x:y
word 3  unsigned width:height
```

Widths and heights are nonzero. Every source rectangle must fit the validated
strike surface after applying its relative byte offset. Descriptor storage
must not overlap either command ring, either surface descriptor, destination
storage, source storage, palette storage, or a protected range. Failure of any
descriptor rejects the run before its first destination write.

## 10. Copper

The copper executes 64-bit instructions from two on-chip banks. Each bank holds
4,096 instructions, for 32 KiB per bank and 64 KiB total. One bank is ACTIVE
and immutable for the frame; the other receives the next validated list. A
scene promotion swaps banks atomically.

The instruction set retains `END`, `MOVE`, `WAIT`, `SKIP`, `IRQ`, and `JUMP`.
`WAIT` and `SKIP` compare the 720p beam position. `MOVE` reaches the complete
Vega/Astraea graphics register space through a hardware whitelist. It cannot
write system control, arbitrary AXI addresses, or unvalidated command data.
It may dispatch a prevalidated Astraea command identifier.

Copper writes have three timing classes:

- **pixel-boundary**: backdrop and active palette entries;
- **next-scanline**: framebuffer viewport and wrap, color key, tile visual and
  scroll state, and sprite visual state used by the line builders;
- **next-vblank**: framebuffer base, pitch, format, virtual dimensions, tile
  map/set bases and formats, output structure, command-ring configuration, and
  other structural state.

Hardware enforces the class. A write to a later class is staged automatically;
software does not need to place it in a safe beam interval. Viewport changes
therefore support independently scrolling horizontal bands. Version 1 does not
promise a framebuffer viewport split at an arbitrary X coordinate within one
already-prefetched scanline.

At every vblank, Vega restores the committed scene baseline before starting
the ACTIVE copper list. Copper mutations never leak into EDITABLE or PENDING
metadata.

## 11. Command and completion transport

Astraea consumes one bounded single-producer/single-consumer submission ring in
the reserved graphics arena:

- 1,024 entries;
- 64 bytes per entry;
- 64 KiB total;
- 32-bit nonzero sequence/fence values with half-range wrap ordering.

Each entry has a version, size, opcode, flags, sequence, validated resource
generation, clip rectangle, and opcode-specific payload. Complex glyph
commands refer to bounded descriptor arrays in the graphics arena. PL commands
contain arena-relative offsets, never unrestricted physical pointers.
All multi-byte ring fields use the documented big-endian Astra hardware wire
order.

The completion ring has 1,024 fixed 32-byte records. A record identifies
sequence, opcode, status, completed byte/pixel count, hardware timestamps, and
engine-fault detail. Ring-full submission returns queue-full; neither ring ever
overwrites an unread entry.

The blitter and draw paths may execute concurrently. Individual completions may
arrive independently, but the globally retired fence advances only across the
largest contiguous successfully completed sequence prefix. Scene presentation
waits on retired fences, not transient engine-busy bits.

Every asynchronous operation has a finite privileged deadline. No command may
request more than 500 ms. Timeout resets only the affected engine, completes
the command with a timeout error, leaves scanout running on the previous scene,
and releases every retained resource exactly once.

## 12. DDR, AXI, and cache ownership

The Arty target reserves one physically contiguous 128 MiB graphics arena from
the 512 MiB DDR. The physical base is a board memory-map decision and is not an
NDK ABI. The arena contains surfaces, sprite images, command rings, completion
rings, tile maps and patterns, validated descriptor arrays, and bounded engine
workspaces. Its usage is reported separately from the Axiom general allocator.

The PL uses 64-bit AXI paths with this intended ownership:

- HP0: framebuffer scanout;
- HP1: tile-map, tile-pattern, and sprite source fetch and line construction;
- HP2: Astraea source and command reads;
- HP3: Astraea destination writes and completion records.

The final interconnect may combine compatible channels only if worst-case
release tests retain the same guarantees. Completed-line scanout has highest
service priority, tile and sprite construction deadlines are second, and
opportunistic rendering uses remaining DDR service. Rendering yields at bounded
burst boundaries. Linux and QEMU traffic may not starve either real-time
client.

The Zynq HP paths are treated as non-coherent:

- control and ring storage use coherent/uncached mappings;
- cached CPU-written resources are cleaned before ownership passes to PL;
- PL-written resources are invalidated before CPU ownership returns;
- a submitted resource loses CPU write access until its completion fence;
- active and pending scanout resources remain read-only to every CPU mapping;
- MMIO access uses ordered accessors and explicit barriers.

No correctness path relies on incidental cache behavior.

## 13. Bandwidth and line deadlines

Active-pixel framebuffer payload at 720p60 is:

| Format | Payload |
|---|---:|
| INDEX8 | 55,296,000 bytes/s |
| RGB565 | 110,592,000 bytes/s |
| XRGB8888 | 221,184,000 bytes/s |

The 16-sprite/2,048-pixel admission contract bounds one scanline to 2,048
sprite source bytes plus framebuffer data and palette/compositor work. Across
all 720 active lines at 60 Hz that is at most 88,473,600 source bytes/s. The
sprite line path processes four output pixels per fabric clock.

One INDEX8 tile layer contributes 55,296,000 active texel bytes/s; INDEX4
contributes 27,648,000 bytes/s. Two INDEX8 layers therefore contribute
110,592,000 bytes/s. With XRGB8888 scanout and the maximum unscaled sprite
case, the combined active source payload is 394,690,560 bytes/s before map
descriptors, AXI overhead, rendering, Linux, or QEMU traffic.

At an 8x8 map-row boundary, two unaligned layers add at most 1,288 descriptor
bytes to one line. Fetching every edge pattern row in full makes the
corresponding pathological line 5,120 framebuffer bytes, 2,048 sprite bytes,
2,576 tile texel bytes, and 1,288 map bytes: 11,032 bytes within the 22.222
microsecond 720p line period, or 496.44 MB/s of instantaneous payload. Reusing
a map row across its eight output lines reduces two-layer map traffic to at
most 7,032,480 bytes/s. Arbitrary per-scanline copper scroll can defeat that
reuse and raise it to 55,641,600 bytes/s. Release testing must include both
cases and may not assume repeated tiles or cache hits.

Vega uses enough on-chip buffering to absorb measured Linux/DDR latency while
meeting every scanline deadline; the baseline architecture uses at least four
complete output-line slots. QoS, burst size, buffering, and fabric clocks are
implementation choices only after the worst-case test proves them.

If a line is unavailable despite the reserved service, Vega repeats the most
recent complete output line, raises a sticky underrun fault, records the missed
address/deadline, and continues scanout. This is diagnostic containment, not an
acceptable steady-state condition.

## 14. Interrupts, counters, and recovery

The graphics complex reports at least:

- vblank/frame completion;
- scene presented, deferred, or failed;
- command completion and command-ring backpressure;
- copper IRQ, invalid instruction, and late effect;
- tile configuration fault, fetch fault, and line-deadline miss;
- sprite collision and per-line overflow;
- scanout underrun;
- AXI decode, timeout, and response failures;
- per-engine watchdog timeout and reset completion.

Allocation-free counters expose bytes and bursts per AXI client, maximum and
average read latency, line-buffer low-water mark, underruns, tile map/pattern
bytes, tile cache hits/misses, sprite pixels admitted/dropped, commands
submitted/completed/failed, engine busy cycles, fence latency, scene deferrals,
and reset counts. A retained first-fault record captures engine, operation,
arena offset, AXI response, sequence, frame, and timestamp.

Invalid commands, copper lists, descriptors, and addresses fail closed. A
stuck renderer cannot halt scanout. A failed scene leaves the previous complete
scene active. Engine reset never clears the active display baseline.

## 15. Required verification and release gates

Reference models and RTL tests must cover every format and operation, malformed
and overflowing arithmetic, clipping, overlap, alpha rounding, conversion,
scaling, all line octants, degenerate geometry, pattern origin, flood-workspace
overflow, glyph coverage, tile descriptor decoding, every tile size/format,
negative and independently wrapped tile scroll, map-edge clipping, copper
line-scroll, ring wrap, sequence wrap, timeout, cancellation, scene retry, and
resource revocation.

The exact routed release candidate must demonstrate on Arty hardware:

1. stable 1280x720p60 HDMI with the required timing constraints met;
2. zero underruns during XRGB8888 scanout with both INDEX8 tile layers, all 64
   unscaled 128-pixel sprite spans overlapping, hostile per-line copper scroll,
   and concurrent bounded rendering;
3. seamless horizontal, vertical, diagonal, and both-axis wrapped scrolling;
4. exact two-layer INDEX4/INDEX8 tile composition, palettes, reflection,
   transparency, clipping, foreground placement, and map-edge behavior;
5. tear-free repeated ring swaps under late and failed render fences;
6. copper palette, backdrop, framebuffer/tile scroll-band, sprite, and
   structural-write timing;
7. exact sprite alpha, priority, scaling, overflow, and collision results;
8. all blitter, geometry, flood, pattern, and AFNT glyph operations;
9. command/address fuzzing without an out-of-arena AXI transaction;
10. engine timeout/reset while HDMI continues the previous scene;
11. a long coexistence soak with QEMU, Linux, graphics IRQs, and DDR pressure,
    with no monotonic resource growth or unreported fault.

Placement estimates, isolated synthesis, lower-resolution output, reduced
sprite cases, and simulation alone are not release evidence.

## 16. Explicitly outside version 1

Version 1 does not include affine or rotated tile layers, tile scaling, a
dedicated cursor plane, a 65th sprite, planar graphics, arbitrary sprite
rotation, bilinear filtering, antialiased geometry, wide stroked paths,
polygon/path rasterization, 3D, hardware TTF/OTF parsing or shaping,
direct-color-to-indexed quantization, full three-source Amiga minterms, or
dynamic display-mode negotiation.

Audio synthesis, PCM playback, and the fixed-point game-math coprocessor are
separate PL subsystems and do not consume graphics command or scene semantics.
