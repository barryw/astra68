// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"

#include <astra/display.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
};

#include "astra_terminal_font.inc"

_Static_assert(sizeof(terminal_font) == FONT_GLYPHS * FONT_ROWS,
               "terminal font must contain 256 8x8 glyphs");
_Static_assert(TEXT_PAGE_BYTES == ASTRA_TEXT_PLANE_BYTES,
               "guest and renderer text pages differ");

static volatile sig_atomic_t running = 1;

static void stop(int signal_number)
{
    (void)signal_number;
    running = 0;
}

struct terminal_cursor {
    uint32_t cell;
    bool visible;
};

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

static int present(const struct astra_graphics_device *device)
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
    astra_mmio_write(device, ASTRA_REG_FB_BASE, ASTRA_FRAMEBUFFER_BASE);
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
    uint8_t current[TEXT_CELLS];
    uint8_t previous[TEXT_CELLS];
    struct stat plane_stat;
    unsigned blink_polls = 0u;
    bool cursor_on = true;
    int plane_fd = -1;
    int result = EXIT_FAILURE;
    uint32_t cell;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc != 2) {
        fprintf(stderr, "usage: %s <shared-post-text-page>\n", argv[0]);
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
    if (astra_graphics_device_open(&device, true) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
        goto done;

    (void)memset((void *)device.framebuffer, 0, ASTRA_FRAMEBUFFER_BYTES);
    copy_cells(current, plane);
    (void)copy_cursor(&cursor, plane);
    for (cell = 0; cell < TEXT_CELLS; ++cell)
        draw_cell(device.framebuffer, ASTRA_FRAMEBUFFER_PITCH,
                  cell / TEXT_COLUMNS, cell % TEXT_COLUMNS, current[cell],
                  cursor.visible && cursor.cell == cell);
    (void)memcpy(previous, current, sizeof(previous));
    previous_cursor = cursor;
    astra_graphics_memory_barrier();
    if (present(&device) != 0 || astra_boot_text_commit(&device, 0) != 0)
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
        struct terminal_cursor sampled;
        bool blink_changed = false;
        bool cursor_changed;

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
    if (plane_fd >= 0)
        (void)close(plane_fd);
    return result;
}
