#ifndef ASTRA_NDK_INTERNAL_SYSCALL_H
#define ASTRA_NDK_INTERNAL_SYSCALL_H

#include <stdint.h>

#include <astra/syscall.h>
#include <astra/types.h>

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

static inline AstraResult astra_internal_result(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK:
        return ASTRA_OK;
    case ASTRA_SYSCALL_BAD_SYSCALL:
        return ASTRA_ERROR_UNSUPPORTED;
    case ASTRA_SYSCALL_INVALID_ARGUMENT:
        return ASTRA_ERROR_INVALID_ARGUMENT;
    case ASTRA_SYSCALL_INVALID_HANDLE:
        return ASTRA_ERROR_INVALID_HANDLE;
    case ASTRA_SYSCALL_ACCESS_DENIED:
        return ASTRA_ERROR_PERMISSION;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
        return ASTRA_ERROR_NO_RESOURCES;
    case ASTRA_SYSCALL_WOULD_BLOCK:
        return ASTRA_ERROR_WOULD_BLOCK;
    case ASTRA_SYSCALL_TIMED_OUT:
        return ASTRA_ERROR_TIMEOUT;
    case ASTRA_SYSCALL_PEER_DEAD:
        return ASTRA_ERROR_PEER_DEAD;
    case ASTRA_SYSCALL_BAD_ADDRESS:
        return ASTRA_ERROR_BAD_ADDRESS;
    case ASTRA_SYSCALL_CANCELLED:
        return ASTRA_ERROR_CANCELLED;
    case ASTRA_SYSCALL_OUT_OF_MEMORY:
        return ASTRA_ERROR_OUT_OF_MEMORY;
    case ASTRA_SYSCALL_IO_ERROR:
        return ASTRA_ERROR_IO;
    case ASTRA_SYSCALL_CLOSED:
        return ASTRA_ERROR_CLOSED;
    case ASTRA_SYSCALL_BUFFER_TOO_SMALL:
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    default:
        return ASTRA_ERROR_IO;
    }
}

#endif
