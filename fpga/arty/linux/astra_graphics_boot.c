// SPDX-License-Identifier: MIT
// Load the fixed 720p boot surface, verify it, and publish real boot stages.

#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include "astra_boot_text.h"
#include "astra_graphics_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    COPY_BUFFER_BYTES = 64u * 1024u,
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t offset;

    for (offset = 0; offset < length; ++offset) {
        unsigned bit;

        crc ^= data[offset];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static int copy_surface(int image_fd, volatile uint8_t *framebuffer,
                        uint32_t *crc_out)
{
    uint8_t *buffer = malloc(COPY_BUFFER_BYTES);
    uint32_t crc = UINT32_MAX;
    size_t destination = 0;

    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    while (destination < ASTRA_FRAMEBUFFER_BYTES) {
        size_t wanted = ASTRA_FRAMEBUFFER_BYTES - destination;
        ssize_t received;

        if (wanted > COPY_BUFFER_BYTES)
            wanted = COPY_BUFFER_BYTES;
        do {
            received = read(image_fd, buffer, wanted);
        } while (received < 0 && errno == EINTR);
        if (received <= 0) {
            if (received == 0)
                fprintf(stderr, "short splash image at byte %zu\n",
                        destination);
            else
                perror("read splash image");
            free(buffer);
            return -1;
        }

        crc = crc32_update(crc, buffer, (size_t)received);
        astra_graphics_memory_copy_to(framebuffer + destination, buffer,
                                      (size_t)received);
        destination += (size_t)received;
    }

    astra_graphics_memory_barrier();
    *crc_out = crc ^ UINT32_MAX;
    free(buffer);
    return 0;
}

static int verify_surface(volatile const uint8_t *framebuffer,
                          uint32_t expected_crc)
{
    uint8_t *buffer = malloc(COPY_BUFFER_BYTES);
    uint32_t crc = UINT32_MAX;
    size_t source = 0;

    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    while (source < ASTRA_FRAMEBUFFER_BYTES) {
        size_t length = ASTRA_FRAMEBUFFER_BYTES - source;
        size_t byte;

        if (length > COPY_BUFFER_BYTES)
            length = COPY_BUFFER_BYTES;
        for (byte = 0; byte < length; ++byte)
            buffer[byte] = framebuffer[source + byte];
        crc = crc32_update(crc, buffer, length);
        source += length;
    }
    free(buffer);
    crc ^= UINT32_MAX;
    if (crc != expected_crc) {
        fprintf(stderr,
                "graphics DDR verification failed: expected=%08" PRIx32
                " actual=%08" PRIx32 "\n",
                expected_crc, crc);
        return -1;
    }
    return 0;
}

static int present_surface(const struct astra_graphics_device *device)
{
    const uint32_t framebuffer_size =
        (ASTRA_FRAMEBUFFER_HEIGHT << 16) | ASTRA_FRAMEBUFFER_WIDTH;
    const uint32_t framebuffer_control = 0x00000003u;
    const uint64_t timeout_ns = UINT64_C(2000000000);
    uint32_t generation;

    if (astra_mmio_read(device, ASTRA_REG_ARENA_BASE) !=
            ASTRA_GRAPHICS_ARENA_BASE ||
        astra_mmio_read(device, ASTRA_REG_ARENA_LIMIT) !=
            ASTRA_GRAPHICS_ARENA_LIMIT) {
        fprintf(stderr, "graphics arena does not match the boot contract\n");
        return -1;
    }

    astra_mmio_write(device, ASTRA_REG_BACKDROP, 0x00000000u);
    astra_mmio_write(device, ASTRA_REG_FB_BASE, ASTRA_FRAMEBUFFER_BASE);
    astra_mmio_write(device, ASTRA_REG_FB_PITCH, ASTRA_FRAMEBUFFER_PITCH);
    astra_mmio_write(device, ASTRA_REG_FB_SIZE, framebuffer_size);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_X, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_VIEWPORT_Y, 0u);
    astra_mmio_write(device, ASTRA_REG_FB_CONTROL, framebuffer_control);
    astra_mmio_write(device, ASTRA_REG_FB_KEY, 0u);
    astra_mmio_write(device, ASTRA_REG_TILE0_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_TILE1_CONTROL, 0u);
    astra_mmio_write(device, ASTRA_REG_GLOBAL_CONTROL, 1u);
    if (astra_graphics_scene_commit(device, timeout_ns, &generation) != 0)
        return -1;
    if ((astra_mmio_read(device, ASTRA_REG_COMMIT) & 2u) == 0u) {
        fprintf(stderr, "graphics scene committed disabled\n");
        return -1;
    }

    printf("ASTRA_GRAPHICS_PRESENT generation=%" PRIu32
           " capabilities=%08" PRIx32
           " deferrals=%" PRIu32 "\n",
           generation,
           astra_mmio_read(device, ASTRA_REG_CAPABILITIES),
           astra_mmio_read(device, ASTRA_REG_COMMIT_DEFERRALS));
    return 0;
}

