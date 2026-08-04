// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "astra_boot_text.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

enum {
    BOOT_TEXT_EXPECTED_GEOMETRY = 0x04242010u,
    BOOT_TEXT_EXPECTED_ORIGIN = 0x01f00108u,
    BOOT_TEXT_READY_MASK = 0x3u,
    BOOT_TEXT_COMMIT_TIMEOUT_NS = 1000000000u,
};

static uint16_t encode_cell(unsigned character,
                            enum astra_boot_text_color color)
{
    if (character < 0x20u || character > 0x7eu)
        character = '?';
    return (uint16_t)(((unsigned)color << 8) | character);
}

void astra_boot_text_line_clear(struct astra_boot_text_line *line)
{
    unsigned column;

    for (column = 0; column < ASTRA_BOOT_TEXT_COLS; ++column)
        line->cell[column] = encode_cell(' ', ASTRA_BOOT_CYAN);
}

void astra_boot_text_line_raw(struct astra_boot_text_line *line,
                              const char *text,
                              enum astra_boot_text_color color)
{
    size_t length;
    size_t column;

    astra_boot_text_line_clear(line);
    length = strlen(text);
    if (length > ASTRA_BOOT_TEXT_COLS)
        length = ASTRA_BOOT_TEXT_COLS;
    for (column = 0; column < length; ++column)
        line->cell[column] = encode_cell((unsigned char)text[column], color);
}

void astra_boot_text_line_stage(struct astra_boot_text_line *line,
                                const char *label, const char *status,
                                enum astra_boot_text_color status_color)
{
    size_t label_length = strlen(label);
    size_t status_length = strlen(status);
    size_t status_column;
    size_t column;

    astra_boot_text_line_clear(line);
    line->cell[0] = encode_cell('>', ASTRA_BOOT_AMBER);
    line->cell[1] = encode_cell(' ', ASTRA_BOOT_CYAN);

    if (label_length > ASTRA_BOOT_TEXT_COLS - 2u)
        label_length = ASTRA_BOOT_TEXT_COLS - 2u;
    for (column = 0; column < label_length; ++column)
        line->cell[column + 2u] =
            encode_cell((unsigned char)label[column], ASTRA_BOOT_CYAN);

    if (status_length > ASTRA_BOOT_TEXT_COLS - 2u)
        status_length = ASTRA_BOOT_TEXT_COLS - 2u;
    status_column = ASTRA_BOOT_TEXT_COLS - status_length;
    if (status_column < 2u + label_length + 1u)
        status_column = 2u + label_length + 1u;
    if (status_column > ASTRA_BOOT_TEXT_COLS)
        status_column = ASTRA_BOOT_TEXT_COLS;

    for (column = 2u + label_length; column < status_column; ++column)
        line->cell[column] = encode_cell('.', ASTRA_BOOT_CYAN);
    for (column = 0;
         column < status_length && status_column + column <
             ASTRA_BOOT_TEXT_COLS;
         ++column) {
        line->cell[status_column + column] =
            encode_cell((unsigned char)status[column], status_color);
    }
}

static int validate_geometry(const struct astra_graphics_device *device)
{
    uint32_t geometry =
        astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_GEOMETRY);
    uint32_t origin = astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_ORIGIN);

    if (geometry != BOOT_TEXT_EXPECTED_GEOMETRY ||
        origin != BOOT_TEXT_EXPECTED_ORIGIN) {
        fprintf(stderr,
                "boot text geometry mismatch: geometry=%08x origin=%08x\n",
                geometry, origin);
        return -1;
    }
    return 0;
}

int astra_boot_text_write_line(const struct astra_graphics_device *device,
                               unsigned row,
                               const struct astra_boot_text_line *line)
{
    unsigned column;

    if (row >= ASTRA_BOOT_TEXT_ROWS || validate_geometry(device) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_INDEX,
                     row * ASTRA_BOOT_TEXT_COLS);
    for (column = 0; column < ASTRA_BOOT_TEXT_COLS; ++column)
        astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_CELL,
                         line->cell[column]);
    return 0;
}

int astra_boot_text_write_all(
    const struct astra_graphics_device *device,
    const struct astra_boot_text_line line[ASTRA_BOOT_TEXT_ROWS])
{
    unsigned row;

    if (validate_geometry(device) != 0)
        return -1;
    astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_INDEX, 0u);
    for (row = 0; row < ASTRA_BOOT_TEXT_ROWS; ++row) {
        unsigned column;

        for (column = 0; column < ASTRA_BOOT_TEXT_COLS; ++column)
            astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_CELL,
                             line[row].cell[column]);
    }
    return 0;
}

int astra_boot_text_commit(const struct astra_graphics_device *device,
                           int enable)
{
    const struct timespec poll_delay = {
        .tv_sec = 0,
        .tv_nsec = 100000,
    };
    uint32_t generation =
        astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_GENERATION);
    uint64_t deadline = astra_monotonic_nanoseconds() +
                        BOOT_TEXT_COMMIT_TIMEOUT_NS;

    if ((astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_COMMIT) &
         BOOT_TEXT_READY_MASK) != BOOT_TEXT_READY_MASK) {
        fprintf(stderr, "boot text plane is busy\n");
        return -1;
    }
    astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_CONTROL,
                     enable ? 1u : 0u);
    astra_mmio_write(device, ASTRA_REG_BOOT_TEXT_COMMIT, 1u);

    for (;;) {
        uint32_t status =
            astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_COMMIT);
        uint32_t active_generation =
            astra_mmio_read(device, ASTRA_REG_BOOT_TEXT_GENERATION);

        if (active_generation != generation &&
            (status & BOOT_TEXT_READY_MASK) == BOOT_TEXT_READY_MASK &&
            ((status >> 2) & 1u) == (enable ? 1u : 0u))
            return 0;
        if (astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr,
                    "boot text commit timed out: status=%08x "
                    "generation=%u\n",
                    status, active_generation);
            return -1;
        }
        (void)nanosleep(&poll_delay, NULL);
    }
}
