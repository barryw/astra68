#ifndef ASTRA_DIVIDE_H
#define ASTRA_DIVIDE_H

#include <stdint.h>

/*
 * 64 by 32, for code that cannot call the compiler's helper.
 *
 * The kernel links no libgcc and no C library, so `value / 1000000000u` on a
 * 64-bit value is an undefined reference to `__udivdi3` rather than an
 * instruction, and a variable-count 64-bit shift is `__lshrdi3` for the same
 * reason. Every freestanding kernel meets this: Linux keeps `div_u64` in
 * lib/div64.c and m68k Linux carries its own `__ashldi3` in arch/m68k/lib for
 * exactly this reason. This is Astra's copy of that, and it is shared with
 * userspace so there is one implementation rather than one per caller.
 *
 * @param remainder may be NULL when only the quotient is wanted.
 */
uint64_t astra_divide_u64(uint64_t value, uint32_t divisor,
                          uint32_t *remainder);

#endif
