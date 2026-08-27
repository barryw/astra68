#include <astra/ntp_core.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define SECOND UINT64_C(1000000000)

static void near(uint64_t actual, uint64_t expected)
{
    assert(actual >= expected - 1u && actual <= expected + 1u);
}

static void put64(uint8_t *out, uint64_t value)
{
    for (uint32_t at = 0u; at < 8u; ++at)
        out[at] = (uint8_t)(value >> (56u - at * 8u));
}

static void valid_reply(uint8_t packet[ASTRA_NTP_PACKET_SIZE],
                        uint64_t originate, uint64_t receive,
                        uint64_t transmit)
{
    memset(packet, 0, ASTRA_NTP_PACKET_SIZE);
    packet[0] = 0x24u; /* synchronized, NTPv4, server */
    packet[1] = 2u;
    put64(&packet[24], originate);
    put64(&packet[32], receive);
    put64(&packet[40], transmit);
}

int main(void)
{
    uint8_t request[ASTRA_NTP_PACKET_SIZE];
    uint8_t response[ASTRA_NTP_PACKET_SIZE];
    AstraNtpSample sample;
    const uint64_t unix_time = UINT64_C(1787700000) * SECOND;
    const uint64_t token = astra_ntp_unix_ns_to_timestamp(unix_time);
    const uint64_t server_receive =
        astra_ntp_unix_ns_to_timestamp(unix_time + 20u * 1000u * 1000u);
    const uint64_t server_transmit =
        astra_ntp_unix_ns_to_timestamp(unix_time + 21u * 1000u * 1000u);

    assert(strcmp(astra_ntp_status_text(ASTRA_NTP_CONFIG),
                  "configuration unavailable or invalid") == 0);

    astra_ntp_request(request, token);
    assert(request[0] == 0xe3u); /* unsynchronized, NTPv4, client */
    valid_reply(response, token, server_receive, server_transmit);

    /* Cold start: server transmit plus half the monotonic round trip. */
    assert(astra_ntp_response(response, sizeof(response), token,
                              100u * 1000u * 1000u,
                              160u * 1000u * 1000u, 0u, 0u,
                              &sample) == ASTRA_NTP_OK);
    near(sample.realtime_ns, unix_time + 51u * 1000u * 1000u);
    assert(sample.round_trip_ns == 60u * 1000u * 1000u);
    assert(sample.stratum == 2u);

    /* Synchronized path uses the four-timestamp NTP offset equation. */
    assert(astra_ntp_response(response, sizeof(response), token,
                              100u * 1000u * 1000u,
                              160u * 1000u * 1000u,
                              unix_time,
                              unix_time + 60u * 1000u * 1000u,
                              &sample) == ASTRA_NTP_OK);
    assert(sample.offset_ns >= -9500 * 1000 - 1 &&
           sample.offset_ns <= -9500 * 1000 + 1);
    near(sample.realtime_ns, unix_time + 50500u * 1000u);
    assert(sample.round_trip_ns == 59u * 1000u * 1000u);

    response[0] = 0xe4u; /* leap alarm */
    assert(astra_ntp_response(response, sizeof(response), token, 0u, 1u,
                              0u, 0u, &sample) == ASTRA_NTP_INVALID);
    valid_reply(response, token + 1u, server_receive, server_transmit);
    assert(astra_ntp_response(response, sizeof(response), token, 0u, 1u,
                              0u, 0u, &sample) == ASTRA_NTP_INVALID);
    assert(astra_ntp_response(response, ASTRA_NTP_PACKET_SIZE - 1u, token,
                              0u, 1u, 0u, 0u,
                              &sample) == ASTRA_NTP_INVALID);

    /* Toybox's era rule: both sides of the 2036 wrap remain Unix time. */
    assert(astra_ntp_timestamp_to_unix_ns(
               astra_ntp_unix_ns_to_timestamp(
                   UINT64_C(2085978495) * SECOND)) ==
           UINT64_C(2085978495) * SECOND);
    assert(astra_ntp_timestamp_to_unix_ns(
               astra_ntp_unix_ns_to_timestamp(
                   UINT64_C(2085978497) * SECOND)) ==
           UINT64_C(2085978497) * SECOND);
    return 0;
}
