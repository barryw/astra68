// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include "astra_graphics_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
    PCM_FRAME_BYTES = 6u,
};

typedef struct PcmInput {
    uint8_t *data;
    uint64_t frames;
} PcmInput;

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

static int parse_seconds(const char *text, uint64_t *frames)
{
    char *end;
    unsigned long long seconds;

    if (text == NULL || frames == NULL || text[0] == '-')
        return -1;
    errno = 0;
    seconds = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || seconds == 0u ||
        seconds > (UINT64_MAX - SILENCE_FRAMES) / AUDIO_RATE)
        return -1;
    *frames = (uint64_t)seconds * AUDIO_RATE;
    return 0;
}

static int32_t sample_s24le(const uint8_t *data)
{
    uint32_t bits = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16);

    return (int32_t)(bits ^ UINT32_C(0x800000)) - INT32_C(0x800000);
}

static int prefill_complete(uint64_t submitted, uint64_t source_frames)
{
    return submitted == PREFILL_FRAMES ||
           submitted == source_frames + SILENCE_FRAMES;
}

static int load_pcm(const char *path, PcmInput *pcm)
{
    struct stat st;
    size_t bytes;
    size_t used = 0u;
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return -1;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uintmax_t)st.st_size > SIZE_MAX ||
        st.st_size % PCM_FRAME_BYTES != 0) {
        (void)close(fd);
        errno = EINVAL;
        return -1;
    }
    bytes = (size_t)st.st_size;
    pcm->data = malloc(bytes);
    if (pcm->data == NULL) {
        (void)close(fd);
        return -1;
    }
    while (used < bytes) {
        ssize_t count = read(fd, pcm->data + used, bytes - used);

        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            int saved_errno = count == 0 ? EIO : errno;

            free(pcm->data);
            pcm->data = NULL;
            (void)close(fd);
            errno = saved_errno;
            return -1;
        }
        used += (size_t)count;
    }
    (void)close(fd);
    pcm->frames = bytes / PCM_FRAME_BYTES;
    return 0;
}

