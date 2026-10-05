# Managed graphics: sessions, GPU surfaces, and draw lists

This is the contract that makes `ndk/include/astra/graphics.h` real. Native
applications and the SDL2 renderer use the same API; SDL is a thin adapter over
it and never draws pixels on the MC68040 when the hardware can.

Status: design accepted 2026-09-26. Implementation is in progress; the phase
table at the end is the continuation record.

## 1. Why the existing paths are not enough

The FPGA can only address its own 512 MiB graphics arena. Guest RAM is not
visible to it. Today an application reaches the hardware in exactly two ways:

- an RGB565 window area, which the display service copies (MC68040 `memcpy`)
  into the render batch on every present; and
- a retained window draw list (ADLT v1.2): at most 128 commands, fills, lines,
  text, and a same-window copy.

Neither can hold an image on the GPU and draw it many times. An SDL renderer,
or any game, needs textures that are uploaded once and composited every frame
by the blitter.

## 2. Window-scoped graphics

GPU objects belong to a window. A window opened with
`ASTRA_WINDOW_CONTENT_SURFACE` has a GPU content surface in Media RAM instead
of an RGB565 area to copy. Its control port is the session: graphics commands
(`ASTRA_GUI_GRAPHICS_COMMAND`, GUI protocol 15) travel on it. The owner
dying closes the port, the service closes the window, and every surface,
list and staging area the window owned is released
(`display_window_graphics_close`).

- `astra_window_display(window, &display)` binds an `AstraDisplay` to the
  window. It borrows the window's control port, so it must be closed before
  the window.
- `astra_window_surface(window, &surface)` borrows the window content as an
  RGB565 draw target and source (surface id 1), sized when it was acquired.
- Presenting such a window only marks damage; its pixels are already on the
  GPU. The window scene shows the content bank the client drew, directly
  (section 5a): presenting copies nothing unless the next frame must inherit
  this one.
- `astra_window_present_discard(window)` presents the whole window and gives
  up its pixels: the next frame starts undefined and the client redraws every
  pixel before presenting again. SDL's renderer requires exactly that of its
  applications, so its present uses it and costs the display service no GPU
  command. Only content-surface windows accept it. It is the
  `ASTRA_GUI_PRESENT_DISCARD` flag of `WINDOW_PRESENT`, new in GUI protocol
  version 16.
- `astra_display_open()` remains reserved for exclusive fullscreen scene
  ownership (phase 5) and returns `NOT_PRESENT`.

RGB565 and retained draw-list windows keep working unchanged.

## 3. Surfaces

A surface is a display-service allocation in Media RAM
(`ASTRA_RENDER_BATCH_WORKSPACE_LIMIT` to `ASTRA_RENDER_BATCH_MEDIA_LIMIT`),
64-byte aligned, at most 4096x4096. A window may hold up to 4096 surfaces and
256 MiB. The service clears every new surface before returning it, because
Media RAM is reused across clients.

| Format | Draw target | Draw source | Notes |
|---|---|---|---|
| RGB565 | yes | yes | window content format |
| XRGB8888 | yes | yes | SDL `RGB888` |
| ARGB8888 | yes | yes | straight alpha; a new target is transparent black |
| INDEX8 | yes | INDEX8 targets only | no blending or triangles; a palette path to direct color is not wired |

`ASTRA_SURFACE_CPU_WRITE` surfaces are written in either of two ways:

- `astra_surface_write(display, surface, rect, pixels, pitch)` packs the rows
  into the display's staging area, then calls the next function.
- `astra_surface_write_staged(surface, rect, offset, pitch)` uploads rows
  straight from staging. A caller that renders directly into staging, as
  SDL's window framebuffer does, skips the NDK copy.

The service splits any write larger than one batch into bands.

