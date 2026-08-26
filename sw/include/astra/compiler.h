#ifndef ASTRA_COMPILER_H
#define ASTRA_COMPILER_H

static inline void astra_compiler_barrier(void)
{
    __asm__ volatile("" : : : "memory");
}

#endif
