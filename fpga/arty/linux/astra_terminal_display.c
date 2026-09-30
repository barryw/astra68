// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"
#include "astra_render_protocol.h"
#include "astra_window_scene.h"

#include <astra/display.h>
#include <astra/display_mailbox.h>
#include <astra/graphics.h>
#include <astra/render_batch.h>
#include <astra/render_builder.h>
#include <astra/theme.h>
#include <astra/window_scene.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#ifdef ASTRA_HOST_APERTURE
#include <sys/ioctl.h>
#include "astra_host_arena_uapi.h"
#endif
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    TEXT_COLUMNS = ASTRA_TEXT_COLUMNS,
    TEXT_ROWS = ASTRA_TEXT_ROWS,
    TEXT_CELLS = TEXT_COLUMNS * TEXT_ROWS,
    TEXT_PAGE_BYTES = 4096u,
    TEXT_CELL_WIDTH = ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH,
    TEXT_CELL_HEIGHT = 24u,
    TEXT_ORIGIN_X = (ASTRA_DISPLAY_WIDTH -
                     TEXT_COLUMNS * TEXT_CELL_WIDTH) / 2u,
    TEXT_ORIGIN_Y = (ASTRA_DISPLAY_HEIGHT -
                     TEXT_ROWS * TEXT_CELL_HEIGHT) / 2u,
    TEXT_FONT_WIDTH = ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH,
    TEXT_FONT_HEIGHT = ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT,
    CURSOR_HEIGHT = 3u,
    CURSOR_BLINK_POLLS = 32u,
    POINTER_WIDTH = 16u,
    POINTER_HEIGHT = 24u,
    POINTER_HOT_X = 3u,
    POINTER_HOT_Y = 1u,
    POINTER_IMAGE_WIDTH = 32u,
    POINTER_IMAGE_HEIGHT = 32u,
    /* DE25 hardware counters: choose a blit only when it beats repainting. */
    DE25_GLYPH_CYCLES = 6200u,
    DE25_FILL_CYCLES_PER_PIXEL = 3u,
    DE25_BLIT_CYCLES_PER_100_PIXELS = 152u,
};

_Static_assert(TEXT_PAGE_BYTES == ASTRA_TEXT_PLANE_BYTES,
               "guest and renderer text pages differ");
_Static_assert(TEXT_COLUMNS * TEXT_CELL_WIDTH <= ASTRA_DISPLAY_WIDTH &&
                   TEXT_ROWS * TEXT_CELL_HEIGHT <= ASTRA_DISPLAY_HEIGHT,
               "text grid must fit the hardware scanout");
_Static_assert(POINTER_WIDTH <= POINTER_IMAGE_WIDTH &&
                   POINTER_HEIGHT <= POINTER_IMAGE_HEIGHT,
               "pointer artwork must fit the hardware pointer plane");

static volatile sig_atomic_t running = 1;
static uint8_t terminal_batch[ASTRA_RENDER_BUILDER_BYTES];
static uint32_t pointer_shape = ASTRA_POINTER_SHAPE_DEFAULT;

#define RENDER_TIMEOUT_NS UINT64_C(2000000000)

static const uint16_t pointer_outer[POINTER_HEIGHT] = {
    0x3000u, 0x3800u, 0x3c00u, 0x3e00u, 0x3f00u, 0x3f80u,
    0x3fc0u, 0x3fe0u, 0x3ff0u, 0x3ff0u, 0x3f80u, 0x3bc0u,
    0x33c0u, 0x01e0u, 0x01e0u, 0x00c0u, 0x0000u, 0x0000u,
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
};

static const uint16_t pointer_inner[POINTER_HEIGHT] = {
    0x0000u, 0x1000u, 0x1800u, 0x1c00u, 0x1e00u, 0x1f00u,
    0x1f80u, 0x1fc0u, 0x1fe0u, 0x1f00u, 0x1b00u, 0x1180u,
    0x0180u, 0x00c0u, 0x00c0u, 0x0000u, 0x0000u, 0x0000u,
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
};

static uint32_t pointer_builtin_pixel(uint32_t shape, unsigned x, unsigned y)
{
    int dx = (int)x - 16;
    int dy = (int)y - 16;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    bool outer = false;
    bool inner = false;

    if (shape == ASTRA_POINTER_SHAPE_DEFAULT) {
        if (x >= POINTER_WIDTH || y >= POINTER_HEIGHT)
            return 0u;
        {
            uint16_t bit = (uint16_t)(UINT16_C(0x8000) >> x);

            return (pointer_inner[y] & bit) != 0u ? UINT32_C(0xffffffff) :
                   (pointer_outer[y] & bit) != 0u ? UINT32_C(0xff000000) : 0u;
        }
    }
    if (shape == ASTRA_POINTER_SHAPE_RESIZE_HORIZONTAL) {
        outer = (ay <= 2 && x >= 4u && x <= 28u) ||
                (x >= 4u && x <= 11u &&
                 (int)(x - 4u) - ay >= -1 &&
                 (int)(x - 4u) - ay <= 1) ||
                (x >= 21u && x <= 28u &&
                 (int)(28u - x) - ay >= -1 &&
                 (int)(28u - x) - ay <= 1);
        inner = (ay == 0 && x >= 6u && x <= 26u) ||
                (x >= 5u && x <= 10u && (int)(x - 4u) == ay) ||
                (x >= 22u && x <= 27u && (int)(28u - x) == ay);
    } else if (shape == ASTRA_POINTER_SHAPE_RESIZE_VERTICAL) {
        outer = (ax <= 2 && y >= 4u && y <= 28u) ||
                (y >= 4u && y <= 11u &&
                 (int)(y - 4u) - ax >= -1 &&
                 (int)(y - 4u) - ax <= 1) ||
                (y >= 21u && y <= 28u &&
                 (int)(28u - y) - ax >= -1 &&
                 (int)(28u - y) - ax <= 1);
        inner = (ax == 0 && y >= 6u && y <= 26u) ||
                (y >= 5u && y <= 10u && (int)(y - 4u) == ax) ||
                (y >= 22u && y <= 27u && (int)(28u - y) == ax);
    } else if (shape == ASTRA_POINTER_SHAPE_RESIZE_NW_SE) {
        outer = (x >= 5u && x <= 27u && y >= 5u && y <= 27u &&
                 dx - dy >= -2 && dx - dy <= 2) ||
                (x >= 5u && x <= 13u && y >= 5u && y <= 13u &&
                 (x <= 8u || y <= 8u)) ||
                (x >= 19u && x <= 27u && y >= 19u && y <= 27u &&
                 (x >= 24u || y >= 24u));
        inner = (x >= 7u && x <= 25u && y >= 7u && y <= 25u && dx == dy) ||
                (x >= 7u && x <= 11u && y >= 7u && y <= 11u &&
                 (x == 7u || y == 7u)) ||
                (x >= 21u && x <= 25u && y >= 21u && y <= 25u &&
                 (x == 25u || y == 25u));
    } else if (shape == ASTRA_POINTER_SHAPE_RESIZE_NE_SW) {
        outer = (x >= 5u && x <= 27u && y >= 5u && y <= 27u &&
                 dx + dy >= -2 && dx + dy <= 2) ||
                (x >= 19u && x <= 27u && y >= 5u && y <= 13u &&
                 (x >= 24u || y <= 8u)) ||
                (x >= 5u && x <= 13u && y >= 19u && y <= 27u &&
                 (x <= 8u || y >= 24u));
        inner = (x >= 7u && x <= 25u && y >= 7u && y <= 25u &&
                 dx == -dy) ||
                (x >= 21u && x <= 25u && y >= 7u && y <= 11u &&
                 (x == 25u || y == 7u)) ||
                (x >= 7u && x <= 11u && y >= 21u && y <= 25u &&
                 (x == 7u || y == 25u));
    } else if (shape == ASTRA_POINTER_SHAPE_TEXT) {
        outer = (x >= 9u && x <= 23u &&
                 ((y >= 5u && y <= 8u) || (y >= 24u && y <= 27u))) ||
                (x >= 14u && x <= 18u && y >= 5u && y <= 27u);
        inner = (x >= 11u && x <= 21u && (y == 6u || y == 26u)) ||
                (x >= 15u && x <= 17u && y >= 6u && y <= 26u);
    } else if (shape == ASTRA_POINTER_SHAPE_WAIT) {
        outer = (x >= 8u && x <= 24u &&
                 ((y >= 4u && y <= 7u) || (y >= 25u && y <= 28u))) ||
                (y >= 7u && y <= 16u &&
                 (ax - (int)(y - 7u) / 2 >= 6 &&
                  ax - (int)(y - 7u) / 2 <= 8)) ||
                (y >= 16u && y <= 25u &&
                 (ax - (int)(25u - y) / 2 >= 6 &&
                  ax - (int)(25u - y) / 2 <= 8));
        inner = (x >= 10u && x <= 22u && (y == 6u || y == 26u)) ||
                (y >= 9u && y <= 14u && ax <= (int)(14u - y) / 2) ||
                (y >= 18u && y <= 23u && ax <= (int)(y - 18u) / 2);
    }
    return inner ? UINT32_C(0xffffffff) :
           outer ? UINT32_C(0xff000000) : 0u;
}

static void pointer_builtin_hotspot(uint32_t shape, unsigned *x, unsigned *y)
{
    *x = shape == ASTRA_POINTER_SHAPE_DEFAULT ? POINTER_HOT_X : 16u;
    *y = shape == ASTRA_POINTER_SHAPE_DEFAULT ? POINTER_HOT_Y : 16u;
}

static int pointer_write_builtin(const struct astra_graphics_device *device,
                                 uint32_t shape)
{
    unsigned hot_x;
    unsigned hot_y;

    if (shape >= ASTRA_POINTER_SHAPE_CUSTOM)
        return -1;
    astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_SELECTOR, 0u);
    for (unsigned y = 0u; y < POINTER_IMAGE_HEIGHT; ++y)
        for (unsigned x = 0u; x < POINTER_IMAGE_WIDTH; ++x)
            astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_DATA,
                             pointer_builtin_pixel(shape, x, y));
    pointer_builtin_hotspot(shape, &hot_x, &hot_y);
    astra_mmio_write(device, ASTRA_REG_POINTER_HOTSPOT,
                     (hot_y << 16) | hot_x);
    return 0;
}

static void stop(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static int wait_pointer_ready(
    const struct astra_graphics_device *device)
{
    uint64_t started = astra_monotonic_nanoseconds();
    uint64_t deadline = started + RENDER_TIMEOUT_NS;

    while ((astra_mmio_read(device, ASTRA_REG_POINTER_STATUS) &
            (ASTRA_POINTER_STATUS_WRITE_READY |
             ASTRA_POINTER_STATUS_COMMIT_READY)) !=
           (ASTRA_POINTER_STATUS_WRITE_READY |
            ASTRA_POINTER_STATUS_COMMIT_READY)) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        astra_graphics_poll_pause(started);
    }
    return 0;
}

static int wait_pointer_generation(
    const struct astra_graphics_device *device, uint32_t previous)
{
    uint64_t started = astra_monotonic_nanoseconds();
    uint64_t deadline = started + RENDER_TIMEOUT_NS;

    while (astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION) ==
           previous) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        astra_graphics_poll_pause(started);
    }
    return 0;
}

static int pointer_initialize(const struct astra_graphics_device *device)
{
    uint32_t generation;

    if ((astra_mmio_read(device, ASTRA_REG_CAPABILITIES) &
         ASTRA_CAP_HARDWARE_POINTER) == 0u || wait_pointer_ready(device) != 0)
        return -1;
    if (pointer_write_builtin(device, ASTRA_POINTER_SHAPE_DEFAULT) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_POINTER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_POINTER_POSITION, 0u);
    pointer_shape = ASTRA_POINTER_SHAPE_DEFAULT;
    generation = astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION);
    astra_mmio_write(device, ASTRA_REG_POINTER_COMMIT, 3u);
    return wait_pointer_generation(device, generation);
}

static int pointer_commit_begin(const struct astra_graphics_device *device,
                                uint32_t *generation, bool swap_image)
{
    if (wait_pointer_ready(device) != 0)
        return -1;
    *generation = astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION);
    astra_mmio_write(device, ASTRA_REG_POINTER_COMMIT,
                     swap_image ? 3u : 1u);
    return 0;
}

static uint32_t load_be32(const volatile uint8_t *bytes);

/*
 * A present issued and not yet on screen. The flip happens at vblank, and
 * only the next change to the screen has to wait for it: rendering never
 * does. Every request except a render-only batch finishes this first, so
 * when a request runs the screen shows everything presented before it --
 * which is what the display service's double banking already relies on.
 */
static struct {
    bool scene;
    struct astra_graphics_commit commit;
    bool pointer;
    uint32_t pointer_generation;
} pending_present;

static int pointer_update(const struct astra_graphics_device *device,
                          uint32_t packed, uint32_t flags, bool commit)
{
    uint32_t x = packed & ASTRA_DISPLAY_HOST_CURSOR_X_MASK;
    uint32_t y = (packed & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
                 ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT;
    uint32_t generation;
    uint32_t shape = (flags & ASTRA_DISPLAY_CURSOR_SHAPE_MASK) >>
                     ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT;
    bool swap_image = false;

    if ((flags & ~ASTRA_DISPLAY_CURSOR_FLAGS_MASK) != 0u ||
        shape >= ASTRA_POINTER_SHAPE_COUNT ||
        wait_pointer_ready(device) != 0)
        return -1;
    if (shape != pointer_shape) {
        if (shape != ASTRA_POINTER_SHAPE_CUSTOM &&
            pointer_write_builtin(device, shape) != 0)
            return -1;
        pointer_shape = shape;
        swap_image = shape != ASTRA_POINTER_SHAPE_CUSTOM;
    }
    if (x >= ASTRA_DISPLAY_WIDTH)
        x = ASTRA_DISPLAY_WIDTH - 1u;
    if (y >= ASTRA_DISPLAY_HEIGHT)
        y = ASTRA_DISPLAY_HEIGHT - 1u;
    astra_mmio_write(device, ASTRA_REG_POINTER_POSITION, (y << 16) | x);
    astra_mmio_write(device, ASTRA_REG_POINTER_CONTROL,
                     (flags & ASTRA_DISPLAY_CURSOR_VISIBLE) != 0u);
    if (!commit)
        return 0;
    /* Issued, not awaited: the next pointer change waits in
       wait_pointer_ready, and nothing else waits at all. */
    if (pointer_commit_begin(device, &generation, swap_image) != 0)
        return -1;
    pending_present.pointer = true;
    pending_present.pointer_generation = generation;
    return 0;
}

static bool pointer_image_valid(const volatile uint8_t *bytes,
                                uint32_t byte_size)
{
    uint32_t hotspot;

    if (bytes == NULL || byte_size != ASTRA_DISPLAY_CURSOR_IMAGE_BYTES ||
        load_be32(bytes) != ASTRA_DISPLAY_CURSOR_IMAGE_MAGIC ||
        load_be32(bytes + 4u) != ASTRA_DISPLAY_CURSOR_IMAGE_VERSION ||
        load_be32(bytes + 12u) != 0u)
        return false;
    hotspot = load_be32(bytes + 8u);
    return (hotspot & UINT32_C(0xffff)) <
               ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH &&
           (hotspot >> 16) < ASTRA_DISPLAY_CURSOR_IMAGE_HEIGHT;
}

static int pointer_image_update(const struct astra_graphics_device *device,
                                const volatile uint8_t *bytes)
{
    uint32_t generation;

    if (wait_pointer_ready(device) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_SELECTOR, 0u);
    for (uint32_t at = 0u; at < ASTRA_DISPLAY_CURSOR_IMAGE_PIXELS; ++at)
        astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_DATA,
                         load_be32(bytes + 16u + at * 4u));
    astra_mmio_write(device, ASTRA_REG_POINTER_HOTSPOT,
                     load_be32(bytes + 8u));
    pointer_shape = ASTRA_POINTER_SHAPE_CUSTOM;
    if (pointer_commit_begin(device, &generation, true) != 0)
        return -1;
    pending_present.pointer = true;
    pending_present.pointer_generation = generation;
    return 0;
}

