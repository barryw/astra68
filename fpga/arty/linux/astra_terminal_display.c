// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"
#include "astra_render_protocol.h"

#include <astra/display.h>
#include <astra/display_mailbox.h>
#include <astra/render_batch.h>

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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    TEXT_COLUMNS = 90u,
    TEXT_ROWS = 30u,
    TEXT_CELLS = TEXT_COLUMNS * TEXT_ROWS,
    TEXT_PAGE_BYTES = 4096u,
    FONT_GLYPHS = 256u,
    FONT_ROWS = 8u,
    FONT_COLUMNS = 8u,
    CURSOR_HEIGHT = 3u,
    CURSOR_BLINK_POLLS = 32u,
    POINTER_SPRITE = 0u,
    POINTER_WIDTH = ASTRA_RENDER_CURSOR_WIDTH,
    POINTER_HEIGHT = ASTRA_RENDER_CURSOR_HEIGHT,
    POINTER_PITCH = 64u,
    POINTER_BYTES = POINTER_PITCH * POINTER_HEIGHT,
    POINTER_PALETTE = ASTRA_RENDER_CURSOR_PALETTE_BANK,
    POINTER_HOT_X = 3u,
    POINTER_HOT_Y = 1u,
    SPRITE_COUNT = 64u,
};

#include "astra_terminal_font.inc"

_Static_assert(sizeof(terminal_font) == FONT_GLYPHS * FONT_ROWS,
               "terminal font must contain 256 8x8 glyphs");
_Static_assert(TEXT_PAGE_BYTES == ASTRA_TEXT_PLANE_BYTES,
               "guest and renderer text pages differ");
_Static_assert(POINTER_PITCH >= POINTER_WIDTH &&
                   (POINTER_PITCH & 63u) == 0u,
               "sprite rows require a 64-byte-aligned pitch");

static volatile sig_atomic_t running = 1;

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

static void stop(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static int wait_sprite_write_ready(
    const struct astra_graphics_device *device)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint64_t deadline = astra_monotonic_nanoseconds() + RENDER_TIMEOUT_NS;

    while ((astra_mmio_read(device, ASTRA_REG_SPRITE_STATUS) &
            ASTRA_SPRITE_STATUS_WRITE_READY) == 0u) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

static void write_sprite_descriptor(
    const struct astra_graphics_device *device, unsigned sprite,
    const uint32_t words[8])
{
    for (unsigned word = 0u; word < 8u; ++word) {
        astra_mmio_write(device, ASTRA_REG_SPRITE_DESCRIPTOR_SELECTOR,
                         (word << 8) | sprite);
        astra_mmio_write(device, ASTRA_REG_SPRITE_DESCRIPTOR_DATA,
                         words[word]);
    }
}

static void encode_pointer_descriptor(uint32_t words[8], uint32_t x,
                                      uint32_t y, bool visible)
{
    words[0] = (visible ? UINT32_C(0x13) : UINT32_C(0x12)) |
               (POINTER_PALETTE << 16);
    words[1] = (uint16_t)(x - POINTER_HOT_X) |
               ((uint32_t)(uint16_t)(y - POINTER_HOT_Y) << 16);
    words[2] = POINTER_WIDTH | (POINTER_HEIGHT << 8) | (255u << 16);
    words[3] = POINTER_WIDTH | (POINTER_HEIGHT << 16);
    words[4] = ASTRA_GRAPHICS_ARENA_BASE + ASTRA_RENDER_CURSOR_OFFSET;
    words[5] = POINTER_PITCH;
    words[6] = 0u;
    words[7] = 0u;
}

static int pointer_initialize(const struct astra_graphics_device *device)
{
    struct astra_graphics_memory_map mapping;
    uint32_t words[8] = { 0u };

    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping,
            ASTRA_GRAPHICS_ARENA_BASE + ASTRA_RENDER_CURSOR_OFFSET,
            POINTER_BYTES) != 0)
        return -1;
    for (unsigned y = 0u; y < POINTER_HEIGHT; ++y) {
        for (unsigned x = 0u; x < POINTER_WIDTH; ++x) {
            uint16_t bit = (uint16_t)(UINT16_C(0x8000) >> x);

            mapping.data[y * POINTER_PITCH + x] =
                (pointer_inner[y] & bit) != 0u ? 2u :
                (pointer_outer[y] & bit) != 0u ? 1u : 0u;
        }
    }
    astra_graphics_memory_barrier();
    astra_graphics_memory_map_close(&mapping);
    if (wait_sprite_write_ready(device) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_SELECTOR,
                     POINTER_PALETTE << 8);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_DATA, 0x00000000u);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_SELECTOR,
                     (POINTER_PALETTE << 8) | 1u);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_DATA, 0xff000000u);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_SELECTOR,
                     (POINTER_PALETTE << 8) | 2u);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_DATA, 0xffffffffu);
    encode_pointer_descriptor(words, 0u, 0u, false);
    for (unsigned sprite = 0u; sprite < SPRITE_COUNT; ++sprite)
        write_sprite_descriptor(device, sprite, words);
    astra_mmio_write(device, ASTRA_REG_SPRITE_CONTROL, 1u);
    return 0;
}