**No service copy.** Each band is a render-only batch whose source pixels
are its *attachment* (`AstraDisplayFrameRequest.attachment*`): the service
reserves the pixels' place at the end of the batch data
(`astra_render_builder_upload_reserve`, rows keep the staging pitch) and
names the staging area, offset and length. The kernel resolves the area range
to physical extents (`kernel_area_extents`, every page committed), holds the
area until the batch is collected, and hands the device an
`AstraRenderAttachment` list through `DISPLAY_REQ_ATTACH`
(`ASTRA_DISPLAY_HOST_CAP_ATTACHMENT`). QEMU copies the batch's own bytes, up
to the attachment's target, and then the extents, into the mailbox payload.
The helper sees an ordinary batch. The batch's DMA stops at the target, so
nothing is pinned or copied for the reserved pixels.

Measured on beast (`emu/qemu/bench-frame.py --fake-helper`, SDLFrameBench,
640x480 ARGB8888 streaming): 519 fps with the service copy, 720 without it.
`astra_surface_write` still copies the caller's pixels into staging once on
the MC68040; only a kernel loan of the caller's own pages would remove it.

`ASTRA_SURFACE_CPU_READ` surfaces, and every window content surface, are
read back with `astra_surface_read(display, surface, rect, pixels, pitch)`.
The service sends a `READ_SURFACE` display request whose DMA buffer holds an
`AstraDisplaySurfaceRead` header; the Linux helper copies the validated
rectangle out of the graphics arena into the mailbox payload, QEMU copies it
into the guest buffer, and the service copies the rows into the client's
staging area (`ASTRA_GUI_GRAPHICS_SURFACE_READ`). Reads larger than one
8 MiB buffer are split into bands. The kernel admits the request only when
the host advertises `ASTRA_DISPLAY_HOST_CAP_READ_SURFACE`; the DMA transfer
is bidirectional, because the device reads the header and writes the rows,
and DMA pages are mapped cache-inhibited. Host-only QEMU, which has no
arena, returns zero rows.

`ASTRA_SURFACE_SCANOUT` returns `UNSUPPORTED`.

## 4. Draw lists (ADLT v1.7)

The retained window list and the session draw list share one wire format and
one lowering (`astra_render_builder_replay`). ADLT v1.7 replaces v1.6; every
in-tree writer and reader moves together.

- The header records `total_bytes` (16 KiB for retained window lists, up to
  1 MiB for session lists), `command_count`, `payload_offset`,
  `payload_bytes`, and the destination width and height. Commands start at
  byte 64.
- Colors are `0xAARRGGBB` straight alpha. The service converts them to the
  destination format. RGB565 written as 8:8:8 with bit replication round-trips
  exactly.
- The 64-byte command carries:
  - `operation`, `flags`
  - a destination rectangle
  - `color`
  - `source` surface id, with 0 meaning the destination
  - a signed 16-bit source origin and an unsigned 16-bit source extent
  - `payload_offset` and `payload_bytes`
  - `font_height` and `radius`
  - a per-command clip in destination pixels
- The operations are FILL, FILL_ROUNDED, TEXT, MONO_TEXT, LINE, BLIT,
  TRIANGLES, FILL_RECTS, LINES, TARGET, and UPLOAD. COPY is removed: it is a BLIT
  whose source is the destination.
- TARGET (10, v1.6) makes surface `source` (never 0) the destination of the
  commands after it; `width` and `height` are that surface's and its clip
  is all of it. So one session list carries a frame for several surfaces
  -- a render target, then the window -- and is one submission. The list's
  first destination is named by the request. A clip may span the current
  destination or the list's own size; the hardware clips to the destination.
  `astra_render_builder_replay_range` stops at a TARGET
  (`ASTRA_RENDER_REPLAY_TARGET`) and the service resumes after it with the
  new destination, in the same batch. Retained window lists have none.
- UPLOAD (11, v1.7) writes rows of the window's staging area into surface
  `source` (never 0, CPU-writable) at the command's rectangle:
  `payload_offset` is the staging offset of the first row and `color` the
  staging pitch; the clip and every other field are zero. So a frame
  carries its texture uploads, in order with its draws, instead of each
  being a SURFACE_WRITE call (`astra_draw_upload`). The replay stops at it
  (`ASTRA_RENDER_REPLAY_UPLOAD`); the service submits the batch so far and
  uploads the rows as SURFACE_WRITE does -- each band its batch's
  attachment -- then resumes. The client leaves the rows alone until the
  list is back: the service has read it, and the device took the rows
  when it accepted their batch.