struct terminal_cursor {
    uint32_t cell;
    bool visible;
};

struct terminal_scanout_state {
    uint8_t cells[TEXT_CELLS];
    struct terminal_cursor cursor;
    bool cursor_drawn;
    bool valid;
};

struct display_request {
    uint32_t sequence;
    uint32_t id;
    uint32_t operation;
    uint32_t color_rgb565;
    uint32_t frame_pitch;
    uint32_t frame_bytes;
};

/* Batches are read where the engine reads them, in memory mapped
   non-cacheable, where every load crosses the bus: one word load, not four
   byte loads, when the word is aligned. */
static uint32_t load_be32(const volatile uint8_t *bytes)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (((uintptr_t)bytes & 3u) == 0u)
        return __builtin_bswap32(
            *(const volatile uint32_t *)(const volatile void *)bytes);
#endif
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void store_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool batch_contains(uint32_t bytes, uint32_t arena_offset,
                           uint32_t length)
{
    uint32_t offset;

    if (arena_offset < ASTRA_RENDER_BATCH_ARENA_OFFSET)
        return false;
    offset = arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    return offset <= bytes && length <= bytes - offset;
}

static bool arena_ranges_overlap(uint32_t left, uint32_t left_bytes,
                                 uint32_t right, uint32_t right_bytes)
{
    return left < (uint64_t)right + right_bytes &&
           right < (uint64_t)left + left_bytes;
}

static bool batch_descriptor_valid(const volatile uint8_t *batch,
                                   uint32_t bytes, uint32_t arena_offset)
{
    uint32_t record;

    if (arena_offset < ASTRA_RENDER_BATCH_DATA_OFFSET ||
        (arena_offset & (ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES - 1u)) != 0u ||
        !batch_contains(bytes, arena_offset,
                        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES))
        return false;
    record = arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    /* The engine may not write the batch window: on the DE25 it reads the
       window from the host arena, so such a write would never be read back,
       and anywhere it would overwrite the batch being executed. */
    if ((load_be32(batch + record + 24u) >> 16 & ASTRA_RENDER_SURFACE_WRITE) !=
            0u &&
        (uint64_t)load_be32(batch + record + 8u) +
                load_be32(batch + record + 12u) >
            ASTRA_RENDER_BATCH_ARENA_OFFSET &&
        load_be32(batch + record + 8u) < ASTRA_RENDER_BATCH_WORKSPACE_LIMIT)
        return false;
    return load_be32(batch + record) ==
               ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES) &&
           load_be32(batch + record + 4u) == load_be32(batch + 24u) &&
           load_be32(batch + record + 20u) != 0u;
}

static bool batch_data_descriptor_valid(const volatile uint8_t *batch,
                                         uint32_t bytes,
                                         uint32_t arena_offset)
{
    uint32_t record;
    uint32_t data;
    uint32_t data_bytes;

    if (!batch_descriptor_valid(batch, bytes, arena_offset))
        return false;
    record = arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    data = load_be32(batch + record + 8u);
    data_bytes = load_be32(batch + record + 12u);
    return data >= ASTRA_RENDER_BATCH_DATA_OFFSET && data_bytes != 0u &&
           batch_contains(bytes, data, data_bytes);
}

static bool batch_glyphs_valid(uint32_t bytes, uint32_t arena_offset,
                               uint32_t count)
{
    return count != 0u && count <= ASTRA_RENDER_MAX_GLYPH_DESCRIPTORS &&
           arena_offset >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (arena_offset & (ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES - 1u)) == 0u &&
           batch_contains(bytes, arena_offset,
                          count * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES);
}

static bool batch_fill_rects_valid(uint32_t bytes, uint32_t arena_offset,
                                   uint32_t count)
{
    return count != 0u && count <= ASTRA_RENDER_MAX_FILL_RECTS &&
           arena_offset >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (arena_offset & (ASTRA_RENDER_FILL_RECT_BYTES - 1u)) == 0u &&
           batch_contains(bytes, arena_offset,
                          count * ASTRA_RENDER_FILL_RECT_BYTES);
}

static bool batch_lines_valid(uint32_t bytes, uint32_t arena_offset,
                              uint32_t count)
{
    return count != 0u && count <= ASTRA_RENDER_MAX_LINE_SEGMENTS &&
           arena_offset >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (arena_offset & (ASTRA_RENDER_LINE_SEGMENT_BYTES - 1u)) == 0u &&
           batch_contains(bytes, arena_offset,
                          count * ASTRA_RENDER_LINE_SEGMENT_BYTES);
}

static bool batch_triangles_valid(uint32_t bytes, uint32_t arena_offset,
                                  uint32_t count)
{
    return count != 0u && count <= ASTRA_RENDER_MAX_TRIANGLES &&
           arena_offset >= ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (arena_offset & 31u) == 0u &&
           batch_contains(bytes, arena_offset,
                          count * 3u * ASTRA_RENDER_TRIANGLE_VERTEX_BYTES);
}

/* Bytes per pixel of a byte-addressed render format, or zero. */
static uint32_t surface_read_pixel_bytes(uint32_t format)
{
    switch (format) {
    case ASTRA_RENDER_FORMAT_INDEX8:
    case ASTRA_RENDER_FORMAT_A8:
        return 1u;
    case ASTRA_RENDER_FORMAT_RGB565:
        return 2u;
    case ASTRA_RENDER_FORMAT_XRGB8888:
    case ASTRA_RENDER_FORMAT_ARGB8888:
        return 4u;
    default:
        return 0u;
    }
}

/* A READ_SURFACE header (docs/TEXTURE_ENGINE.md §8) whose surface lies in
   Media RAM and whose rectangle fills exactly @p bytes of request. */
static bool surface_read_valid(const volatile uint8_t *header,
                               uint32_t bytes)
{
    uint32_t data_offset = load_be32(header + 8u);
    uint32_t data_bytes = load_be32(header + 12u);
    uint32_t pitch = load_be32(header + 16u);
    uint32_t size = load_be32(header + 20u);
    uint32_t pixel = surface_read_pixel_bytes(load_be32(header + 24u));
    uint32_t origin = load_be32(header + 28u);
    uint32_t extent = load_be32(header + 32u);
    uint32_t width = size >> 16;
    uint32_t height = size & 0xffffu;
    uint32_t read_width = extent >> 16;
    uint32_t read_height = extent & 0xffffu;

    for (uint32_t offset = 36u;
         offset < ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES; offset += 4u)
        if (load_be32(header + offset) != 0u)
            return false;
    return bytes > ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES &&
           bytes <= ASTRA_RENDER_BATCH_MAX_BYTES &&
           load_be32(header) == ASTRA_DISPLAY_SURFACE_READ_MAGIC &&
           load_be32(header + 4u) == ASTRA_DISPLAY_SURFACE_READ_VERSION &&
           pixel != 0u && width != 0u && height != 0u &&
           data_offset >= ASTRA_RENDER_BATCH_WORKSPACE_LIMIT &&
           data_offset <= ASTRA_RENDER_BATCH_MEDIA_LIMIT &&
           data_bytes <= ASTRA_RENDER_BATCH_MEDIA_LIMIT - data_offset &&
           pitch >= width * pixel &&
           (uint64_t)pitch * height <= data_bytes &&
           read_width != 0u && read_height != 0u &&
           (origin >> 16) + read_width <= width &&
           (origin & 0xffffu) + read_height <= height &&
           ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES +
                   (uint64_t)read_width * pixel * read_height == bytes;
}

/* Copy a validated rectangle from the arena into the rows behind header. */
static int read_surface(const struct astra_graphics_device *device,
                        volatile uint8_t *request)
{
    struct astra_graphics_memory_map mapping;
    uint32_t pitch = load_be32(request + 16u);
    uint32_t pixel = surface_read_pixel_bytes(load_be32(request + 24u));
    uint32_t origin = load_be32(request + 28u);
    uint32_t extent = load_be32(request + 32u);
    uint32_t row = (extent >> 16) * pixel;
    uint32_t rows = extent & 0xffffu;
    uint32_t first = load_be32(request + 8u) + (origin & 0xffffu) * pitch +
                     (origin >> 16) * pixel;

    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping, ASTRA_GRAPHICS_ARENA_BASE + first,
            (size_t)pitch * (rows - 1u) + row) != 0)
        return -1;
    astra_graphics_memory_barrier();
    for (uint32_t y = 0u; y < rows; ++y)
        astra_graphics_memory_copy_from(
            (uint8_t *)(uintptr_t)request +
                ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES + (size_t)y * row,
            mapping.data + (size_t)y * pitch, row);
    astra_graphics_memory_map_close(&mapping);
    return 0;
}

static bool render_batch_valid(const volatile uint8_t *batch,
                               uint32_t bytes)
{
    uint32_t command_count;
    uint32_t presentation;
    uint32_t scene_offset;
    uint32_t scene_bytes;
    uint32_t version;

    if (bytes < ASTRA_RENDER_BATCH_MIN_BYTES ||
        bytes > ASTRA_RENDER_BATCH_MAX_BYTES ||
        load_be32(batch + 0u) != ASTRA_RENDER_BATCH_MAGIC ||
        load_be32(batch + 8u) != bytes ||
        load_be32(batch + 16u) != ASTRA_RENDER_BATCH_SUBMISSION_OFFSET ||
        load_be32(batch + 20u) != ASTRA_RENDER_BATCH_COMPLETION_OFFSET ||
        load_be32(batch + 24u) == 0u ||
        (load_be32(batch + 28u) != ASTRA_RENDER_BATCH_SCANOUT0_OFFSET &&
         load_be32(batch + 28u) != ASTRA_RENDER_BATCH_SCANOUT1_OFFSET))
        return false;
    version = load_be32(batch + 4u);
    scene_offset = load_be32(batch + 48u);
    scene_bytes = load_be32(batch + 52u);
    if ((version != ASTRA_RENDER_BATCH_VERSION_1_2 &&
         version != ASTRA_RENDER_BATCH_VERSION_1_3 &&
         version != ASTRA_RENDER_BATCH_VERSION_1_4) ||
        ((version == ASTRA_RENDER_BATCH_VERSION_1_2 ||
          version == ASTRA_RENDER_BATCH_VERSION_1_4) &&
         (scene_offset != 0u || scene_bytes != 0u)) ||
        (version == ASTRA_RENDER_BATCH_VERSION_1_3 &&
         (scene_offset < ASTRA_RENDER_BATCH_DATA_OFFSET ||
          scene_bytes < ASTRA_WINDOW_SCENE_HEADER_BYTES ||
          !batch_contains(bytes, scene_offset, scene_bytes))))
        return false;
    presentation = load_be32(batch + 32u);
    if ((presentation & ~ASTRA_RENDER_BATCH_PRESENT_CURSOR) != 0u ||
        (version == ASTRA_RENDER_BATCH_VERSION_1_4 && presentation != 0u) ||
        ((presentation & ASTRA_RENDER_BATCH_PRESENT_CURSOR) != 0u ?
             (load_be32(batch + 36u) >= ASTRA_DISPLAY_WIDTH ||
              load_be32(batch + 40u) >= ASTRA_DISPLAY_HEIGHT ||
              (load_be32(batch + 44u) &
               ~ASTRA_DISPLAY_CURSOR_FLAGS_MASK) != 0u ||
              ((load_be32(batch + 44u) &
                ASTRA_DISPLAY_CURSOR_SHAPE_MASK) >>
                   ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT) >=
                  ASTRA_POINTER_SHAPE_COUNT) :
             (load_be32(batch + 36u) != 0u ||
              load_be32(batch + 40u) != 0u ||
              load_be32(batch + 44u) != 0u)))
        return false;
    command_count = load_be32(batch + 12u);
    if ((command_count == 0u &&
         version != ASTRA_RENDER_BATCH_VERSION_1_3) ||
        command_count > ASTRA_RENDER_RING_ENTRIES)
        return false;
    for (uint32_t offset = 56u; offset < ASTRA_RENDER_BATCH_HEADER_BYTES;
         offset += 4u)
        if (load_be32(batch + offset) != 0u)
            return false;
    for (uint32_t index = 0u; index < command_count; ++index) {
        uint32_t offset = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;
        uint32_t operation = load_be32(batch + offset + 4u) >> 16;

        if (load_be32(batch + offset) !=
                ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                 ASTRA_RENDER_COMMAND_BYTES) ||
            load_be32(batch + offset + 8u) == 0u ||
            load_be32(batch + offset + 12u) != load_be32(batch + 24u) ||
            !batch_descriptor_valid(
                batch, bytes, load_be32(batch + offset + 32u)) ||
            ((operation == ASTRA_RENDER_OP_BLIT ||
              operation == ASTRA_RENDER_OP_GLYPH_RUN) &&
             !batch_descriptor_valid(
                 batch, bytes, load_be32(batch + offset + 36u))) ||
            (operation == ASTRA_RENDER_OP_GLYPH_RUN &&
             (!batch_data_descriptor_valid(
                  batch, bytes, load_be32(batch + offset + 36u)) ||
              !batch_glyphs_valid(bytes, load_be32(batch + offset + 40u),
                                  load_be32(batch + offset + 44u)))) ||
            (operation == ASTRA_RENDER_OP_FILL_RECTS &&
             !batch_fill_rects_valid(
                 bytes,
                 load_be32(batch + offset +
                           ASTRA_RENDER_FILL_RECTS_WORD_RECORD_OFFSET * 4u),
                 load_be32(batch + offset +
                           ASTRA_RENDER_FILL_RECTS_WORD_RECORD_COUNT * 4u))) ||
            (operation == ASTRA_RENDER_OP_LINES &&
             !batch_lines_valid(
                 bytes,
                 load_be32(batch + offset +
                           ASTRA_RENDER_LINES_WORD_SEGMENT_OFFSET * 4u),
                 load_be32(batch + offset +
                           ASTRA_RENDER_LINES_WORD_SEGMENT_COUNT * 4u))) ||
            (operation == ASTRA_RENDER_OP_TRIANGLES &&
             ((load_be32(batch + offset + 36u) != 0u &&
               !batch_descriptor_valid(
                   batch, bytes, load_be32(batch + offset + 36u))) ||
              !batch_triangles_valid(bytes, load_be32(batch + offset + 40u),
                                     load_be32(batch + offset + 44u)))))
            return false;
    }
    return true;
}