static int pointer_update(const struct astra_graphics_device *device,
                          uint32_t packed, bool commit)
{
    uint32_t words[8];

    if (wait_sprite_write_ready(device) != 0)
        return -1;
    encode_pointer_descriptor(
        words, packed & ASTRA_DISPLAY_HOST_CURSOR_X_MASK,
        (packed & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
            ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT,
        (packed & ASTRA_DISPLAY_HOST_CURSOR_VISIBLE) != 0u);
    write_sprite_descriptor(device, POINTER_SPRITE, words);
    return commit ?
        astra_graphics_scene_commit(device, RENDER_TIMEOUT_NS, NULL) : 0;
}

struct terminal_cursor {
    uint32_t cell;
    bool visible;
};

struct display_request {
    uint32_t sequence;
    uint32_t id;
    uint32_t operation;
    uint32_t color_rgb565;
    uint32_t frame_pitch;
    uint32_t frame_bytes;
};

static uint32_t load_be32(const volatile uint8_t *bytes)
{
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

static bool render_batch_valid(const volatile uint8_t *batch,
                               uint32_t bytes)
{
    uint32_t command_count;

    if (bytes < ASTRA_RENDER_BATCH_MIN_BYTES ||
        bytes > ASTRA_RENDER_BATCH_MAX_BYTES ||
        load_be32(batch + 0u) != ASTRA_RENDER_BATCH_MAGIC ||
        load_be32(batch + 4u) != ASTRA_RENDER_BATCH_VERSION_1_0 ||
        load_be32(batch + 8u) != bytes ||
        load_be32(batch + 16u) != ASTRA_RENDER_BATCH_SUBMISSION_OFFSET ||
        load_be32(batch + 20u) != ASTRA_RENDER_BATCH_COMPLETION_OFFSET ||
        load_be32(batch + 24u) == 0u ||
        (load_be32(batch + 28u) != ASTRA_RENDER_BATCH_SCANOUT0_OFFSET &&
         load_be32(batch + 28u) != ASTRA_RENDER_BATCH_SCANOUT1_OFFSET))
        return false;
    command_count = load_be32(batch + 12u);
    if (command_count == 0u || command_count > ASTRA_RENDER_RING_ENTRIES)
        return false;
    for (uint32_t offset = 32u; offset < ASTRA_RENDER_BATCH_HEADER_BYTES;
         offset += 4u)
        if (load_be32(batch + offset) != 0u)
            return false;
    for (uint32_t index = 0u; index < command_count; ++index) {
        uint32_t offset = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET +
            index * ASTRA_RENDER_COMMAND_BYTES;

        if (load_be32(batch + offset) !=
                ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                 ASTRA_RENDER_COMMAND_BYTES) ||
            load_be32(batch + offset + 8u) == 0u ||
            load_be32(batch + offset + 12u) != load_be32(batch + 24u))
            return false;
    }
    return true;
}

static int wait_render(const struct astra_graphics_device *device,
                       uint32_t command_count)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint64_t deadline = astra_monotonic_nanoseconds() + RENDER_TIMEOUT_NS;

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
        while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
        }
    }
}