static int self_test_pcm_file(void)
{
    const uint8_t frame[] = {0xffu, 0xffu, 0x7fu,
                             0x00u, 0x00u, 0x80u};
    char path[] = "/tmp/astra-audio-XXXXXX";
    PcmInput pcm = {0};
    int fd = mkstemp(path);
    int passed = 0;

    if (fd < 0)
        return 0;
    if (write(fd, frame, sizeof(frame)) != sizeof(frame))
        goto done;
    if (close(fd) != 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if (load_pcm(path, &pcm) != 0 || pcm.frames != 1u ||
        sample_s24le(pcm.data) != INT32_C(0x7fffff) ||
        sample_s24le(pcm.data + 3u) != -INT32_C(0x800000))
        goto done;
    free(pcm.data);
    pcm.data = NULL;
    fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0 ||
        write(fd, frame, sizeof(frame) - 1u) != sizeof(frame) - 1u)
        goto done;
    if (close(fd) != 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    passed = load_pcm(path, &pcm) != 0 && pcm.data == NULL;

done:
    if (fd >= 0)
        (void)close(fd);
    free(pcm.data);
    (void)unlink(path);
    return passed;
}

static int self_test(void)
{
    uint64_t frames = 0u;
    const uint8_t positive[] = {0xffu, 0xffu, 0x7fu};
    const uint8_t negative[] = {0x00u, 0x00u, 0x80u};
    const uint8_t minus_one[] = {0xffu, 0xffu, 0xffu};

    return triangle(0u) == 0 &&
           triangle(UINT32_C(0x40000000)) == INT32_C(0x7fffff) &&
           triangle(UINT32_C(0x80000000)) == 0 &&
           triangle(UINT32_C(0xc0000000)) == -INT32_C(0x7fffff) &&
           sample_s24le(positive) == INT32_C(0x7fffff) &&
           sample_s24le(negative) == -INT32_C(0x800000) &&
           sample_s24le(minus_one) == -1 &&
           self_test_pcm_file() &&
           prefill_complete(65u, 1u) &&
           !prefill_complete(64u, 1u) &&
           prefill_complete(PREFILL_FRAMES, AUDIO_RATE) &&
           !prefill_complete(PREFILL_FRAMES - 1u, AUDIO_RATE) &&
           parse_seconds("1", &frames) == 0 && frames == AUDIO_RATE &&
           parse_seconds("0", &frames) != 0 &&
           parse_seconds("-1", &frames) != 0 &&
           parse_seconds("invalid", &frames) != 0 &&
           parse_seconds("18446744073709551615", &frames) != 0 ?
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

static uint64_t elapsed_us(const struct timeval *start,
                           const struct timeval *end)
{
    int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
    int64_t microseconds = (int64_t)end->tv_usec - (int64_t)start->tv_usec;

    return (uint64_t)(seconds * INT64_C(1000000) + microseconds);
}

static uint64_t elapsed_ns(const struct timespec *start,
                           const struct timespec *end)
{
    int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
    int64_t nanoseconds = (int64_t)end->tv_nsec - (int64_t)start->tv_nsec;

    return (uint64_t)(seconds * INT64_C(1000000000) + nanoseconds);
}

int main(int argc, char **argv)
{
    volatile uint32_t *registers;
    struct rusage usage_start;
    struct rusage usage_end;
    struct timespec time_start;
    struct timespec time_end;
    struct timespec last_refill;
    uint32_t underruns;
    uint32_t overflows;
    uint32_t minimum_level = AUDIO_FRAMES;
    uint32_t phase = 0u;
    uint32_t phase_step = (uint32_t)(
        (UINT64_C(440) << 32) / AUDIO_RATE);
    uint64_t submitted = 0u;
    uint64_t tone_frames = AUDIO_RATE;
    uint64_t maximum_gap_ns = 0u;
    PcmInput pcm = {0};
    int enabled = 0;
    int memory_fd;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
        return self_test();
    if (argc == 3 && strcmp(argv[1], "--seconds") == 0) {
        if (parse_seconds(argv[2], &tone_frames) != 0) {
            fprintf(stderr, "invalid audio duration: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    } else if (argc == 3 && strcmp(argv[1], "--pcm-s24le") == 0) {
        if (load_pcm(argv[2], &pcm) != 0) {
            perror("load PCM");
            return EXIT_FAILURE;
        }
        tone_frames = pcm.frames;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--self-test | --seconds N | "
                "--pcm-s24le FILE]\n", argv[0]);
        return EXIT_FAILURE;
    }
    memory_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd < 0) {
        perror("open /dev/mem");
        free(pcm.data);
        return EXIT_FAILURE;
    }
    registers = mmap(NULL, AUDIO_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                     memory_fd, AUDIO_BASE);
    if (registers == MAP_FAILED) {
        perror("mmap audio");
        (void)close(memory_fd);
        free(pcm.data);
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
    if (getrusage(RUSAGE_SELF, &usage_start) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &time_start) != 0)
        goto fail;

    while (submitted < tone_frames + SILENCE_FRAMES) {
        uint32_t level = read_reg(registers, REG_STATUS) & STATUS_LEVEL_MASK;
        uint32_t room = AUDIO_FRAMES - level;

        if (enabled) {
            struct timespec now;
            uint64_t gap_ns;

            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
                goto fail;
            gap_ns = elapsed_ns(&last_refill, &now);
            if (gap_ns > maximum_gap_ns)
                maximum_gap_ns = gap_ns;
            last_refill = now;
            if (submitted < tone_frames && level < minimum_level)
                minimum_level = level;
        }

        if (room == 0u) {
            if (wait_below(registers, AUDIO_FRAMES, 100u) != 0)
                goto fail;
            continue;
        }
        while (room-- != 0u && submitted < tone_frames + SILENCE_FRAMES) {
            int32_t left = 0;
            int32_t right = 0;

            if (submitted < tone_frames && pcm.data != NULL) {
                const uint8_t *frame = pcm.data +
                    (size_t)submitted * PCM_FRAME_BYTES;

                left = sample_s24le(frame);
                right = sample_s24le(frame + 3u);
            } else if (submitted < tone_frames) {
                left = triangle(phase);
                right = left;
            }
            submit(registers, left, right);
            phase += phase_step;
            ++submitted;
            if (prefill_complete(submitted, tone_frames)) {
                write_reg(registers, REG_CONTROL, CONTROL_ENABLE);
                if (clock_gettime(CLOCK_MONOTONIC, &last_refill) != 0)
                    goto fail;
                enabled = 1;
            }
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
    if (clock_gettime(CLOCK_MONOTONIC, &time_end) != 0 ||
        getrusage(RUSAGE_SELF, &usage_end) != 0)
        goto fail;

    printf("ASTRA HDMI AUDIO PASS rate=%u frames=%" PRIu64
           " source=%s elapsed_ms=%" PRIu64 " user_ms=%" PRIu64
           " system_ms=%" PRIu64 " voluntary_switches=%ld"
           " involuntary_switches=%ld min_fifo=%u max_refill_gap_us=%" PRIu64
           "\n",
           AUDIO_RATE, submitted, pcm.data != NULL ? "pcm-s24le" : "tone-440Hz",
           elapsed_ns(&time_start, &time_end) / UINT64_C(1000000),
           elapsed_us(&usage_start.ru_utime, &usage_end.ru_utime) / 1000u,
           elapsed_us(&usage_start.ru_stime, &usage_end.ru_stime) / 1000u,
           usage_end.ru_nvcsw - usage_start.ru_nvcsw,
           usage_end.ru_nivcsw - usage_start.ru_nivcsw,
           tone_frames > PREFILL_FRAMES ? minimum_level : 0u,
           maximum_gap_ns / 1000u);
    (void)munmap((void *)registers, AUDIO_BYTES);
    (void)close(memory_fd);
    free(pcm.data);
    return EXIT_SUCCESS;

fail:
    write_reg(registers, REG_CONTROL, 0u);
    (void)munmap((void *)registers, AUDIO_BYTES);
    (void)close(memory_fd);
    free(pcm.data);
    return EXIT_FAILURE;
}