static bool render_batch_cursor(const volatile uint8_t *batch,
                                uint32_t *packed, uint32_t *flags)
{
    if ((load_be32(batch + 32u) & ASTRA_RENDER_BATCH_PRESENT_CURSOR) == 0u)
        return false;
    *packed = ASTRA_DISPLAY_HOST_CURSOR_PACK(
        load_be32(batch + 36u), load_be32(batch + 40u),
        (load_be32(batch + 44u) & ASTRA_DISPLAY_CURSOR_VISIBLE) != 0u);
    *flags = load_be32(batch + 44u);
    return true;
}

/* Reports and clears the fabric's record of refused or unmapped register
   accesses. Silent when it is empty or the bitstream has none. */
static void log_access_faults(const struct astra_graphics_device *device,
                              const char *when)
{
    struct astra_access_fault fault;

    if (astra_graphics_access_fault_take(device, &fault) != 1)
        return;
    fprintf(stderr,
            "graphics access faults %s: count=%u first=%s %s offset=0x%04x\n",
            when, fault.count,
            (fault.first & ASTRA_ACCESS_FAULT_STORE) != 0u ? "store" : "load",
            ((fault.first >> ASTRA_ACCESS_FAULT_REASON_SHIFT) &
             ASTRA_ACCESS_FAULT_REASON_MASK) == ASTRA_ACCESS_FAULT_REJECTED ?
                "rejected" : "unmapped",
            fault.first & ASTRA_ACCESS_FAULT_OFFSET_MASK);
}

static int wait_render(const struct astra_graphics_device *device,
                       uint32_t command_count)
{
    uint64_t started = astra_monotonic_nanoseconds();
    uint64_t deadline = started + RENDER_TIMEOUT_NS;

    for (;;) {
        uint32_t completed = astra_mmio_read(
            device, ASTRA_REG_RENDER_COMPLETION_PRODUCER);
        uint32_t status = astra_mmio_read(device, ASTRA_REG_RENDER_STATUS);

        if (completed == command_count &&
            (status & ASTRA_RENDER_ENGINE_BUSY) == 0u)
            return 0;
        if ((status & ASTRA_RENDER_ENGINE_CONFIG_FAULT) != 0u ||
            astra_monotonic_nanoseconds() >= deadline)
            return -1;
        astra_graphics_poll_pause(started);
    }
}

#ifdef ASTRA_HOST_APERTURE
/* The payload is the host arena, and the render engine reads a guest batch
   from it through the host aperture: nothing to copy. QEMU completed its
   payload stores with a DSB before publishing the request, and mailbox_take
   ordered this thread after that with its own; the engine writes (the
   completion ring) still land in media RAM.
   A batch the helper builds itself (terminal text, the cursor, retiring a
   render target) lives in the helper's memory, not in the payload. It is
   copied into media RAM like on a build without the aperture, and runs
   with the aperture off: with the aperture on, the engine would read
   whatever the guest last left in the payload. */
static uint32_t host_aperture_base;
static const volatile uint8_t *host_payload;
static uint32_t active_aperture_base = UINT32_MAX;

/* The engine must be stopped: the aperture refuses a store otherwise. */
static int select_render_aperture(const struct astra_graphics_device *device,
                                  uint32_t base)
{
    if (base == active_aperture_base)
        return 0;
    if (astra_graphics_render_host_aperture_set(device, base,
                                                RENDER_TIMEOUT_NS) != 0)
        return -1;
    active_aperture_base = base;
    return 0;
}
#endif

/* Copies the header, the commands and the resources into the batch window
   of the arena, where the engine reads them without the host aperture. */
static void stage_render_batch(volatile uint8_t *arena_window,
                               const volatile uint8_t *batch, uint32_t bytes,
                               uint32_t command_bytes)
{
    uint32_t resource_start = ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                              ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t data_start = ASTRA_RENDER_BATCH_DATA_OFFSET -
                          ASTRA_RENDER_BATCH_ARENA_OFFSET;

    astra_graphics_memory_copy_to(arena_window, (const void *)batch,
                                  ASTRA_RENDER_BATCH_HEADER_BYTES);
    astra_graphics_memory_copy_to(
        arena_window + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET,
        (const uint8_t *)(const void *)batch +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET,
        command_bytes);
    if (bytes > resource_start) {
        uint32_t resource_end = bytes < data_start ? bytes : data_start;

        astra_graphics_memory_copy_to(
            arena_window + resource_start,
            (const uint8_t *)(const void *)batch + resource_start,
            resource_end - resource_start);
    }
    if (bytes > data_start)
        astra_graphics_memory_copy_to(
            arena_window + data_start,
            (const uint8_t *)(const void *)batch + data_start,
            bytes - data_start);
    astra_graphics_memory_barrier();
}

/* With ASTRA_DISPLAY_STALL_DUMP=DIR, a stalled or failed batch is written to
   DIR/stall-batch.bin as submitted, with the render register bank in
   DIR/stall-registers.txt, so the command that stopped the engine can be
   replayed off the board. Only the first stall of a process is kept. */
static void dump_stalled_batch(const struct astra_graphics_device *device,
                               const volatile uint8_t *batch, uint32_t bytes)
{
    static int dumped;
    const char *directory = getenv("ASTRA_DISPLAY_STALL_DUMP");
    char path[512];
    uint8_t *copy;
    FILE *file;

    if (directory == NULL || dumped)
        return;
    dumped = 1;
    copy = malloc(bytes);
    if (copy == NULL)
        return;
    for (uint32_t at = 0u; at < bytes; ++at)
        copy[at] = batch[at];
    snprintf(path, sizeof(path), "%s/stall-batch.bin", directory);
    file = fopen(path, "wb");
    if (file != NULL) {
        fwrite(copy, 1, bytes, file);
        fclose(file);
    }
    free(copy);
    snprintf(path, sizeof(path), "%s/stall-registers.txt", directory);
    file = fopen(path, "w");
    if (file == NULL)
        return;
    for (unsigned offset = ASTRA_REG_RENDER_CONTROL; offset <= 0x244u;
         offset += 4u)
        fprintf(file, "%03x %08x\n", offset, astra_mmio_read(device, offset));
    fclose(file);
    fprintf(stderr, "render stall dumped to %s\n", directory);
}

/* @p validated: the caller has already checked the batch with
   render_batch_valid(); guest batches are, when their request is taken. */
static int execute_render_batch(const struct astra_graphics_device *device,
                                const volatile uint8_t *batch,
                                uint32_t bytes, bool validated,
                                uint32_t *scanout_offset)
{
    struct astra_graphics_memory_map mapping;
    uint64_t profile_started = astra_monotonic_nanoseconds();
    uint64_t profile_copied;
    uint64_t profile_rendered;
    uint32_t command_count;
    uint32_t command_bytes;
    uint32_t generation;
    uint32_t records;
    uint32_t failed_before = 0u;
    uint32_t completed_before = 0u;
    bool from_payload = false;
    int result = -1;
    static int profile_commands = -1;

    if (profile_commands < 0)
        profile_commands = getenv("ASTRA_DISPLAY_PROFILE_COMMANDS") != NULL;
    if (!validated && !render_batch_valid(batch, bytes)) {
        fprintf(stderr, "render batch rejected before submission (%u bytes)\n",
                bytes);
        return -1;
    }
    *scanout_offset = load_be32(batch + 28u);
    command_count = load_be32(batch + 12u);
    command_bytes = command_count * ASTRA_RENDER_COMMAND_BYTES;
    records = command_count;
    generation = load_be32(batch + 24u);
    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping,
            ASTRA_GRAPHICS_ARENA_BASE + ASTRA_RENDER_BATCH_ARENA_OFFSET,
            bytes) != 0) {
        fprintf(stderr, "render batch graphics mapping failed\n");
        return -1;
    }
#ifdef ASTRA_HOST_APERTURE
    from_payload = batch == host_payload;
#endif
    if (!from_payload)
        stage_render_batch(mapping.data, batch, bytes, command_bytes);
    profile_copied = astra_monotonic_nanoseconds();

    if (command_count != 0u) {
        if (astra_graphics_render_stop(device, RENDER_TIMEOUT_NS) != 0) {
            fprintf(stderr, "render engine did not stop: status=0x%08x\n",
                    astra_mmio_read(device, ASTRA_REG_RENDER_STATUS));
            goto done;
        }
#ifdef ASTRA_HOST_APERTURE
        if (select_render_aperture(device,
                                   from_payload ? host_aperture_base : 0u) !=
            0)
            goto done;
#endif
        astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER, 0u);
        astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_CONSUMER, 0u);
        astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET,
                         ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
        astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET,
                         ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
        astra_mmio_write(device, ASTRA_REG_RENDER_RESOURCE_GENERATION,
                         generation);
        astra_mmio_write(device, ASTRA_REG_RENDER_IRQ_PENDING, 1u);
        astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                         ASTRA_RENDER_CONTROL_REBASE);
        if (astra_mmio_read(device,
                           ASTRA_REG_RENDER_SUBMISSION_CONSUMER) != 0u ||
            astra_mmio_read(device,
                           ASTRA_REG_RENDER_COMPLETION_PRODUCER) != 0u) {
            fprintf(stderr, "render batch rebase failed\n");
            goto done;
        }
        failed_before = astra_mmio_read(device,
                                        ASTRA_REG_RENDER_COMMANDS_FAILED);
        completed_before = astra_mmio_read(
            device, ASTRA_REG_RENDER_COMMANDS_COMPLETED);
        astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                         ASTRA_RENDER_CONTROL_ENABLE);
        astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                         command_count);
        if (wait_render(device, command_count) != 0) {
            fprintf(stderr,
                    "render batch stalled: commands=%u completed=%u status=0x%08x\n",
                    command_count,
                    astra_mmio_read(device,
                                    ASTRA_REG_RENDER_COMPLETION_PRODUCER),
                    astra_mmio_read(device, ASTRA_REG_RENDER_STATUS));
            log_access_faults(device, "at render stall");
            dump_stalled_batch(device, batch, bytes);
            goto done;
        }
    }
    profile_rendered = astra_monotonic_nanoseconds();
    if (getenv("ASTRA_DISPLAY_PROFILE") != NULL)
        fprintf(stderr,
                "render profile copy_us=%llu hardware_us=%llu bytes=%u "
                "commands=%u staged=%d\n",
                (unsigned long long)
                    ((profile_copied - profile_started) / 1000u),
                (unsigned long long)
                    ((profile_rendered - profile_copied) / 1000u),
                bytes, command_count, !from_payload);
    /* Every command retires into COMPLETED, and a failed one into FAILED
       too. When the counters say all succeeded the records need not be read:
       each is several reads across the bridge, a few milliseconds a batch. */
    if (!profile_commands && command_count != 0u &&
        astra_mmio_read(device, ASTRA_REG_RENDER_COMMANDS_FAILED) ==
            failed_before &&
        astra_mmio_read(device, ASTRA_REG_RENDER_COMMANDS_COMPLETED) -
                completed_before == command_count)
        records = 0u;
    for (uint32_t index = 0u; index < records; ++index) {
        uint32_t offset = ASTRA_RENDER_BATCH_COMPLETION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMPLETION_BYTES;
        uint32_t command_offset = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;
        volatile uint8_t *completion = mapping.data + offset;

        if (profile_commands)
            fprintf(stderr,
                    "render cmd %u op=%u dst=%u src=%u "
                    "size=%ux%u pixels=%u cycles=%u\n",
                    index, load_be32(completion + 4u) >> 16,
                    load_be32(batch + command_offset + 32u),
                    load_be32(batch + command_offset + 36u),
                    load_be32(batch + command_offset + 56u) >> 16,
                    load_be32(batch + command_offset + 56u) & 0xffffu,
                    load_be32(completion + 12u),
                    load_be32(completion + 20u) -
                        load_be32(completion + 16u));
        if (load_be32(completion) !=
                ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                 ASTRA_RENDER_COMPLETION_BYTES) ||
            (load_be32(completion + 4u) & UINT32_C(0xffff)) !=
                ASTRA_RENDER_STATUS_OK ||
            load_be32(completion + 28u) != generation) {
            fprintf(stderr,
                    "render command %u failed: op=%u completion=0x%08x "
                    "status=0x%08x fault=0x%08x generation=%u/%u\n",
                    index, load_be32(batch + command_offset + 4u) >> 16,
                    load_be32(completion), load_be32(completion + 4u),
                    load_be32(completion + 24u),
                    load_be32(completion + 28u), generation);
            dump_stalled_batch(device, batch, bytes);
            goto done;
        }
    }
    result = 0;

done:
    astra_graphics_memory_map_close(&mapping);
    return result;
}

static int compile_window_scene(
    const struct astra_graphics_device *device,
    const volatile uint8_t *batch, uint32_t batch_bytes,
    uint32_t *output_offset_out, uint32_t *output_bytes_out,
    uint32_t *dimensions_out)
{
    struct astra_graphics_memory_map output;
    uint32_t scene_offset = load_be32(batch + 48u);
    uint32_t scene_bytes = load_be32(batch + 52u);
    uint32_t scene_record = scene_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t output_offset = load_be32(batch + scene_record + 28u);
    uint32_t output_capacity = load_be32(batch + scene_record + 32u);
    uint32_t written = 0u;
    int status;

    static uint8_t *compiled;
    static uint32_t compiled_capacity;

    if (!batch_contains(batch_bytes, scene_offset, scene_bytes))
        return -1;
    if (output_capacity > compiled_capacity) {
        uint8_t *grown = realloc(compiled, output_capacity);

        if (grown == NULL)
            return -1;
        compiled = grown;
        compiled_capacity = output_capacity;
    }
    status = astra_window_scene_compile(
        (const uint8_t *)(const void *)batch + scene_record, scene_bytes,
        compiled, output_capacity, ASTRA_GRAPHICS_ARENA_BYTES, &written);
    if (status == ASTRA_WINDOW_SCENE_OK) {
        astra_graphics_memory_map_init(&output);
        if (astra_graphics_memory_map_open(
                device, &output, ASTRA_GRAPHICS_ARENA_BASE + output_offset,
                output_capacity) != 0)
            return -1;
        /*
         * Invalidate, body, then header: a scene is valid only once it is
         * complete, and never while its old header still describes it.
         */
        astra_graphics_memory_fill(output.data, 0u,
                                   ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES);
        astra_graphics_memory_barrier();
        astra_graphics_memory_copy_to(
            output.data + ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES,
            compiled + ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES,
            written - ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES);
        astra_graphics_memory_barrier();
        astra_graphics_memory_copy_to(
            output.data, compiled, ASTRA_WINDOW_SCENE_COMPILED_HEADER_BYTES);
        astra_graphics_memory_barrier();
        astra_graphics_memory_map_close(&output);
    }
    if (status != ASTRA_WINDOW_SCENE_OK) {
        fprintf(stderr, "window scene compile failed: %d\n", status);
        return -1;
    }
    *output_offset_out = output_offset;
    *output_bytes_out = written;
    *dimensions_out = load_be32(batch + scene_record + 16u);
    return 0;
}

