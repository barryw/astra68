// SPDX-License-Identifier: MIT
// Exercise the complete 64-sprite DDR fetch path and leave it quiescent.

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "astra_graphics_hw.h"
#include <astra/graphics.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    SPRITE_COUNT = ASTRA_GRAPHICS_SPRITE_COUNT,
    PALETTE_BANK_COUNT = 16u,
    SOURCE_WIDTH = ASTRA_SPRITE_SOURCE_WIDTH_MAX,
    SOURCE_HEIGHT = ASTRA_SPRITE_SOURCE_HEIGHT_MAX,
    SOURCE_PITCH = 128u,
    SHAPE_BYTES = SOURCE_PITCH * SOURCE_HEIGHT,
    MAX_SHAPE_STORAGE_BYTES = SPRITE_COUNT * SHAPE_BYTES,
    VARIABLE_SCENE_COUNT = 2u,
    VARIABLE_SHAPE_STORAGE_BYTES = 1024u * 1024u,
    SHAPE_STORAGE_BYTES = MAX_SHAPE_STORAGE_BYTES +
        VARIABLE_SHAPE_STORAGE_BYTES,
    SHAPE_STORAGE_BASE = ASTRA_GRAPHICS_ARENA_BASE + 0x00200000u,
    STRESS_DEST_WIDTH = 128u,
    STRESS_DEST_HEIGHT = 720u,
    CLIP_DEST_WIDTH = 96u,
    CLIP_DEST_HEIGHT = 64u,
    GRID_DEST_WIDTH = 112u,
    GRID_DEST_HEIGHT = 72u,
    BUILD_CYCLE_LIMIT = 4300u,
    COMMIT_TIMEOUT_NS = 2000000000u,
    PHASE_TIMEOUT_NS = 3000000000u,
    PHASE_FRAMES = 3u,
};

_Static_assert(SHAPE_BYTES == 16u * 1024u,
               "maximum sprite shape must occupy 16 KiB");
_Static_assert(MAX_SHAPE_STORAGE_BYTES == 1024u * 1024u,
               "64 maximum shapes must occupy 1 MiB");
_Static_assert(SHAPE_STORAGE_BYTES == 2u * 1024u * 1024u,
               "sprite certification storage must occupy 2 MiB");
_Static_assert(SHAPE_STORAGE_BASE >=
                   ASTRA_FRAMEBUFFER_BASE + ASTRA_FRAMEBUFFER_BYTES,
               "sprite certification shapes overlap the framebuffer");
_Static_assert(SHAPE_STORAGE_BASE + SHAPE_STORAGE_BYTES <=
                   ASTRA_GRAPHICS_ARENA_LIMIT,
               "sprite certification shapes exceed the graphics arena");
_Static_assert(ASTRA_SPRITES_PER_LINE * SOURCE_WIDTH ==
                   ASTRA_SPRITE_PIXELS_PER_LINE,
               "stress scene must saturate the published scanline limits");

static const uint32_t palette_color[PALETTE_BANK_COUNT] = {
    0xffff3b5cu, 0xffff7a32u, 0xffffc43du, 0xffc8f04au,
    0xff56e36du, 0xff2ee6b8u, 0xff32d9f5u, 0xff3697ffu,
    0xff5165ffu, 0xff8d59ffu, 0xffc44dffu, 0xffff50d8u,
    0xffff62a3u, 0xfff2f2f2u, 0xff8ce8ffu, 0xffffe58cu,
};

struct phase_metrics {
    uint32_t frames;
    uint32_t status_or;
    uint32_t max_build_cycles;
    uint32_t hardware_max_build_cycles;
    uint32_t max_read_bytes;
    uint32_t admitted;
    uint32_t dropped;
    uint32_t overflow;
    uint32_t axi_errors;
    uint32_t deadline_errors;
};

struct counter_snapshot {
    uint32_t frame;
    uint32_t admitted;
    uint32_t dropped;
    uint32_t overflow;
    uint32_t axi_errors;
    uint32_t deadline_errors;
};

struct sprite_shape {
    uint32_t offset;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
};

static int sleep_nanoseconds(long nanoseconds)
{
    struct timespec delay = { .tv_sec = 0, .tv_nsec = nanoseconds };

    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR) {
            perror("nanosleep");
            return -1;
        }
    }
    return 0;
}

static uint64_t deadline_after(uint64_t interval)
{
    uint64_t now = astra_monotonic_nanoseconds();

    return interval > UINT64_MAX - now ? UINT64_MAX : now + interval;
}

static int wait_for_sprite_write_ready(
    const struct astra_graphics_device *device, uint64_t timeout_ns)
{
    uint64_t deadline = deadline_after(timeout_ns);

    for (;;) {
        uint32_t status =
            astra_mmio_read(device, ASTRA_REG_SPRITE_STATUS);

        if ((status & ASTRA_SPRITE_STATUS_WRITE_READY) != 0u)
            return 0;
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "sprite scene store did not become writable: "
                    "status=%08" PRIx32 "\n",
                    status);
            return -1;
        }
        if (sleep_nanoseconds(1000000L) != 0)
            return -1;
    }
}

