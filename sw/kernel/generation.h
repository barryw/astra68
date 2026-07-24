#ifndef ASTRA_KERNEL_GENERATION_H
#define ASTRA_KERNEL_GENERATION_H

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

#endif