static bool mailbox_take(volatile const AstraDisplayMailbox *mailbox,
                         uint32_t previous_sequence,
                         struct display_request *request)
{
    uint32_t sequence = mailbox->request_sequence;

    astra_graphics_memory_barrier();
    if (mailbox->magic != ASTRA_DISPLAY_MAILBOX_MAGIC ||
        mailbox->version != ASTRA_DISPLAY_MAILBOX_VERSION_1_7 ||
        sequence == 0u || sequence == previous_sequence)
        return false;
    request->sequence = sequence;
    request->id = mailbox->request_id;
    request->operation = mailbox->operation;
    request->color_rgb565 = mailbox->color_rgb565;
    request->frame_pitch = mailbox->frame_pitch;
    request->frame_bytes = mailbox->frame_bytes;
    astra_graphics_memory_barrier();
    return mailbox->request_sequence == sequence;
}

static void mailbox_complete(volatile AstraDisplayMailbox *mailbox,
                             const struct display_request *request,
                             uint32_t status, uint32_t generation)
{
    mailbox->completion_id = request->id;
    mailbox->completion_status = status;
    mailbox->completion_generation = generation;
    astra_graphics_memory_barrier();
    mailbox->completion_sequence = request->sequence;
    /* The emulator sleeps on completion_sequence. */
    (void)syscall(SYS_futex, &mailbox->completion_sequence, FUTEX_WAKE, 1,
                  NULL, NULL, 0);
}

static int mailbox_wait(volatile AstraDisplayMailbox *mailbox,
                        uint32_t sequence)
{
    while (running && mailbox->request_sequence == sequence) {
        if (syscall(SYS_futex, &mailbox->request_sequence,
                    FUTEX_WAIT, sequence, NULL, NULL, 0) == 0 ||
            errno == EAGAIN || errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static const uint16_t cp437_unicode[128] = {
    0x00c7u, 0x00fcu, 0x00e9u, 0x00e2u, 0x00e4u, 0x00e0u, 0x00e5u, 0x00e7u,
    0x00eau, 0x00ebu, 0x00e8u, 0x00efu, 0x00eeu, 0x00ecu, 0x00c4u, 0x00c5u,
    0x00c9u, 0x00e6u, 0x00c6u, 0x00f4u, 0x00f6u, 0x00f2u, 0x00fbu, 0x00f9u,
    0x00ffu, 0x00d6u, 0x00dcu, 0x00a2u, 0x00a3u, 0x00a5u, 0x20a7u, 0x0192u,
    0x00e1u, 0x00edu, 0x00f3u, 0x00fau, 0x00f1u, 0x00d1u, 0x00aau, 0x00bau,
    0x00bfu, 0x2310u, 0x00acu, 0x00bdu, 0x00bcu, 0x00a1u, 0x00abu, 0x00bbu,
    0x2591u, 0x2592u, 0x2593u, 0x2502u, 0x2524u, 0x2561u, 0x2562u, 0x2556u,
    0x2555u, 0x2563u, 0x2551u, 0x2557u, 0x255du, 0x255cu, 0x255bu, 0x2510u,
    0x2514u, 0x2534u, 0x252cu, 0x251cu, 0x2500u, 0x253cu, 0x255eu, 0x255fu,
    0x255au, 0x2554u, 0x2569u, 0x2566u, 0x2560u, 0x2550u, 0x256cu, 0x2567u,
    0x2568u, 0x2564u, 0x2565u, 0x2559u, 0x2558u, 0x2552u, 0x2553u, 0x256bu,
    0x256au, 0x2518u, 0x250cu, 0x2588u, 0x2584u, 0x258cu, 0x2590u, 0x2580u,
    0x03b1u, 0x00dfu, 0x0393u, 0x03c0u, 0x03a3u, 0x03c3u, 0x00b5u, 0x03c4u,
    0x03a6u, 0x0398u, 0x03a9u, 0x03b4u, 0x221eu, 0x03c6u, 0x03b5u, 0x2229u,
    0x2261u, 0x00b1u, 0x2265u, 0x2264u, 0x2320u, 0x2321u, 0x00f7u, 0x2248u,
    0x00b0u, 0x2219u, 0x00b7u, 0x221au, 0x207fu, 0x00b2u, 0x25a0u, 0x00a0u,
};

static uint32_t cp437_to_utf8(char *out, const uint8_t *cells,
                              uint32_t count)
{
    uint32_t bytes = 0u;

    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t scalar = cells[index] < 0x80u ?
            cells[index] : cp437_unicode[cells[index] - 0x80u];

        if (scalar < 0x80u) {
            out[bytes++] = (char)scalar;
        } else if (scalar < 0x800u) {
            out[bytes++] = (char)(0xc0u | (scalar >> 6));
            out[bytes++] = (char)(0x80u | (scalar & 0x3fu));
        } else {
            out[bytes++] = (char)(0xe0u | (scalar >> 12));
            out[bytes++] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
            out[bytes++] = (char)(0x80u | (scalar & 0x3fu));
        }
    }
    return bytes;
}

static bool copy_cells(uint8_t out[TEXT_CELLS],
                       volatile const uint8_t *plane)
{
    unsigned attempt;

    for (attempt = 0; attempt < 4u; ++attempt) {
        uint8_t first_sequence =
            plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET];
        uint32_t cell;
        uint8_t second_sequence;

        if ((first_sequence & 1u) != 0u)
            continue;
        astra_graphics_memory_barrier();
        for (cell = 0; cell < TEXT_CELLS; ++cell)
            out[cell] = plane[cell];
        astra_graphics_memory_barrier();
        second_sequence = plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET];
        if (first_sequence == second_sequence &&
            (second_sequence & 1u) == 0u)
            return true;
    }
    return false;
}

static bool copy_cursor(struct terminal_cursor *out,
                        volatile const uint8_t *plane)
{
    unsigned attempt;

    for (attempt = 0; attempt < 4u; ++attempt) {
        uint8_t first_sequence = plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET];
        uint8_t row;
        uint8_t column;
        uint8_t flags;
        bool magic;
        uint8_t second_sequence;

        astra_graphics_memory_barrier();
        magic = plane[ASTRA_TEXT_CURSOR_OFFSET + 0u] ==
                    ASTRA_TEXT_CURSOR_MAGIC_0 &&
                plane[ASTRA_TEXT_CURSOR_OFFSET + 1u] ==
                    ASTRA_TEXT_CURSOR_MAGIC_1 &&
                plane[ASTRA_TEXT_CURSOR_OFFSET + 2u] ==
                    ASTRA_TEXT_CURSOR_MAGIC_2 &&
                plane[ASTRA_TEXT_CURSOR_OFFSET + 3u] ==
                    ASTRA_TEXT_CURSOR_MAGIC_3;
        row = plane[ASTRA_TEXT_CURSOR_ROW_OFFSET];
        column = plane[ASTRA_TEXT_CURSOR_COLUMN_OFFSET];
        flags = plane[ASTRA_TEXT_CURSOR_FLAGS_OFFSET];
        astra_graphics_memory_barrier();
        second_sequence = plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET];
        if (first_sequence != second_sequence ||
            (second_sequence & 1u) != 0u)
            continue;
        out->visible = false;
        out->cell = 0u;
        if (magic && (flags & ASTRA_TEXT_CURSOR_VISIBLE) != 0u &&
            row < TEXT_ROWS && column <= TEXT_COLUMNS) {
            uint32_t cell = (uint32_t)row * TEXT_COLUMNS + column;

            out->cell = cell < TEXT_CELLS ? cell : TEXT_CELLS - 1u;
            out->visible = true;
        }
        return true;
    }
    return false;
}

static int finish_pending_present(const struct astra_graphics_device *device)
{
    int status = 0;

    if (pending_present.scene &&
        astra_graphics_scene_commit_wait(device, &pending_present.commit,
                                         UINT64_C(2000000000), NULL) != 0)
        status = -1;
    if (pending_present.pointer &&
        wait_pointer_generation(device,
                                pending_present.pointer_generation) != 0)
        status = -1;
    pending_present.scene = false;
    pending_present.pointer = false;
    return status;
}

static int issue_present(const struct astra_graphics_device *device,
                         bool commit_pointer)
{
    if (commit_pointer &&
        pointer_commit_begin(device, &pending_present.pointer_generation,
                             false) != 0)
        return -1;
    pending_present.pointer = commit_pointer;
    astra_graphics_scene_commit_begin(device, &pending_present.commit);
    pending_present.scene = true;
    return 0;
}

static int present(const struct astra_graphics_device *device,
                   uint32_t scanout_offset, bool commit_pointer)
{
    uint32_t size = (ASTRA_FRAMEBUFFER_HEIGHT << 16) |
                    ASTRA_FRAMEBUFFER_WIDTH;

    if (astra_mmio_read(device, ASTRA_REG_ARENA_BASE) !=
            ASTRA_GRAPHICS_ARENA_BASE ||
        astra_mmio_read(device, ASTRA_REG_ARENA_LIMIT) !=
            ASTRA_GRAPHICS_ARENA_LIMIT) {
        fprintf(stderr, "graphics arena does not match the terminal\n");
        return -1;
    }
    astra_graphics_scene_prepare_empty(device);
    astra_mmio_write(device, ASTRA_REG_FB_BASE,
                     ASTRA_FRAMEBUFFER_BASE + scanout_offset);
    astra_mmio_write(device, ASTRA_REG_FB_PITCH, ASTRA_FRAMEBUFFER_PITCH);
    astra_mmio_write(device, ASTRA_REG_FB_SIZE, size);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_X, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_Y, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL, 3u);
    astra_mmio_write(device, ASTRA_REG_FB_KEY, 0u);
    return issue_present(device, commit_pointer);
}

/* A present whose caller draws next into what was on screen: it waits. */
static int present_now(const struct astra_graphics_device *device,
                       uint32_t scanout_offset, bool commit_pointer)
{
    int status = present(device, scanout_offset, commit_pointer);

    return finish_pending_present(device) == 0 && status == 0 ? 0 : -1;
}

static int window_scene_layout(uint32_t dimensions,
                               AstraDisplayLayout *layout)
{
    AstraDisplayMode mode = ASTRA_DISPLAY_MODE_INIT;

    mode.width = (uint16_t)(dimensions >> 16);
    mode.height = (uint16_t)dimensions;
    return astra_display_layout_calculate(
        &mode, ASTRA_FRAMEBUFFER_WIDTH, ASTRA_FRAMEBUFFER_HEIGHT,
        layout) == ASTRA_OK ? 0 : -1;
}

static int present_window_scene(const struct astra_graphics_device *device,
                                uint32_t scene_offset,
                                uint32_t scene_bytes,
                                uint32_t dimensions,
                                bool commit_pointer)
{
    AstraDisplayLayout layout;

    if (window_scene_layout(dimensions, &layout) != 0)
        return -1;
    astra_graphics_scene_prepare_empty(device);
    astra_mmio_write(device, ASTRA_REG_FB_BASE,
                     ASTRA_GRAPHICS_ARENA_BASE + scene_offset);
    astra_mmio_write(device, ASTRA_REG_FB_PITCH, ASTRA_FRAMEBUFFER_PITCH);
    astra_mmio_write(device, ASTRA_REG_FB_SIZE, dimensions);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_X, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_Y, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL,
                     ASTRA_FRAMEBUFFER_ENABLE |
                         ASTRA_FRAMEBUFFER_FORMAT_RGB565 |
                         ASTRA_FRAMEBUFFER_WINDOW_SCENE);
    astra_mmio_write(device, ASTRA_REG_FB_KEY, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_WINDOW_SCENE_BYTES, scene_bytes);
    astra_mmio_write(device, ASTRA_REG_DISPLAY_SOURCE_SIZE,
                     (uint32_t)layout.source_height << 16 |
                         layout.source_width);
    astra_mmio_write(device, ASTRA_REG_DISPLAY_CROP_ORIGIN,
                     (uint32_t)layout.crop_y << 16 | layout.crop_x);
    astra_mmio_write(device, ASTRA_REG_DISPLAY_CROP_SIZE,
                     (uint32_t)layout.crop_height << 16 |
                         layout.crop_width);
    astra_mmio_write(device, ASTRA_REG_DISPLAY_VIEWPORT_ORIGIN,
                     (uint32_t)layout.viewport_y << 16 |
                         layout.viewport_x);
    astra_mmio_write(device, ASTRA_REG_DISPLAY_VIEWPORT_SIZE,
                     (uint32_t)layout.viewport_height << 16 |
                         layout.viewport_width);
    return issue_present(device, commit_pointer);
}

struct terminal_damage {
    uint8_t first[TEXT_ROWS];
    uint8_t last[TEXT_ROWS];
    uint32_t glyphs;
};

static uint32_t cell_x(uint32_t column)
{
    return TEXT_ORIGIN_X + column * TEXT_CELL_WIDTH;
}

static uint32_t cell_y(uint32_t row)
{
    return TEXT_ORIGIN_Y + row * TEXT_CELL_HEIGHT;
}

static uint32_t scanout_for_generation(uint32_t generation)
{
    return (generation & 1u) != 0u ?
        ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
        ASTRA_RENDER_BATCH_SCANOUT0_OFFSET;
}

static uint32_t next_generation(uint32_t *generation,
                                uint32_t active_scanout)
{
    do {
        ++*generation;
        if (*generation == 0u)
            ++*generation;
    } while (scanout_for_generation(*generation) == active_scanout);
    return *generation;
}

