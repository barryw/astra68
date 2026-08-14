#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/fixed.h>

static int32_t absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

int main(void)
{
    AstraFraction2_30 sine;
    AstraFraction2_30 cosine;

    assert(astra_fixed_from_int(2) == INT32_C(0x00020000));
    assert(astra_fixed_from_int(40000) == INT32_MAX);
    assert(astra_fixed_mul(INT32_C(0x00018000), INT32_C(0x00020000)) ==
           INT32_C(0x00030000));
    assert(astra_fixed_div(INT32_C(0x00030000), INT32_C(0x00020000)) ==
           INT32_C(0x00018000));
    assert(astra_fixed_div(1, 0) == INT32_MAX);
    assert(astra_fixed_div(-1, 0) == INT32_MIN);
    assert(astra_fixed_sqrt(-1) == 0);
    assert(absolute(astra_fixed_sqrt(INT32_C(0x00020000)) - 92681) <= 1);

    astra_fixed_sincos(0, &sine, &cosine);
    assert(absolute(sine) < 40000);
    assert(absolute(cosine - ASTRA_FRACTION2_30_ONE) < 40000);
    astra_fixed_sincos(UINT32_C(0x40000000), &sine, &cosine);
    assert(absolute(sine - ASTRA_FRACTION2_30_ONE) < 40000);
    assert(absolute(cosine) < 40000);
    assert(absolute(astra_fixed_sin(UINT32_C(0x20000000)) - 759250125) <
           40000);
    assert(absolute(astra_fixed_cos(UINT32_C(0x60000000)) + 759250125) <
           40000);
    assert(astra_fixed_sin(UINT32_C(0x80000000)) < 40000 &&
           astra_fixed_sin(UINT32_C(0x80000000)) > -40000);
    puts("Astra fixed-point tests passed");
    return 0;
}
