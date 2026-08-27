/*
 * SNTP packet and clock math adapted from Toybox sntp.c.
 * Copyright 2019 Rob Landley <rob@landley.net>; 0BSD license.
 * See third_party/toybox/ASTRA_VENDOR.md.
 */

#include <astra/ntp_core.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define SEVENTIES UINT64_C(2208988800)

const char *astra_ntp_status_text(AstraNtpStatus status)
{
    switch (status) {
    case ASTRA_NTP_OK: return "time synchronized";
    case ASTRA_NTP_INVALID: return "invalid NTP response";
    case ASTRA_NTP_RESOLVE: return "could not resolve the time server";
    case ASTRA_NTP_IO: return "NTP network I/O failed";
    case ASTRA_NTP_TIMED_OUT: return "the time server did not reply";
    case ASTRA_NTP_CONFIG: return "configuration unavailable or invalid";
    case ASTRA_NTP_CLOCK: return "the system clock rejected the NTP update";
    }
    return "time synchronization failed";
}

static uint64_t get64(const uint8_t *bytes)
{
    uint64_t value = 0u;

    for (uint32_t at = 0u; at < 8u; ++at)
        value = (value << 8) | bytes[at];
    return value;
}

static void put64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t at = 0u; at < 8u; ++at)
        bytes[at] = (uint8_t)(value >> (56u - at * 8u));
}

uint64_t astra_ntp_unix_ns_to_timestamp(uint64_t nanoseconds)
{
    uint64_t seconds = nanoseconds / NANOSECONDS_PER_SECOND;
    uint64_t fraction = nanoseconds % NANOSECONDS_PER_SECOND;

    return ((seconds + SEVENTIES) << 32) |
           ((fraction << 32) / NANOSECONDS_PER_SECOND);
}

uint64_t astra_ntp_timestamp_to_unix_ns(uint64_t timestamp)
{
    uint64_t seconds = timestamp >> 32;
    uint64_t fraction = (uint32_t)timestamp;

    /* Toybox's 2036 era fixup covers Unix dates from 1968 through 2104. */
    if ((timestamp & (UINT64_C(1) << 63)) == 0u)
        seconds += UINT64_C(1) << 32;
    seconds -= SEVENTIES;
    return seconds * NANOSECONDS_PER_SECOND +
           (fraction * NANOSECONDS_PER_SECOND >> 32);
}

void astra_ntp_request(uint8_t packet[ASTRA_NTP_PACKET_SIZE],
                       uint64_t transmit_timestamp)
{
    memset(packet, 0, ASTRA_NTP_PACKET_SIZE);
    packet[0] = 0xe3u; /* leap alarm, NTPv4, client */
    packet[2] = 8u;
    put64(&packet[40], transmit_timestamp);
}

static int difference(uint64_t left, uint64_t right, int64_t *result)
{
    if (left >= right) {
        uint64_t magnitude = left - right;

        if (magnitude > (uint64_t)INT64_MAX)
            return 0;
        *result = (int64_t)magnitude;
    } else {
        uint64_t magnitude = right - left;

        if (magnitude > (uint64_t)INT64_MAX)
            return 0;
        *result = -(int64_t)magnitude;
    }
    return 1;
}

static int move(uint64_t value, int64_t difference_ns, uint64_t *result)
{
    if (difference_ns >= 0) {
        uint64_t amount = (uint64_t)difference_ns;

        if (value > UINT64_MAX - amount)
            return 0;
        *result = value + amount;
    } else {
        uint64_t amount = (uint64_t)(-(difference_ns + 1)) + 1u;

        if (amount > value)
            return 0;
        *result = value - amount;
    }
    return 1;
}

AstraNtpStatus astra_ntp_response(
    const uint8_t *packet, uint32_t length, uint64_t transmit_timestamp,
    uint64_t monotonic_send_ns, uint64_t monotonic_receive_ns,
    uint64_t realtime_send_ns, uint64_t realtime_receive_ns,
    AstraNtpSample *sample)
{
    uint8_t leap, mode, version, stratum;
    uint64_t server_receive, server_transmit;

    if (packet == NULL || sample == NULL || length < ASTRA_NTP_PACKET_SIZE ||
        monotonic_receive_ns < monotonic_send_ns)
        return ASTRA_NTP_INVALID;
    leap = packet[0] >> 6;
    version = (packet[0] >> 3) & 7u;
    mode = packet[0] & 7u;
    stratum = packet[1];
    if (leap == 3u || version < 3u || version > 4u || mode != 4u ||
        stratum == 0u || stratum > 15u ||
        get64(&packet[24]) != transmit_timestamp)
        return ASTRA_NTP_INVALID;
    server_receive = astra_ntp_timestamp_to_unix_ns(get64(&packet[32]));
    server_transmit = astra_ntp_timestamp_to_unix_ns(get64(&packet[40]));
    if (server_receive == 0u || server_transmit == 0u ||
        server_transmit < server_receive)
        return ASTRA_NTP_INVALID;

    memset(sample, 0, sizeof(*sample));
    sample->stratum = stratum;
    if (realtime_send_ns == 0u || realtime_receive_ns == 0u) {
        sample->round_trip_ns = monotonic_receive_ns - monotonic_send_ns;
        if (server_transmit > UINT64_MAX - sample->round_trip_ns / 2u)
            return ASTRA_NTP_INVALID;
        sample->realtime_ns = server_transmit + sample->round_trip_ns / 2u;
        return ASTRA_NTP_OK;
    }
    {
        int64_t receive_offset, transmit_offset;
        uint64_t local_elapsed = realtime_receive_ns - realtime_send_ns;
        uint64_t server_elapsed = server_transmit - server_receive;

        if (realtime_receive_ns < realtime_send_ns ||
            !difference(server_receive, realtime_send_ns, &receive_offset) ||
            !difference(server_transmit, realtime_receive_ns,
                        &transmit_offset))
            return ASTRA_NTP_INVALID;
        sample->offset_ns = receive_offset / 2 + transmit_offset / 2 +
            (receive_offset % 2 + transmit_offset % 2) / 2;
        sample->round_trip_ns = local_elapsed > server_elapsed ?
            local_elapsed - server_elapsed : 0u;
        if (!move(realtime_receive_ns, sample->offset_ns,
                  &sample->realtime_ns))
            return ASTRA_NTP_INVALID;
    }
    return ASTRA_NTP_OK;
}
