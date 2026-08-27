/* ICMP checksum loop adapted from Toybox ping.c; see ASTRA_VENDOR.md. */

#include <astra/ping_core.h>

#include <stddef.h>

uint16_t astra_ping_checksum(const void *bytes, uint32_t length)
{
    const uint8_t *at = bytes;
    uint32_t sum = 0u;

    while (length >= 2u) {
        sum += ((uint32_t)at[0] << 8) | at[1];
        at += 2;
        length -= 2u;
    }
    if (length != 0u)
        sum += (uint32_t)at[0] << 8;
    while ((sum >> 16) != 0u)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)~sum;
}
