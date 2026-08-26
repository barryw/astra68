#include "generation.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t index = UINT32_MAX;
    uint16_t generation = 0u;
    uint32_t handle = kernel_handle16_make(6u, 9u);

    assert(handle == UINT32_C(0x00090007));
    assert(kernel_handle16_decode(handle, 8u, &index, &generation));
    assert(index == 6u);
    assert(generation == 9u);

    assert(!kernel_handle16_decode(0u, 8u, &index, &generation));
    assert(!kernel_handle16_decode(UINT32_C(0x00000001), 8u,
                                   &index, &generation));
    assert(!kernel_handle16_decode(UINT32_C(0x00010009), 8u,
                                   &index, &generation));
    assert(!kernel_handle16_decode(handle, 0u, &index, &generation));
    assert(!kernel_handle16_decode(handle, 8u, NULL, &generation));
    assert(!kernel_handle16_decode(handle, 8u, &index, NULL));

    puts("GENERATION PASS");
    return 0;
}
