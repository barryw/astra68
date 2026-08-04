// SPDX-License-Identifier: MIT
// Certify the Astra dual-bank copper and renderer dispatch boundary.

#define _POSIX_C_SOURCE 200809L

#include "astra_graphics_hw.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    COPPER_DEVICE_ID = 0x434f5052u,
    COPPER_VERSION = 0x00010001u,
    COPPER_TIMEOUT_NS = 2000000000u,
    COPPER_IRQ_SOURCE = 0xcafeu,
    COPPER_LIST_COUNT = 8u,
    COPPER_FAULT_BAD_TARGET = 4u,
    RENDER_SUBMISSION_OFFSET = 0x00400000u,
    RENDER_COMPLETION_OFFSET = 0x00410000u,
    OP_END = 0u,
    OP_MOVE = 1u,
    OP_WAIT = 2u,
    OP_SKIP = 3u,
    OP_IRQ = 4u,
    OP_JUMP = 5u,
    OP_DISPATCH = 6u,
};

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

static void write_instruction(const struct astra_graphics_device *device,
                              unsigned index, uint32_t word0,
                              uint32_t word1)
{
    unsigned offset = ASTRA_REG_COPPER_PROGRAM + index * 8u;

    astra_mmio_write(device, offset, word0);
    astra_mmio_write(device, offset + 4u, word1);
}

static int wait_for_mask(const struct astra_graphics_device *device,
                         unsigned offset, uint32_t mask,
                         uint32_t expected, uint32_t *value_out)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };
    uint64_t deadline = astra_monotonic_nanoseconds() + COPPER_TIMEOUT_NS;

    for (;;) {
        uint32_t value = astra_mmio_read(device, offset);

        if ((value & mask) == expected) {
            if (value_out != NULL)
                *value_out = value;
            return 0;
        }
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "copper timeout offset=%04x value=%08" PRIx32
                    " mask=%08" PRIx32 " expected=%08" PRIx32 "\n",
                    offset, value, mask, expected);
            return -1;
        }
        (void)nanosleep(&delay, NULL);
    }
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
        if (wait_for_mask(device, ASTRA_REG_COPPER_STATUS,
                          ASTRA_COPPER_STATUS_VALIDATE_VALID,
                          ASTRA_COPPER_STATUS_VALIDATE_VALID,
                          &value) != 0)
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

int main(void)
{
    struct astra_graphics_device device;
    struct render_state saved;
    uint32_t status;
    uint32_t retired_before;
    int result = EXIT_FAILURE;
    bool renderer_changed = false;

    astra_graphics_device_init(&device);
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, false) != 0)
        goto done;
    if ((astra_mmio_read(&device, ASTRA_REG_CAPABILITIES) &
         ASTRA_CAP_COPPER) == 0u ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_DEVICE_ID) !=
            COPPER_DEVICE_ID ||
        astra_mmio_read(&device, ASTRA_REG_COPPER_VERSION) !=
            COPPER_VERSION) {
        fprintf(stderr, "Astra copper identity is not present\n");
        goto done;
    }

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

    write_instruction(&device, 0u, beam_instruction0(OP_WAIT, 1u), 0u);
    write_instruction(&device, 1u, beam_instruction0(OP_SKIP, 1u), 0u);
    write_instruction(&device, 2u,
                      instruction0(OP_MOVE, ASTRA_REG_BACKDROP),
                      0x00010203u);
    write_instruction(&device, 3u,
                      instruction0(OP_MOVE, ASTRA_REG_BACKDROP),
                      0x00040506u);
    write_instruction(&device, 4u, instruction0(OP_DISPATCH, 3u), 0u);
    write_instruction(&device, 5u,
                      instruction0(OP_IRQ, COPPER_IRQ_SOURCE), 0u);
    write_instruction(&device, 6u, instruction0(OP_JUMP, 7u), 0u);
    write_instruction(&device, 7u, instruction0(OP_END, 0u), 0u);
    if (validate_list(&device, COPPER_LIST_COUNT, true) != 0)
        goto cleanup;

    retired_before = astra_mmio_read(&device, ASTRA_REG_COPPER_RETIRED);
    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL,
                     ASTRA_COPPER_ENABLE | ASTRA_COPPER_PROMOTE);
    if (wait_for_mask(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u, 1u,
                      NULL) != 0)
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
    write_instruction(&device, 0u, instruction0(OP_MOVE, 0xfffcu), 0u);
    write_instruction(&device, 1u, instruction0(OP_END, 0u), 0u);
    if (validate_list(&device, 2u, false) != 0)
        goto cleanup;

    printf("ASTRA_COPPER PASS banks=2 instructions=%u irq=%04x "
           "dispatch_endpoint=0 invalid_target=contained\n",
           COPPER_LIST_COUNT, COPPER_IRQ_SOURCE);
    result = EXIT_SUCCESS;

cleanup:
    astra_mmio_write(&device, ASTRA_REG_COPPER_CONTROL, 0u);
    astra_mmio_write(&device, ASTRA_REG_COPPER_IRQ_PENDING, 1u);
    if (renderer_changed)
        restore_renderer(&device, &saved);
done:
    astra_graphics_device_close(&device);
    return result;
}
