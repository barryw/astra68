// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include <astra/display_capture.h>

#include "astra_display_capture_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

_Static_assert(sizeof(struct astra_display_capture_info) == 48u,
               "display capture ioctl ABI changed");

static int write_all(int descriptor, const uint8_t *data, size_t bytes)
{
    while (bytes != 0u) {
        ssize_t written = write(descriptor, data, bytes);

        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        data += (size_t)written;
        bytes -= (size_t)written;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct astra_display_capture_info info;
    const uint8_t *frame = MAP_FAILED;
    void *writable_frame;
    int capture = -1;
    int output = -1;
    int status = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.rgb\n", argv[0]);
        return EXIT_FAILURE;
    }
    capture = open("/dev/astra-display-capture", O_RDONLY | O_CLOEXEC);
    if (capture < 0) {
        perror("open display capture");
        goto done;
    }
    if (ioctl(capture, ASTRA_DISPLAY_CAPTURE_IOC_CAPTURE, &info) != 0) {
        perror("capture display");
        goto done;
    }
    if (info.width != ASTRA_DISPLAY_CAPTURE_WIDTH ||
        info.height != ASTRA_DISPLAY_CAPTURE_HEIGHT ||
        info.stride != ASTRA_DISPLAY_CAPTURE_WIDTH *
                           ASTRA_DISPLAY_CAPTURE_PIXEL_BYTES ||
        info.frame_bytes != ASTRA_DISPLAY_CAPTURE_FRAME_BYTES ||
        info.dma_channels == 0u) {
        fputs("display capture returned an invalid frame contract\n", stderr);
        goto done;
    }
    writable_frame = mmap(NULL, info.frame_bytes, PROT_READ | PROT_WRITE,
                          MAP_SHARED, capture, 0);
    if (writable_frame != MAP_FAILED) {
        (void)munmap(writable_frame, info.frame_bytes);
        fputs("display capture accepted a writable mapping\n", stderr);
        goto done;
    }
    frame = mmap(NULL, info.frame_bytes, PROT_READ, MAP_SHARED, capture, 0);
    if (frame == MAP_FAILED) {
        perror("map display capture");
        goto done;
    }
    output = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (output < 0 || write_all(output, frame, info.frame_bytes) != 0) {
        perror("write display capture");
        goto done;
    }
    printf("ASTRA_DISPLAY_CAPTURE_DMA PASS bytes=%" PRIu32
           " generation=%" PRIu32 " capture_cycles=%" PRIu32
           " dma_channels=%" PRIu32 " capture_ns=%" PRIu64
           " dma_ns=%" PRIu64 "\n",
           info.frame_bytes, info.generation, info.hardware_cycles,
           info.dma_channels, (uint64_t)info.capture_nanoseconds,
           (uint64_t)info.dma_nanoseconds);
    status = EXIT_SUCCESS;

done:
    if (output >= 0 && close(output) != 0)
        status = EXIT_FAILURE;
    if (capture >= 0)
        (void)close(capture);
    if (frame != MAP_FAILED)
        (void)munmap((void *)frame, info.frame_bytes);
    return status;
}
