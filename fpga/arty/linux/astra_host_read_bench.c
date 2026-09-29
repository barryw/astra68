// SPDX-License-Identifier: MIT
/*
 * The DE25 render host aperture, measured: how fast the CPU fills the host
 * arena, how fast it fills media RAM (the copy the aperture removes), and
 * how long the render engine takes to BLIT an uploaded image when it reads
 * the batch from the host arena over F2SDRAM against reading a media RAM
 * copy. Every BLIT is checked byte for byte.
 *
 * usage: astra-host-read-bench [WIDTH HEIGHT [ROUNDS [COMMANDS]]]
 * COMMANDS repeats the BLIT in one batch (default 1). The engine prefetches
 * up to 32 queued ring entries in one read, so COMMANDS sets the length of
 * the first host burst: 64 bytes per entry, 2 KiB at 32.
 * Owns the graphics device and writes media RAM: run it with Astra stopped.
 *
 * Each phase is announced on stderr before it runs. With
 * ASTRA_HOST_READ_BENCH_PAUSE=SECONDS the bench also waits that long after
 * each announcement, so a phase that stops the HPS is the last one a remote
 * log received.
 */
#define _GNU_SOURCE

#include "astra_graphics_hw.h"
#include "astra_host_arena_uapi.h"
#include "astra_render_protocol.h"

#include <astra/render_builder.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define HOST_ARENA "/dev/astra-host-arena"
/* The first arena offset outside the batch window a surface may use. */
#define DESTINATION_OFFSET ASTRA_RENDER_BATCH_WORKSPACE_LIMIT
#define STOP_TIMEOUT_NS UINT64_C(1000000000)

static unsigned phase_pause;

static void phase(const char *name)
{
    fprintf(stderr, "phase %s\n", name);
    fflush(stderr);
    if (phase_pause != 0u)
        sleep(phase_pause);
}

static void report_copy(const char *name, uint32_t bytes, unsigned rounds,
                        uint64_t elapsed)
{
    printf("%-14s %8u bytes x %u: %8.1f MB/s\n", name, bytes, rounds,
           (double)bytes * rounds * 1000.0 / (double)elapsed);
}

static uint32_t batch_commands = 1u;

/* Every completion record must say OK for this batch: counting completions
   alone passes a batch whose commands failed. */
static int check_completions(const struct astra_graphics_device *device,
                             uint32_t generation)
{
    for (uint32_t index = 0u; index < batch_commands; ++index) {
        uint8_t record[ASTRA_RENDER_COMPLETION_BYTES];
        uint32_t word[ASTRA_RENDER_COMPLETION_BYTES / 4u];

        astra_graphics_memory_copy_from(
            record, device->arena + ASTRA_RENDER_BATCH_COMPLETION_OFFSET +
                        index * ASTRA_RENDER_COMPLETION_BYTES,
            sizeof(record));
        for (unsigned w = 0u; w < sizeof(word) / sizeof(word[0]); ++w)
            word[w] = (uint32_t)record[w * 4u] << 24 |
                      (uint32_t)record[w * 4u + 1u] << 16 |
                      (uint32_t)record[w * 4u + 2u] << 8 |
                      (uint32_t)record[w * 4u + 3u];
        if (word[0] != ((uint32_t)ASTRA_RENDER_ABI_VERSION << 16 |
                        ASTRA_RENDER_COMPLETION_BYTES) ||
            (word[1] & UINT32_C(0xffff)) != ASTRA_RENDER_STATUS_OK ||
            word[7] != generation) {
            fprintf(stderr,
                    "COMPLETION FAILED: command %u of %u: header=0x%08x "
                    "status=0x%08x fault=0x%08x generation=%u/%u\n",
                    index, batch_commands, word[0], word[1], word[6],
                    word[7], generation);
            return -1;
        }
    }
    return 0;
}