static uint8_t shape_pixel(unsigned sprite, unsigned x, unsigned y)
{
    unsigned dx = x > 63u ? x - 63u : 63u - x;
    unsigned dy = y > 63u ? y - 63u : 63u - y;
    unsigned radius = dx + dy;

    (void)sprite;
    if (radius > 59u)
        return 0u;
    return radius > 49u ? 2u : 1u;
}

static uint8_t variable_shape_pixel(unsigned scene, unsigned sprite,
                                    unsigned x, unsigned y,
                                    unsigned width, unsigned height)
{
    if (x == 0u || y == 0u || x + 1u == width || y + 1u == height)
        return 2u;
    return 1u + ((scene + sprite + x + y) & 1u);
}

static uint32_t prepare_variable_shapes(
    struct sprite_shape shapes[VARIABLE_SCENE_COUNT][SPRITE_COUNT])
{
    uint32_t next = MAX_SHAPE_STORAGE_BYTES;
    unsigned scene;

    for (scene = 0; scene < VARIABLE_SCENE_COUNT; ++scene) {
        unsigned sprite;

        for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
            struct sprite_shape *shape = &shapes[scene][sprite];
            unsigned width = sprite * 2u + scene + 1u;
            unsigned height = 128u - sprite * 2u - scene;
            unsigned pitch = width <= 64u ? 64u : 128u;
            uint32_t bytes = pitch * height;

            shape->offset = next;
            shape->width = (uint16_t)width;
            shape->height = (uint16_t)height;
            shape->pitch = (uint16_t)pitch;
            next += bytes;
        }
    }
    return next;
}

static void write_max_shapes(volatile uint8_t *storage)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        unsigned y;

        for (y = 0; y < SOURCE_HEIGHT; ++y) {
            unsigned x;

            for (x = 0; x < SOURCE_WIDTH; ++x) {
                size_t offset = (size_t)sprite * SHAPE_BYTES +
                    (size_t)y * SOURCE_PITCH + x;

                storage[offset] = shape_pixel(sprite, x, y);
            }
        }
    }
    astra_graphics_memory_barrier();
}

static int verify_max_shapes(volatile const uint8_t *storage)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        unsigned y;

        for (y = 0; y < SOURCE_HEIGHT; ++y) {
            unsigned x;

            for (x = 0; x < SOURCE_WIDTH; ++x) {
                size_t offset = (size_t)sprite * SHAPE_BYTES +
                    (size_t)y * SOURCE_PITCH + x;
                uint8_t expected = shape_pixel(sprite, x, y);
                uint8_t actual = storage[offset];

                if (actual != expected) {
                    fprintf(stderr,
                            "sprite shape readback failed: sprite=%u "
                            "x=%u y=%u expected=%02x actual=%02x\n",
                            sprite, x, y, expected, actual);
                    return -1;
                }
            }
        }
    }
    return 0;
}

static void write_variable_shapes(
    volatile uint8_t *storage,
    const struct sprite_shape shapes[VARIABLE_SCENE_COUNT][SPRITE_COUNT])
{
    unsigned scene;

    for (scene = 0; scene < VARIABLE_SCENE_COUNT; ++scene) {
        unsigned sprite;

        for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
            const struct sprite_shape *shape = &shapes[scene][sprite];
            unsigned y;

            for (y = 0; y < shape->height; ++y) {
                unsigned x;

                for (x = 0; x < shape->width; ++x) {
                    size_t offset = shape->offset +
                        (size_t)y * shape->pitch + x;

                    storage[offset] = variable_shape_pixel(
                        scene, sprite, x, y,
                        shape->width, shape->height);
                }
            }
        }
    }
    astra_graphics_memory_barrier();
}

