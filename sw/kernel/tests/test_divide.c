/*
 * 64 by 32, against the host's own division.
 *
 * The kernel cannot call `__udivdi3`, so this loop is what stands in for it --
 * and a long division that is wrong only in its carry is wrong for a handful
 * of inputs out of billions. So this does not check a few values somebody
 * chose: it sweeps the shapes that break carries, at every divisor the tree
 * actually uses and a few that stress the edges.
 */

#include <astra/divide.h>
#include <astra/integer.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void check(uint64_t value, uint32_t divisor)
{
    uint32_t remainder = 0x5a5a5a5au;
    uint64_t quotient = astra_divide_u64(value, divisor, &remainder);

    assert(quotient == value / divisor);
    assert(remainder == (uint32_t)(value % divisor));
    /* The quotient alone, for a caller that does not want the remainder. */
    assert(astra_divide_u64(value, divisor, NULL) == value / divisor);
}

int main(void)
{
    static const uint32_t divisors[] = {
        1u, 2u, 3u, 7u, 10u, 60u, 86400u, 1000000u, 1000000000u,
        0x7fffffffu, 0x80000000u, 0xffffffffu
    };
    static const uint64_t values[] = {
        0u, 1u, 2u, 9u,
        0xffffffffull, 0x100000000ull, 0x100000001ull,
        UINT64_C(1787187161000000000),      /* a plausible instant in ns */
        UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000),
        UINT64_MAX
    };
    uint32_t sum = 0u;

    assert(!astra_u32_is_power_of_two(0u));
    assert(astra_u32_is_power_of_two(1u));
    assert(astra_u32_is_power_of_two(0x80000000u));
    assert(!astra_u32_is_power_of_two(3u));
    assert(astra_u32_add_checked(1u, 2u, &sum) && sum == 3u);
    assert(astra_u32_add_checked(UINT32_MAX, 0u, &sum) &&
           sum == UINT32_MAX);
    assert(!astra_u32_add_checked(UINT32_MAX, 1u, &sum));
    assert(!astra_u32_add_checked(1u, 1u, NULL));
    sum = 0u;
    astra_u32_increment_saturating(&sum);
    assert(sum == 1u);
    sum = UINT32_MAX;
    astra_u32_increment_saturating(&sum);
    assert(sum == UINT32_MAX);
    assert(astra_u64_bit(0u) == UINT64_C(1));
    assert(astra_u64_bit(31u) == UINT64_C(0x0000000080000000));
    assert(astra_u64_bit(32u) == UINT64_C(0x0000000100000000));
    assert(astra_u64_bit(63u) == UINT64_C(0x8000000000000000));

    for (unsigned d = 0u; d < sizeof(divisors) / sizeof(divisors[0]); ++d) {
        for (unsigned v = 0u; v < sizeof(values) / sizeof(values[0]); ++v)
            check(values[v], divisors[d]);
        /* Around every power of two, where the carry lives. */
        for (unsigned bit = 0u; bit < 64u; ++bit) {
            uint64_t base = UINT64_C(1) << bit;

            check(base, divisors[d]);
            check(base - 1u, divisors[d]);
            check(base + 1u, divisors[d]);
        }
        /* And a long uneven walk, which is what found nothing and proves it. */
        for (uint64_t step = 0u; step < 20000u; ++step)
            check(step * UINT64_C(982451653), divisors[d]);
    }
    for (unsigned v = 0u; v < sizeof(values) / sizeof(values[0]); ++v) {
        for (unsigned d = 0u; d < sizeof(values) / sizeof(values[0]); ++d) {
            uint64_t remainder;
            uint64_t divisor = values[d] == 0u ? 1u : values[d];

            assert(astra_divide_u64_u64(values[v], divisor, &remainder) ==
                   values[v] / divisor);
            assert(remainder == values[v] % divisor);
        }
    }
    for (uint64_t value = 0u; value < 100u; ++value)
        for (uint64_t multiplier = 0u; multiplier < 100u; ++multiplier)
            for (uint64_t divisor = 1u; divisor < 100u; ++divisor)
                assert(astra_multiply_divide_u64(value, multiplier,
                                                  divisor) ==
                       value * multiplier / divisor);
    assert(astra_multiply_divide_u64(UINT64_MAX - 1u,
                                     UINT64_C(1000000000), UINT64_MAX) ==
           UINT64_C(999999999));
    assert(astra_multiply_divide_u64(UINT64_MAX, 2u, 3u) ==
           UINT64_C(12297829382473034410));
    assert(astra_multiply_divide_u64(UINT64_MAX, UINT64_MAX,
                                     UINT64_MAX) == UINT64_MAX);
    assert(astra_multiply_divide_u64(UINT64_MAX, 2u, 1u) == UINT64_MAX);
    assert(astra_multiply_divide_u64(1u, 1u, 0u) == UINT64_MAX);

    /* Dividing by zero is a caller's bug, and it saturates rather than trapping. */
    {
        uint32_t remainder = 7u;

        assert(astra_divide_u64(12345u, 0u, &remainder) == UINT64_MAX);
        assert(remainder == 0u);
    }

    puts("DIVIDE PASS");
    return 0;
}