static uint32_t current_scanout(
    const struct astra_graphics_device *device)
{
    uint32_t base = astra_mmio_read(device, ASTRA_REG_FB_BASE);

    if (base == ASTRA_FRAMEBUFFER_BASE + ASTRA_RENDER_BATCH_SCANOUT0_OFFSET)
        return ASTRA_RENDER_BATCH_SCANOUT0_OFFSET;
    if (base == ASTRA_FRAMEBUFFER_BASE + ASTRA_RENDER_BATCH_SCANOUT1_OFFSET)
        return ASTRA_RENDER_BATCH_SCANOUT1_OFFSET;
    return UINT32_MAX;
}

static uint32_t scanout_index(uint32_t scanout)
{
    if (scanout == ASTRA_RENDER_BATCH_SCANOUT0_OFFSET)
        return 0u;
    if (scanout == ASTRA_RENDER_BATCH_SCANOUT1_OFFSET)
        return 1u;
    return UINT32_MAX;
}

static void remember_text(struct terminal_scanout_state *state,
                          const uint8_t cells[TEXT_CELLS],
                          const struct terminal_cursor *cursor,
                          bool cursor_drawn)
{
    (void)memcpy(state->cells, cells, TEXT_CELLS);
    state->cursor = *cursor;
    state->cursor_drawn = cursor_drawn;
    state->valid = true;
}

static void damage_clear(struct terminal_damage *damage)
{
    for (uint32_t row = 0u; row < TEXT_ROWS; ++row) {
        damage->first[row] = TEXT_COLUMNS;
        damage->last[row] = 0u;
    }
    damage->glyphs = 0u;
}

static void damage_add(struct terminal_damage *damage, uint32_t row,
                       uint32_t first, uint32_t last)
{
    if (row >= TEXT_ROWS || first >= last || last > TEXT_COLUMNS)
        return;
    if (first < damage->first[row])
        damage->first[row] = (uint8_t)first;
    if (last > damage->last[row])
        damage->last[row] = (uint8_t)last;
}

static void damage_finish(struct terminal_damage *damage)
{
    damage->glyphs = 0u;
    for (uint32_t row = 0u; row < TEXT_ROWS; ++row)
        if (damage->first[row] < damage->last[row])
            damage->glyphs += damage->last[row] - damage->first[row];
}

static void damage_for_scroll(struct terminal_damage *damage,
                              const uint8_t current[TEXT_CELLS],
                              const uint8_t previous[TEXT_CELLS],
                              uint32_t distance)
{
    const uint32_t first_row = ASTRA_TEXT_TOP_MARGIN;
    const uint32_t last_row = TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN;
    const uint32_t first_column = ASTRA_TEXT_LEFT_MARGIN;
    const uint32_t last_column = TEXT_COLUMNS - ASTRA_TEXT_RIGHT_MARGIN;

    damage_clear(damage);
    for (uint32_t row = 0u; row < TEXT_ROWS; ++row) {
        for (uint32_t column = 0u; column < TEXT_COLUMNS; ++column) {
            bool in_columns = column >= first_column &&
                              column < last_column;
            bool shifted = distance != 0u && in_columns &&
                           row >= first_row && row + distance < last_row;
            bool exposed = distance != 0u && in_columns &&
                           row >= first_row && row < last_row &&
                           row + distance >= last_row;
            uint32_t cell = row * TEXT_COLUMNS + column;
            uint32_t source = shifted ?
                cell + distance * TEXT_COLUMNS : cell;

            if (exposed || current[cell] != previous[source])
                damage_add(damage, row, column, column + 1u);
        }
    }
}

static void damage_old_cursor(struct terminal_damage *damage,
                              const struct terminal_cursor *cursor,
                              bool drawn, uint32_t scroll_rows)
{
    const uint32_t first_row = ASTRA_TEXT_TOP_MARGIN;
    const uint32_t last_row = TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN;
    const uint32_t first_column = ASTRA_TEXT_LEFT_MARGIN;
    const uint32_t last_column = TEXT_COLUMNS - ASTRA_TEXT_RIGHT_MARGIN;
    uint32_t row;
    uint32_t column;

    if (!drawn)
        return;
    row = cursor->cell / TEXT_COLUMNS;
    column = cursor->cell % TEXT_COLUMNS;
    if (scroll_rows != 0u && column >= first_column &&
        column < last_column && row >= first_row + scroll_rows &&
        row < last_row) {
        damage_add(damage, row - scroll_rows, column, column + 1u);
        return;
    }
    if (scroll_rows == 0u || column < first_column ||
        column >= last_column || row < first_row || row >= last_row)
        damage_add(damage, row, column, column + 1u);
}

static uint64_t damage_cost(const struct terminal_damage *damage,
                            const uint8_t cells[TEXT_CELLS],
                            uint32_t scroll_rows)
{
    uint64_t cycles = 0u;

    for (uint32_t row = 0u; row < TEXT_ROWS; ++row) {
        if (damage->first[row] < damage->last[row]) {
            uint32_t first = damage->first[row];
            uint32_t last = damage->last[row];
            uint32_t span = last - first;
            uint32_t cell = row * TEXT_COLUMNS;

            while (first < last && cells[cell + first] == ' ')
                ++first;
            while (last > first && cells[cell + last - 1u] == ' ')
                --last;
            cycles += (uint64_t)span * TEXT_CELL_WIDTH * TEXT_CELL_HEIGHT *
                      DE25_FILL_CYCLES_PER_PIXEL +
                      (uint64_t)(last - first) * DE25_GLYPH_CYCLES;
        }
    }
    if (scroll_rows != 0u) {
        uint32_t columns = TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                           ASTRA_TEXT_RIGHT_MARGIN;
        uint32_t rows = TEXT_ROWS - ASTRA_TEXT_TOP_MARGIN -
                        ASTRA_TEXT_BOTTOM_MARGIN - scroll_rows;
        uint64_t pixels = (uint64_t)columns * TEXT_CELL_WIDTH * rows *
                          TEXT_CELL_HEIGHT;

        cycles += pixels * DE25_BLIT_CYCLES_PER_100_PIXELS / 100u;
    }
    return cycles;
}

static uint32_t select_scroll(
    struct terminal_damage *selected,
    const uint8_t current[TEXT_CELLS],
    const struct terminal_scanout_state *previous,
    const struct terminal_cursor *cursor, bool cursor_drawn)
{
    const uint32_t rows = TEXT_ROWS - ASTRA_TEXT_TOP_MARGIN -
                          ASTRA_TEXT_BOTTOM_MARGIN;
    uint64_t best_cost = UINT64_MAX;
    uint32_t best_distance = 0u;

    for (uint32_t distance = 0u; distance < rows; ++distance) {
        struct terminal_damage candidate;
        uint64_t cost;

        damage_for_scroll(&candidate, current, previous->cells, distance);
        damage_old_cursor(&candidate, &previous->cursor,
                          previous->cursor_drawn, distance);
        if (cursor_drawn)
            damage_add(&candidate, cursor->cell / TEXT_COLUMNS,
                       cursor->cell % TEXT_COLUMNS,
                       cursor->cell % TEXT_COLUMNS + 1u);
        damage_finish(&candidate);
        cost = damage_cost(&candidate, current, distance);
        if (cost < best_cost) {
            best_cost = cost;
            best_distance = distance;
            *selected = candidate;
        }
    }
    return best_distance;
}

static int add_text_span(AstraRenderBuilder *builder, uint32_t destination,
                         const uint8_t cells[TEXT_CELLS], uint32_t row,
                         uint32_t first, uint32_t last)
{
    char utf8[TEXT_COLUMNS * 3u];
    uint32_t length = last - first;
    uint32_t glyph_first = first;
    uint32_t glyph_last = last;
    uint32_t cell;
    uint32_t utf8_bytes;

    if (!astra_render_builder_fill(
            builder, destination, (int32_t)cell_x(first),
            (int32_t)cell_y(row), length * TEXT_CELL_WIDTH,
            TEXT_CELL_HEIGHT, 0u))
        return 0;
    cell = row * TEXT_COLUMNS;
    while (glyph_first < glyph_last && cells[cell + glyph_first] == ' ')
        ++glyph_first;
    while (glyph_last > glyph_first && cells[cell + glyph_last - 1u] == ' ')
        --glyph_last;
    if (glyph_first == glyph_last)
        return 1;
    utf8_bytes = cp437_to_utf8(
        utf8, &cells[cell + glyph_first], glyph_last - glyph_first);
    return astra_render_builder_mono_text(
        builder, destination,
        (int32_t)cell_x(glyph_first) +
            (TEXT_CELL_WIDTH - TEXT_FONT_WIDTH) / 2,
        (int32_t)cell_y(row) +
            (TEXT_CELL_HEIGHT - TEXT_FONT_HEIGHT) / 2,
        utf8, utf8_bytes, TEXT_FONT_HEIGHT, TEXT_CELL_WIDTH, 0xffffu);
}

static int add_cursor(AstraRenderBuilder *builder, uint32_t destination,
                      const struct terminal_cursor *cursor, bool drawn)
{
    uint32_t row;
    uint32_t column;

    if (!drawn)
        return 1;
    row = cursor->cell / TEXT_COLUMNS;
    column = cursor->cell % TEXT_COLUMNS;
    return astra_render_builder_fill(
        builder, destination, (int32_t)cell_x(column),
        (int32_t)(cell_y(row + 1u) - CURSOR_HEIGHT), TEXT_CELL_WIDTH,
        CURSOR_HEIGHT, 0xffffu);
}

static int execute_finished_batch(
    const struct astra_graphics_device *device, AstraRenderBuilder *builder,
    uint32_t *scanout)
{
    uint32_t bytes = astra_render_builder_finish(builder);

    return bytes != 0u &&
           execute_render_batch(device, terminal_batch, bytes, false,
                                scanout) == 0 ?
               0 : -1;
}

static int present_solid_frame(
    const struct astra_graphics_device *device, uint16_t color,
    uint32_t *render_generation, uint32_t *active_scanout)
{
    AstraRenderBuilder builder;
    uint32_t frame_generation = next_generation(
        render_generation, *active_scanout);
    uint32_t destination;
    uint32_t scanout;

    if (!astra_render_builder_init(&builder, terminal_batch,
                                   sizeof(terminal_batch), frame_generation))
        return -1;
    destination = astra_render_builder_frame(&builder);
    if (!astra_render_builder_fill(
            &builder, destination, 0, 0, ASTRA_DISPLAY_WIDTH,
            ASTRA_DISPLAY_HEIGHT, color) ||
        execute_finished_batch(device, &builder, &scanout) != 0 ||
        scanout != scanout_for_generation(frame_generation) ||
        present_now(device, scanout, false) != 0)
        return -1;
    *active_scanout = scanout;
    return 0;
}

static int present_rgb565_frame(
    const struct astra_graphics_device *device, const void *frame,
    uint32_t *render_generation, uint32_t *active_scanout)
{
    struct astra_graphics_memory_map mapping;
    uint32_t frame_generation = next_generation(
        render_generation, *active_scanout);
    uint32_t target = scanout_for_generation(frame_generation);

    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping, ASTRA_FRAMEBUFFER_BASE + target,
            ASTRA_FRAMEBUFFER_BYTES) != 0)
        return -1;
    astra_graphics_memory_copy_to(mapping.data, frame,
                                  ASTRA_FRAMEBUFFER_BYTES);
    astra_graphics_memory_barrier();
    astra_graphics_memory_map_close(&mapping);
    if (present_now(device, target, false) != 0)
        return -1;
    *active_scanout = target;
    return 0;
}

static int make_render_target_inactive(
    const struct astra_graphics_device *device, uint32_t target,
    uint32_t *render_generation, uint32_t *active_scanout)
{
    AstraRenderBuilder builder;
    uint32_t frame_generation;
    uint32_t destination;
    uint32_t source;
    uint32_t scanout;

    if (target != *active_scanout)
        return 0;
    frame_generation = next_generation(render_generation, *active_scanout);
    if (!astra_render_builder_init(&builder, terminal_batch,
                                   sizeof(terminal_batch), frame_generation))
        return -1;
    destination = astra_render_builder_frame(&builder);
    source = astra_render_builder_scanout(&builder, *active_scanout);
    if (source == 0u ||
        !astra_render_builder_blit_region(
            &builder, destination, source, 0, 0, 0, 0,
            ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT) ||
        execute_finished_batch(device, &builder, &scanout) != 0 ||
        scanout == target || present_now(device, scanout, false) != 0)
        return -1;
    *active_scanout = scanout;
    return 0;
}

static int present_full_text(const struct astra_graphics_device *device,
                             const uint8_t cells[TEXT_CELLS],
                             const struct terminal_cursor *cursor,
                             bool cursor_drawn, uint32_t *generation,
                             uint32_t *active_scanout)
{
    AstraRenderBuilder builder;
    uint32_t frame_generation = next_generation(generation, *active_scanout);
    uint32_t target = scanout_for_generation(frame_generation);

    {
        uint32_t destination;
        uint32_t scanout;

        if (!astra_render_builder_init(&builder, terminal_batch,
                                       sizeof(terminal_batch),
                                       frame_generation))
            return -1;
        destination = astra_render_builder_frame(&builder);
        if (!astra_render_builder_fill(
                &builder, destination, 0, 0, ASTRA_DISPLAY_WIDTH,
                ASTRA_DISPLAY_HEIGHT, 0u))
            return -1;
        for (uint32_t row = 0u; row < TEXT_ROWS; ++row)
            if (!add_text_span(&builder, destination, cells, row, 0u,
                               TEXT_COLUMNS))
                return -1;
        if (cursor_drawn && !add_cursor(&builder, destination, cursor, true))
            return -1;
        if (execute_finished_batch(device, &builder, &scanout) != 0 ||
            scanout != target)
            return -1;
    }
    if (present_now(device, target, false) != 0)
        return -1;
    *active_scanout = target;
    return 0;
}

static int present_text_on_both_scanouts(
    const struct astra_graphics_device *device,
    const uint8_t cells[TEXT_CELLS], const struct terminal_cursor *cursor,
    bool cursor_drawn, struct terminal_scanout_state states[2],
    uint32_t *generation, uint32_t *active_scanout)
{
    uint32_t index;
    uint32_t source;

    if (present_full_text(device, cells, cursor, cursor_drawn,
                          generation, active_scanout) != 0)
        return -1;
    index = scanout_index(*active_scanout);
    if (index >= 2u)
        return -1;
    remember_text(&states[index], cells, cursor, cursor_drawn);
    source = *active_scanout;
    if (make_render_target_inactive(device, source, generation,
                                    active_scanout) != 0)
        return -1;
    index = scanout_index(*active_scanout);
    if (index >= 2u)
        return -1;
    remember_text(&states[index], cells, cursor, cursor_drawn);
    return 0;
}