static int verify_variable_shapes(
    volatile const uint8_t *storage,
    const struct sprite_shape shapes[VARIABLE_SCENE_COUNT][SPRITE_COUNT])
{
    unsigned scene;

    for (scene = 0; scene < VARIABLE_SCENE_COUNT; ++scene) {
        unsigned sprite;

        for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
            const struct sprite_shape *shape = &shapes[scene][sprite];
            unsigned y;

            for (y = 0; y < shape->height; ++y) {
                unsigned x;

                for (x = 0; x < shape->width; ++x) {
                    size_t offset = shape->offset +
                        (size_t)y * shape->pitch + x;
                    uint8_t expected = variable_shape_pixel(
                        scene, sprite, x, y,
                        shape->width, shape->height);
                    uint8_t actual = storage[offset];

                    if (actual != expected) {
                        fprintf(stderr,
                                "variable sprite readback failed: "
                                "scene=%u sprite=%u size=%ux%u "
                                "x=%u y=%u expected=%02x actual=%02x\n",
                                scene, sprite, shape->width, shape->height,
                                x, y, expected, actual);
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

static void write_palette_entry(
    const struct astra_graphics_device *device,
    unsigned bank, unsigned index, uint32_t argb)
{
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_SELECTOR,
                     (bank << 8) | index);
    astra_mmio_write(device, ASTRA_REG_SPRITE_PALETTE_DATA, argb);
}

static void program_palettes(const struct astra_graphics_device *device)
{
    unsigned bank;

    for (bank = 0; bank < PALETTE_BANK_COUNT; ++bank) {
        uint32_t complement = 0xff000000u |
            ((palette_color[bank] ^ 0x00ffffffu) & 0x00ffffffu);

        write_palette_entry(device, bank, 0u, 0x00000000u);
        write_palette_entry(device, bank, 1u, palette_color[bank]);
        write_palette_entry(device, bank, 2u, complement);
    }
}

static void encode_shape_descriptor(uint32_t words[8], unsigned sprite,
                                    int x, int y,
                                    unsigned destination_width,
                                    unsigned destination_height,
                                    bool reflected,
                                    const struct sprite_shape *shape)
{
    uint32_t flags = 0x00000013u;

    if (reflected)
        flags |= (sprite & 1u) != 0u ? 1u << 2 : 1u << 3;
    flags |= (sprite & 0xffu) << 8;
    flags |= (sprite % PALETTE_BANK_COUNT) << 16;

    words[0] = flags;
    words[1] = (uint32_t)(uint16_t)x |
        ((uint32_t)(uint16_t)y << 16);
    words[2] = shape->width | ((uint32_t)shape->height << 8) |
        (255u << 16);
    words[3] = destination_width | (destination_height << 16);
    words[4] = SHAPE_STORAGE_BASE + shape->offset;
    words[5] = shape->pitch;
    words[6] = 0u;
    words[7] = 0u;
}

static void encode_max_descriptor(uint32_t words[8], unsigned sprite,
                                  int x, int y,
                                  unsigned destination_width,
                                  unsigned destination_height,
                                  bool reflected)
{
    const struct sprite_shape shape = {
        .offset = sprite * SHAPE_BYTES,
        .width = SOURCE_WIDTH,
        .height = SOURCE_HEIGHT,
        .pitch = SOURCE_PITCH,
    };

    encode_shape_descriptor(words, sprite, x, y, destination_width,
                            destination_height, reflected, &shape);
}

static void write_descriptor(const struct astra_graphics_device *device,
                             unsigned sprite, const uint32_t words[8])
{
    unsigned word;

    for (word = 0; word < 8u; ++word) {
        astra_mmio_write(device, ASTRA_REG_SPRITE_DESCRIPTOR_SELECTOR,
                         (word << 8) | sprite);
        astra_mmio_write(device, ASTRA_REG_SPRITE_DESCRIPTOR_DATA,
                         words[word]);
    }
}

static void program_stress_scene(const struct astra_graphics_device *device)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];

        encode_max_descriptor(words, sprite, 576, 0, STRESS_DEST_WIDTH,
                              STRESS_DEST_HEIGHT, true);
        write_descriptor(device, sprite, words);
    }
}

static void program_hidden_scene(const struct astra_graphics_device *device)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];
        unsigned column = sprite & 7u;
        unsigned row = sprite >> 3;
        int x = 24 + (int)column * 160;
        int y = 9 + (int)row * 90;

        switch (sprite & 3u) {
        case 0u:
            x = -(int)GRID_DEST_WIDTH;
            break;
        case 1u:
            x = ASTRA_FRAMEBUFFER_WIDTH;
            break;
        case 2u:
            y = -(int)GRID_DEST_HEIGHT;
            break;
        default:
            y = ASTRA_FRAMEBUFFER_HEIGHT;
            break;
        }
        encode_max_descriptor(words, sprite, x, y, GRID_DEST_WIDTH,
                              GRID_DEST_HEIGHT, true);
        write_descriptor(device, sprite, words);
    }
}

static int quiesce_sprites(const struct astra_graphics_device *device,
                           uint32_t *generation)
{
    if (wait_for_sprite_write_ready(device, COMMIT_TIMEOUT_NS) != 0)
        return -1;
    program_hidden_scene(device);
    astra_mmio_write(device, ASTRA_REG_SPRITE_CONTROL, 1u);
    astra_mmio_write(device, ASTRA_REG_GLOBAL_CONTROL, 1u);
    return astra_graphics_scene_commit(device, COMMIT_TIMEOUT_NS,
                                       generation);
}

