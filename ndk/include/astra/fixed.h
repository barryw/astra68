#ifndef ASTRA_FIXED_H
#define ASTRA_FIXED_H

#include <astra/types.h>

#include <stdint.h>

ASTRA_EXTERN_C_BEGIN

/** Signed 16.16 value used by game, animation, and physics code. */
typedef int32_t AstraFixed16_16;

/** Signed Q2.30 value returned by the trigonometric functions. */
typedef int32_t AstraFraction2_30;

#define ASTRA_FIXED16_16_ONE INT32_C(0x00010000)
#define ASTRA_FRACTION2_30_ONE INT32_C(0x40000000)

/** Convert an integer to 16.16, saturating when it is out of range. */
AstraFixed16_16 astra_fixed_from_int(int32_t value);

/** Multiply or divide 16.16 values with a saturating result. */
AstraFixed16_16 astra_fixed_mul(AstraFixed16_16 left,
                                AstraFixed16_16 right);
AstraFixed16_16 astra_fixed_div(AstraFixed16_16 numerator,
                                AstraFixed16_16 denominator);

/** Return the non-negative 16.16 square root; negative input returns zero. */
AstraFixed16_16 astra_fixed_sqrt(AstraFixed16_16 value);

/**
 * Compute sine and cosine from an unsigned binary-turn phase.
 *
 * Zero is zero degrees, 0x40000000 is 90 degrees, and wrap is exact. The
 * results are deterministic Q2.30 values and use no floating-point state.
 */
void astra_fixed_sincos(uint32_t phase, AstraFraction2_30 *sine,
                        AstraFraction2_30 *cosine);
AstraFraction2_30 astra_fixed_sin(uint32_t phase);
AstraFraction2_30 astra_fixed_cos(uint32_t phase);

ASTRA_EXTERN_C_END

#endif
