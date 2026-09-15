// SPDX-License-Identifier: MIT
// Capture one final composited 1080p frame while holding the capture lease.

#define _POSIX_C_SOURCE 200809L

#include "astra_graphics_hw.h"

#include <astra/render_batch.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(ASTRA_CAPTURE_FRAME_BYTES ==
                   ASTRA_DISPLAY_CAPTURE_FRAME_BYTES,
               "hardware and shared capture frame sizes differ");

int main(int argc, char **argv)
{
    const uint32_t buffer_base = ASTRA_GRAPHICS_ARENA_BASE +
                                 ASTRA_DISPLAY_CAPTURE_ARENA_OFFSET;
    struct astra_display_capture_result capture;
    struct astra_graphics_device device;
    int result = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.rgb\n", argv[0]);
        return EXIT_FAILURE;
    }

    astra_graphics_device_init(&device);
    if (astra_display_capture_device_open(&device) != 0)
        goto done;
    if (astra_graphics_capture_rgb(&device, buffer_base, argv[1],
                                   &capture) != 0) {
        perror("capture final display");
        goto done;
    }
    printf("ASTRA_DISPLAY_CAPTURE PASS bytes=%u generation=%" PRIu32
           " capture_cycles=%" PRIu32 " elapsed_ns=%" PRIu64 "\n",
           ASTRA_CAPTURE_FRAME_BYTES, capture.generation, capture.cycles,
           capture.elapsed_ns);
    result = EXIT_SUCCESS;
done:
    astra_graphics_device_close(&device);
    return result;
}