- FILL, FILL_RECTS, LINE, LINES, BLIT, and TRIANGLES carry a blend field (bits 6:4): `NONE`,
  `BLEND`, `ADD`, `MOD`, or `MUL`, SDL2's equations
  (`docs/TEXTURE_ENGINE.md` §6). BLIT also has `FLIP_X` and `FLIP_Y`; BLIT
  and textured TRIANGLES have `FILTER_LINEAR`.
- For BLIT, `color` is the per-channel modulation, where `0xFFFFFFFF` is
  identity; its alpha is the constant opacity.
- TRIANGLES carries `3 * n` `AstraDrawListVertex` records in the list
  payload (x, y signed 24.8 pixels; u, v signed 16.16 texels; ARGB color),
  and a source surface id, or 0 for untextured triangles.
- FILL_RECTS (8, v1.5) carries `n` 8-byte `AstraDrawListRect` records in
  the payload (x, y signed 16-bit; width, height unsigned 16-bit; zero width
  or height draws nothing), word aligned, and one `color` for all of them.
  Its own rectangle, source, radius, and font height are zero. It means
  exactly `n` FILLs in order with the command's clip and blend mode, at the
  cost of one command. The NDK writes it from `astra_draw_rectangles`; every
  NDK payload starts word aligned.
- LINES (9, v1.5) carries `n` 8-byte `AstraDrawListSegment` records (x0,
  y0, x1, y1 signed 16-bit), word aligned, and one `color`. It means `n`
  LINEs in order, both endpoints drawn, and blends as LINE does: `NONE`, or
  `BLEND` at alpha 255. The NDK writes it from `astra_draw_lines`.