static int render(const struct astra_graphics_device *device,
                  uint32_t generation, uint64_t *elapsed)
{
    uint64_t started;
    uint64_t deadline;

    if (astra_graphics_render_stop(device, STOP_TIMEOUT_NS) != 0)
        return -1;
    /* No stale OK record may stand in for one the engine did not write. */
    astra_graphics_memory_fill(device->arena +
                                   ASTRA_RENDER_BATCH_COMPLETION_OFFSET,
                               0u,
                               batch_commands *
                                   ASTRA_RENDER_COMPLETION_BYTES);
    astra_graphics_memory_barrier();
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_CONSUMER, 0u);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_RING_OFFSET,
                     ASTRA_RENDER_BATCH_SUBMISSION_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_COMPLETION_RING_OFFSET,
                     ASTRA_RENDER_BATCH_COMPLETION_OFFSET);
    astra_mmio_write(device, ASTRA_REG_RENDER_RESOURCE_GENERATION,
                     generation);
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_REBASE);
    if (astra_mmio_read(device, ASTRA_REG_RENDER_SUBMISSION_CONSUMER) != 0u)
        return -1;
    astra_mmio_write(device, ASTRA_REG_RENDER_CONTROL,
                     ASTRA_RENDER_CONTROL_ENABLE);
    started = astra_monotonic_nanoseconds();
    deadline = started + UINT64_C(2000000000);
    astra_mmio_write(device, ASTRA_REG_RENDER_SUBMISSION_PRODUCER,
                     batch_commands);
    for (;;) {
        uint32_t status = astra_mmio_read(device, ASTRA_REG_RENDER_STATUS);

        if (astra_mmio_read(device, ASTRA_REG_RENDER_COMPLETION_PRODUCER) ==
                batch_commands &&
            (status & ASTRA_RENDER_ENGINE_BUSY) == 0u)
            break;
        if ((status & ASTRA_RENDER_ENGINE_CONFIG_FAULT) != 0u ||
            astra_monotonic_nanoseconds() >= deadline) {
            fprintf(stderr, "render stalled: status 0x%08x\n", status);
            return -1;
        }
    }
    *elapsed = astra_monotonic_nanoseconds() - started;
    return check_completions(device, generation);
}

