// SPDX-License-Identifier: MIT
// Certify the Astra dual-bank copper and renderer dispatch boundary.

#define _POSIX_C_SOURCE 200809L

#include "astra_graphics_hw.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    COPPER_TIMEOUT_NS = 2000000000u,
    COPPER_IRQ_SOURCE = 0xcafeu,
    COPPER_LIST_COUNT = 8u,
    COPPER_FAULT_BAD_TARGET = 4u,
    COPPER_CAPTURE_LIST_COUNT = 9u,
    CAPTURE_STORAGE_BASE =
        ASTRA_GRAPHICS_ARENA_LIMIT - ASTRA_CAPTURE_FRAME_BYTES,
    RENDER_SUBMISSION_OFFSET = 0x00400000u,
    RENDER_COMPLETION_OFFSET = 0x00410000u,
};

struct scene_controls {
    uint32_t global;
    uint32_t backdrop;
    uint32_t framebuffer;
    uint32_t tile0;
    uint32_t tile1;
    uint32_t sprites;
};

_Static_assert(CAPTURE_STORAGE_BASE + ASTRA_CAPTURE_FRAME_BYTES <=
                   ASTRA_GRAPHICS_ARENA_LIMIT,
               "copper capture exceeds the graphics arena");

struct render_state {
    uint32_t control;
    uint32_t submission_offset;
    uint32_t submission_producer;
    uint32_t completion_offset;
    uint32_t completion_consumer;
    uint32_t resource_generation;
};

static uint32_t instruction0(unsigned opcode, uint16_t argument)
{
    return ((uint32_t)opcode << 29) | argument;
}

static uint32_t beam_instruction0(unsigned opcode, uint16_t y)
{
    return ((uint32_t)opcode << 29) | y;
}

static void save_render_state(const struct astra_graphics_device *device,
                              struct render_state *state)
{
    state->control = astra_mmio_read(device, ASTRA_REG_RENDER_CONTROL);
    state->submission_offset = astra_mmio_read(
        device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET);
    state->submission_producer = astra_mmio_read(
        device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER);
    state->completion_offset = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET);
    state->completion_consumer = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMPLETION_CONSUMER);
    state->resource_generation = astra_mmio_read(
        device, ASTRA_REG_RENDER_RESOURCE_GENERATION);
}

static int configure_empty_renderer(
    const struct astra_graphics_device *device,
    const struct render_state *saved)
{
    uint32_t submission_consumer = astra_mmio_read(
        device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER);
    uint32_t completion_producer = astra_mmio_read(
        device, ASTRA_REG_RENDER_COMPLETION_PRODUCER);
    uint32_t status = astra_mmio_read(device, ASTRA_REG_RENDER_STATUS);

    if ((status & ASTRA_RENDER_ENGINE_BUSY) != 0u ||
        saved->submission_producer != submission_consumer ||
        completion_producer != saved->completion_consumer) {
        fprintf(stderr, "renderer must be idle with empty rings\n");
        return -1;
    }
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET,
                     RENDER_SUBMISSION_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET,
                     RENDER_COMPLETION_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_CONSUMER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_RESOURCE_GENERATION, 1u);
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_ENABLE |
                     ASTRA_RENDER_CONTROL_REBASE);
    return 0;
}

static void restore_renderer(const struct astra_graphics_device *device,
                             const struct render_state *saved)
{
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET,
                     saved->submission_offset);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     saved->submission_producer);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET,
                     saved->completion_offset);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_CONSUMER,
                     saved->completion_consumer);
    astra_mmio_write(device, ASTRA_REG_RENDER_RESOURCE_GENERATION,
                     saved->resource_generation);
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     (saved->control & ASTRA_RENDER_CONTROL_ENABLE) |
                     ASTRA_RENDER_CONTROL_REBASE);
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     saved->control & ASTRA_RENDER_CONTROL_ENABLE);
}

