// SPDX-License-Identifier: MIT

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_row(const char *text, unsigned *row_out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value >= ASTRA_BOOT_TEXT_ROWS || value > UINT_MAX)
        return -1;
    *row_out = (unsigned)value;
    return 0;
}

static int parse_color(const char *text,
                       enum astra_boot_text_color *color_out)
{
    if (strcmp(text, "cyan") == 0)
        *color_out = ASTRA_BOOT_CYAN;
    else if (strcmp(text, "amber") == 0)
        *color_out = ASTRA_BOOT_AMBER;
    else if (strcmp(text, "white") == 0)
        *color_out = ASTRA_BOOT_WHITE;
    else if (strcmp(text, "red") == 0)
        *color_out = ASTRA_BOOT_RED;
    else
        return -1;
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s line <row> <cyan|amber|white|red> <text>\n"
            "  %s stage <row> <label> <status> "
            "<cyan|amber|white|red>\n",
            program, program);
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct astra_boot_text_line line;
    enum astra_boot_text_color color;
    unsigned row;
    int result = EXIT_FAILURE;

    if (argc < 2 ||
        ((strcmp(argv[1], "line") == 0 && argc != 5) ||
         (strcmp(argv[1], "stage") == 0 && argc != 6)) ||
        (strcmp(argv[1], "line") != 0 &&
         strcmp(argv[1], "stage") != 0)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_row(argv[2], &row) != 0) {
        fprintf(stderr, "invalid boot text row: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "line") == 0) {
        if (parse_color(argv[3], &color) != 0) {
            fprintf(stderr, "invalid boot text color: %s\n", argv[3]);
            return EXIT_FAILURE;
        }
        astra_boot_text_line_raw(&line, argv[4], color);
    } else {
        if (parse_color(argv[5], &color) != 0) {
            fprintf(stderr, "invalid boot text color: %s\n", argv[5]);
            return EXIT_FAILURE;
        }
        astra_boot_text_line_stage(&line, argv[3], argv[4], color);
    }

    if (astra_graphics_device_open(&device, false) != 0)
        return EXIT_FAILURE;
    if (astra_graphics_device_validate(&device, true) != 0 ||
        astra_boot_text_write_line(&device, row, &line) != 0 ||
        astra_boot_text_commit(&device, 1) != 0)
        goto done;

    printf("ASTRA_BOOT_STATUS PASS row=%u generation=%u\n", row,
           astra_mmio_read(&device, ASTRA_REG_BOOT_TEXT_GENERATION));
    result = EXIT_SUCCESS;

done:
    astra_graphics_device_close(&device);
    return result;
}
