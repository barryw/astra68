// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "astra_graphics_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

enum {
    AUDIO_BASE = ASTRA_CONTROL_BASE + 0x6000u,
    AUDIO_BYTES = 0x1000u,
    AUDIO_ID = 0x41554430u,
    AUDIO_VERSION = 0x00010000u,
    AUDIO_RATE = 48000u,
    AUDIO_FRAMES = 512u,
    REG_ID = 0x00u,
    REG_VERSION = 0x04u,
    REG_CONTROL = 0x0cu,
    REG_STATUS = 0x10u,
    REG_LEFT = 0x14u,
    REG_RIGHT = 0x18u,
    REG_UNDERRUNS = 0x1cu,
    REG_OVERFLOWS = 0x20u,
    REG_RATE = 0x24u,
    REG_FRAMES = 0x28u,
    CONTROL_ENABLE = 1u,
    CONTROL_DRAIN = 2u,
    STATUS_LEVEL_MASK = 0x3ffu,
    PREFILL_FRAMES = 384u,
    SILENCE_FRAMES = 64u,
};

static uint32_t read_reg(volatile uint32_t *registers, unsigned offset)
{
    return registers[offset / 4u];
}

static void write_reg(volatile uint32_t *registers, unsigned offset,
                      uint32_t value)
{
    registers[offset / 4u] = value;
}

static int32_t triangle(uint32_t phase)
{
    uint32_t quadrant = phase >> 30;
    uint32_t fraction = (phase >> 7) & UINT32_C(0x7fffff);

    switch (quadrant) {
    case 0u:
        return (int32_t)fraction;
    case 1u:
        return (int32_t)(UINT32_C(0x7fffff) - fraction);
    case 2u:
        return -(int32_t)fraction;
    default:
        return -(int32_t)(UINT32_C(0x7fffff) - fraction);
    }
}

static int self_test(void)
{
    return triangle(0u) == 0 &&
           triangle(UINT32_C(0x40000000)) == INT32_C(0x7fffff) &&
           triangle(UINT32_C(0x80000000)) == 0 &&
           triangle(UINT32_C(0xc0000000)) == -INT32_C(0x7fffff) ?
           EXIT_SUCCESS : EXIT_FAILURE;
}

static int wait_below(volatile uint32_t *registers, uint32_t level,
                      uint32_t attempts)
{
    const struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };

    while ((read_reg(registers, REG_STATUS) & STATUS_LEVEL_MASK) >= level) {
        if (attempts-- == 0u)
            return -1;
        while (nanosleep(&delay, NULL) != 0 && errno == EINTR) {
        }
    }
    return 0;
}

static void submit(volatile uint32_t *registers, int32_t left, int32_t right)
{
    write_reg(registers, REG_LEFT, (uint32_t)left & UINT32_C(0x00ffffff));
    write_reg(registers, REG_RIGHT, (uint32_t)right & UINT32_C(0x00ffffff));
}

int main(int argc, char **argv)
{
    volatile uint32_t *registers;
    uint32_t underruns;
    uint32_t overflows;
    uint32_t phase = 0u;
    uint32_t phase_step = (uint32_t)(
        (UINT64_C(440) << 32) / AUDIO_RATE);
    uint32_t submitted = 0u;
    int memory_fd;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test]\n", argv[0]);
        return EXIT_FAILURE;
    }
    memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) {
        perror("open /dev/mem");
        return EXIT_FAILURE;
    }
    registers = mmap(NULL, AUDIO_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                     memory_fd, AUDIO_BASE);
    if (registers == MAP_FAILED) {
        perror("mmap audio");
        (void)close(memory_fd);
        return EXIT_FAILURE;
    }
    if (read_reg(registers, REG_ID) != AUDIO_ID ||
        read_reg(registers, REG_VERSION) != AUDIO_VERSION ||
        read_reg(registers, REG_RATE) != AUDIO_RATE ||
        read_reg(registers, REG_FRAMES) != AUDIO_FRAMES) {
        fprintf(stderr, "Astra HDMI audio device mismatch\n");
        goto fail;
    }

    write_reg(registers, REG_CONTROL, CONTROL_DRAIN);
    if (wait_below(registers, 1u, 100u) != 0) {
        fprintf(stderr, "audio FIFO would not drain\n");
        goto fail;
    }
    write_reg(registers, REG_CONTROL, 0u);
    underruns = read_reg(registers, REG_UNDERRUNS);
    overflows = read_reg(registers, REG_OVERFLOWS);

    while (submitted < AUDIO_RATE + SILENCE_FRAMES) {
        uint32_t level = read_reg(registers, REG_STATUS) & STATUS_LEVEL_MASK;
        uint32_t room = AUDIO_FRAMES - level;

        if (room == 0u) {
            if (wait_below(registers, AUDIO_FRAMES, 100u) != 0)
                goto fail;
            continue;
        }
        while (room-- != 0u && submitted < AUDIO_RATE + SILENCE_FRAMES) {
            int32_t sample = submitted < AUDIO_RATE ? triangle(phase) : 0;

            submit(registers, sample, sample);
            phase += phase_step;
            ++submitted;
            if (submitted == PREFILL_FRAMES)
                write_reg(registers, REG_CONTROL, CONTROL_ENABLE);
        }
    }
    if (wait_below(registers, SILENCE_FRAMES, 2000u) != 0)
        goto fail;
    write_reg(registers, REG_CONTROL, 0u);
    if (read_reg(registers, REG_UNDERRUNS) != underruns ||
        read_reg(registers, REG_OVERFLOWS) != overflows) {
        fprintf(stderr, "audio queue fault underruns=%u overflows=%u\n",
                read_reg(registers, REG_UNDERRUNS) - underruns,
                read_reg(registers, REG_OVERFLOWS) - overflows);
        goto fail;
    }

    printf("ASTRA HDMI AUDIO PASS rate=%u frames=%u tone=440Hz\n",
           AUDIO_RATE, submitted);
    (void)munmap((void *)registers, AUDIO_BYTES);
    (void)close(memory_fd);
    return EXIT_SUCCESS;

fail:
    write_reg(registers, REG_CONTROL, 0u);
    (void)munmap((void *)registers, AUDIO_BYTES);
    (void)close(memory_fd);
    return EXIT_FAILURE;
}