static int validate_list(const struct astra_graphics_device *device,
                         unsigned count, bool expect_valid)
{
    uint32_t value = 0u;

    astra_mmio_write(device, ASTRA_REG_COPPER_VALIDATE_RANGE,
                     (uint32_t)count << 16);
    astra_mmio_write(device, ASTRA_REG_COPPER_VALIDATE_START, 1u);
    if (expect_valid) {
        if (astra_graphics_wait_register_mask(
                device, ASTRA_REG_COPPER_STATUS,
                ASTRA_COPPER_STATUS_VALIDATE_VALID,
                ASTRA_COPPER_STATUS_VALIDATE_VALID,
                COPPER_TIMEOUT_NS, &value) != 0)
            return -1;
    } else {
        uint64_t deadline = astra_monotonic_nanoseconds() +
                            COPPER_TIMEOUT_NS;

        do {
            value = astra_mmio_read(
                device, ASTRA_REG_COPPER_VALIDATE_STATUS);
            if (((value >> 8) & 0xffu) != 0u)
                break;
        } while (astra_monotonic_nanoseconds() < deadline);
        if (((value >> 8) & 0xffu) != COPPER_FAULT_BAD_TARGET) {
            fprintf(stderr, "bad target validation mismatch: %08" PRIx32
                    "\n", value);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct render_state saved;
    struct scene_controls scene;
    const char *capture_path = NULL;
    uint32_t status;
    uint32_t retired_before;
    int result = EXIT_FAILURE;
    bool renderer_changed = false;
    bool scene_changed = false;

    if (argc == 3 && strcmp(argv[1], "--capture") == 0)
        capture_path = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--capture OUTPUT.rgb]\n", argv[0]);
        return EXIT_FAILURE;
    }

    astra_graphics_device_init(&device);
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, false) != 0)
        goto done;
    if (capture_path != NULL && astra_display_capture_claim(&device) != 0)
        goto done;
    if ((astra_mmio_read(&device, ASTRA_REG_CAPABILITIES) &
         ASTRA_CAP_COPPER) == 0u ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_DEVICE_ID) !=
            ASTRA_COPPER_DEVICE_ID ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_VERSION) !=
            ASTRA_COPPER_VERSION) {
        fprintf(stderr, "Astra copper identity is not present\n");
        goto done;
    }

    scene.global = astra_mmio_read(&device, ASTRA_REG_GLOBAL_CONTROL);
    scene.backdrop = astra_mmio_read(&device, ASTRA_REG_BACKDROP);
    scene.framebuffer = astra_mmio_read(&device, ASTRA_REG_FB_CONTROL);
    scene.tile0 = astra_mmio_read(&device, ASTRA_REG_TILE0_CONTROL);
    scene.tile1 = astra_mmio_read(&device, ASTRA_REG_TILE1_CONTROL);
    scene.sprites = astra_mmio_read(&device, ASTRA_REG_SPRITE_CONTROL);

    save_render_state(&device, &saved);
    if (configure_empty_renderer(&device, &saved) != 0)
        goto done;
    renderer_changed = true;

    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL,
                     ASTRA_COPPER_CLEAR_FAULT);
    astra_mmio_write(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    astra_mmio_write(&device, ASTRA_REG_COPPER_DISPATCH_SELECTOR, 3u);
    astra_mmio_write(&device, ASTRA_REG_COPPER_DISPATCH_ENDPOINT,
                     0x80000000u);

    astra_graphics_copper_write_instruction(
        &device, 0u, beam_instruction0(ASTRA_COPPER_OP_WAIT, 1u), 0u);
    astra_graphics_copper_write_instruction(
        &device, 1u, beam_instruction0(ASTRA_COPPER_OP_SKIP, 1u), 0u);
    astra_graphics_copper_write_instruction(&device, 2u,
                      instruction0(ASTRA_COPPER_OP_MOVE, ASTRA_REG_BACKDROP),
                      0x00010203u);
    astra_graphics_copper_write_instruction(&device, 3u,
                      instruction0(ASTRA_COPPER_OP_MOVE, ASTRA_REG_BACKDROP),
                      0x00040506u);
    astra_graphics_copper_write_instruction(
        &device, 4u, instruction0(ASTRA_COPPER_OP_DISPATCH, 3u), 0u);
    astra_graphics_copper_write_instruction(
        &device, 5u,
        instruction0(ASTRA_COPPER_OP_IRQ, COPPER_IRQ_SOURCE), 0u);
    astra_graphics_copper_write_instruction(
        &device, 6u, instruction0(ASTRA_COPPER_OP_JUMP, 7u), 0u);
    astra_graphics_copper_write_instruction(
        &device, 7u, instruction0(ASTRA_COPPER_OP_END, 0u), 0u);
    if (validate_list(&device, COPPER_LIST_COUNT, true) != 0)
        goto cleanup;

    retired_before = astra_mmio_read(&device, ASTRA_REG_COPPER_RETIRED);
    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL,
                     ASTRA_COPPER_ENABLE | ASTRA_COPPER_PROMOTE);
    if (astra_graphics_wait_register_mask(
            &device, ASTRA_REG_COPPER_IRQ_PENDING, 1u, 1u,
            COPPER_TIMEOUT_NS, NULL) != 0)
        goto cleanup;
    status = astra_mmio_read(&device, ASTRA_REG_COPPER_STATUS);
    if ((status & ASTRA_COPPER_STATUS_FAULT) != 0u ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_IRQ_SOURCES) !=
            COPPER_IRQ_SOURCE ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_RETIRED) <=
            retired_before ||
        astra_mmio_read(&device,
            ASTRA_REG_RENDER_SUBMISSION_PRODUCER) != 0u) {
        fprintf(stderr,
                "copper runtime failed status=%08" PRIx32
                " fault=%08" PRIx32 " irq=%08" PRIx32
                " retired=%08" PRIx32 "\n",
                status,
                astra_mmio_read(&device, ASTRA_REG_COPPER_FAULT),
                astra_mmio_read(&device, ASTRA_REG_COPPER_IRQ_SOURCES),
                astra_mmio_read(&device, ASTRA_REG_COPPER_RETIRED));
        goto cleanup;
    }

    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL, 0u);
    astra_mmio_write(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    if (capture_path != NULL) {
        struct astra_display_capture_result capture;
        uint32_t generation;

        astra_mmio_write(&device, ASTRA_REG_FB_CONTROL, 0u);
        astra_mmio_write(&device, ASTRA_REG_TILE0_CONTROL, 0u);
        astra_mmio_write(&device, ASTRA_REG_TILE1_CONTROL, 0u);
        astra_mmio_write(&device, ASTRA_REG_SPRITE_CONTROL, 0u);
        astra_mmio_write(&device, ASTRA_REG_GLOBAL_CONTROL, 1u);
        if (astra_graphics_scene_commit(&device, COPPER_TIMEOUT_NS,
                                        &generation) != 0)
            goto cleanup;
        scene_changed = true;

        astra_graphics_copper_write_instruction(
            &device, 0u,
                          instruction0(ASTRA_COPPER_OP_MOVE,
                                       ASTRA_REG_BACKDROP),
                          0x0011273bu);
        astra_graphics_copper_write_instruction(
            &device, 1u,
            beam_instruction0(ASTRA_COPPER_OP_WAIT, 270u), 0u);
        astra_graphics_copper_write_instruction(
            &device, 2u,
                          instruction0(ASTRA_COPPER_OP_MOVE,
                                       ASTRA_REG_BACKDROP),
                          0x001b5060u);
        astra_graphics_copper_write_instruction(
            &device, 3u,
            beam_instruction0(ASTRA_COPPER_OP_WAIT, 540u), 0u);
        astra_graphics_copper_write_instruction(
            &device, 4u,
                          instruction0(ASTRA_COPPER_OP_MOVE,
                                       ASTRA_REG_BACKDROP),
                          0x00513a6bu);
        astra_graphics_copper_write_instruction(
            &device, 5u,
            beam_instruction0(ASTRA_COPPER_OP_WAIT, 810u), 0u);
        astra_graphics_copper_write_instruction(
            &device, 6u,
                          instruction0(ASTRA_COPPER_OP_MOVE,
                                       ASTRA_REG_BACKDROP),
                          0x00722f4cu);
        astra_graphics_copper_write_instruction(
            &device, 7u,
            instruction0(ASTRA_COPPER_OP_IRQ, COPPER_IRQ_SOURCE), 0u);
        astra_graphics_copper_write_instruction(
            &device, 8u, instruction0(ASTRA_COPPER_OP_END, 0u), 0u);
        if (validate_list(&device, COPPER_CAPTURE_LIST_COUNT, true) != 0)
            goto cleanup;
        astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL,
                         ASTRA_COPPER_ENABLE | ASTRA_COPPER_PROMOTE);
        if (astra_graphics_wait_register_mask(
                &device, ASTRA_REG_COPPER_IRQ_PENDING, 1u, 1u,
                COPPER_TIMEOUT_NS, NULL) != 0)
            goto cleanup;
        if (astra_graphics_capture_rgb(&device, CAPTURE_STORAGE_BASE,
                                       capture_path, &capture) != 0) {
            perror("capture copper bands");
            goto cleanup;
        }
        printf("ASTRA_COPPER_CAPTURE generation=%" PRIu32
               " cycles=%" PRIu32 " elapsed_ns=%" PRIu64 "\n",
               capture.generation, capture.cycles, capture.elapsed_ns);
        astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL, 0u);
        astra_mmio_write(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    }
    astra_graphics_copper_write_instruction(
        &device, 0u,
        instruction0(ASTRA_COPPER_OP_MOVE, 0xfffcu), 0u);
    astra_graphics_copper_write_instruction(
        &device, 1u, instruction0(ASTRA_COPPER_OP_END, 0u), 0u);
    if (validate_list(&device, 2u, false) != 0)
        goto cleanup;

    printf("ASTRA_COPPER PASS banks=2 instructions=%u irq=%04x "
           "dispatch_endpoint=0 invalid_target=contained\n",
           COPPER_LIST_COUNT, COPPER_IRQ_SOURCE);
    result = EXIT_SUCCESS;

cleanup:
    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL, 0u);
    astra_mmio_write(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    if (scene_changed) {
        uint32_t generation;

        astra_mmio_write(&device, ASTRA_REG_BACKDROP, scene.backdrop);
        astra_mmio_write(&device, ASTRA_REG_FB_CONTROL, scene.framebuffer);
        astra_mmio_write(&device, ASTRA_REG_TILE0_CONTROL, scene.tile0);
        astra_mmio_write(&device, ASTRA_REG_TILE1_CONTROL, scene.tile1);
        astra_mmio_write(&device, ASTRA_REG_SPRITE_CONTROL, scene.sprites);
        astra_mmio_write(&device, ASTRA_REG_GLOBAL_CONTROL, scene.global);
        if (astra_graphics_scene_commit(&device, COPPER_TIMEOUT_NS,
                                        &generation) != 0)
            result = EXIT_FAILURE;
    }
    if (renderer_changed)
        restore_renderer(&device, &saved);
done:
    astra_graphics_device_close(&device);
    return result;
}
