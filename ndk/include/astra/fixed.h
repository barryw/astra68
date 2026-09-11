/** @file fixed.h @brief Deterministic fixed-point arithmetic. */
#ifndef ASTRA_FIXED_H
#define ASTRA_FIXED_H

#include <astra/types.h>

#include <stdint.h>

ASTRA_EXTERN_C_BEGIN

/** Signed 16.16 value used by game, animation, and physics code. */
typedef int32_t AstraFixed16_16;

/** Signed Q2.30 value returned by the trigonometric functions. */
typedef int32_t AstraFraction2_30;

/** 16.16 representation of one. */
#define ASTRA_FIXED16_16_ONE INT32_C(0x00010000)
/** Q2.30 representation of one. */
#define ASTRA_FRACTION2_30_ONE INT32_C(0x40000000)

/**
 * Convert an integer to 16.16, saturating when it is out of range.
 * @param value Integer to convert.
 * @return Saturated 16.16 value.
 */
AstraFixed16_16 astra_fixed_from_int(int32_t value);

/**
 * Multiply two 16.16 values with a saturating result.
 * @param left Left operand.
 * @param right Right operand.
 * @return Saturated 16.16 product.
 */
AstraFixed16_16 astra_fixed_mul(AstraFixed16_16 left,
                                AstraFixed16_16 right);
/**
 * Divide two 16.16 values with a saturating result.
 * @param numerator Dividend.
 * @param denominator Divisor; zero produces the sign-appropriate limit.
 * @return Saturated 16.16 quotient.
 */
AstraFixed16_16 astra_fixed_div(AstraFixed16_16 numerator,
                                AstraFixed16_16 denominator);

/**
 * Return the non-negative 16.16 square root; negative input returns zero.
 * @param value Radicand in 16.16 form.
 * @return Square root in 16.16 form.
 */
AstraFixed16_16 astra_fixed_sqrt(AstraFixed16_16 value);

/**
 * Compute sine and cosine from an unsigned binary-turn phase.
 *
 * Zero is zero degrees, 0x40000000 is 90 degrees, and wrap is exact. The
 * results are deterministic Q2.30 values and use no floating-point state.
 * @param phase Unsigned binary-turn phase.
 * @param sine Receives the Q2.30 sine when non-NULL.
 * @param cosine Receives the Q2.30 cosine when non-NULL.
 */
void astra_fixed_sincos(uint32_t phase, AstraFraction2_30 *sine,
                        AstraFraction2_30 *cosine);
/**
 * Compute sine for an unsigned binary-turn phase.
 * @param phase Unsigned binary-turn phase.
 * @return Sine in Q2.30 form.
 */
AstraFraction2_30 astra_fixed_sin(uint32_t phase);
/**
 * Compute cosine for an unsigned binary-turn phase.
 * @param phase Unsigned binary-turn phase.
 * @return Cosine in Q2.30 form.
 */
AstraFraction2_30 astra_fixed_cos(uint32_t phase);

ASTRA_EXTERN_C_END

#endif