static int execute_render_batch(const struct astra_graphics_device *device,
                                const volatile uint8_t *batch,
                                uint32_t bytes, uint32_t *scanout_offset)
{
    struct astra_graphics_memory_map mapping;
    uint64_t profile_started = astra_monotonic_nanoseconds();
    uint64_t profile_copied;
    uint64_t profile_rendered;
    uint32_t command_count;
    uint32_t generation;
    int result = -1;
    static int profile_commands = -1;

    if (profile_commands < 0)
        profile_commands = getenv("ASTRA_DISPLAY_PROFILE_COMMANDS") != NULL;
    if (!render_batch_valid(batch, bytes)) {
        fprintf(stderr, "render batch rejected before submission (%u bytes)\n",
                bytes);
        return -1;
    }
    *scanout_offset = load_be32(batch + 28u);
    command_count = load_be32(batch + 12u);
    generation = load_be32(batch + 24u);
    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping,
            ASTRA_GRAPHICS_ARENA_BASE + ASTRA_RENDER_BATCH_ARENA_OFFSET,
            bytes) != 0) {
        fprintf(stderr, "render batch graphics mapping failed\n");
        return -1;
    }
    astra_graphics_memory_copy_to(mapping.data, (const void *)batch, bytes);
    astra_graphics_memory_barrier();
    profile_copied = astra_monotonic_nanoseconds();

    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL, 0u);
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
    if (astra_mmio_read(device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) != 0u ||
        astra_mmio_read(device, ASTRA_REG_RENDER_COMPLETION_PRODUCER) != 0u) {
        fprintf(stderr, "render batch rebase failed\n");
        goto done;
    }
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
        goto done;
    }
    profile_rendered = astra_monotonic_nanoseconds();
    if (getenv("ASTRA_DISPLAY_PROFILE") != NULL)
        fprintf(stderr,
                "render profile copy_us=%llu hardware_us=%llu\n",
                (unsigned long long)
                    ((profile_copied - profile_started) / 1000u),
                (unsigned long long)
                    ((profile_rendered - profile_copied) / 1000u));
    for (uint32_t index = 0u; index < command_count; ++index) {
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
            goto done;
        }
    }
    result = 0;

done:
    astra_graphics_memory_map_close(&mapping);
    return result;
}

static bool mailbox_take(volatile const AstraDisplayMailbox *mailbox,
                         uint32_t previous_sequence,
                         struct display_request *request)
{
    uint32_t sequence = mailbox->request_sequence;