static void program_clip_scene(const struct astra_graphics_device *device)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];
        unsigned column = sprite & 7u;
        unsigned row = sprite >> 3;
        int x = 32 + (int)column * 153;
        int y = 16 + (int)row * 86;

        if (sprite == 0u)
            x = -24;
        if (sprite == SPRITE_COUNT - 1u) {
            x = 1210;
            y = 680;
        }
        encode_max_descriptor(words, sprite, x, y, CLIP_DEST_WIDTH,
                              CLIP_DEST_HEIGHT, true);
        write_descriptor(device, sprite, words);
    }
}

static void program_grid_scene(const struct astra_graphics_device *device)
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];
        unsigned column = sprite & 7u;
        unsigned row = sprite >> 3;
        int x = 24 + (int)column * 160;
        int y = 9 + (int)row * 90;

        encode_max_descriptor(words, sprite, x, y, GRID_DEST_WIDTH,
                              GRID_DEST_HEIGHT, true);
        write_descriptor(device, sprite, words);
    }
}

static uint32_t dimension_admitted_width_sum(
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    uint32_t total = 0u;
    unsigned sprite;

    for (sprite = SPRITE_COUNT - ASTRA_SPRITES_PER_LINE;
         sprite < SPRITE_COUNT; ++sprite)
        total += shapes[sprite].width;
    return total;
}

static uint32_t dimension_dropped_width_sum(
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    uint32_t total = 0u;
    unsigned sprite;

    for (sprite = 0u;
         sprite < SPRITE_COUNT - ASTRA_SPRITES_PER_LINE; ++sprite)
        total += shapes[sprite].width;
    return total;
}

static uint32_t dimension_admitted_read_sum(
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    uint32_t total = 0u;
    unsigned sprite;

    for (sprite = SPRITE_COUNT - ASTRA_SPRITES_PER_LINE;
         sprite < SPRITE_COUNT; ++sprite)
        total += (shapes[sprite].width + 7u) & ~7u;
    return total;
}

static uint32_t dimension_storage_sum(
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    uint32_t total = 0u;
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite)
        total += (uint32_t)shapes[sprite].pitch * shapes[sprite].height;
    return total;
}

static void program_dimension_scene(
    const struct astra_graphics_device *device,
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];

        encode_shape_descriptor(words, sprite, 0, 0,
                                shapes[sprite].width,
                                ASTRA_FRAMEBUFFER_HEIGHT, true,
                                &shapes[sprite]);
        write_descriptor(device, sprite, words);
    }
}

static void program_variable_grid_scene(
    const struct astra_graphics_device *device,
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    unsigned sprite;

    for (sprite = 0; sprite < SPRITE_COUNT; ++sprite) {
        uint32_t words[8];
        unsigned column = sprite & 7u;
        unsigned row = sprite >> 3;
        unsigned destination_width = shapes[sprite].width < 16u ?
            16u : shapes[sprite].width;
        unsigned destination_height = (shapes[sprite].height + 1u) / 2u;
        int x = 16 + (int)column * 160;
        int y = 8 + (int)row * 88;

        if (destination_height < 16u)
            destination_height = 16u;
        encode_shape_descriptor(words, sprite, x, y,
                                destination_width, destination_height,
                                true, &shapes[sprite]);
        write_descriptor(device, sprite, words);
    }
}

static void capture_counters(const struct astra_graphics_device *device,
                             struct counter_snapshot *snapshot)
{
    snapshot->frame =
        astra_mmio_read(device, ASTRA_REG_SPRITE_COLLISION_FRAME);
    snapshot->admitted =
        astra_mmio_read(device, ASTRA_REG_SPRITE_PIXELS_ADMITTED);
    snapshot->dropped =
        astra_mmio_read(device, ASTRA_REG_SPRITE_PIXELS_DROPPED);
    snapshot->overflow =
        astra_mmio_read(device, ASTRA_REG_SPRITE_OVERFLOW_COUNT);
    snapshot->axi_errors =
        astra_mmio_read(device, ASTRA_REG_SPRITE_AXI_ERRORS);
    snapshot->deadline_errors =
        astra_mmio_read(device, ASTRA_REG_SPRITE_DEADLINE_ERRORS);
}

static int monitor_phase(const struct astra_graphics_device *device,
                         unsigned required_frames,
                         const struct counter_snapshot *initial,
                         struct phase_metrics *metrics)
{
    uint64_t deadline = deadline_after(PHASE_TIMEOUT_NS);

