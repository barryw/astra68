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

uint64_t astra_divide_u64_u64(uint64_t value, uint64_t divisor,
                              uint64_t *remainder)
{
    uint64_t quotient = 0u;
    uint64_t rest = 0u;

    if (divisor == 0u) {
        if (remainder != NULL)
            *remainder = 0u;
        return UINT64_MAX;
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        uint64_t carry = rest >> 63;

        rest = (rest << 1) | (value >> 63);
        value <<= 1;
        quotient <<= 1;
        if (carry != 0u || rest >= divisor) {
            rest -= divisor;
            quotient |= 1u;
        }
    }
    if (remainder != NULL)
        *remainder = rest;
    return quotient;
}

uint64_t astra_multiply_divide_u64(uint64_t value, uint64_t multiplier,
                                   uint64_t divisor)
{
    uint64_t whole;
    uint64_t remainder;
    uint64_t fraction = 0u;
    uint64_t fraction_remainder = 0u;
    uint64_t result;

    if (divisor == 0u)
        return UINT64_MAX;
    if (value == 0u || multiplier == 0u)
        return 0u;
    whole = astra_divide_u64_u64(value, divisor, &remainder);
    if (whole > UINT64_MAX / multiplier)
        return UINT64_MAX;
    result = whole * multiplier;

    for (uint64_t mask = UINT64_C(1) << 63; mask != 0u; mask >>= 1) {
        if (fraction > UINT64_MAX / 2u)
            return UINT64_MAX;
        fraction *= 2u;
        if (fraction_remainder >= divisor - fraction_remainder) {
            fraction_remainder -= divisor - fraction_remainder;
            ++fraction;
        } else {
            fraction_remainder += fraction_remainder;
        }
        if ((multiplier & mask) != 0u) {
            if (fraction_remainder >= divisor - remainder) {
                fraction_remainder -= divisor - remainder;
                if (fraction == UINT64_MAX)
                    return UINT64_MAX;
                ++fraction;
            } else {
                fraction_remainder += remainder;
            }
        }
    }
    return result > UINT64_MAX - fraction ? UINT64_MAX : result + fraction;
}
