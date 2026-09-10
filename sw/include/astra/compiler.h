#ifndef ASTRA_COMPILER_H
#define ASTRA_COMPILER_H

#ifdef __cplusplus
#ifndef _Alignas
#define _Alignas alignas
#endif
#ifndef _Alignof
#define _Alignof alignof
#endif
#ifndef _Static_assert
#define _Static_assert static_assert
#endif
#endif

static inline void astra_compiler_barrier(void)
{
    __asm__ volatile("" : : : "memory");
}

static inline void astra_memory_release_fence(void)
{
#if defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#else
    astra_compiler_barrier();
#endif
}

static inline void astra_memory_acquire_fence(void)
{
#if defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#else
    astra_compiler_barrier();
#endif
}

#endif