    *metrics = (struct phase_metrics){0};
    for (;;) {
        uint32_t frame =
            astra_mmio_read(device, ASTRA_REG_SPRITE_COLLISION_FRAME);
        uint32_t status =
            astra_mmio_read(device, ASTRA_REG_SPRITE_STATUS);
        uint32_t build_cycles =
            astra_mmio_read(device, ASTRA_REG_SPRITE_BUILD_CYCLES);
        uint32_t read_bytes =
            astra_mmio_read(device, ASTRA_REG_SPRITE_READ_BYTES);
        uint32_t hardware_max_build_cycles = astra_mmio_read(
            device, ASTRA_REG_SPRITE_MAX_BUILD_CYCLES);

        metrics->frames = frame - initial->frame;
        metrics->status_or |= status;
        if (build_cycles > metrics->max_build_cycles)
            metrics->max_build_cycles = build_cycles;
        if (read_bytes > metrics->max_read_bytes)
            metrics->max_read_bytes = read_bytes;
        metrics->hardware_max_build_cycles = hardware_max_build_cycles;
        if (metrics->frames >= required_frames)
            break;
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "sprite frame monitor timed out: start=%" PRIu32
                    " current=%" PRIu32 " status=%08" PRIx32 "\n",
                    initial->frame, frame, status);
            return -1;
        }
        if (sleep_nanoseconds(50000L) != 0)
            return -1;
    }

    metrics->admitted =
        astra_mmio_read(device, ASTRA_REG_SPRITE_PIXELS_ADMITTED) -
        initial->admitted;
    metrics->dropped =
        astra_mmio_read(device, ASTRA_REG_SPRITE_PIXELS_DROPPED) -
        initial->dropped;
    metrics->overflow =
        astra_mmio_read(device, ASTRA_REG_SPRITE_OVERFLOW_COUNT) -
        initial->overflow;
    metrics->axi_errors =
        astra_mmio_read(device, ASTRA_REG_SPRITE_AXI_ERRORS) -
        initial->axi_errors;
    metrics->deadline_errors =
        astra_mmio_read(device, ASTRA_REG_SPRITE_DEADLINE_ERRORS) -
        initial->deadline_errors;
    return 0;
}

static int validate_stress_metrics(const struct phase_metrics *metrics)
{
    const uint32_t complete_frames = metrics->frames == 0u ?
        0u : metrics->frames - 1u;
    const uint32_t maximum_frames = metrics->frames + 1u;
    const uint32_t admitted_per_frame =
        ASTRA_SPRITE_PIXELS_PER_LINE * ASTRA_FRAMEBUFFER_HEIGHT;
    const uint32_t dropped_per_frame =
        (SPRITE_COUNT * STRESS_DEST_WIDTH -
         ASTRA_SPRITE_PIXELS_PER_LINE) * ASTRA_FRAMEBUFFER_HEIGHT;
    const uint32_t minimum_overflow =
        ASTRA_FRAMEBUFFER_HEIGHT * complete_frames;

    if ((metrics->status_or & ASTRA_SPRITE_STATUS_SLOT_VALID_MASK) == 0u ||
        (metrics->status_or & (ASTRA_SPRITE_STATUS_FETCH_ERROR |
                               ASTRA_SPRITE_STATUS_DEADLINE_ERROR)) != 0u ||
        metrics->max_build_cycles == 0u ||
        metrics->max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->hardware_max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->max_read_bytes != ASTRA_SPRITE_PIXELS_PER_LINE ||
        metrics->admitted < admitted_per_frame * complete_frames ||
        metrics->admitted > admitted_per_frame * maximum_frames ||
        metrics->dropped < dropped_per_frame * complete_frames ||
        metrics->dropped > dropped_per_frame * maximum_frames ||
        metrics->overflow < minimum_overflow ||
        metrics->overflow > ASTRA_FRAMEBUFFER_HEIGHT * maximum_frames ||
        metrics->axi_errors != 0u ||
        metrics->deadline_errors != 0u) {
        fprintf(stderr,
                "64-way sprite stress failed: frames=%" PRIu32
                " status=%08" PRIx32 " build=%" PRIu32
                " hardware_max=%" PRIu32 " read=%" PRIu32
                " admitted=%" PRIu32 " dropped=%" PRIu32
                " overflow=%" PRIu32 " axi_errors=%" PRIu32
                " deadline_errors=%" PRIu32 "\n",
                metrics->frames, metrics->status_or,
                metrics->max_build_cycles,
                metrics->hardware_max_build_cycles,
                metrics->max_read_bytes,
                metrics->admitted, metrics->dropped, metrics->overflow,
                metrics->axi_errors, metrics->deadline_errors);
        return -1;
    }
    return 0;
}