- Lowering (`render_builder.c`):
  - Plain and BLEND copies stay on the blitter, alpha via its opacity.
  - A BLIT that needs color modulation, ADD/MOD/MUL, or linear filtering
    becomes two triangles (`ASTRA_RENDER_OP_TRIANGLES`, 32-byte big-endian
    vertices in the batch data arena). A rectangle beyond the engine's
    +-32768 pixel vertex range is first cut to the clip, with its texel edges
    moved proportionally.
  - A FILL with `BLEND` and alpha below 255 is a one-record
    `ASTRA_RENDER_OP_FILL_RECTS` with `OPTION_BLEND` and the straight ARGB
    color. A destination the list cannot blend (an RGB565, XRGB8888, or
    ARGB8888 pitch that is not a multiple of eight bytes) takes a scaled
    BLIT from a one-pixel ARGB8888 source instead, which the list matches
    bit for bit. ADD/MOD/MUL fills are untextured triangles.
  - A LINE may only blend as opaque `BLEND`; SDL draws translucent lines as
    row spans of blended fills.
  - FILL_RECTS becomes `ASTRA_RENDER_OP_FILL_RECTS` commands of at most
    4096 16-byte big-endian records in the batch data arena: opaque (`NONE`,
    or `BLEND` at alpha 255) with the color in the destination format, and
    translucent `BLEND` with `OPTION_BLEND` and the straight ARGB color where
    the destination can blend it. Each record is copied once out of the
    client's list; empty ones and ones wholly outside the clip are dropped,
    and a list with nothing left emits nothing. Alpha 0 draws nothing; ADD,
    MOD, MUL, and BLEND the list cannot do lower each rectangle as the FILL
    it stands for. A list that does not fit the batch moves whole to the
    next one.
  - LINES becomes `ASTRA_RENDER_OP_LINES` commands of at most 4096 16-byte
    segments (x0,y0 and x1,y1 packed as LINE's words 11 and 12), the color
    in the destination format; segments whose bounds miss the clip are
    dropped.
  - TRIANGLES commands are split at 4096 triangles. Every vertex is copied
    once out of the client's list and validated on that copy: coordinates
    within +-32768 pixels, no texel coordinates when untextured.
  - Triangles reject INDEX8 destinations and sources, and a source that is
    the destination. Glyph runs reject ARGB8888 destinations.

## 5. Submission and batching

`astra_draw_post(list, flags)` sends `FRAME`, posted: no reply capability
and no reply (`docs/MEDIA_DATA_PLANE.md`, Video). The service replays the
list as it would a `LIST_SUBMIT`, signals the event the client attached
with the list -- Haiku's buffer recycling -- and with `FRAME_PRESENT`
presents the window as `WINDOW_PRESENT` does, but without a STATE event and
composed once no message is waiting, so presents that arrive together
compose once. The client's next change to the list waits for that event; a
failed frame is logged and dropped. It logs a window's first posted frame,
which the gates check.

`astra_draw_submit(list, fence)` sends `LIST_SUBMIT`. The service lowers the
list into its batch, submits it, and replies without waiting for the FPGA, so
the client's next frame runs while the hardware draws this one. The returned
fence is already signaled, and needs no kernel object: everything the list
uses was copied when the service accepted it (the commands into the batch,
uploads by the device), and the device runs requests in order, so every later
draw, readback and present sees the result. Validation errors come back from
the call itself. A batch that fails on the hardware is logged and is the
failure of the service's next request that needs the device or the batch
buffer, which collects it first. A submitted list is sealed until
`astra_draw_list_reset`.

The service keeps two batch buffers and the device holds two requests
(display host 1.1). Each request in flight holds the buffer it was built
in; building the next batch waits only when that buffer's request is still
running, and a present does not wait for its own completion -- its state is
committed when it is submitted, because everything after it runs after it.
The service collects a completion when it next needs the slot or buffer,
the way Haiku's app_server syncs to the engine only when it must.

A submitted list is lowered into **render-only batches**
(batch version 1.4, `ASTRA_RENDER_BATCH_VERSION_1_4`), which never touch scanout or the window
scene. The hardware ring holds 1024 commands per batch. The lowering therefore
flushes a batch whenever the ring or data arena fills, and continues in the
next batch. This is safe because every destination is an off-screen surface.
Window content becomes visible only through the next compose.

## 5a. Content banks

The service keeps three content banks per window, in the scene's
ACTIVE/PENDING/EDITABLE roles (GRAPHICS_ARCHITECTURE section 6). Surface id 1
always names the EDITABLE bank; render-only batches, uploads and readbacks
reach nothing else. A compose that follows a present points the window's
scene layers at that bank, and the next EDITABLE bank becomes the one neither
this scene nor the scene before it shows (PRESENTATION.md, "Bank
retirement").

The new EDITABLE bank is then brought up to date inside the same compose
batch: one BLIT from the bank just presented, covering the union of every
damage it missed. A discarding present skips the copy. The compose for a
changed content-surface window therefore emits one blit, or none after a
discarding present; before content banks it copied the whole client area into
a per-window cache in fifteen blits (the rounded bottom corners split it).

The cache now holds only the frame and title bar and is rebuilt only when
they change. Under a title bar only the client area's bottom corners are
rounded, so the client area is two scene layers: the bottom 2r rows with
radius r, and above them a square layer of every row but the bottom r, which
hides the strip's rounded top corners. The content pitch is a multiple of 64
bytes so the strip may begin on any row. A title-less window's content is one
layer with radius r; an undecorated window has no cache at all.

The display mailbox payload now covers a full 8 MiB batch. Before this change
it held 4,147,200 bytes, and a batch carrying more than that in uploads overran
the shared mapping.

## 6. SDL2 mapping

The renderer is `sw/userspace/sdl2/render/SDL_astrarender.c`, registered the
same way as the video and audio drivers. Upstream SDL is unchanged.

| SDL callback | Astra lowering |
|---|---|
| CreateTexture | `astra_surface_create` in RGB565, XRGB8888 (`RGB888`), or ARGB8888; targets are CPU-readable |
| LockTexture / UnlockTexture | the lock is rows of the display's staging area; unlock appends their UPLOAD to the frame list. A second lock that does not fit beside an open one is a private copy, uploaded as UpdateTexture at unlock |
| UpdateTexture | `astra_display_stage` (copy engine) into staging beside the frame's other uploads, then an UPLOAD |
| SetRenderTarget | draw list destination = texture, any of the three formats |
| Clear | FILL |
| QueueFillRects, QueueDrawPoints | `astra_draw_rectangles` with the SDL blend mode; consecutive commands in one color and mode, one rectangle per SDL call as `testdraw2` draws, gather into one FILL_RECTS until another draw or a clip change |
| QueueDrawLines | opaque: `astra_draw_lines`, a strip as its segments, gathered across SDL commands like QueueFillRects; otherwise row spans of blended fills, gathered as rectangles |
| QueueCopy | BLIT with color and alpha modulation, blend mode, and the texture's scale mode |
| QueueCopyEx, 0 or 180 degrees | BLIT with FLIP_X/FLIP_Y |
| QueueCopyEx, any other angle | two textured triangles around the center |
| RenderPresent | `astra_window_present_discard`: SDL leaves the backbuffer undefined |
| QueueGeometry | `astra_draw_triangles`, indices expanded, uv times texture size |
| RenderPresent | submit the list, then `astra_window_present` |
| RenderReadPixels | `astra_surface_read`; `SDL_ConvertPixels` only for another format |

Anything the hardware cannot do fails the SDL call with a message. There is
no silent MC68040 fallback.

## 7. The texture engine

Rotation, geometry, linear filtering, color modulation, ADD/MOD/MUL, and
ARGB8888 targets are one engine: affine textured, color-interpolated
triangles, specified in `docs/TEXTURE_ENGINE.md`. The software side emits
`ASTRA_RENDER_OP_TRIANGLES` exactly as §1 there; the RTL is the FPGA line of
work. YUV textures remain SDL's upload conversion.

## 8. Phases

| Phase | Content | Gate |
|---|---|---|
| 0 | Mailbox payload covers 8 MiB batches | QEMU and helper build; display and terminal gates |
| 1 | Window graphics, surfaces, ADLT v1.3, render-only batches, `graphics.h` implemented | host tests (display, surface, NDK); QEMU gate creating, uploading, and blitting surfaces |
| 2 | SDL render driver over phase 1 | upstream `testdraw2`, `testsprite2`, `testrendertarget` run in QEMU; DE25 visual and performance gate |

Status 2026-09-26, uncommitted:

- Phases 0 to 2 are implemented.
- Host tests pass on beast, and each was checked against a deliberate
  perturbation of the code it covers:
  - graphics `test_surface`
  - display `test_display`, including the sanitizer build
  - NDK `make test`
  - `system-library-contract`
  - SDL `make test`, including `test_astravideo` and `test_astrarender`
- The display service and SDL build for m68k, and `SDL2.library` binds
  `ASTRA_SYSTEM_2.3`.
- QEMU boot gates and the DE25 visual and performance gate are next.
- ADLT does not yet carry the hardware's CIRCLE, ELLIPSE, PATTERN, and FLOOD
  operations, or text layouts. Those `graphics.h` calls return
  `UNSUPPORTED`.
| 3 | Readback and DMA-backed uploads | `RenderReadPixels`; measured upload cost |
| 4 | Texture engine: triangles, modulation, blend modes, ARGB targets | software: ADLT v1.4 (now v1.5), TRIANGLES lowering, SDL driver, host tests, QEMU gates; RTL: full route, timing, pixel tests; DE25 `testgeometry`, `testrendercopyex` |

Status 2026-09-26, uncommitted:

- Phase 3 readback is implemented end to end (kernel, QEMU, Linux helper,
  display service, NDK `astra_surface_read`, SDL `RenderReadPixels`).
  DMA-backed uploads are not.
- Phase 4 software is implemented: ADLT v1.4, `astra_draw_triangles`, blend
  modes and linear filtering in the NDK (`system.library` 2.4), ARGB8888
  targets, and every SDL path in §6. Host tests cover each layer with
  deliberate perturbation checks.
- The texture-engine RTL, and so DE25 pixel results for TRIANGLES, are the
  open half. QEMU counts TRIANGLES commands but renders nothing.
| 5 | Fullscreen modes and logical size through the display scaler | Chocolate Doom fullscreen on the DE25 |
