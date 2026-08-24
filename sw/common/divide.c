/*
 * Long division on two 32-bit halves, with constant shifts only.
 *
 * The obvious loop -- shift a 64-bit remainder left, compare, subtract -- is
 * unavailable in a freestanding kernel for the same reason the division is:
 * `value >> index` with a variable index compiles to `__lshrdi3`. So the
 * dividend is carried as two 32-bit words and every shift is by one.
 *
 * The carry is the whole subtlety. The running remainder is always smaller
 * than the divisor, so doubling it can leave 33 bits; the bit that falls off
 * the top says the true value is at least 2^32, which is larger than any
 * divisor, so the subtraction must happen. In wrapping 32-bit arithmetic it
 * lands on the right answer, because the 2^32 that was dropped and the 2^32
 * the subtraction borrows are the same one.
 */

#include <astra/divide.h>

#include <stddef.h>

uint64_t astra_divide_u64(uint64_t value, uint32_t divisor,
                          uint32_t *remainder)
{
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)value;
    uint32_t quotient_high = 0u;
    uint32_t quotient_low = 0u;
    uint32_t rest = 0u;

    if (divisor == 0u) {
        /*
         * A caller dividing by zero has a bug, and a kernel that faults here
         * takes the machine down over it. Answering the saturated value keeps
         * the bug where it belongs -- in the caller's arithmetic -- and every
         * caller in this tree divides by a constant.
         */
        if (remainder != NULL)
            *remainder = 0u;
        return UINT64_MAX;
    }
    if (high == 0u) {
        quotient_low = low / divisor;
        rest = low - quotient_low * divisor;
        if (remainder != NULL)
            *remainder = rest;
        return quotient_low;
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        uint32_t carry = rest >> 31;
        uint32_t bit = high >> 31;

        rest = (rest << 1) | bit;
        high = (high << 1) | (low >> 31);
        low <<= 1;
        quotient_high = (quotient_high << 1) | (quotient_low >> 31);
        quotient_low <<= 1;
        if (carry != 0u || rest >= divisor) {
            rest -= divisor;
            quotient_low |= 1u;
        }
    }
    if (remainder != NULL)
        *remainder = rest;
    return ((uint64_t)quotient_high << 32) | quotient_low;
}