static int validate_grid_metrics(const struct phase_metrics *metrics)
{
    if ((metrics->status_or & ASTRA_SPRITE_STATUS_SLOT_VALID_MASK) == 0u ||
        (metrics->status_or & (ASTRA_SPRITE_STATUS_FETCH_ERROR |
                               ASTRA_SPRITE_STATUS_DEADLINE_ERROR)) != 0u ||
        metrics->max_build_cycles == 0u ||
        metrics->max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->hardware_max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->max_read_bytes < 7u * SOURCE_WIDTH ||
        metrics->admitted == 0u || metrics->dropped != 0u ||
        metrics->overflow != 0u || metrics->axi_errors != 0u ||
        metrics->deadline_errors != 0u) {
        fprintf(stderr,
                "visible sprite grid failed: frames=%" PRIu32
                " status=%08" PRIx32 " build=%" PRIu32
                " hardware_max=%" PRIu32 " read=%" PRIu32
                " admitted=%" PRIu32 " dropped=%" PRIu32
                " overflow=%" PRIu32 " axi_errors=%" PRIu32
                " deadline_errors=%" PRIu32 "\n",
                metrics->frames, metrics->status_or,
                metrics->max_build_cycles,
                metrics->hardware_max_build_cycles,
                metrics->max_read_bytes,
                metrics->admitted, metrics->dropped, metrics->overflow,
                metrics->axi_errors, metrics->deadline_errors);
        return -1;
    }
    return 0;
}

static int validate_dimension_metrics(
    const struct phase_metrics *metrics,
    const struct sprite_shape shapes[SPRITE_COUNT])
{
    const uint32_t admitted_width =
        dimension_admitted_width_sum(shapes);
    const uint32_t dropped_width =
        dimension_dropped_width_sum(shapes);
    const uint32_t expected_read = dimension_admitted_read_sum(shapes);
    const uint32_t complete_frames = metrics->frames == 0u ?
        0u : metrics->frames - 1u;
    const uint32_t maximum_frames = metrics->frames + 1u;
    /* Counter snapshots can bracket partial first and last frames. */
    const uint32_t admitted_per_frame = admitted_width *
        ASTRA_FRAMEBUFFER_HEIGHT;
    const uint32_t dropped_per_frame = dropped_width *
        ASTRA_FRAMEBUFFER_HEIGHT;
    const uint32_t minimum_admitted =
        admitted_per_frame * complete_frames;

    if ((metrics->status_or & ASTRA_SPRITE_STATUS_SLOT_VALID_MASK) == 0u ||
        (metrics->status_or & (ASTRA_SPRITE_STATUS_FETCH_ERROR |
                               ASTRA_SPRITE_STATUS_DEADLINE_ERROR)) != 0u ||
        metrics->max_build_cycles == 0u ||
        metrics->max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->hardware_max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->max_read_bytes != expected_read ||
        metrics->admitted < admitted_per_frame * complete_frames ||
        metrics->admitted > admitted_per_frame * maximum_frames ||
        metrics->dropped < dropped_per_frame * complete_frames ||
        metrics->dropped > dropped_per_frame * maximum_frames ||
        metrics->overflow < ASTRA_FRAMEBUFFER_HEIGHT * complete_frames ||
        metrics->overflow > ASTRA_FRAMEBUFFER_HEIGHT * maximum_frames ||
        metrics->axi_errors != 0u ||
        metrics->deadline_errors != 0u) {
        fprintf(stderr,
                "variable-dimension sprite stress failed: "
                "frames=%" PRIu32 " status=%08" PRIx32
                " build=%" PRIu32 " hardware_max=%" PRIu32
                " read=%" PRIu32 "/%" PRIu32
                " admitted=%" PRIu32 "/%" PRIu32
                " dropped=%" PRIu32 " overflow=%" PRIu32
                " axi_errors=%" PRIu32 " deadline_errors=%" PRIu32
                "\n",
                metrics->frames, metrics->status_or,
                metrics->max_build_cycles,
                metrics->hardware_max_build_cycles,
                metrics->max_read_bytes, expected_read,
                metrics->admitted, minimum_admitted,
                metrics->dropped, metrics->overflow,
                metrics->axi_errors, metrics->deadline_errors);
        return -1;
    }
    return 0;
}

static int validate_variable_grid_metrics(
    const struct phase_metrics *metrics)
{
    if ((metrics->status_or & ASTRA_SPRITE_STATUS_SLOT_VALID_MASK) == 0u ||
        (metrics->status_or & (ASTRA_SPRITE_STATUS_FETCH_ERROR |
                               ASTRA_SPRITE_STATUS_DEADLINE_ERROR)) != 0u ||
        metrics->max_build_cycles == 0u ||
        metrics->max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->hardware_max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->max_read_bytes == 0u || metrics->admitted == 0u ||
        metrics->dropped != 0u || metrics->overflow != 0u ||
        metrics->axi_errors != 0u || metrics->deadline_errors != 0u) {
        fprintf(stderr,
                "variable-dimension sprite grid failed: "
                "frames=%" PRIu32 " status=%08" PRIx32
                " build=%" PRIu32 " hardware_max=%" PRIu32
                " read=%" PRIu32 " admitted=%" PRIu32
                " dropped=%" PRIu32 " overflow=%" PRIu32
                " axi_errors=%" PRIu32 " deadline_errors=%" PRIu32
                "\n",
                metrics->frames, metrics->status_or,
                metrics->max_build_cycles,
                metrics->hardware_max_build_cycles,
                metrics->max_read_bytes, metrics->admitted,
                metrics->dropped, metrics->overflow,
                metrics->axi_errors, metrics->deadline_errors);
        return -1;
    }
    return 0;
}

