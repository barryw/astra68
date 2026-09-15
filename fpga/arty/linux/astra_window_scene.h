#ifndef ASTRA_WINDOW_SCENE_COMPILER_H
#define ASTRA_WINDOW_SCENE_COMPILER_H

#include <stddef.h>
#include <stdint.h>

enum astra_window_scene_status {
    ASTRA_WINDOW_SCENE_OK = 0,
    ASTRA_WINDOW_SCENE_BAD_REQUEST = -1,
    ASTRA_WINDOW_SCENE_BAD_LAYER = -2,
    ASTRA_WINDOW_SCENE_NO_SPACE = -3,
};

int astra_window_scene_compile(const void *request, size_t request_bytes,
                               volatile void *output, size_t output_bytes,
                               uint32_t arena_bytes, uint32_t *written_out);

#endif
