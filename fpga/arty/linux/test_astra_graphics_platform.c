// SPDX-License-Identifier: MIT

#include "astra_graphics_hw.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef EXPECT_ARENA_BASE
#define EXPECT_ARENA_BASE 0x18000000u
#define EXPECT_ARENA_LIMIT 0x20000000u
#define EXPECT_CONTROL_BASE 0x43c00000u
#endif

_Static_assert(ASTRA_GRAPHICS_ARENA_BASE == EXPECT_ARENA_BASE,
               "graphics arena base mismatch");
_Static_assert(ASTRA_GRAPHICS_ARENA_LIMIT == EXPECT_ARENA_LIMIT,
               "graphics arena limit mismatch");
_Static_assert(ASTRA_CONTROL_BASE == EXPECT_CONTROL_BASE,
               "graphics control base mismatch");

int main(void)
{
    uint8_t device[35];
    uint8_t source[35];

    for (size_t index = 0; index < sizeof(source); ++index)
        source[index] = (uint8_t)(index * 7u + 3u);
    (void)memset(device, 0, sizeof(device));
    astra_graphics_memory_fill(device + 1, 0xa5u, sizeof(device) - 2u);
    if (device[0] != 0u || device[sizeof(device) - 1u] != 0u)
        return 1;
    for (size_t index = 1; index + 1u < sizeof(device); ++index)
        if (device[index] != 0xa5u)
            return 1;
    astra_graphics_memory_copy_to(device + 1, source + 1,
                                  sizeof(device) - 2u);
    if (memcmp(device + 1, source + 1, sizeof(device) - 2u) != 0)
        return 1;
    return 0;
}
