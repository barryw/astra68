// SPDX-License-Identifier: MIT
#ifndef ASTRA_BOOT_TEXT_H
#define ASTRA_BOOT_TEXT_H

#include "astra_graphics_hw.h"

#include <stddef.h>
#include <stdint.h>

enum astra_boot_text_color {
    ASTRA_BOOT_CYAN = 0,
    ASTRA_BOOT_AMBER = 1,
    ASTRA_BOOT_WHITE = 2,
    ASTRA_BOOT_RED = 3,
};

struct astra_boot_text_line {
    uint16_t cell[ASTRA_BOOT_TEXT_COLS];
};

void astra_boot_text_line_clear(struct astra_boot_text_line *line);
void astra_boot_text_line_raw(struct astra_boot_text_line *line,
                              const char *text,
                              enum astra_boot_text_color color);
void astra_boot_text_line_stage(struct astra_boot_text_line *line,
                                const char *label, const char *status,
                                enum astra_boot_text_color status_color);
int astra_boot_text_write_line(const struct astra_graphics_device *device,
                               unsigned row,
                               const struct astra_boot_text_line *line);
int astra_boot_text_write_all(
    const struct astra_graphics_device *device,
    const struct astra_boot_text_line line[ASTRA_BOOT_TEXT_ROWS]);
int astra_boot_text_commit(const struct astra_graphics_device *device,
                           int enable);

#endif
