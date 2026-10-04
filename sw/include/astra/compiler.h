#ifndef ASTRA_COMPILER_H
#define ASTRA_COMPILER_H

/**
 * @file compiler.h
 * @brief C and C++ spellings and memory-ordering primitives shared by every
 *        ABI header.
 *
 * Nothing in this file applies to assembly: the whole of its content is
 * guarded by `__ASSEMBLER__` and compiled out for assembler translation
 * units.
 */

/* C and C++ spellings shared by the ABI headers; nothing for assembly. */
#ifndef __ASSEMBLER__

#ifdef __cplusplus
#ifndef _Alignas
/** C++ spelling of C11's `_Alignas`, provided only when the compiler has not already defined it. */
#define _Alignas alignas
#endif
#ifndef _Alignof
/** C++ spelling of C11's `_Alignof`, provided only when the compiler has not already defined it. */
#define _Alignof alignof
#endif
#ifndef _Static_assert
/** C++ spelling of C11's `_Static_assert`, provided only when the compiler has not already defined it. */
#define _Static_assert static_assert
#endif
#endif

/** Compiler memory barrier: forbids the compiler from reordering memory accesses across this call. */
static inline void astra_compiler_barrier(void)
{
    __asm__ volatile("" : : : "memory");
}

/**
 * Release fence: orders this thread's prior memory writes before whatever
 * follows.
 *
 * On m68k this emits a `nop` carrying a compiler memory clobber; on every
 * other target it falls back to ::astra_compiler_barrier.
 */
static inline void astra_memory_release_fence(void)
{
#if defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#else
    astra_compiler_barrier();
#endif
}

/**
 * Acquire fence: orders whatever follows after another thread's prior
 * memory writes.
 *
 * On m68k this emits a `nop` carrying a compiler memory clobber; on every
 * other target it falls back to ::astra_compiler_barrier.
 */
static inline void astra_memory_acquire_fence(void)
{
#if defined(__m68k__)
    __asm__ volatile("nop" : : : "memory");
#else
    astra_compiler_barrier();
#endif
}

#endif /* __ASSEMBLER__ */

#endif
