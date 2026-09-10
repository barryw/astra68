// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"
#include "astra_render_protocol.h"

#include <astra/display.h>
#include <astra/display_mailbox.h>
#include <astra/render_batch.h>
#include <astra/render_builder.h>
#include <astra/theme.h>

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
    TEXT_ROWS_PER_BATCH =
        ASTRA_RENDER_BUILDER_GLYPH_MAX / TEXT_COLUMNS,
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
                   TEXT_ROWS * TEXT_CELL_HEIGHT <= ASTRA_DISPLAY_HEIGHT &&
                   TEXT_ROWS_PER_BATCH != 0u,
               "text grid must fit the hardware scanout and glyph arena");
_Static_assert(POINTER_WIDTH <= POINTER_IMAGE_WIDTH &&
                   POINTER_HEIGHT <= POINTER_IMAGE_HEIGHT,
               "pointer artwork must fit the hardware pointer plane");

static volatile sig_atomic_t running = 1;
static uint8_t terminal_batch[ASTRA_RENDER_BUILDER_BYTES];

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

static int wait_pointer_ready(
    const struct astra_graphics_device *device)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint64_t deadline = astra_monotonic_nanoseconds() + RENDER_TIMEOUT_NS;

    while ((astra_mmio_read(device, ASTRA_REG_POINTER_STATUS) &
            (ASTRA_POINTER_STATUS_WRITE_READY |
             ASTRA_POINTER_STATUS_COMMIT_READY)) !=
           (ASTRA_POINTER_STATUS_WRITE_READY |
            ASTRA_POINTER_STATUS_COMMIT_READY)) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

static int wait_pointer_generation(
    const struct astra_graphics_device *device, uint32_t previous)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint64_t deadline = astra_monotonic_nanoseconds() + RENDER_TIMEOUT_NS;

    while (astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION) ==
           previous) {
        if (astra_monotonic_nanoseconds() >= deadline)
            return -1;
        while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

static int pointer_initialize(const struct astra_graphics_device *device)
{
    uint32_t generation;

    if ((astra_mmio_read(device, ASTRA_REG_CAPABILITIES) &
         ASTRA_CAP_HARDWARE_POINTER) == 0u || wait_pointer_ready(device) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_SELECTOR, 0u);
    for (unsigned y = 0u; y < POINTER_IMAGE_HEIGHT; ++y) {
        for (unsigned x = 0u; x < POINTER_IMAGE_WIDTH; ++x) {
            uint32_t argb = 0u;

            if (x < POINTER_WIDTH && y < POINTER_HEIGHT) {
                uint16_t bit = (uint16_t)(UINT16_C(0x8000) >> x);

                argb = (pointer_inner[y] & bit) != 0u ?
                    UINT32_C(0xffffffff) :
                    (pointer_outer[y] & bit) != 0u ?
                        UINT32_C(0xff000000) : 0u;
            }
            astra_mmio_write(device, ASTRA_REG_POINTER_IMAGE_DATA, argb);
        }
    }
    astra_mmio_write(device, ASTRA_REG_POINTER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_POINTER_POSITION, 0u);
    astra_mmio_write(device, ASTRA_REG_POINTER_HOTSPOT,
                     (POINTER_HOT_Y << 16) | POINTER_HOT_X);
    generation = astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION);
    astra_mmio_write(device, ASTRA_REG_POINTER_COMMIT, 3u);
    return wait_pointer_generation(device, generation);
}