static void build_initial_status(
    struct astra_boot_text_line line[ASTRA_BOOT_TEXT_ROWS])
{
    astra_boot_text_line_stage(&line[0], "Linux host", "OK",
                               ASTRA_BOOT_AMBER);
    astra_boot_text_line_stage(&line[1], "Graphics fabric", "OK",
                               ASTRA_BOOT_AMBER);
    astra_boot_text_line_stage(&line[2], "Splash readback", "RUN",
                               ASTRA_BOOT_WHITE);
    astra_boot_text_line_stage(&line[3], "Axiom launch", "WAIT",
                               ASTRA_BOOT_WHITE);
}

static int publish_status(
    const struct astra_graphics_device *device,
    const struct astra_boot_text_line line[ASTRA_BOOT_TEXT_ROWS])
{
    return astra_boot_text_write_all(device, line) == 0 &&
           astra_boot_text_commit(device, 1) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    struct astra_graphics_device device;
    struct astra_boot_text_line line[ASTRA_BOOT_TEXT_ROWS];
    struct stat image_stat;
    uint32_t image_crc;
    int image_fd = -1;
    int result = EXIT_FAILURE;

    astra_graphics_device_init(&device);
    if (argc != 2) {
        fprintf(stderr, "usage: %s <1280x720-big-endian-rgb565>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    image_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (image_fd < 0) {
        perror("open splash image");
        goto done;
    }
    if (fstat(image_fd, &image_stat) != 0) {
        perror("stat splash image");
        goto done;
    }
    if (!S_ISREG(image_stat.st_mode) ||
        image_stat.st_size != (off_t)ASTRA_FRAMEBUFFER_BYTES) {
        fprintf(stderr,
                "splash must be exactly %u bytes; got %" PRId64 "\n",
                ASTRA_FRAMEBUFFER_BYTES, (int64_t)image_stat.st_size);
        goto done;
    }

    if (astra_graphics_device_open(&device, true) != 0 ||
        astra_graphics_device_validate(&device, true) != 0)
        goto done;
    if (copy_surface(image_fd, device.framebuffer, &image_crc) != 0 ||
        present_surface(&device) != 0)
        goto done;

    build_initial_status(line);
    if (publish_status(&device, line) != 0)
        goto done;

    if (verify_surface(device.framebuffer, image_crc) != 0) {
        astra_boot_text_line_stage(&line[2], "Splash readback", "FAIL",
                                   ASTRA_BOOT_RED);
        astra_boot_text_line_stage(&line[3], "Axiom launch", "HALT",
                                   ASTRA_BOOT_RED);
        (void)publish_status(&device, line);
        goto done;
    }

    astra_boot_text_line_stage(&line[2], "Splash readback", "OK",
                               ASTRA_BOOT_AMBER);
    astra_boot_text_line_stage(&line[3], "Axiom launch", "READY",
                               ASTRA_BOOT_AMBER);
    if (publish_status(&device, line) != 0)
        goto done;

    printf("ASTRA_GRAPHICS_BOOT PASS bytes=%u crc32=%08" PRIx32
           " text_generation=%" PRIu32 "\n",
           ASTRA_FRAMEBUFFER_BYTES, image_crc,
           astra_mmio_read(&device, ASTRA_REG_BOOT_TEXT_GENERATION));
    result = EXIT_SUCCESS;

done:
    astra_graphics_device_close(&device);
    if (image_fd >= 0)
        (void)close(image_fd);
    return result;
}
