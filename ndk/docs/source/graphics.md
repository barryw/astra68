# Graphics and Display

A program draws through its window. The display service owns the screen,
validates every request, and drives the shared graphics engines; programs
never receive physical addresses or program chipset registers.

## Rendering flow

1. Create a window with `ASTRA_WINDOW_CONTENT_SURFACE` using
   {c:func}`astra_window_create`.
2. Bind a graphics connection to it with {c:func}`astra_window_display`, and
   borrow its content as a draw target with {c:func}`astra_window_surface`.
3. Create any further surfaces -- textures, sprites, back buffers -- with
   {c:func}`astra_surface_create`, stating their dimensions, format, and uses.
   Fill them from the CPU with {c:func}`astra_surface_write`, or through the
   display's staging area with {c:func}`astra_display_staging` and
   {c:func}`astra_surface_write_staged`.
4. Record drawing in an {c:struct}`AstraDrawList` created with a destination
   and a clip rectangle: blits, triangles, lines, rectangles, and UI text.
5. Submit it with {c:func}`astra_draw_submit`. The returned
   {c:struct}`AstraFence` signals when the work is done; wait on it, poll it,
   or close it if nothing depends on the result.
6. Show the frame with {c:func}`astra_window_present`,
   {c:func}`astra_window_present_region`, or
   {c:func}`astra_window_present_discard` when the next frame redraws every
   pixel.

A game hands each frame over instead: one list carries the whole frame --
{c:func}`astra_draw_list_set_target` moves it between render targets and
the window -- and {c:func}`astra_draw_post` with `ASTRA_DRAW_POST_PRESENT`
sends it and presents the window without waiting for either. The list
empties itself the next time it is changed, which waits only if the
display service has not read it yet. Requests sent to the window after a
post, such as {c:func}`astra_surface_write`, run after its commands.

The frame carries its texture uploads too. Write the pixels straight into
the display's staging area ({c:func}`astra_display_staging`), or copy them
there with {c:func}`astra_display_stage`, and append
{c:func}`astra_draw_upload`: it runs in order with the frame's draws, so
nothing waits for the device and nothing is sent until the post. The rows
belong to the service until the list is back -- changed again, or reset
with {c:func}`astra_draw_list_reset`, after the post -- so a frame lays its
uploads side by side and the next frame may reuse the area.

The screen is 1920x1080. A window's content is RGB565.
{c:func}`astra_display_layout_calculate` computes the exact crop and viewport
the hardware scaler would use for a logical scene, without floating point.

## Batching

Commands that share a paint batch: {c:func}`astra_draw_rectangles` appends
any number of filled rectangles as one command, which the service lowers to
one hardware rectangle list per 4,096 rectangles;
{c:func}`astra_draw_lines` does the same for line segments, and
{c:func}`astra_draw_triangles` for triangles. Every command costs the service
and the hardware work of its own, so one call per rectangle is the slow way
to draw points and spans.

Draw lists are mutable until submission and sealed while in flight.
Surfaces a list references stay alive until its fence signals, so closing
them early cannot corrupt work in flight.

## Surfaces and formats

Creation flags state whether a surface may be a draw source or target, or
read and written by the CPU. These are validation rights, not hints. The
service chooses the pitch and reports it through
{c:func}`astra_surface_get_info`. Draw targets are RGB565, XRGB8888,
ARGB8888, or INDEX8 (INDEX8 takes no blending and no triangles).

## The pointer

A window chooses the pointer shown over its content: a system shape with
{c:func}`astra_window_set_pointer_shape`, or its own 32x32 image with
{c:func}`astra_window_set_pointer_image`. The pointer is a hardware plane
composed after scaling, moved by the display service at vertical blank.

## Synchronizing with the display

A window that subscribes to `ASTRA_WINDOW_SUBSCRIBE_VBLANK` can wait on
{c:func}`astra_window_vblank_wait_handle`, which the display signals once per
frame.

## Lifetime

Use `ASTRA_AUTO_DISPLAY`, `ASTRA_AUTO_SURFACE`, `ASTRA_AUTO_DRAW_LIST`, and
`ASTRA_AUTO_FENCE` for scope cleanup. Cleanup closes handles but never
cancels submitted work. Close the display before its window.
