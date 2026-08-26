#ifndef ASTRA_NDK_INTERNAL_SYSCALL_H
#define ASTRA_NDK_INTERNAL_SYSCALL_H

#include <stdint.h>

#include <astra/result.h>
#include <astra/syscall.h>

#if defined(ASTRA_NDK_TEST)

uint32_t astra_ndk_test_syscall(uint32_t number,
                                uintptr_t d1,
                                uintptr_t d2,
                                uintptr_t d3,
                                uintptr_t d4,
                                uintptr_t d5,
                                uint32_t *out_d1,
                                uint32_t *out_d2);

#define astra_internal_syscall astra_ndk_test_syscall

#else

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t),
               "Astra syscall pointers must be 32-bit");

static inline uint32_t astra_internal_syscall(uint32_t number,
                                               uintptr_t arg1,
                                               uintptr_t arg2,
                                               uintptr_t arg3,
                                               uintptr_t arg4,
                                               uintptr_t arg5,
                                               uint32_t *out_d1,
                                               uint32_t *out_d2)
{
    register uint32_t d0 __asm__("d0") = number;
    register uint32_t d1 __asm__("d1") = (uint32_t)arg1;
    register uint32_t d2 __asm__("d2") = (uint32_t)arg2;
    register uint32_t d3 __asm__("d3") = (uint32_t)arg3;
    register uint32_t d4 __asm__("d4") = (uint32_t)arg4;
    register uint32_t d5 __asm__("d5") = (uint32_t)arg5;

    __asm__ volatile("trap #15"
                     : "+d"(d0), "+d"(d1), "+d"(d2), "+d"(d3),
                       "+d"(d4), "+d"(d5)
                     :
                     : "cc", "memory");
    *out_d1 = d1;
    *out_d2 = d2;
    return d0;
}

#endif

#define astra_internal_result astra_result_from_syscall

#endif
