# Graphics and Display

The graphics API exposes protected display surfaces, asynchronous draw lists,
hardware sprites, beam-synchronized raster programs, and completion fences.
Applications never receive physical SDRAM addresses or map Vega/Astraea MMIO.
The display service validates every object, pins referenced storage, arbitrates
the shared engines, and retires ownership only after a fence signals.

## Rendering flow

1. Open an {c:struct}`AstraDisplay` and query
   {c:func}`astra_graphics_get_info`.
2. Create an {c:struct}`AstraSurface` with explicit dimensions, format, and
   intended uses.
3. Create an {c:struct}`AstraDrawList` with a mandatory clip rectangle and
   append geometry, patterns, flood fills, or immutable text layouts.
4. Submit the list with {c:func}`astra_draw_submit` and wait or poll its
   {c:struct}`AstraFence`.
5. Present a scanout surface at vblank with
   {c:func}`astra_display_present_surface`.

Draw lists are mutable until submission and sealed while in flight. Surfaces,
font strikes, palettes, and other referenced objects remain pinned through the
completion fence, so application cleanup cannot create a DMA use-after-free.

## Surfaces and formats

Creation flags state whether a surface may be scanned out, used as a draw
source or target, used as a tile map, or mapped by the CPU. These are validation
rights, not hints. The service chooses a hardware-compatible pitch and reports
it through {c:func}`astra_surface_get_info`.

Scanout and geometry support INDEX8 and RGB565. Font sources additionally use
MASK1, A4, and INDEX4. A4 blends into RGB565 with the exact native-channel rule
specified by the AFNT contract; indexed glyph palettes are RGB565 and use a
caller-selected transparent index.

An {c:struct}`AstraPalette` is a copied, mutable set of up to 256 opaque sRGB
entries. One presentation snapshot supplies indexed framebuffer, tile, and
sprite colors. Tile and sprite descriptors select sixteen-color banks within
that palette; transparency remains an explicit descriptor index rather than
palette alpha.

## Sprites and raster programs

An {c:struct}`AstraTileLayers` object owns two independently configurable
hardware layers. Each layer references a power-of-two TILE16 map and an INDEX4
tile-pattern surface, selects 8x8 or 16x16 tiles, and carries signed pixel
scroll, independent X/Y wrapping, transparency, and foreground/background
placement. Updating a layer copies and validates the descriptor; passing null
disables it.

An {c:struct}`AstraSpriteSet` contains up to 32 copied sprite descriptions.
Replacing an entry is atomic from the next presentation that references the
set. The service validates source bounds, clipping, priority, palette bank,
transparency, and collision policy before publishing hardware descriptors.
Vega admits complete sprites in descending priority and ascending descriptor
index until the scanline pixel budget is exhausted. The primary 720-pixel mode
guarantees 1,024 sprite pixels/line with INDEX8 scanout and 512 with RGB565;
query the corresponding {c:struct}`AstraGraphicsInfo` fields rather than
assuming a descriptor count implies unlimited overlapping width. Overflow is
reported as {c:enumerator}`ASTRA_DISPLAY_STATUS_SPRITE_OVERFLOW` and never
becomes a scanout underrun.

An {c:struct}`AstraRasterProgram` is an immutable ordered list of validated beam
changes. Public target identifiers deliberately expose only display-safe
operations; they are translated to privileged copper instructions by the
service. Applications cannot issue arbitrary copper MMIO writes.

## Lifetime

Use `ASTRA_AUTO_DISPLAY`, `ASTRA_AUTO_SURFACE`, `ASTRA_AUTO_DRAW_LIST`,
`ASTRA_AUTO_SPRITE_SET`, `ASTRA_AUTO_RASTER_PROGRAM`, and `ASTRA_AUTO_FENCE` for
scope cleanup. Cleanup closes handles but never cancels submitted work. The
service holds its own references until the associated fence retires.

The current direct-MMIO NDK intentionally reports the graphics service as not
present. The API and validation surface are linkable now; the operating-system
display service will implement the same source contract without exposing raw
hardware to applications.