static int pointer_update(const struct astra_graphics_device *device,
                          uint32_t packed)
{
    uint32_t x = packed & ASTRA_DISPLAY_HOST_CURSOR_X_MASK;
    uint32_t y = (packed & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
                 ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT;
    uint32_t generation;

    if (wait_pointer_ready(device) != 0)
        return -1;
    if (x >= ASTRA_DISPLAY_WIDTH)
        x = ASTRA_DISPLAY_WIDTH - 1u;
    if (y >= ASTRA_DISPLAY_HEIGHT)
        y = ASTRA_DISPLAY_HEIGHT - 1u;
    astra_mmio_write(device, ASTRA_REG_POINTER_POSITION, (y << 16) | x);
    astra_mmio_write(device, ASTRA_REG_POINTER_CONTROL,
                     (packed & ASTRA_DISPLAY_HOST_CURSOR_VISIBLE) != 0u);
    generation = astra_mmio_read(device, ASTRA_REG_POINTER_GENERATION);
    astra_mmio_write(device, ASTRA_REG_POINTER_COMMIT, 1u);
    return wait_pointer_generation(device, generation);
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

static bool batch_contains(uint32_t bytes, uint32_t arena_offset,
                           uint32_t length)
{
    uint32_t offset;

    if (arena_offset < ASTRA_RENDER_BATCH_ARENA_OFFSET)
        return false;
    offset = arena_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET;
    return offset <= bytes && length <= bytes - offset;
}

static bool batch_descriptor_valid(uint32_t bytes, uint32_t arena_offset)
{
    return arena_offset >= ASTRA_RENDER_BATCH_RESOURCE_OFFSET &&
           arena_offset < ASTRA_RENDER_BATCH_GLYPH_OFFSET &&
           (arena_offset - ASTRA_RENDER_BATCH_RESOURCE_OFFSET) %
                   ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES == 0u &&
           batch_contains(bytes, arena_offset,
                          ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES);
}

static bool batch_data_descriptor_valid(const volatile uint8_t *batch,
                                         uint32_t bytes,
                                         uint32_t arena_offset)
{
    uint32_t record;
    uint32_t data;
    uint32_t data_bytes;

    if (!batch_descriptor_valid(bytes, arena_offset))
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
    uint32_t capacity = (ASTRA_RENDER_BATCH_DATA_OFFSET -
                         ASTRA_RENDER_BATCH_GLYPH_OFFSET) /
                        ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES;

    return count != 0u && count <= capacity &&
           arena_offset >= ASTRA_RENDER_BATCH_GLYPH_OFFSET &&
           arena_offset < ASTRA_RENDER_BATCH_DATA_OFFSET &&
           (arena_offset - ASTRA_RENDER_BATCH_GLYPH_OFFSET) %
                   ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES == 0u &&
           batch_contains(bytes, arena_offset,
                          count * ASTRA_RENDER_GLYPH_DESCRIPTOR_BYTES);
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
        uint32_t operation = load_be32(batch + offset + 4u) >> 16;

        if (load_be32(batch + offset) !=
                ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                 ASTRA_RENDER_COMMAND_BYTES) ||
            load_be32(batch + offset + 8u) == 0u ||
            load_be32(batch + offset + 12u) != load_be32(batch + 24u) ||
            !batch_descriptor_valid(
                bytes, load_be32(batch + offset + 32u)) ||
            ((operation == ASTRA_RENDER_OP_BLIT ||
              operation == ASTRA_RENDER_OP_GLYPH_RUN) &&
             !batch_descriptor_valid(
                 bytes, load_be32(batch + offset + 36u))) ||
            (operation == ASTRA_RENDER_OP_GLYPH_RUN &&
             (!batch_data_descriptor_valid(
                  batch, bytes, load_be32(batch + offset + 36u)) ||
              !batch_glyphs_valid(bytes, load_be32(batch + offset + 40u),
                                  load_be32(batch + offset + 44u)))))
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
    uint32_t command_bytes;
    uint32_t generation;
    uint32_t resource_start = ASTRA_RENDER_BATCH_RESOURCE_OFFSET -
                              ASTRA_RENDER_BATCH_ARENA_OFFSET;
    uint32_t data_start = ASTRA_RENDER_BATCH_DATA_OFFSET -
                          ASTRA_RENDER_BATCH_ARENA_OFFSET;
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
    command_bytes = command_count * ASTRA_RENDER_COMMAND_BYTES;
    generation = load_be32(batch + 24u);
    astra_graphics_memory_map_init(&mapping);
    if (astra_graphics_memory_map_open(
            device, &mapping,
            ASTRA_GRAPHICS_ARENA_BASE + ASTRA_RENDER_BATCH_ARENA_OFFSET,
            bytes) != 0) {
        fprintf(stderr, "render batch graphics mapping failed\n");
        return -1;
    }
    astra_graphics_memory_copy_to(mapping.data, (const void *)batch,
                                  ASTRA_RENDER_BATCH_HEADER_BYTES);
    astra_graphics_memory_copy_to(
        mapping.data + ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET,
        (const uint8_t *)(const void *)batch +
            ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
            ASTRA_RENDER_BATCH_ARENA_OFFSET,
        command_bytes);
    if (bytes > resource_start) {
        uint32_t resource_end = bytes < data_start ? bytes : data_start;

        astra_graphics_memory_copy_to(
            mapping.data + resource_start,
            (const uint8_t *)(const void *)batch + resource_start,
            resource_end - resource_start);
    }
    if (bytes > data_start)
        astra_graphics_memory_copy_to(
            mapping.data + data_start,
            (const uint8_t *)(const void *)batch + data_start,
            bytes - data_start);
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
           execute_render_batch(device, terminal_batch, bytes, scanout) == 0 ?
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
        present(device, scanout) != 0)
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
    if (present(device, target) != 0)
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
        scanout == target || present(device, scanout) != 0)
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

    for (uint32_t first = 0u; first < TEXT_ROWS;
         first += TEXT_ROWS_PER_BATCH) {
        uint32_t count = TEXT_ROWS - first;
        uint32_t destination;
        uint32_t scanout;

        if (count > TEXT_ROWS_PER_BATCH)
            count = TEXT_ROWS_PER_BATCH;
        if (!astra_render_builder_init(&builder, terminal_batch,
                                       sizeof(terminal_batch),
                                       frame_generation))
            return -1;
        destination = astra_render_builder_frame(&builder);
        if (first == 0u &&
            !astra_render_builder_fill(
                &builder, destination, 0, 0, ASTRA_DISPLAY_WIDTH,
                ASTRA_DISPLAY_HEIGHT, 0u))
            return -1;
        for (uint32_t row = first; row < first + count; ++row)
            if (!add_text_span(&builder, destination, cells, row, 0u,
                               TEXT_COLUMNS))
                return -1;
        if (cursor_drawn && cursor->cell / TEXT_COLUMNS >= first &&
            cursor->cell / TEXT_COLUMNS < first + count &&
            !add_cursor(&builder, destination, cursor, true))
            return -1;
        if (execute_finished_batch(device, &builder, &scanout) != 0 ||
            scanout != target)
            return -1;
    }
    if (present(device, target) != 0)
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
        present(device, scanout) != 0)
        return -1;
    *active_scanout = scanout;
    return 0;
}