static int present_text_update(
    const struct astra_graphics_device *device,
    const uint8_t cells[TEXT_CELLS], const struct terminal_damage *damage,
    uint32_t scroll_rows, const struct terminal_cursor *cursor,
    bool cursor_drawn, uint32_t *generation, uint32_t *active_scanout)
{
    AstraRenderBuilder builder;
    uint32_t frame_generation = next_generation(generation, *active_scanout);
    uint32_t destination;
    uint32_t scanout;

    if (*active_scanout != ASTRA_RENDER_BATCH_SCANOUT0_OFFSET &&
        *active_scanout != ASTRA_RENDER_BATCH_SCANOUT1_OFFSET)
        return -1;
    if (!astra_render_builder_init(&builder, terminal_batch,
                                   sizeof(terminal_batch), frame_generation))
        return -1;
    destination = astra_render_builder_frame(&builder);
    if (scroll_rows != 0u) {
        uint32_t first_row = ASTRA_TEXT_TOP_MARGIN;
        uint32_t last_row = TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN;
        uint32_t first_column = ASTRA_TEXT_LEFT_MARGIN;
        uint32_t columns = TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                           ASTRA_TEXT_RIGHT_MARGIN;

        if (!astra_render_builder_blit_region(
                &builder, destination, destination,
                (int32_t)cell_x(first_column),
                (int32_t)cell_y(first_row + scroll_rows),
                (int32_t)cell_x(first_column),
                (int32_t)cell_y(first_row),
                (uint16_t)(columns * TEXT_CELL_WIDTH),
                (uint16_t)((last_row - first_row - scroll_rows) *
                           TEXT_CELL_HEIGHT)))
            return -1;
    }
    for (uint32_t row = 0u; row < TEXT_ROWS; ++row)
        if (damage->first[row] < damage->last[row] &&
            !add_text_span(&builder, destination, cells, row,
                           damage->first[row], damage->last[row]))
            return -1;
    if (!add_cursor(&builder, destination, cursor, cursor_drawn) ||
        execute_finished_batch(device, &builder, &scanout) != 0 ||
        scanout != scanout_for_generation(frame_generation) ||
        present_now(device, scanout, false) != 0)
        return -1;
    *active_scanout = scanout;
    return 0;
}

static int self_test(void)
{
    AstraDisplayLayout layout;
    char utf8[6];
    const uint8_t cp437[] = { 'A', 0x80u, 0xdbu };
    uint8_t plane[TEXT_PAGE_BYTES];
    uint8_t blank[TEXT_CELLS];
    uint8_t current[TEXT_CELLS];
    uint8_t previous[TEXT_CELLS];
    struct terminal_cursor cursor;
    struct terminal_scanout_state previous_state = {0};
    struct terminal_damage damage;
    AstraRenderBuilder builder;
    uint32_t destination;
    uint32_t source;
    uint32_t batch_bytes;
    const uint8_t *command;
    AstraDisplayMailbox mailbox;
    struct display_request request;
    static uint8_t validation_batch[
        ASTRA_RENDER_BATCH_MIN_BYTES +
        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES +
        3u * ASTRA_RENDER_TRIANGLE_VERTEX_BYTES];
    uint8_t read_header[ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES];
    static uint8_t validation_cursor[ASTRA_DISPLAY_CURSOR_IMAGE_BYTES];
    volatile AstraDisplayMailbox *shared;
    uint64_t wait_started;
    pid_t child;
    int child_status;

    if (window_scene_layout(320u << 16 | 200u, &layout) != 0 ||
        layout.viewport_x != 160u || layout.viewport_y != 40u ||
        layout.viewport_width != 1600u ||
        layout.viewport_height != 1000u ||
        window_scene_layout(ASTRA_DISPLAY_WIDTH << 16 |
                            ASTRA_DISPLAY_HEIGHT, &layout) != 0 ||
        layout.viewport_x != 0u || layout.viewport_y != 0u ||
        layout.viewport_width != ASTRA_DISPLAY_WIDTH ||
        layout.viewport_height != ASTRA_DISPLAY_HEIGHT ||
        window_scene_layout(0u << 16 | 200u, &layout) == 0 ||
        window_scene_layout((ASTRA_DISPLAY_WIDTH + 1u) << 16 | 200u,
                            &layout) == 0)
        return EXIT_FAILURE;

    if (TEXT_FONT_WIDTH != ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH ||
        TEXT_FONT_HEIGHT != ASTRA_THEME_SYSTEM_MONO_FONT_HEIGHT ||
        cell_x(1u) - cell_x(0u) != ASTRA_THEME_SYSTEM_MONO_CELL_WIDTH)
        return EXIT_FAILURE;

    if (cp437_to_utf8(utf8, cp437, sizeof(cp437)) != 6u ||
        memcmp(utf8, "A\xc3\x87\xe2\x96\x88", sizeof(utf8)) != 0)
        return EXIT_FAILURE;

    for (uint32_t y = 0u; y < POINTER_HEIGHT; ++y)
        if ((pointer_inner[y] & (uint16_t)~pointer_outer[y]) != 0u)
            return EXIT_FAILURE;
    if (pointer_builtin_pixel(ASTRA_POINTER_SHAPE_RESIZE_NW_SE, 16u, 16u) !=
            UINT32_C(0xffffffff) ||
        pointer_builtin_pixel(ASTRA_POINTER_SHAPE_RESIZE_NE_SW, 16u, 16u) !=
            UINT32_C(0xffffffff) ||
        pointer_builtin_pixel(ASTRA_POINTER_SHAPE_RESIZE_NW_SE, 0u, 31u) !=
            0u ||
        pointer_builtin_pixel(ASTRA_POINTER_SHAPE_RESIZE_NE_SW, 0u, 0u) !=
            0u ||
        pointer_builtin_pixel(ASTRA_POINTER_SHAPE_CUSTOM, 16u, 16u) != 0u)
        return EXIT_FAILURE;
    (void)memset(validation_cursor, 0, sizeof(validation_cursor));
    store_be32(validation_cursor, ASTRA_DISPLAY_CURSOR_IMAGE_MAGIC);
    store_be32(validation_cursor + 4u,
               ASTRA_DISPLAY_CURSOR_IMAGE_VERSION);
    store_be32(validation_cursor + 8u, (16u << 16) | 16u);
    if (!pointer_image_valid(validation_cursor,
                             sizeof(validation_cursor)))
        return EXIT_FAILURE;
    store_be32(validation_cursor + 8u,
               ASTRA_DISPLAY_CURSOR_IMAGE_WIDTH);
    if (pointer_image_valid(validation_cursor, sizeof(validation_cursor)))
        return EXIT_FAILURE;
    (void)memset(plane, 0, sizeof(plane));
    plane[0] = 'A';
    plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET] = 2u;
    if (!copy_cells(current, plane) || current[0] != 'A')
        return EXIT_FAILURE;
    plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET] = 3u;
    if (copy_cells(current, plane))
        return EXIT_FAILURE;
    plane[ASTRA_TEXT_CURSOR_OFFSET + 0u] = ASTRA_TEXT_CURSOR_MAGIC_0;
    plane[ASTRA_TEXT_CURSOR_OFFSET + 1u] = ASTRA_TEXT_CURSOR_MAGIC_1;
    plane[ASTRA_TEXT_CURSOR_OFFSET + 2u] = ASTRA_TEXT_CURSOR_MAGIC_2;
    plane[ASTRA_TEXT_CURSOR_OFFSET + 3u] = ASTRA_TEXT_CURSOR_MAGIC_3;
    plane[ASTRA_TEXT_CURSOR_ROW_OFFSET] = 1u;
    plane[ASTRA_TEXT_CURSOR_COLUMN_OFFSET] = 2u;
    plane[ASTRA_TEXT_CURSOR_FLAGS_OFFSET] = ASTRA_TEXT_CURSOR_VISIBLE;
    plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET] = 2u;
    if (!copy_cursor(&cursor, plane) || !cursor.visible ||
        cursor.cell != TEXT_COLUMNS + 2u)
        return EXIT_FAILURE;
    plane[ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET] = 3u;
    if (copy_cursor(&cursor, plane))
        return EXIT_FAILURE;

    (void)memset(previous, ' ', sizeof(previous));
    for (uint32_t row = ASTRA_TEXT_TOP_MARGIN;
         row < TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN; ++row)
        (void)memset(previous + row * TEXT_COLUMNS +
                         ASTRA_TEXT_LEFT_MARGIN,
                     (int)('A' + row),
                     TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                         ASTRA_TEXT_RIGHT_MARGIN);
    (void)memcpy(current, previous, sizeof(current));
    for (uint32_t row = ASTRA_TEXT_TOP_MARGIN;
         row + 1u < TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN; ++row)
        (void)memcpy(current + row * TEXT_COLUMNS +
                         ASTRA_TEXT_LEFT_MARGIN,
                     previous + (row + 1u) * TEXT_COLUMNS +
                         ASTRA_TEXT_LEFT_MARGIN,
                     TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                         ASTRA_TEXT_RIGHT_MARGIN);
    (void)memset(current +
                     (TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN - 1u) *
                         TEXT_COLUMNS + ASTRA_TEXT_LEFT_MARGIN,
                 'Z', TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                          ASTRA_TEXT_RIGHT_MARGIN);
    (void)memcpy(previous_state.cells, previous, sizeof(previous));
    previous_state.valid = true;
    if (select_scroll(&damage, current, &previous_state, &cursor, false) != 1u ||
        damage.glyphs != TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                            ASTRA_TEXT_RIGHT_MARGIN)
        return EXIT_FAILURE;
    current[0] = '!';
    if (select_scroll(&damage, current, &previous_state, &cursor, false) != 1u ||
        damage.first[0] != 0u || damage.last[0] != 1u)
        return EXIT_FAILURE;
    current[0] = previous[0];

    damage_clear(&damage);
    damage_add(&damage, TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN - 1u,
               ASTRA_TEXT_LEFT_MARGIN,
               TEXT_COLUMNS - ASTRA_TEXT_RIGHT_MARGIN);
    damage_finish(&damage);
    if (damage.glyphs != TEXT_COLUMNS - ASTRA_TEXT_LEFT_MARGIN -
                              ASTRA_TEXT_RIGHT_MARGIN)
        return EXIT_FAILURE;
    (void)memset(blank, ' ', sizeof(blank));
    if (!astra_render_builder_init(&builder, terminal_batch,
                                   sizeof(terminal_batch), 1u))
        return EXIT_FAILURE;
    destination = astra_render_builder_frame(&builder);
    if (!add_text_span(&builder, destination, blank, 0u, 0u,
                       TEXT_COLUMNS) ||
        builder.command_count != 1u || builder.glyph_count != 0u ||
        astra_render_builder_finish(&builder) !=
            ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                ASTRA_RENDER_BATCH_ARENA_OFFSET +
                ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES)
        return EXIT_FAILURE;
    if (!astra_render_builder_init(&builder, terminal_batch,
                                   sizeof(terminal_batch), 1u))
        return EXIT_FAILURE;
    destination = astra_render_builder_frame(&builder);
    source = astra_render_builder_scanout(
        &builder, ASTRA_RENDER_BATCH_SCANOUT0_OFFSET);
    if (source == 0u ||
        !astra_render_builder_blit_region(
            &builder, destination, source, 0, 0, 0, 0,
            ASTRA_DISPLAY_WIDTH, ASTRA_DISPLAY_HEIGHT) ||
        !astra_render_builder_blit_region(
            &builder, destination, destination, 0, TEXT_CELL_HEIGHT, 0, 0,
            ASTRA_DISPLAY_WIDTH,
            ASTRA_DISPLAY_HEIGHT - TEXT_CELL_HEIGHT) ||
        !add_text_span(&builder, destination, current,
                       TEXT_ROWS - ASTRA_TEXT_BOTTOM_MARGIN - 1u,
                       ASTRA_TEXT_LEFT_MARGIN,
                       TEXT_COLUMNS - ASTRA_TEXT_RIGHT_MARGIN) ||
        (batch_bytes = astra_render_builder_finish(&builder)) == 0u ||
        batch_bytes >= ASTRA_RENDER_BUILDER_BYTES ||
        load_be32(terminal_batch + 8u) != batch_bytes)
        return EXIT_FAILURE;
    command = terminal_batch + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
              ASTRA_RENDER_BATCH_ARENA_OFFSET;
    if (load_be32(command + 4u) >> 16 != ASTRA_RENDER_OP_BLIT ||
        load_be32(command + 32u) == load_be32(command + 36u))
        return EXIT_FAILURE;
    command += ASTRA_RENDER_COMMAND_BYTES;
    if (load_be32(command + 4u) >> 16 != ASTRA_RENDER_OP_BLIT ||
        load_be32(command + 32u) != load_be32(command + 36u))
        return EXIT_FAILURE;

    (void)memset(&mailbox, 0, sizeof(mailbox));
    mailbox.magic = ASTRA_DISPLAY_MAILBOX_MAGIC;
    mailbox.version = ASTRA_DISPLAY_MAILBOX_VERSION_1_7;
    mailbox.request_id = 7u;
    mailbox.operation = ASTRA_DISPLAY_FRAME_PRESENT_SOLID;
    mailbox.color_rgb565 = 0x135du;
    mailbox.request_sequence = 4u;
    if (!mailbox_take(&mailbox, 0u, &request) || request.sequence != 4u ||
        request.id != 7u || request.color_rgb565 != 0x135du ||
        mailbox_take(&mailbox, 4u, &request))
        return EXIT_FAILURE;
    mailbox_complete(&mailbox, &request, ASTRA_DISPLAY_COMPLETION_OK, 9u);
    if (mailbox.completion_sequence != 4u ||
        mailbox.completion_id != 7u ||
        mailbox.completion_status != ASTRA_DISPLAY_COMPLETION_OK ||
        mailbox.completion_generation != 9u)
        return EXIT_FAILURE;
    mailbox.request_sequence = 5u;
    mailbox.request_id = UINT32_MAX;
    mailbox.operation = ASTRA_DISPLAY_PANIC_TEXT;
    mailbox.frame_pitch = 0u;
    mailbox.frame_bytes = 0u;
    if (!mailbox_take(&mailbox, 4u, &request) || request.sequence != 5u ||
        request.id != UINT32_MAX ||
        request.operation != ASTRA_DISPLAY_PANIC_TEXT)
        return EXIT_FAILURE;

    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED)
        return EXIT_FAILURE;
    (void)memset((void *)shared, 0, sizeof(*shared));
    shared->version = ASTRA_DISPLAY_MAILBOX_VERSION_1_7;
    child = fork();
    if (child == 0) {
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = 2000000 };

        (void)nanosleep(&delay, NULL);
        shared->request_sequence = 1u;
        (void)syscall(SYS_futex, &shared->request_sequence,
                      FUTEX_WAKE, 1, NULL, NULL, 0);
        _exit(EXIT_SUCCESS);
    }
    if (child < 0) {
        (void)munmap((void *)shared, sizeof(*shared));
        return EXIT_FAILURE;
    }
    wait_started = astra_monotonic_nanoseconds();
    if (mailbox_wait(shared, 0u) != 0 ||
        astra_monotonic_nanoseconds() - wait_started >= UINT64_C(10000000) ||
        waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != EXIT_SUCCESS ||
        shared->request_sequence != 1u) {
        (void)munmap((void *)shared, sizeof(*shared));
        return EXIT_FAILURE;
    }
    /* A completion wakes the emulator sleeping on completion_sequence. */
    child = fork();
    if (child == 0) {
        const struct timespec limit = { .tv_sec = 1, .tv_nsec = 0 };

        while (shared->completion_sequence == 0u) {
            if (syscall(SYS_futex, &shared->completion_sequence, FUTEX_WAIT,
                        0u, &limit, NULL, 0) != 0 &&
                errno == ETIMEDOUT)
                _exit(EXIT_FAILURE);
        }
        _exit(shared->completion_sequence == 1u ? EXIT_SUCCESS
                                                : EXIT_FAILURE);
    }
    if (child < 0) {
        (void)munmap((void *)shared, sizeof(*shared));
        return EXIT_FAILURE;
    }
    {
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = 2000000 };
        const struct display_request completed = { .sequence = 1u, .id = 1u };

        (void)nanosleep(&delay, NULL);
        wait_started = astra_monotonic_nanoseconds();
        mailbox_complete(shared, &completed, ASTRA_DISPLAY_COMPLETION_OK, 1u);
    }
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != EXIT_SUCCESS ||
        astra_monotonic_nanoseconds() - wait_started >= UINT64_C(10000000)) {
        (void)munmap((void *)shared, sizeof(*shared));
        return EXIT_FAILURE;
    }
    (void)munmap((void *)shared, sizeof(*shared));

    (void)memset(validation_batch, 0, sizeof(validation_batch));
    store_be32(validation_batch + 0u, ASTRA_RENDER_BATCH_MAGIC);
    store_be32(validation_batch + 4u, ASTRA_RENDER_BATCH_VERSION_1_2);
    store_be32(validation_batch + 8u, sizeof(validation_batch));
    store_be32(validation_batch + 12u, 1u);
    store_be32(validation_batch + 16u,
               ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    store_be32(validation_batch + 20u,
               ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    store_be32(validation_batch + 24u, 7u);
    store_be32(validation_batch + 28u, ASTRA_RENDER_BATCH_SCANOUT1_OFFSET);
    store_be32(validation_batch + 32u, ASTRA_RENDER_BATCH_PRESENT_CURSOR);
    store_be32(validation_batch + 36u, ASTRA_DISPLAY_WIDTH - 1u);
    store_be32(validation_batch + 40u, ASTRA_DISPLAY_HEIGHT - 1u);
    store_be32(validation_batch + 44u,
               ASTRA_DISPLAY_CURSOR_VISIBLE |
                   ASTRA_DISPLAY_CURSOR_SHAPE(ASTRA_POINTER_SHAPE_TEXT));
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET,
               (uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                   ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 4u,
               7u);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 20u,
               (uint32_t)ASTRA_DISPLAY_WIDTH << 16 | ASTRA_DISPLAY_HEIGHT);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET,
               (uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                   ASTRA_RENDER_COMMAND_BYTES);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 8u,
               1u);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 12u,
               7u);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 4u,
               (uint32_t)ASTRA_RENDER_OP_FILL << 16);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_RESOURCE_OFFSET);
    if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;
    {
        /* A writable surface may not touch the batch window. */
        uint8_t *record = validation_batch +
            ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET;

        store_be32(record + 8u, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT - 64u);
        store_be32(record + 12u, 64u);
        store_be32(record + 24u, (uint32_t)ASTRA_RENDER_SURFACE_WRITE << 16);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(record + 8u, ASTRA_RENDER_BATCH_ARENA_OFFSET - 64u);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(record + 12u, 65u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(record + 8u, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(record + 8u, 0u);
        store_be32(record + 12u, 0u);
        store_be32(record + 24u, 0u);
    }
    {
        uint32_t packed = 0u;
        uint32_t flags = 0u;

        if (!render_batch_cursor(validation_batch, &packed, &flags) ||
            packed != ASTRA_DISPLAY_HOST_CURSOR_PACK(
                ASTRA_DISPLAY_WIDTH - 1u, ASTRA_DISPLAY_HEIGHT - 1u, true) ||
            flags != (ASTRA_DISPLAY_CURSOR_VISIBLE |
                      ASTRA_DISPLAY_CURSOR_SHAPE(
                          ASTRA_POINTER_SHAPE_TEXT)))
            return EXIT_FAILURE;
    }
    store_be32(validation_batch + 36u, ASTRA_DISPLAY_WIDTH);
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;
    store_be32(validation_batch + 36u, ASTRA_DISPLAY_WIDTH - 1u);
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_RESOURCE_OFFSET + 4u);
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_RESOURCE_OFFSET + 1u);
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_RESOURCE_OFFSET);
    {
        /* One untextured triangle right after the frame descriptor. */
        uint8_t *triangles = validation_batch +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET;
        const uint32_t vertices = ASTRA_RENDER_BATCH_RESOURCE_OFFSET +
                                  ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;

        store_be32(triangles + 4u, (uint32_t)ASTRA_RENDER_OP_TRIANGLES << 16);
        store_be32(triangles + 40u, vertices);
        store_be32(triangles + 44u, 1u);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 36u, ASTRA_RENDER_BATCH_RESOURCE_OFFSET);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 36u, ASTRA_RENDER_BATCH_RESOURCE_OFFSET + 4u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 36u, 0u);
        store_be32(triangles + 44u, 2u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 44u, 0u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 44u, 1u);
        store_be32(triangles + 40u, vertices - 16u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 40u, vertices);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(triangles + 4u, (uint32_t)ASTRA_RENDER_OP_FILL << 16);
        store_be32(triangles + 40u, 0u);
        store_be32(triangles + 44u, 0u);
    }
    {
        /* FILL_RECTS: the record array must be aligned, bounded and
           inside the batch. Two records right after the descriptor. */
        uint8_t *rects = validation_batch +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET;
        const uint32_t records = ASTRA_RENDER_BATCH_RESOURCE_OFFSET +
                                 ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES;
        const uint32_t batch_end = ASTRA_RENDER_BATCH_ARENA_OFFSET +
                                   (uint32_t)sizeof(validation_batch);
        uint8_t *offset_word = rects +
            ASTRA_RENDER_FILL_RECTS_WORD_RECORD_OFFSET * 4u;
        uint8_t *count_word = rects +
            ASTRA_RENDER_FILL_RECTS_WORD_RECORD_COUNT * 4u;

        store_be32(rects + 4u, (uint32_t)ASTRA_RENDER_OP_FILL_RECTS << 16);
        store_be32(offset_word, records);
        store_be32(count_word, 2u);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, 0u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, ASTRA_RENDER_MAX_FILL_RECTS + 1u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, 2u);
        store_be32(offset_word, records + 8u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        /* Exactly to the end of the batch, then one record past it. */
        store_be32(offset_word, batch_end - 2u * ASTRA_RENDER_FILL_RECT_BYTES);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(offset_word, batch_end - ASTRA_RENDER_FILL_RECT_BYTES);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        /* LINES: the same array rules for its segments. */
        store_be32(rects + 4u, (uint32_t)ASTRA_RENDER_OP_LINES << 16);
        store_be32(offset_word, records);
        store_be32(count_word, 2u);
        if (!render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, 0u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, ASTRA_RENDER_MAX_LINE_SEGMENTS + 1u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(count_word, 2u);
        store_be32(offset_word, records + 8u);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(offset_word,
                   batch_end - ASTRA_RENDER_LINE_SEGMENT_BYTES);
        if (render_batch_valid(validation_batch, sizeof(validation_batch)))
            return EXIT_FAILURE;
        store_be32(rects + 4u, (uint32_t)ASTRA_RENDER_OP_FILL << 16);
        store_be32(offset_word, 0u);
        store_be32(count_word, 0u);
    }
    validation_batch[0] = 0u;
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;

    /* READ_SURFACE: a 4x2 XRGB8888 read at (1,1) of a 16x8 surface. */
    (void)memset(read_header, 0, sizeof(read_header));
    store_be32(read_header + 0u, ASTRA_DISPLAY_SURFACE_READ_MAGIC);
    store_be32(read_header + 4u, ASTRA_DISPLAY_SURFACE_READ_VERSION);
    store_be32(read_header + 8u, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT);
    store_be32(read_header + 12u, 16u * 4u * 8u);
    store_be32(read_header + 16u, 16u * 4u);
    store_be32(read_header + 20u, 16u << 16 | 8u);
    store_be32(read_header + 24u, ASTRA_RENDER_FORMAT_XRGB8888);
    store_be32(read_header + 28u, 1u << 16 | 1u);
    store_be32(read_header + 32u, 4u << 16 | 2u);
    if (!surface_read_valid(read_header, 64u + 4u * 4u * 2u) ||
        surface_read_valid(read_header, 64u + 4u * 4u * 2u + 4u) ||
        surface_read_valid(read_header, 64u))
        return EXIT_FAILURE;
    {
        static const struct {
            uint32_t offset;
            uint32_t value;
        } rejected[] = {
            { 0u, 0u },                                     /* magic */
            { 4u, 2u },                                     /* version */
            { 8u, ASTRA_RENDER_BATCH_WORKSPACE_LIMIT - 64u }, /* workspace */
            { 8u, ASTRA_RENDER_BATCH_MEDIA_LIMIT - 64u },   /* past Media RAM */
            { 12u, 16u * 4u * 8u - 1u },                    /* short surface */
            { 16u, 16u * 4u - 4u },                         /* short pitch */
            { 24u, ASTRA_RENDER_FORMAT_MASK1 },             /* sub-byte */
            { 28u, 13u << 16 | 1u },                        /* past right */
            { 28u, 1u << 16 | 7u },                         /* past bottom */
            { 32u, 0u << 16 | 2u },                         /* empty */
            { 60u, 1u },                                    /* reserved */
        };

        for (size_t index = 0u;
             index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
            uint32_t saved = load_be32(read_header + rejected[index].offset);

            store_be32(read_header + rejected[index].offset,
                       rejected[index].value);
            if (surface_read_valid(read_header, 64u + 4u * 4u * 2u))
                return EXIT_FAILURE;
            store_be32(read_header + rejected[index].offset, saved);
        }
    }
    if (!surface_read_valid(read_header, 64u + 4u * 4u * 2u))
        return EXIT_FAILURE;
#ifdef ASTRA_HOST_APERTURE
    {
        /* Guest batches run with the aperture on, helper batches with it
           off; a batch that keeps the current choice stores nothing. */
        static uint32_t registers[ASTRA_CONTROL_BYTES / sizeof(uint32_t)];
        struct astra_graphics_device mock = {
            .memory_fd = -1,
            .capture_lock_fd = -1,
            .registers = registers,
            .framebuffer = NULL,
        };
        uint32_t *aperture =
            &registers[ASTRA_REG_RENDER_HOST_APERTURE_BASE / 4u];

        registers[ASTRA_REG_CAPABILITIES / 4u] =
            ASTRA_CAP_RENDER_HOST_APERTURE;
        registers[ASTRA_REG_RENDER_STATUS / 4u] = 0u;
        active_aperture_base = UINT32_MAX;
        if (select_render_aperture(&mock, 0xbcd00000u) != 0 ||
            *aperture != 0xbcd00000u)
            return EXIT_FAILURE;
        *aperture = 0x5a5a5000u;
        if (select_render_aperture(&mock, 0xbcd00000u) != 0 ||
            *aperture != 0x5a5a5000u)
            return EXIT_FAILURE;
        *aperture = 0xbcd00000u;
        if (select_render_aperture(&mock, 0u) != 0 || *aperture != 0u)
            return EXIT_FAILURE;
        active_aperture_base = UINT32_MAX;
    }
#endif

    puts("ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS");
    return EXIT_SUCCESS;
}

