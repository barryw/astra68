#include <astra/fixed.h>

#include <limits.h>
#include <stddef.h>

static const int32_t cordic_angles[] = {
    536870912, 316933406, 167458907, 85004756,
    42667331, 21354465, 10679838, 5340245,
    2670163, 1335087, 667544, 333772,
    166886, 83443, 41722, 20861
};

static int32_t clamp64(int64_t value)
{
    if (value > INT32_MAX)
        return INT32_MAX;
    if (value < INT32_MIN)
        return INT32_MIN;
    return (int32_t)value;
}

static int32_t shift_toward_zero(int32_t value, unsigned shift)
{
    uint32_t magnitude;

    if (value >= 0)
        return (int32_t)((uint32_t)value >> shift);
    magnitude = (uint32_t)(-(value + 1)) + 1u;
    return -(int32_t)(magnitude >> shift);
}

AstraFixed16_16 astra_fixed_from_int(int32_t value)
{
    if (value > INT32_C(32767))
        return INT32_MAX;
    if (value < -INT32_C(32768))
        return INT32_MIN;
    return value * ASTRA_FIXED16_16_ONE;
}

AstraFixed16_16 astra_fixed_mul(AstraFixed16_16 left,
                                AstraFixed16_16 right)
{
    int64_t product = (int64_t)left * right;

    return clamp64(product / ASTRA_FIXED16_16_ONE);
}

AstraFixed16_16 astra_fixed_div(AstraFixed16_16 numerator,
                                AstraFixed16_16 denominator)
{
    if (denominator == 0)
        return numerator < 0 ? INT32_MIN : INT32_MAX;
    return clamp64((int64_t)numerator * ASTRA_FIXED16_16_ONE / denominator);
}

AstraFixed16_16 astra_fixed_sqrt(AstraFixed16_16 value)
{
    uint64_t bit = UINT64_C(1) << 62;
    uint64_t remainder;
    uint64_t root = 0;

    if (value <= 0)
        return 0;
    remainder = (uint64_t)(uint32_t)value << 16;
    while (bit > remainder)
        bit >>= 2;
    while (bit != 0) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root > INT32_MAX ? INT32_MAX : (int32_t)root;
}

void astra_fixed_sincos(uint32_t phase, AstraFraction2_30 *sine,
                        AstraFraction2_30 *cosine)
{
    int32_t x = INT32_C(652032874);
    int32_t y = 0;
    int32_t z = (int32_t)phase;
    int32_t sign = 1;
    unsigned index;

    if (z > INT32_C(0x40000000)) {
        z = (int32_t)(phase - UINT32_C(0x80000000));
        sign = -1;
    } else if (z < -INT32_C(0x40000000)) {
        z = (int32_t)(phase + UINT32_C(0x80000000));
        sign = -1;
    }
    for (index = 0; index < sizeof(cordic_angles) / sizeof(cordic_angles[0]);
         ++index) {
        int32_t shifted_x = shift_toward_zero(x, index);
        int32_t shifted_y = shift_toward_zero(y, index);
        int32_t next_x;

        if (z >= 0) {
            next_x = x - shifted_y;
            y += shifted_x;
            z -= cordic_angles[index];
        } else {
            next_x = x + shifted_y;
            y -= shifted_x;
            z += cordic_angles[index];
        }
        x = next_x;
    }
    if (sign < 0) {
        x = -x;
        y = -y;
    }
    if (sine != NULL)
        *sine = y;
    if (cosine != NULL)
        *cosine = x;
}

AstraFraction2_30 astra_fixed_sin(uint32_t phase)
{
    AstraFraction2_30 result;

    astra_fixed_sincos(phase, &result, NULL);
    return result;
}

AstraFraction2_30 astra_fixed_cos(uint32_t phase)
{
    AstraFraction2_30 result;

    astra_fixed_sincos(phase, NULL, &result);
    return result;
}
