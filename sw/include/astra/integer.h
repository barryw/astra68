#ifndef ASTRA_INTEGER_H
#define ASTRA_INTEGER_H

#include <stdint.h>

static inline int astra_u32_is_power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static inline int astra_u32_add_checked(uint32_t left, uint32_t right,
                                        uint32_t *result)
{
    if (result == 0 || right > UINT32_MAX - left)
        return 0;
    *result = left + right;
    return 1;
}

/* Avoids the 68030 libgcc call emitted for a variable 64-bit shift. */
static inline uint64_t astra_u64_bit(uint32_t bit)
{
    return bit < 32u ? (uint64_t)((uint32_t)1u << bit)
                     : (uint64_t)((uint32_t)1u << (bit - 32u)) << 32;
}

#if defined(ASTRA_INTEGER_FORCE_INLINE)
#define ASTRA_INTEGER_INLINE __attribute__((always_inline))
#else
#define ASTRA_INTEGER_INLINE
#endif

static inline ASTRA_INTEGER_INLINE void
astra_u32_increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

#undef ASTRA_INTEGER_INLINE

#endif