#ifdef ASTRA_HOST_APERTURE
_Static_assert(ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES == ASTRA_HOST_ARENA_BYTES,
               "the host arena is the display mailbox payload");
_Static_assert(ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES ==
                   ASTRA_RENDER_BATCH_BUFFER_BYTES,
               "the render host aperture, the batch window, is the payload");
#endif

/* Maps the payload. On the DE25 it must be the host arena, whose physical
   base becomes the render engine's host aperture. */
static volatile uint8_t *display_payload_map(int fd)
{
    void *mapped;
#ifdef ASTRA_HOST_APERTURE
    struct astra_host_arena_info info;

    if (ioctl(fd, ASTRA_HOST_ARENA_IOC_INFO, &info) != 0) {
        perror("display payload is not the host arena");
        return MAP_FAILED;
    }
    if (info.bytes != ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES ||
        info.physical == 0u || info.physical > UINT32_MAX ||
        (info.physical & 0xfffu) != 0u) {
        fprintf(stderr, "host arena unusable: 0x%llx, %llu bytes\n",
                (unsigned long long)info.physical,
                (unsigned long long)info.bytes);
        return MAP_FAILED;
    }
    host_aperture_base = (uint32_t)info.physical;
#else
    struct stat payload_stat;

    if (fstat(fd, &payload_stat) != 0 ||
        payload_stat.st_size < (off_t)ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES) {
        fprintf(stderr, "shared display payload must be at least %u bytes\n",
                ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES);
        return MAP_FAILED;
    }
#endif
    mapped = mmap(NULL, ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES,
                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED)
        perror("map shared display payload");
    return mapped;
}