static int validate_hidden_metrics(const struct phase_metrics *metrics)
{
    if ((metrics->status_or & ASTRA_SPRITE_STATUS_SLOT_VALID_MASK) == 0u ||
        (metrics->status_or & (ASTRA_SPRITE_STATUS_FETCH_ERROR |
                               ASTRA_SPRITE_STATUS_DEADLINE_ERROR)) != 0u ||
        metrics->max_build_cycles == 0u ||
        metrics->max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->hardware_max_build_cycles >= BUILD_CYCLE_LIMIT ||
        metrics->max_read_bytes != 0u || metrics->admitted != 0u ||
        metrics->dropped != 0u || metrics->overflow != 0u ||
        metrics->axi_errors != 0u || metrics->deadline_errors != 0u) {
        fprintf(stderr,
                "fully off-screen sprite scene failed: frames=%" PRIu32
                " status=%08" PRIx32 " build=%" PRIu32
                " hardware_max=%" PRIu32 " read=%" PRIu32
                " admitted=%" PRIu32 " dropped=%" PRIu32
                " overflow=%" PRIu32 " axi_errors=%" PRIu32
                " deadline_errors=%" PRIu32 "\n",
                metrics->frames, metrics->status_or,
                metrics->max_build_cycles,
                metrics->hardware_max_build_cycles,
                metrics->max_read_bytes,
                metrics->admitted, metrics->dropped, metrics->overflow,
                metrics->axi_errors, metrics->deadline_errors);
        return -1;
    }
    return 0;
}