static int self_test(void)
{
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
        ASTRA_RENDER_SURFACE_DESCRIPTOR_BYTES];
    volatile AstraDisplayMailbox *shared;
    uint64_t wait_started;
    pid_t child;
    int child_status;

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
    mailbox.version = ASTRA_DISPLAY_MAILBOX_VERSION_1_3;
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

    (void)memset(validation_batch, 0, sizeof(validation_batch));
    store_be32(validation_batch + 0u, ASTRA_RENDER_BATCH_MAGIC);
    store_be32(validation_batch + 4u, ASTRA_RENDER_BATCH_VERSION_1_0);
    store_be32(validation_batch + 8u, sizeof(validation_batch));
    store_be32(validation_batch + 12u, 1u);
    store_be32(validation_batch + 16u,
               ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    store_be32(validation_batch + 20u,
               ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    store_be32(validation_batch + 24u, 7u);
    store_be32(validation_batch + 28u, ASTRA_RENDER_BATCH_SCANOUT1_OFFSET);
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
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_DATA_OFFSET);
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;
    store_be32(validation_batch +
                   ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                   ASTRA_RENDER_BATCH_ARENA_OFFSET + 32u,
               ASTRA_RENDER_BATCH_RESOURCE_OFFSET);
    validation_batch[0] = 0u;
    if (render_batch_valid(validation_batch, sizeof(validation_batch)))
        return EXIT_FAILURE;

    puts("ASTRA_TERMINAL_DISPLAY_SELF_TEST PASS");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    const struct timespec poll_delay = { .tv_sec = 0, .tv_nsec = 16000000 };
    struct astra_graphics_device device;
    struct terminal_cursor cursor = { .cell = 0u, .visible = false };
    struct terminal_scanout_state text_states[2] = {0};
    volatile uint8_t *plane = MAP_FAILED;
    volatile AstraDisplayMailbox *mailbox = MAP_FAILED;
    uint8_t current[TEXT_CELLS];
    struct stat plane_stat;
    struct stat mailbox_stat;
    unsigned blink_polls = 0u;
    bool cursor_on = true;
    int plane_fd = -1;
    int mailbox_fd = -1;
    int result = EXIT_FAILURE;
    uint32_t text_generation = 0u;
    uint32_t active_scanout;
    uint32_t mailbox_sequence = 0u;
    bool display_owned = false;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc == 2 && strcmp(argv[1], "--mailbox-bytes") == 0) {
        printf("%u\n", ASTRA_DISPLAY_MAILBOX_BYTES);
        return EXIT_SUCCESS;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: %s [--self-test|--mailbox-bytes] | "
                "<shared-post-text-page> <shared-display-mailbox>\n",
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
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
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
            if (request.id != 0u &&
                request.operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
                (request.color_rgb565 & 0xffff0000u) == 0u) {
                if (present_solid_frame(
                        &device, (uint16_t)request.color_rgb565,
                        &text_generation, &active_scanout) == 0) {
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
                if (present_rgb565_frame(
                    &device,
                    (const uint8_t *)(const void *)mailbox +
                        ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                    &text_generation, &active_scanout) == 0) {
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
                render_status = make_render_target_inactive(
                    &device, scanout_offset, &text_generation,
                    &active_scanout);
                if (render_status == 0) {
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
                    active_scanout = scanout_offset;
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
                if (pointer_update(&device, request.color_rgb565) == 0) {
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
                    pointer_update(&device, 0u) == 0 &&
                    present_text_on_both_scanouts(
                        &device, current, &panic_cursor, false, text_states,
                        &text_generation, &active_scanout) == 0 &&
                    astra_boot_text_commit(&device, 0) == 0) {
                    cursor = panic_cursor;
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
            damage.glyphs = ASTRA_RENDER_BUILDER_GLYPH_MAX + 1u;
            scroll_rows = 0u;
        } else {
            scroll_rows = select_scroll(&damage, current, target_state,
                                        &cursor, cursor_drawn);
        }
        if (cells_changed || target_state->cursor_drawn != cursor_drawn ||
            (cursor_drawn && target_state->cursor.cell != cursor.cell)) {
            int update_status = !target_state->valid ||
                                damage.glyphs >
                                    ASTRA_RENDER_BUILDER_GLYPH_MAX ?
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
        (void)munmap((void *)mailbox, ASTRA_DISPLAY_MAILBOX_BYTES);
    if (plane_fd >= 0)
        (void)close(plane_fd);
    if (mailbox_fd >= 0)
        (void)close(mailbox_fd);
    return result;
}