int main(int argc, char **argv)
{
    const struct timespec poll_delay = { .tv_sec = 0, .tv_nsec = 16000000 };
    struct astra_graphics_device device;
    struct terminal_cursor cursor = { .cell = 0u, .visible = false };
    struct terminal_scanout_state text_states[2] = {0};
    volatile uint8_t *plane = MAP_FAILED;
    volatile AstraDisplayMailbox *mailbox = MAP_FAILED;
    volatile uint8_t *payload = MAP_FAILED;
    uint8_t current[TEXT_CELLS];
    struct stat plane_stat;
    struct stat mailbox_stat;
    unsigned blink_polls = 0u;
    bool cursor_on = true;
    int plane_fd = -1;
    int mailbox_fd = -1;
    int payload_fd = -1;
    int result = EXIT_FAILURE;
    uint32_t text_generation = 0u;
    uint32_t active_scanout;
    uint32_t active_window_scene_offset = 0u;
    uint32_t active_window_scene_bytes = 0u;
    uint32_t mailbox_sequence = 0u;
    bool display_owned = false;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc == 2 && strcmp(argv[1], "--mailbox-bytes") == 0) {
        printf("%u\n", ASTRA_DISPLAY_MAILBOX_HEADER_BYTES);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--payload-bytes") == 0) {
        printf("%u\n", ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES);
        return EXIT_SUCCESS;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s "
                "[--self-test|--mailbox-bytes|--payload-bytes] | "
                "<shared-post-text-page> <shared-display-mailbox> "
                "<shared-display-payload>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    astra_graphics_device_init(&device);
    plane_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (plane_fd < 0) {
        perror("open shared post-text page");
        goto done;
    }
    if (fstat(plane_fd, &plane_stat) != 0 ||
        plane_stat.st_size < (off_t)TEXT_PAGE_BYTES) {
        fprintf(stderr, "shared post-text page must be at least %u bytes\n",
                TEXT_PAGE_BYTES);
        goto done;
    }
    plane = mmap(NULL, TEXT_PAGE_BYTES, PROT_READ, MAP_SHARED, plane_fd, 0);
    if (plane == MAP_FAILED) {
        perror("map shared post-text page");
        goto done;
    }
    mailbox_fd = open(argv[2], O_RDWR | O_CLOEXEC);
    if (mailbox_fd < 0) {
        perror("open shared display mailbox");
        goto done;
    }
    if (fstat(mailbox_fd, &mailbox_stat) != 0 ||
        mailbox_stat.st_size < (off_t)ASTRA_DISPLAY_MAILBOX_HEADER_BYTES) {
        fprintf(stderr, "shared display mailbox must be at least %u bytes\n",
                ASTRA_DISPLAY_MAILBOX_HEADER_BYTES);
        goto done;
    }
    mailbox = mmap(NULL, ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                   PROT_READ | PROT_WRITE, MAP_SHARED, mailbox_fd, 0);
    if (mailbox == MAP_FAILED) {
        perror("map shared display mailbox");
        goto done;
    }
    payload_fd = open(argv[3], O_RDWR | O_CLOEXEC);
    if (payload_fd < 0) {
        perror("open shared display payload");
        goto done;
    }
    payload = display_payload_map(payload_fd);
    if (payload == MAP_FAILED)
        goto done;
    mailbox->magic = ASTRA_DISPLAY_MAILBOX_MAGIC;
    mailbox->version = ASTRA_DISPLAY_MAILBOX_VERSION_1_7;
    mailbox->completion_sequence = 0u;
    astra_graphics_memory_barrier();
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
        goto done;
    log_access_faults(&device, "at startup");
#ifdef ASTRA_HOST_APERTURE
    /* A previous helper may have left the engine enabled, and the aperture
       refuses a store while it is; a bitstream without the aperture has no
       register there at all. The helper checks both before storing. */
    host_payload = payload;
    if (select_render_aperture(&device, host_aperture_base) != 0)
        goto done;
#endif
    if (astra_graphics_scene_commit_drain(&device, UINT64_C(1000000000)) !=
        0) {
        /* Restarting cannot clear it: only a reset of the graphics block
           does. Stay up and say so once instead of restarting forever. */
        fprintf(stderr,
                "graphics scene commit held by hardware: COMMIT=%08x; "
                "power-cycle the board\n",
                astra_mmio_read(&device, ASTRA_REG_COMMIT));
        for (;;)
            pause();
    }
    astra_graphics_scene_prepare_empty(&device);
    if (astra_graphics_scene_commit(
            &device, UINT64_C(2000000000), NULL) != 0)
        goto done;
    if (pointer_initialize(&device) != 0)
        goto done;

    if (!copy_cells(current, plane))
        goto done;
    (void)copy_cursor(&cursor, plane);
    active_scanout = current_scanout(&device);
    if (present_text_on_both_scanouts(
            &device, current, &cursor, cursor.visible, text_states,
            &text_generation, &active_scanout) != 0)
        goto done;
    if (astra_boot_text_commit(&device, 0) != 0)
        goto done;

    if (signal(SIGTERM, stop) == SIG_ERR || signal(SIGINT, stop) == SIG_ERR) {
        perror("install terminal display signal handler");
        goto done;
    }
    printf("ASTRA_TERMINAL_DISPLAY READY columns=%u rows=%u origin=0,0 "
           "size=%u,%u cursor=underline\n",
           TEXT_COLUMNS, TEXT_ROWS, ASTRA_FRAMEBUFFER_WIDTH,
           ASTRA_FRAMEBUFFER_HEIGHT);
    fflush(stdout);
    while (running) {
        struct display_request request;
        struct terminal_cursor sampled;
        struct terminal_damage damage = {0};
        struct terminal_scanout_state *target_state;
        uint32_t target_index;
        uint32_t scroll_rows;
        bool cells_changed;
        bool cursor_drawn;

        if (mailbox_take(mailbox, mailbox_sequence, &request)) {
            uint32_t generation = 0u;
            uint32_t status = ASTRA_DISPLAY_COMPLETION_BAD_REQUEST;

            mailbox_sequence = request.sequence;
            /*
             * Only a request that changes the screen waits, and only for a
             * previous change still in flight: render-only batches, surface
             * reads and the cursor (which orders itself) never do.
             */
            if (request.operation != ASTRA_DISPLAY_FRAME_READ_SURFACE &&
                request.operation != ASTRA_DISPLAY_CURSOR_UPDATE &&
                request.operation != ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE &&
                !(request.operation ==
                      ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
                  request.frame_bytes >= 8u &&
                  load_be32((const uint8_t *)(const void *)payload + 4u) ==
                      ASTRA_RENDER_BATCH_VERSION_1_4) &&
                finish_pending_present(&device) != 0) {
                /* The previous present never reached the screen. */
                status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
            } else if (request.id != 0u &&
                request.operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
                (request.color_rgb565 & 0xffff0000u) == 0u) {
                if (present_solid_frame(
                        &device, (uint16_t)request.color_rgb565,
                        &text_generation, &active_scanout) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    active_window_scene_offset = 0u;
                    active_window_scene_bytes = 0u;
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                    display_owned = true;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation ==
                           ASTRA_DISPLAY_FRAME_PRESENT_RGB565 &&
                       request.frame_pitch == ASTRA_FRAMEBUFFER_PITCH &&
                       request.frame_bytes == ASTRA_FRAMEBUFFER_BYTES) {
                if (present_rgb565_frame(
                    &device,
                    (const uint8_t *)(const void *)payload,
                    &text_generation, &active_scanout) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    active_window_scene_offset = 0u;
                    active_window_scene_bytes = 0u;
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                    display_owned = true;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation ==
                           ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
                       request.frame_pitch == 0u &&
                       render_batch_valid(
                           (const uint8_t *)(const void *)payload,
                           request.frame_bytes)) {
                uint32_t scanout_offset;
                uint32_t window_scene_offset = 0u;
                uint32_t window_scene_bytes = 0u;
                uint32_t window_scene_dimensions = 0u;
                uint64_t profile_started = astra_monotonic_nanoseconds();
                uint64_t profile_rendered;
                uint64_t profile_presented;
                uint32_t cursor_packed = 0u;
                uint32_t cursor_flags = 0u;
                bool batch_cursor;
                bool window_scene_batch;
                bool render_only_batch;
                int render_status;
                int present_status;
                const volatile uint8_t *batch =
                    (const uint8_t *)(const void *)payload;

                window_scene_batch = load_be32(batch + 4u) ==
                                     ASTRA_RENDER_BATCH_VERSION_1_3;
                render_only_batch = load_be32(batch + 4u) ==
                                    ASTRA_RENDER_BATCH_VERSION_1_4;
                batch_cursor = render_batch_cursor(
                    batch, &cursor_packed, &cursor_flags);
                scanout_offset = load_be32(batch + 28u);
                render_status = 0;
                if (window_scene_batch) {
                    uint32_t scene_record = load_be32(batch + 48u) -
                        ASTRA_RENDER_BATCH_ARENA_OFFSET;
                    uint32_t target = load_be32(batch + scene_record + 28u);
                    uint32_t capacity =
                        load_be32(batch + scene_record + 32u);

                    if (active_window_scene_bytes != 0u &&
                        arena_ranges_overlap(
                            target, capacity, active_window_scene_offset,
                            active_window_scene_bytes))
                        render_status = -1;
                } else if (!render_only_batch) {
                    render_status = make_render_target_inactive(
                        &device, scanout_offset, &text_generation,
                        &active_scanout);
                }
                if (render_status == 0) {
                    render_status = execute_render_batch(
                        &device, batch, request.frame_bytes, true,
                        &scanout_offset);
                }
                if (render_status == 0 && window_scene_batch) {
                    render_status = compile_window_scene(
                        &device, batch, request.frame_bytes,
                        &window_scene_offset, &window_scene_bytes,
                        &window_scene_dimensions);
                }
                if (render_status == 0 && batch_cursor)
                    render_status = pointer_update(
                        &device, cursor_packed, cursor_flags, false);
                profile_rendered = astra_monotonic_nanoseconds();
                present_status = render_status != 0 ? -1 :
                    render_only_batch ? 0 :
                    window_scene_batch ?
                        present_window_scene(
                            &device, window_scene_offset,
                            window_scene_bytes, window_scene_dimensions,
                            batch_cursor) :
                        present(&device, scanout_offset, batch_cursor);
                profile_presented = astra_monotonic_nanoseconds();
                if (getenv("ASTRA_DISPLAY_PROFILE") != NULL)
                    fprintf(stderr,
                            "display profile bytes=%u commands=%u "
                            "render_us=%llu present_us=%llu\n",
                            request.frame_bytes,
                            load_be32((const uint8_t *)(const void *)payload + 12u),
                            (unsigned long long)
                                ((profile_rendered - profile_started) / 1000u),
                            (unsigned long long)
                                ((profile_presented - profile_rendered) / 1000u));
                if (render_status == 0 && present_status == 0) {
                    if (render_only_batch) {
                        /* Off-screen work: the presented state is unchanged. */
                    } else if (window_scene_batch) {
                        active_scanout = UINT32_MAX;
                        active_window_scene_offset = window_scene_offset;
                        active_window_scene_bytes = window_scene_bytes;
                    } else {
                        active_scanout = scanout_offset;
                        active_window_scene_offset = 0u;
                        active_window_scene_bytes = 0u;
                    }
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                    if (!render_only_batch)
                        display_owned = true;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation ==
                           ASTRA_DISPLAY_FRAME_READ_SURFACE &&
                       request.frame_pitch == 0u &&
                       surface_read_valid(
                           (const uint8_t *)(const void *)payload,
                           request.frame_bytes)) {
                if (read_surface(&device,
                                 payload) ==
                    0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation ==
                           ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE &&
                       request.frame_pitch == 0u &&
                       pointer_image_valid(
                           (const uint8_t *)(const void *)payload,
                           request.frame_bytes)) {
                if (pointer_image_update(
                        &device,
                        (const uint8_t *)(const void *)payload) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation == ASTRA_DISPLAY_CURSOR_UPDATE &&
                       request.frame_pitch == 0u &&
                       (request.frame_bytes &
                        ~ASTRA_DISPLAY_CURSOR_FLAGS_MASK) == 0u &&
                       ((request.frame_bytes &
                         ASTRA_DISPLAY_CURSOR_SHAPE_MASK) >>
                            ASTRA_DISPLAY_CURSOR_SHAPE_SHIFT) <
                           ASTRA_POINTER_SHAPE_COUNT) {
                if (pointer_update(&device, request.color_rgb565,
                                   request.frame_bytes, true) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation == ASTRA_DISPLAY_PANIC_TEXT &&
                       request.frame_pitch == 0u &&
                       request.frame_bytes == 0u) {
                const struct terminal_cursor panic_cursor = {0};

                if (copy_cells(current, plane) &&
                    pointer_update(&device, 0u,
                                   ASTRA_DISPLAY_CURSOR_SHAPE(
                                       ASTRA_POINTER_SHAPE_DEFAULT),
                                   true) == 0 &&
                    present_text_on_both_scanouts(
                        &device, current, &panic_cursor, false, text_states,
                        &text_generation, &active_scanout) == 0 &&
                    astra_boot_text_commit(&device, 0) == 0) {
                    cursor = panic_cursor;
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    active_window_scene_offset = 0u;
                    active_window_scene_bytes = 0u;
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                    display_owned = false;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            }
            mailbox_complete(mailbox, &request, status, generation);
        }
        if (display_owned) {
            if (mailbox_wait(mailbox, mailbox_sequence) != 0)
                goto done;
            continue;
        }
        if (!copy_cells(current, plane)) {
            while (nanosleep(&poll_delay, NULL) != 0 && errno == EINTR &&
                   running) {
            }
            continue;
        }
        if (copy_cursor(&sampled, plane))
            cursor = sampled;
        if (++blink_polls >= CURSOR_BLINK_POLLS) {
            blink_polls = 0u;
            cursor_on = !cursor_on;
        }
        cursor_drawn = cursor_on && cursor.visible;
        target_index = active_scanout ==
                               ASTRA_RENDER_BATCH_SCANOUT0_OFFSET ? 1u : 0u;
        target_state = &text_states[target_index];
        cells_changed = !target_state->valid ||
            memcmp(current, target_state->cells, sizeof(current)) != 0;
        if (!target_state->valid) {
            damage_clear(&damage);
            damage.glyphs = TEXT_CELLS;
            scroll_rows = 0u;
        } else {
            scroll_rows = select_scroll(&damage, current, target_state,
                                        &cursor, cursor_drawn);
        }
        if (cells_changed || target_state->cursor_drawn != cursor_drawn ||
            (cursor_drawn && target_state->cursor.cell != cursor.cell)) {
            int update_status = !target_state->valid ?
                present_full_text(&device, current, &cursor, cursor_drawn,
                                  &text_generation, &active_scanout) :
                present_text_update(&device, current, &damage, scroll_rows,
                                    &cursor, cursor_drawn, &text_generation,
                                    &active_scanout);

            if (update_status != 0)
                goto done;
            target_index = scanout_index(active_scanout);
            if (target_index >= 2u)
                goto done;
            remember_text(&text_states[target_index], current, &cursor,
                          cursor_drawn);
        }
        while (nanosleep(&poll_delay, NULL) != 0 && errno == EINTR && running) {
        }
    }
    result = EXIT_SUCCESS;

done:
    astra_graphics_device_close(&device);
    if (plane != MAP_FAILED)
        (void)munmap((void *)plane, TEXT_PAGE_BYTES);
    if (mailbox != MAP_FAILED)
        (void)munmap((void *)mailbox, ASTRA_DISPLAY_MAILBOX_HEADER_BYTES);
    if (payload != MAP_FAILED)
        (void)munmap((void *)payload, ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES);
    if (plane_fd >= 0)
        (void)close(plane_fd);
    if (mailbox_fd >= 0)
        (void)close(mailbox_fd);
    if (payload_fd >= 0)
        (void)close(payload_fd);
    return result;
}