    astra_graphics_memory_barrier();
    if (mailbox->magic != ASTRA_DISPLAY_MAILBOX_MAGIC ||
        (mailbox->version != ASTRA_DISPLAY_MAILBOX_VERSION_1_1 &&
         mailbox->version != ASTRA_DISPLAY_MAILBOX_VERSION_1_2 &&
         mailbox->version != ASTRA_DISPLAY_MAILBOX_VERSION_1_3 &&
         mailbox->version != ASTRA_DISPLAY_MAILBOX_VERSION_1_4) ||
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

static bool request_sequence_restarted(uint32_t request_id,
                                       uint32_t previous_id)
{
    return request_id != 0u && previous_id != 0u &&
           request_id <= previous_id;
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
}

static int mailbox_wait(volatile AstraDisplayMailbox *mailbox,
                        uint32_t sequence)
{
    if (mailbox->version < ASTRA_DISPLAY_MAILBOX_VERSION_1_4) {
        const struct timespec delay = { .tv_sec = 0, .tv_nsec = 16000000 };

        while (nanosleep(&delay, NULL) != 0 && errno == EINTR && running) {
        }
        return 0;
    }
    while (running && mailbox->request_sequence == sequence) {
        if (syscall(SYS_futex, &mailbox->request_sequence,
                    FUTEX_WAIT, sequence, NULL, NULL, 0) == 0 ||
            errno == EAGAIN || errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static void fill_solid(volatile uint8_t *framebuffer, uint16_t color)
{
    uint8_t high = (uint8_t)(color >> 8);
    uint8_t low = (uint8_t)color;
    size_t offset;

    for (offset = 0u; offset < ASTRA_FRAMEBUFFER_BYTES; offset += 2u) {
        framebuffer[offset] = high;
        framebuffer[offset + 1u] = low;
    }
}

static uint32_t cell_x(uint32_t column)
{
    return column * ASTRA_FRAMEBUFFER_WIDTH / TEXT_COLUMNS;
}

static uint32_t cell_y(uint32_t row)
{
    return row * ASTRA_FRAMEBUFFER_HEIGHT / TEXT_ROWS;
}

static void draw_cell(volatile uint8_t *framebuffer, uint32_t pitch,
                      uint32_t cell_row, uint32_t cell_column,
                      uint8_t character, bool underline)
{
    uint32_t left = cell_x(cell_column);
    uint32_t right = cell_x(cell_column + 1u);
    uint32_t top = cell_y(cell_row);
    uint32_t bottom = cell_y(cell_row + 1u);
    uint32_t pixel_y;

    for (pixel_y = top; pixel_y < bottom; ++pixel_y) {
        uint8_t bits = terminal_font[(uint32_t)character * FONT_ROWS +
                                     (pixel_y - top) * FONT_ROWS /
                                         (bottom - top)];
        uint32_t pixel_x;
        size_t row_offset = (size_t)pixel_y * pitch + (size_t)left * 2u;

        if (underline && pixel_y >= bottom - CURSOR_HEIGHT)
            bits = 0xffu;

        for (pixel_x = left; pixel_x < right; ++pixel_x) {
            uint32_t source_x =
                (pixel_x - left) * FONT_COLUMNS / (right - left);
            uint8_t value =
                (bits & (0x80u >> source_x)) != 0u ? 0xffu : 0u;

            framebuffer[row_offset + (pixel_x - left) * 2u] = value;
            framebuffer[row_offset + (pixel_x - left) * 2u + 1u] = value;
        }
    }
}

static void copy_cells(uint8_t out[TEXT_CELLS],
                       volatile const uint8_t *plane)
{
    uint8_t first[TEXT_CELLS];
    uint8_t second[TEXT_CELLS];
    unsigned attempt;

    for (attempt = 0; attempt < 4u; ++attempt) {
        uint32_t cell;

        for (cell = 0; cell < TEXT_CELLS; ++cell)
            first[cell] = plane[cell];
        astra_graphics_memory_barrier();
        for (cell = 0; cell < TEXT_CELLS; ++cell)
            second[cell] = plane[cell];
        if (memcmp(first, second, sizeof(first)) == 0)
            break;
    }
    (void)memcpy(out, second, sizeof(second));
}

static void draw_text_frame(volatile uint8_t *framebuffer,
                            uint8_t cells[TEXT_CELLS],
                            volatile const uint8_t *plane)
{
    copy_cells(cells, plane);
    for (uint32_t cell = 0u; cell < TEXT_CELLS; ++cell)
        draw_cell(framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                  cell / TEXT_COLUMNS, cell % TEXT_COLUMNS,
                  cells[cell], false);
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

static int present(const struct astra_graphics_device *device,
                   uint32_t scanout_offset)
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
    astra_mmio_write(device, ASTRA_REG_BACKDROP, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_BASE,
                     ASTRA_FRAMEBUFFER_BASE + scanout_offset);
    astra_mmio_write(device, ASTRA_REG_FB_PITCH, ASTRA_FRAMEBUFFER_PITCH);
    astra_mmio_write(device, ASTRA_REG_FB_SIZE, size);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_X, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_Y, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL, 3u);
    astra_mmio_write(device, ASTRA_REG_FB_KEY, 0u);
    astra_mmio_write(device, ASTRA_REG_TILE0_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_TILE1_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_GLOBAL_CONTROL, 1u);
    return astra_graphics_scene_commit(device, UINT64_C(2000000000), NULL);
}

static int self_test(void)
{
    uint8_t framebuffer[ASTRA_FRAMEBUFFER_PITCH * ASTRA_FRAMEBUFFER_HEIGHT];
    uint8_t plane[TEXT_PAGE_BYTES];
    struct terminal_cursor cursor;
    uint32_t left = cell_x(0u);
    uint32_t right = cell_x(1u);
    uint32_t top = cell_y(0u);
    uint32_t bottom = cell_y(1u);
    uint32_t y;
    AstraDisplayMailbox mailbox;
    struct display_request request;
    static uint8_t batch[ASTRA_RENDER_BATCH_MIN_BYTES];
    volatile AstraDisplayMailbox *shared;
    uint64_t wait_started;
    pid_t child;
    int child_status;

    for (y = 0u; y < POINTER_HEIGHT; ++y)
        if ((pointer_inner[y] & (uint16_t)~pointer_outer[y]) != 0u)
            return EXIT_FAILURE;

    (void)memset(framebuffer, 0x5a, sizeof(framebuffer));
    draw_cell(framebuffer, ASTRA_FRAMEBUFFER_PITCH, 0u, 0u, 'A', false);
    for (y = top; y < bottom; ++y) {
        uint8_t bits = terminal_font[(uint32_t)'A' * FONT_ROWS +
                                     (y - top) * FONT_ROWS /
                                         (bottom - top)];
        uint32_t x;

        for (x = left; x < right; ++x) {
            uint32_t source_x =
                (x - left) * FONT_COLUMNS / (right - left);
            uint8_t expected =
                (bits & (0x80u >> source_x)) != 0u ? 0xffu : 0u;
            size_t offset = (size_t)y * ASTRA_FRAMEBUFFER_PITCH +
                            (size_t)x * 2u;

            if (framebuffer[offset] != expected ||
                framebuffer[offset + 1u] != expected)
                return EXIT_FAILURE;
        }
    }

    draw_cell(framebuffer, ASTRA_FRAMEBUFFER_PITCH, 0u, 0u, 'A', true);
    for (y = bottom - CURSOR_HEIGHT; y < bottom; ++y) {
        uint32_t x;

        for (x = left; x < right; ++x) {
            size_t offset = (size_t)y * ASTRA_FRAMEBUFFER_PITCH +
                            (size_t)x * 2u;

            if (framebuffer[offset] != 0xffu ||
                framebuffer[offset + 1u] != 0xffu)
                return EXIT_FAILURE;
        }
    }

    (void)memset(plane, 0, sizeof(plane));
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

    (void)memset(&mailbox, 0, sizeof(mailbox));
    mailbox.magic = ASTRA_DISPLAY_MAILBOX_MAGIC;
    mailbox.version = ASTRA_DISPLAY_MAILBOX_VERSION_1_3;
    mailbox.request_id = 7u;
    mailbox.operation = ASTRA_DISPLAY_FRAME_PRESENT_SOLID;
    mailbox.color_rgb565 = 0x135du;
    mailbox.request_sequence = 4u;
    if (!mailbox_take(&mailbox, 0u, &request) || request.sequence != 4u ||
        request.id != 7u || request.color_rgb565 != 0x135du ||
        mailbox_take(&mailbox, 4u, &request))
        return EXIT_FAILURE;
    if (request_sequence_restarted(8u, 7u) ||
        !request_sequence_restarted(1u, 7u))
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
    shared->version = ASTRA_DISPLAY_MAILBOX_VERSION_1_4;
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
    (void)munmap((void *)shared, sizeof(*shared));

    (void)memset(batch, 0, sizeof(batch));
    store_be32(batch + 0u, ASTRA_RENDER_BATCH_MAGIC);
    store_be32(batch + 4u, ASTRA_RENDER_BATCH_VERSION_1_0);
    store_be32(batch + 8u, sizeof(batch));
    store_be32(batch + 12u, 1u);
    store_be32(batch + 16u, ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    store_be32(batch + 20u, ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    store_be32(batch + 24u, 7u);
    store_be32(batch + 28u, ASTRA_RENDER_BATCH_SCANOUT1_OFFSET);
    store_be32(batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET,
               (uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                   ASTRA_RENDER_COMMAND_BYTES);
    store_be32(batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 8u,
               1u);
    store_be32(batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 12u,
               7u);
    if (!render_batch_valid(batch, sizeof(batch)))
        return EXIT_FAILURE;
    batch[0] = 0u;
    if (render_batch_valid(batch, sizeof(batch)))
        return EXIT_FAILURE;

    puts("ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    const struct timespec poll_delay = { .tv_sec = 0, .tv_nsec = 16000000 };
    struct astra_graphics_device device;
    struct terminal_cursor cursor = { .cell = 0u, .visible = false };
    struct terminal_cursor previous_cursor;
    volatile uint8_t *plane = MAP_FAILED;
    volatile AstraDisplayMailbox *mailbox = MAP_FAILED;
    uint8_t current[TEXT_CELLS];
    uint8_t previous[TEXT_CELLS];
    struct stat plane_stat;
    struct stat mailbox_stat;
    unsigned blink_polls = 0u;
    bool cursor_on = true;
    int plane_fd = -1;
    int mailbox_fd = -1;
    int result = EXIT_FAILURE;
    uint32_t cell;
    uint32_t mailbox_sequence = 0u;
    uint32_t previous_request_id = 0u;
    bool guest_restarted = false;
    bool display_owned = false;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc != 3) {
        fprintf(stderr, "usage: %s <shared-post-text-page> "
                "<shared-display-mailbox>\n", argv[0]);
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
        mailbox_stat.st_size < (off_t)ASTRA_DISPLAY_MAILBOX_BYTES) {
        fprintf(stderr, "shared display mailbox must be at least %u bytes\n",
                ASTRA_DISPLAY_MAILBOX_BYTES);
        goto done;
    }
    mailbox = mmap(NULL, ASTRA_DISPLAY_MAILBOX_BYTES,
                   PROT_READ | PROT_WRITE, MAP_SHARED, mailbox_fd, 0);
    if (mailbox == MAP_FAILED) {
        perror("map shared display mailbox");
        goto done;
    }
    mailbox->magic = ASTRA_DISPLAY_MAILBOX_MAGIC;
    mailbox->version = ASTRA_DISPLAY_MAILBOX_VERSION_1_4;
    mailbox->completion_sequence = 0u;
    astra_graphics_memory_barrier();
    if (astra_graphics_device_open(&device, true) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
        goto done;
    if (pointer_initialize(&device) != 0)
        goto done;

    astra_graphics_memory_fill(device.framebuffer, 0,
                               ASTRA_FRAMEBUFFER_BYTES);
    copy_cells(current, plane);
    (void)copy_cursor(&cursor, plane);
    for (cell = 0; cell < TEXT_CELLS; ++cell)
        draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                  cell / TEXT_COLUMNS, cell % TEXT_COLUMNS, current[cell],
                  cursor.visible && cursor.cell == cell);
    (void)memcpy(previous, current, sizeof(previous));
    previous_cursor = cursor;
    astra_graphics_memory_barrier();
    if (present(&device, ASTRA_RENDER_BATCH_SCANOUT0_OFFSET) != 0 ||
        astra_boot_text_commit(&device, 0) != 0)
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
        bool blink_changed = false;
        bool cursor_changed;

        if (mailbox_take(mailbox, mailbox_sequence, &request)) {
            uint32_t generation = 0u;
            uint32_t status = ASTRA_DISPLAY_COMPLETION_BAD_REQUEST;

            mailbox_sequence = request.sequence;
            if (request_sequence_restarted(request.id, previous_request_id))
                guest_restarted = true;
            if (request.id != 0u)
                previous_request_id = request.id;
            if (request.id != 0u &&
                request.operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
                (request.color_rgb565 & 0xffff0000u) == 0u) {
                fill_solid(device.framebuffer,
                           (uint16_t)request.color_rgb565);
                astra_graphics_memory_barrier();
                if (present(&device, ASTRA_RENDER_BATCH_SCANOUT0_OFFSET) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
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
                astra_graphics_memory_copy_to(
                    device.framebuffer,
                    (const uint8_t *)(const void *)mailbox +
                        ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                    ASTRA_FRAMEBUFFER_BYTES);
                astra_graphics_memory_barrier();
                if (present(&device, ASTRA_RENDER_BATCH_SCANOUT0_OFFSET) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
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
                           (const uint8_t *)(const void *)mailbox +
                               ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                           request.frame_bytes)) {
                uint32_t scanout_offset;
                uint64_t profile_started = astra_monotonic_nanoseconds();
                uint64_t profile_rendered;
                uint64_t profile_presented;
                int render_status;
                int present_status;

                scanout_offset = load_be32(
                    (const uint8_t *)(const void *)mailbox +
                    ASTRA_DISPLAY_MAILBOX_HEADER_BYTES + 28u);
                render_status = guest_restarted ? present(
                    &device,
                    scanout_offset == ASTRA_RENDER_BATCH_SCANOUT0_OFFSET ?
                        ASTRA_RENDER_BATCH_SCANOUT1_OFFSET :
                        ASTRA_RENDER_BATCH_SCANOUT0_OFFSET) : 0;
                if (render_status == 0) {
                    guest_restarted = false;
                    render_status = execute_render_batch(
                        &device,
                        (const uint8_t *)(const void *)mailbox +
                            ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                        request.frame_bytes, &scanout_offset);
                }
                profile_rendered = astra_monotonic_nanoseconds();
                present_status = render_status == 0 ?
                    present(&device, scanout_offset) : -1;
                profile_presented = astra_monotonic_nanoseconds();
                if (getenv("ASTRA_DISPLAY_PROFILE") != NULL)
                    fprintf(stderr,
                            "display profile bytes=%u commands=%u "
                            "render_us=%llu present_us=%llu\n",
                            request.frame_bytes,
                            load_be32((const uint8_t *)(const void *)mailbox +
                                      ASTRA_DISPLAY_MAILBOX_HEADER_BYTES + 12u),
                            (unsigned long long)
                                ((profile_rendered - profile_started) / 1000u),
                            (unsigned long long)
                                ((profile_presented - profile_rendered) / 1000u));
                if (render_status == 0 && present_status == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
                    status = ASTRA_DISPLAY_COMPLETION_OK;
                    display_owned = true;
                } else {
                    status = ASTRA_DISPLAY_COMPLETION_IO_ERROR;
                }
            } else if (request.id != 0u &&
                       request.operation == ASTRA_DISPLAY_CURSOR_UPDATE &&
                       request.frame_pitch == 0u &&
                       (request.frame_bytes &
                        ~(ASTRA_DISPLAY_CURSOR_VISIBLE |
                          ASTRA_DISPLAY_CURSOR_DEFER_COMMIT)) == 0u) {
                if (pointer_update(
                        &device, request.color_rgb565,
                        (request.frame_bytes &
                         ASTRA_DISPLAY_CURSOR_DEFER_COMMIT) == 0u) == 0) {
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
                draw_text_frame(device.framebuffer, current, plane);
                (void)memcpy(previous, current, sizeof(previous));
                cursor = (struct terminal_cursor){0};
                previous_cursor = cursor;
                astra_graphics_memory_barrier();
                if (pointer_update(&device, 0u, true) == 0 &&
                    present(&device,
                            ASTRA_RENDER_BATCH_SCANOUT0_OFFSET) == 0 &&
                    astra_boot_text_commit(&device, 0) == 0) {
                    generation = astra_mmio_read(&device,
                                                 ASTRA_REG_GENERATION);
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
        copy_cells(current, plane);
        if (copy_cursor(&sampled, plane))
            cursor = sampled;
        if (++blink_polls >= CURSOR_BLINK_POLLS) {
            blink_polls = 0u;
            cursor_on = !cursor_on;
            blink_changed = true;
        }
        cursor_changed = cursor.visible != previous_cursor.visible ||
                         (cursor.visible &&
                          cursor.cell != previous_cursor.cell);
        for (cell = 0; cell < TEXT_CELLS; ++cell) {
            if (current[cell] == previous[cell])
                continue;
            draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                      cell / TEXT_COLUMNS, cell % TEXT_COLUMNS, current[cell],
                      cursor_on && cursor.visible && cursor.cell == cell);
            previous[cell] = current[cell];
        }
        if (cursor_changed) {
            if (previous_cursor.visible) {
                cell = previous_cursor.cell;
                draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                          cell / TEXT_COLUMNS, cell % TEXT_COLUMNS,
                          current[cell], false);
            }
            if (cursor.visible) {
                cell = cursor.cell;
                draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                          cell / TEXT_COLUMNS, cell % TEXT_COLUMNS,
                          current[cell], cursor_on);
            }
        } else if (blink_changed && cursor.visible) {
            cell = cursor.cell;
            draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                      cell / TEXT_COLUMNS, cell % TEXT_COLUMNS, current[cell],
                      cursor_on);
        }
        previous_cursor = cursor;
        astra_graphics_memory_barrier();
        while (nanosleep(&poll_delay, NULL) != 0 && errno == EINTR && running) {
        }
    }
    result = EXIT_SUCCESS;

done:
    astra_graphics_device_close(&device);
    if (plane != MAP_FAILED)
        (void)munmap((void *)plane, TEXT_PAGE_BYTES);
    if (mailbox != MAP_FAILED)
        (void)munmap((void *)mailbox, ASTRA_DISPLAY_MAILBOX_BYTES);
    if (plane_fd >= 0)
        (void)close(plane_fd);
    if (mailbox_fd >= 0)
        (void)close(mailbox_fd);
    return result;
}
