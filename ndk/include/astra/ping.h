#ifndef ASTRA_PING_H
#define ASTRA_PING_H

/**
 * @file ping.h
 * @brief The Internet checksum ICMP echo uses (network.library).
 */

#include <stdint.h>

#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/**
 * Compute the RFC 1071 Internet checksum of @p length bytes: the one's
 * complement of the one's-complement sum of big-endian 16-bit words, an odd
 * final byte padded with zero.
 *
 * @param bytes Message whose checksum field is zero.
 * @param length Message length in bytes.
 * @return The checksum, in host order, to store big-endian.
 */
uint16_t astra_ping_checksum(const void *bytes, uint32_t length);

ASTRA_EXTERN_C_END

#endif
