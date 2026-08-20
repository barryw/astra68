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

    /* Dividing by zero is a caller's bug, and it saturates rather than trapping. */
    {
        uint32_t remainder = 7u;

        assert(astra_divide_u64(12345u, 0u, &remainder) == UINT64_MAX);
        assert(remainder == 0u);
    }

    puts("DIVIDE PASS");
    return 0;
}
