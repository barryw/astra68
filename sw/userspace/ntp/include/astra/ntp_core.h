#ifndef ASTRA_NTP_CORE_H
#define ASTRA_NTP_CORE_H

#include <stdint.h>

#define ASTRA_NTP_PACKET_SIZE 48u

typedef enum AstraNtpStatus {
    ASTRA_NTP_OK = 0,
    ASTRA_NTP_INVALID,
    ASTRA_NTP_RESOLVE,
    ASTRA_NTP_IO,
    ASTRA_NTP_TIMED_OUT,
    ASTRA_NTP_CONFIG,
    ASTRA_NTP_CLOCK
} AstraNtpStatus;

typedef struct AstraNtpSample {
    uint64_t realtime_ns;
    uint64_t round_trip_ns;
    int64_t offset_ns;
    uint8_t stratum;
} AstraNtpSample;

uint64_t astra_ntp_unix_ns_to_timestamp(uint64_t nanoseconds);
uint64_t astra_ntp_timestamp_to_unix_ns(uint64_t timestamp);
void astra_ntp_request(uint8_t packet[ASTRA_NTP_PACKET_SIZE],
                       uint64_t transmit_timestamp);
AstraNtpStatus astra_ntp_response(
    const uint8_t *packet, uint32_t length, uint64_t transmit_timestamp,
    uint64_t monotonic_send_ns, uint64_t monotonic_receive_ns,
    uint64_t realtime_send_ns, uint64_t realtime_receive_ns,
    AstraNtpSample *sample);
AstraNtpStatus astra_ntp_query(const char *server, AstraNtpSample *sample);
const char *astra_ntp_status_text(AstraNtpStatus status);

#endif
