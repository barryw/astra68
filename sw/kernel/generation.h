#ifndef ASTRA_KERNEL_GENERATION_H
#define ASTRA_KERNEL_GENERATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline __attribute__((always_inline))
uint32_t kernel_generation_next(uint32_t generation)
{
    ++generation;
    return generation == 0u ? 1u : generation;
}

static inline __attribute__((always_inline))
uint32_t kernel_generation_next_masked(uint32_t generation, uint32_t mask)
{
    generation = (generation + 1u) & mask;
    return generation == 0u ? 1u : generation;
}

static inline __attribute__((always_inline))
uint32_t kernel_handle16_make(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static inline __attribute__((always_inline))
bool kernel_handle16_decode(uint32_t handle, uint32_t slot_count,
                            uint32_t *index, uint16_t *generation)
{
    uint32_t encoded_index = handle & UINT32_C(0xffff);
    uint16_t decoded_generation = (uint16_t)(handle >> 16);

    if (encoded_index == 0u || encoded_index > slot_count ||
        decoded_generation == 0u || index == NULL || generation == NULL)
        return false;
    *index = encoded_index - 1u;
    *generation = decoded_generation;
    return true;
}

#endif
