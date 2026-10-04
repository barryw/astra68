#ifndef ASTRA_DIVIDE_H
#define ASTRA_DIVIDE_H

/**
 * @file divide.h
 * @brief 64-bit division and scaled multiply-divide, for code that cannot
 *        call the compiler's runtime division helpers.
 *
 * The kernel links no libgcc and no C library, so `value / 1000000000u` on a
 * 64-bit value is an undefined reference to `__udivdi3` rather than an
 * instruction, and a variable-count 64-bit shift is `__lshrdi3` for the same
 * reason. Every freestanding kernel meets this: Linux keeps `div_u64` in
 * lib/div64.c and m68k Linux carries its own `__ashldi3` in arch/m68k/lib for
 * exactly this reason. This is Astra's copy of that, and it is shared with
 * userspace so there is one implementation rather than one per caller.
 */

#include <stdint.h>

/**
 * Divide a 64-bit value by a 32-bit divisor.
 *
 * @param value 64-bit dividend.
 * @param divisor 32-bit divisor.
 * @param remainder Output for the remainder; may be NULL when only the
 *     quotient is wanted.
 * @return value / divisor. If divisor is 0, returns UINT64_MAX and, if
 *     remainder is non-NULL, stores 0 in it, rather than faulting.
 */
uint64_t astra_divide_u64(uint64_t value, uint32_t divisor,
                          uint32_t *remainder);

/**
 * Divide a 64-bit value by a 64-bit divisor.
 *
 * @param value 64-bit dividend.
 * @param divisor 64-bit divisor.
 * @param remainder Output for the remainder; may be NULL when only the
 *     quotient is wanted.
 * @return value / divisor. If divisor is 0, returns UINT64_MAX and, if
 *     remainder is non-NULL, stores 0 in it, rather than faulting.
 */
uint64_t astra_divide_u64_u64(uint64_t value, uint64_t divisor,
                              uint64_t *remainder);

/**
 * Compute (value * multiplier) / divisor without the intermediate product
 * overflowing 64 bits.
 *
 * @param value 64-bit value to scale.
 * @param multiplier 64-bit multiplier.
 * @param divisor 64-bit divisor, applied after the multiply.
 * @return (value * multiplier) / divisor, rounded down. Saturates to
 *     UINT64_MAX, rather than wrapping, if divisor is 0 or the true result
 *     would not fit in 64 bits.
 */
uint64_t astra_multiply_divide_u64(uint64_t value, uint64_t multiplier,
                                   uint64_t divisor);

#endif