static void print_phase(const char *name, uint32_t generation,
                        const struct phase_metrics *metrics)
{
    printf("ASTRA_SPRITE_%s PASS generation=%" PRIu32
           " frames=%" PRIu32 " status=%08" PRIx32
           " build_max=%" PRIu32 " hardware_max=%" PRIu32
           " read_max=%" PRIu32
           " admitted=%" PRIu32 " dropped=%" PRIu32
           " overflow=%" PRIu32 " axi_errors=%" PRIu32
           " deadline_errors=%" PRIu32 "\n",
           name, generation, metrics->frames, metrics->status_or,
           metrics->max_build_cycles, metrics->hardware_max_build_cycles,
           metrics->max_read_bytes,
           metrics->admitted, metrics->dropped, metrics->overflow,
           metrics->axi_errors, metrics->deadline_errors);
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct astra_graphics_memory_map shape_map;
    struct phase_metrics metrics;
    struct counter_snapshot counters;
    struct sprite_shape
        variable_shapes[VARIABLE_SCENE_COUNT][SPRITE_COUNT];
    uint32_t variable_end;
    uint32_t generation;
    uint32_t capabilities;
    unsigned scene;
    bool sprites_accessible = false;
    int result = EXIT_FAILURE;

    astra_graphics_device_init(&device);
    astra_graphics_memory_map_init(&shape_map);
    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
        goto done;
    capabilities = astra_mmio_read(&device, ASTRA_REG_CAPABILITIES);
    if ((capabilities & ASTRA_CAP_SPRITE_ENGINE) == 0u) {
        fprintf(stderr, "Astra sprite engine is not present\n");
        goto done;
    }
    if (astra_mmio_read(&device, ASTRA_REG_ARENA_BASE) !=
            ASTRA_GRAPHICS_ARENA_BASE ||
        astra_mmio_read(&device, ASTRA_REG_ARENA_LIMIT) !=
            ASTRA_GRAPHICS_ARENA_LIMIT) {
        fprintf(stderr, "graphics arena does not match the sprite contract\n");
        goto done;
    }
    sprites_accessible = true;
    if (quiesce_sprites(&device, &generation) != 0)
        goto done;
    variable_end = prepare_variable_shapes(variable_shapes);
    if (variable_end > SHAPE_STORAGE_BYTES) {
        fprintf(stderr,
                "variable sprite layouts exceed certification storage: "
                "end=%" PRIu32 " limit=%u\n",
                variable_end, SHAPE_STORAGE_BYTES);
        goto done;
    }
    if (astra_graphics_memory_map_open(&device, &shape_map,
                                       SHAPE_STORAGE_BASE,
                                       SHAPE_STORAGE_BYTES) != 0) {
        perror("map sprite shape storage");
        goto done;
    }

    write_max_shapes(shape_map.data);
    write_variable_shapes(shape_map.data, variable_shapes);
    if (verify_max_shapes(shape_map.data) != 0 ||
        verify_variable_shapes(shape_map.data, variable_shapes) != 0)
        goto done;
    printf("ASTRA_SPRITE_MAX_SHAPES PASS base=%08x bytes=%u "
           "shapes=%u geometry=%ux%u pitch=%u\n",
           SHAPE_STORAGE_BASE, MAX_SHAPE_STORAGE_BYTES, SPRITE_COUNT,
           SOURCE_WIDTH, SOURCE_HEIGHT, SOURCE_PITCH);
    printf("ASTRA_SPRITE_VARIABLE_SHAPES PASS base=%08x bytes=%" PRIu32
           " scenes=%u widths=1..128 heights=1..128 pitches=64,128\n",
           SHAPE_STORAGE_BASE + MAX_SHAPE_STORAGE_BYTES,
           variable_end - MAX_SHAPE_STORAGE_BYTES,
           VARIABLE_SCENE_COUNT);

    if (wait_for_sprite_write_ready(&device, COMMIT_TIMEOUT_NS) != 0)
        goto done;
    program_palettes(&device);
    program_stress_scene(&device);
    astra_mmio_write(&device, ASTRA_REG_SPRITE_CONTROL, 1u);
    astra_mmio_write(&device, ASTRA_REG_GLOBAL_CONTROL, 1u);
    if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                    &generation) != 0)
        goto done;
    capture_counters(&device, &counters);
    if (monitor_phase(&device, PHASE_FRAMES, &counters, &metrics) != 0 ||
        validate_stress_metrics(&metrics) != 0)
        goto done;
    print_phase("STRESS", generation, &metrics);

    if (wait_for_sprite_write_ready(&device, COMMIT_TIMEOUT_NS) != 0)
        goto done;
    program_hidden_scene(&device);
    if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                    &generation) != 0)
        goto done;
    capture_counters(&device, &counters);
    if (monitor_phase(&device, PHASE_FRAMES, &counters, &metrics) != 0 ||
        validate_hidden_metrics(&metrics) != 0)
        goto done;
    print_phase("HIDDEN", generation, &metrics);

    if (wait_for_sprite_write_ready(&device, COMMIT_TIMEOUT_NS) != 0)
        goto done;
    program_clip_scene(&device);
    if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                    &generation) != 0)
        goto done;
    capture_counters(&device, &counters);
    if (monitor_phase(&device, PHASE_FRAMES, &counters, &metrics) != 0 ||
        validate_grid_metrics(&metrics) != 0)
        goto done;
    print_phase("CLIP", generation, &metrics);

    if (wait_for_sprite_write_ready(&device, COMMIT_TIMEOUT_NS) != 0)
        goto done;
    program_grid_scene(&device);
    if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                    &generation) != 0)
        goto done;
    capture_counters(&device, &counters);
    if (monitor_phase(&device, PHASE_FRAMES, &counters, &metrics) != 0 ||
        validate_grid_metrics(&metrics) != 0)
        goto done;
    print_phase("GRID", generation, &metrics);

    for (scene = 0; scene < VARIABLE_SCENE_COUNT; ++scene) {
        if (wait_for_sprite_write_ready(&device,
                                        COMMIT_TIMEOUT_NS) != 0)
            goto done;
        program_dimension_scene(&device, variable_shapes[scene]);
        if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                        &generation) != 0)
            goto done;
        capture_counters(&device, &counters);
        if (monitor_phase(&device, PHASE_FRAMES,
                          &counters, &metrics) != 0 ||
            validate_dimension_metrics(&metrics,
                                       variable_shapes[scene]) != 0)
            goto done;
        print_phase(scene == 0u ? "DIMENSIONS_ODD" :
                                  "DIMENSIONS_EVEN",
                    generation, &metrics);
    }

    if (wait_for_sprite_write_ready(&device, COMMIT_TIMEOUT_NS) != 0)
        goto done;
    program_variable_grid_scene(
        &device, variable_shapes[VARIABLE_SCENE_COUNT - 1u]);
    if (astra_graphics_scene_commit(&device, COMMIT_TIMEOUT_NS,
                                    &generation) != 0)
        goto done;
    capture_counters(&device, &counters);
    if (monitor_phase(&device, PHASE_FRAMES, &counters, &metrics) != 0 ||
        validate_variable_grid_metrics(&metrics) != 0)
        goto done;
    print_phase("VARIABLE_GRID", generation, &metrics);

    printf("ASTRA_SPRITE_CERTIFICATION PASS sprites=%u "
           "max_shape_bytes=%u variable_live_shape_bytes=%" PRIu32
           " width_range=1..128 height_range=1..128\n",
           SPRITE_COUNT, SHAPE_BYTES,
           dimension_storage_sum(
               variable_shapes[VARIABLE_SCENE_COUNT - 1u]));
    result = EXIT_SUCCESS;

done:
    if (sprites_accessible && quiesce_sprites(&device, &generation) != 0) {
        fprintf(stderr, "failed to restore the quiescent sprite scene\n");
        result = EXIT_FAILURE;
    }
    astra_graphics_memory_map_close(&shape_map);
    astra_graphics_device_close(&device);
    return result;
}
