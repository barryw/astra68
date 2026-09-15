// SPDX-License-Identifier: MIT

#include "astra_graphics_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CAPTURE_LOCK_TEST_PATH "/tmp/astra-display-capture-test.lock"

#ifndef EXPECT_ARENA_BASE
#define EXPECT_ARENA_BASE 0x18000000u
#define EXPECT_ARENA_LIMIT 0x20000000u
#define EXPECT_CONTROL_BASE 0x43c00000u
#endif

_Static_assert(ASTRA_GRAPHICS_ARENA_BASE == EXPECT_ARENA_BASE,
               "graphics arena base mismatch");
_Static_assert(ASTRA_GRAPHICS_ARENA_LIMIT == EXPECT_ARENA_LIMIT,
               "graphics arena limit mismatch");
_Static_assert(ASTRA_CONTROL_BASE == EXPECT_CONTROL_BASE,
               "graphics control base mismatch");
_Static_assert(ASTRA_REG_CAPTURE_DEVICE_ID == 0x5000u,
               "capture aperture must not overlap Copper memory");
_Static_assert(ASTRA_CAPTURE_FRAME_BYTES == 1920u * 1080u * 3u,
               "capture frame contract mismatch");

int main(void)
{
    uint32_t registers[ASTRA_CONTROL_BYTES / sizeof(uint32_t)];
    struct astra_graphics_device graphics = {
        .memory_fd = -1,
        .capture_lock_fd = -1,
        .registers = registers,
        .framebuffer = NULL,
    };
    uint8_t device[35];
    uint8_t readback[35];
    uint8_t source[35];
    struct astra_graphics_device capture_a;
    struct astra_graphics_device capture_b;

    for (size_t index = 0; index < sizeof(source); ++index)
        source[index] = (uint8_t)(index * 7u + 3u);
    (void)memset(device, 0, sizeof(device));
    astra_graphics_memory_fill(device + 1, 0xa5u, sizeof(device) - 2u);
    if (device[0] != 0u || device[sizeof(device) - 1u] != 0u)
        return 1;
    for (size_t index = 1; index + 1u < sizeof(device); ++index)
        if (device[index] != 0xa5u)
            return 1;
    astra_graphics_memory_copy_to(device + 1, source + 1,
                                  sizeof(device) - 2u);
    if (memcmp(device + 1, source + 1, sizeof(device) - 2u) != 0)
        return 1;
    (void)memset(readback, 0, sizeof(readback));
    astra_graphics_memory_copy_from(readback + 1, device + 1,
                                    sizeof(device) - 2u);
    if (readback[0] != 0u ||
        readback[sizeof(readback) - 1u] != 0u ||
        memcmp(readback + 1, source + 1, sizeof(readback) - 2u) != 0)
        return 1;
    (void)memset(registers, 0xa5, sizeof(registers));
    astra_graphics_scene_prepare_empty(&graphics);
    if (registers[ASTRA_REG_COPPER_CONTROL / 4u] != 0u ||
        registers[ASTRA_REG_COPPER_IRQ_PENDING / 4u] != 1u ||
        registers[ASTRA_REG_BACKDROP / 4u] != 0u ||
        registers[ASTRA_REG_FB_CONTROL / 4u] != 0u ||
        registers[ASTRA_REG_FB_WINDOW_SCENE_BYTES / 4u] != 0u ||
        registers[ASTRA_REG_TILE0_CONTROL / 4u] != 0u ||
        registers[ASTRA_REG_TILE1_CONTROL / 4u] != 0u ||
        registers[ASTRA_REG_SPRITE_CONTROL / 4u] != 0u ||
        registers[ASTRA_REG_GLOBAL_CONTROL / 4u] != 1u)
        return 1;
    (void)unlink(CAPTURE_LOCK_TEST_PATH);
    astra_graphics_device_init(&capture_a);
    astra_graphics_device_init(&capture_b);
    capture_a.memory_fd = open("/dev/null", O_RDWR);
    capture_b.memory_fd = open("/dev/null", O_RDWR);
    if (capture_a.memory_fd < 0 || capture_b.memory_fd < 0 ||
        astra_display_capture_claim(&capture_a) != 0 ||
        astra_display_capture_claim(&capture_b) == 0 ||
        (errno != EWOULDBLOCK && errno != EAGAIN))
        return 1;
    astra_display_capture_release(&capture_a);
    if (astra_display_capture_claim(&capture_b) != 0)
        return 1;
    astra_display_capture_release(&capture_b);
    (void)close(capture_a.memory_fd);
    (void)close(capture_b.memory_fd);
    (void)unlink(CAPTURE_LOCK_TEST_PATH);
    return 0;
}