static int check(const struct astra_graphics_device *device,
                 const uint8_t *pixels, uint32_t bytes, uint8_t *readback)
{
    astra_graphics_memory_copy_from(readback,
                                    device->arena + DESTINATION_OFFSET,
                                    bytes);
    if (memcmp(readback, pixels, bytes) != 0) {
        fprintf(stderr, "BLIT result differs from the uploaded pixels\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct astra_host_arena_info info;
    AstraRenderBuilder builder;
    unsigned width = argc > 2 ? (unsigned)strtoul(argv[1], NULL, 0) : 640u;
    unsigned height = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 480u;
    unsigned rounds = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : 20u;
    unsigned long commands = argc > 4 ? strtoul(argv[4], NULL, 0) : 1u;
    uint32_t pitch = width * 4u;
    uint32_t image = pitch * height;
    uint32_t destination;
    uint32_t source;
    uint32_t batch_bytes;
    uint32_t saved_base;
    uint8_t *batch = malloc(ASTRA_HOST_ARENA_BYTES);
    uint8_t *pixels = malloc(image);
    uint8_t *readback = malloc(image);
    volatile uint8_t *host;
    uint64_t started;
    uint64_t elapsed;
    uint64_t total;
    int fd;
    int result = EXIT_FAILURE;
    const char *pause = getenv("ASTRA_HOST_READ_BENCH_PAUSE");

    if (pause != NULL)
        phase_pause = (unsigned)strtoul(pause, NULL, 0);
    if (batch == NULL || pixels == NULL || readback == NULL ||
        width == 0u || height == 0u || rounds == 0u || commands == 0u ||
        commands >= ASTRA_RENDER_RING_ENTRIES ||
        width > ASTRA_RENDER_MAX_SURFACE_DIMENSION ||
        height > ASTRA_RENDER_MAX_SURFACE_DIMENSION) {
        fprintf(stderr, "usage: %s [WIDTH HEIGHT [ROUNDS [COMMANDS]]]\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    phase("open-host-arena");
    fd = open(HOST_ARENA, O_RDWR | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, ASTRA_HOST_ARENA_IOC_INFO, &info) != 0) {
        perror(HOST_ARENA);
        return EXIT_FAILURE;
    }
    if (info.bytes != ASTRA_HOST_ARENA_BYTES || info.physical > UINT32_MAX) {
        fprintf(stderr, "host arena unusable: 0x%llx, %llu bytes\n",
                (unsigned long long)info.physical,
                (unsigned long long)info.bytes);
        return EXIT_FAILURE;
    }
    host = mmap(NULL, ASTRA_HOST_ARENA_BYTES, PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
    if (host == MAP_FAILED) {
        perror("map host arena");
        return EXIT_FAILURE;
    }
    printf("host arena 0x%08llx\n", (unsigned long long)info.physical);

    srand(1u);
    for (uint32_t at = 0u; at < image; ++at)
        pixels[at] = (uint8_t)rand();
    if (!astra_render_builder_init(&builder, batch, ASTRA_HOST_ARENA_BYTES,
                                   1u))
        return EXIT_FAILURE;
    destination = astra_render_builder_surface_format_at(
        &builder, DESTINATION_OFFSET, image, (uint16_t)width,
        (uint16_t)height, pitch, ASTRA_RENDER_FORMAT_ARGB8888,
        ASTRA_RENDER_SURFACE_READ | ASTRA_RENDER_SURFACE_WRITE);
    source = astra_render_builder_upload(&builder, pixels, pitch,
                                         (uint16_t)width, (uint16_t)height,
                                         ASTRA_RENDER_FORMAT_ARGB8888);
    if (destination == 0u || source == 0u)
        goto build_failed;
    batch_commands = (uint32_t)commands;
    for (uint32_t command = 0u; command < batch_commands; ++command)
        if (astra_render_builder_blit(&builder, destination, source, 0, 0,
                                      (uint16_t)width, (uint16_t)height, 0u,
                                      0) == 0)
            goto build_failed;
    batch_bytes = astra_render_builder_finish_render_only(&builder);
    if (batch_bytes == 0u)
        goto build_failed;

    phase("open-device");
    if (astra_graphics_device_open(&device, false) != 0 ||
        astra_graphics_device_validate(&device, false) != 0)
        return EXIT_FAILURE;
    phase("read-aperture-register");
    /* An older bitstream has no register at 0x248: a load there is a DECERR,
       which panics the DE25. */
    if (!astra_graphics_has_capability(&device,
                                       ASTRA_CAP_RENDER_HOST_APERTURE)) {
        fprintf(stderr, "bitstream has no render host aperture\n");
        astra_graphics_device_close(&device);
        return EXIT_FAILURE;
    }
    saved_base = astra_mmio_read(&device, ASTRA_REG_RENDER_HOST_APERTURE_BASE);

    /* CPU fills: the host arena (what QEMU does now) and media RAM (the
       copy the helper used to make). */
    phase("cpu-write-host-arena");
    started = astra_monotonic_nanoseconds();
    for (unsigned round = 0u; round < rounds; ++round) {
        memcpy((void *)host, batch, batch_bytes);
        astra_graphics_memory_barrier();
    }
    report_copy("host write", batch_bytes, rounds,
                astra_monotonic_nanoseconds() - started);
    phase("cpu-write-media");
    started = astra_monotonic_nanoseconds();
    for (unsigned round = 0u; round < rounds; ++round) {
        astra_graphics_memory_copy_to(
            device.arena + ASTRA_RENDER_BATCH_ARENA_OFFSET, batch,
            batch_bytes);
        astra_graphics_memory_barrier();
    }
    report_copy("media write", batch_bytes, rounds,
                astra_monotonic_nanoseconds() - started);

    /* The engine reading the media RAM copy just made, then the host
       arena through the aperture. */
    for (int aperture = 0; aperture <= 1; ++aperture) {
        phase(aperture ? "write-aperture-register" : "media-blit");
        if (astra_graphics_render_host_aperture_set(
                &device, aperture ? (uint32_t)info.physical : 0u,
                STOP_TIMEOUT_NS) != 0)
            goto done;
        if (aperture)
            phase("host-blit");
        total = 0u;
        for (unsigned round = 0u; round < rounds; ++round) {
            astra_graphics_memory_fill(device.arena + DESTINATION_OFFSET, 0u,
                                       image);
            astra_graphics_memory_barrier();
            if (render(&device, 1u, &elapsed) != 0 ||
                check(&device, pixels, image, readback) != 0)
                goto done;
            total += elapsed;
        }
        printf("%-14s %ux%u ARGB8888 BLIT x %u: %8.1f us, %8.1f MB/s read\n",
               aperture ? "host blit" : "media blit", width, height, rounds,
               (double)total / rounds / 1000.0,
               (double)image * rounds * 1000.0 / (double)total);
    }
    result = EXIT_SUCCESS;

done:
    /* A stalled engine stays busy: leave the aperture as it is rather than
       store to a register that refuses it. */
    if (astra_graphics_render_host_aperture_set(&device, saved_base,
                                                STOP_TIMEOUT_NS) != 0)
        fprintf(stderr, "render engine still busy; aperture left set\n");
    astra_graphics_device_close(&device);
    return result;

build_failed:
    fprintf(stderr, "render batch build failed: %u\n", builder.failed);
    return EXIT_FAILURE;
}
