#ifndef ASTRA_NTP_CLIENT_H
#define ASTRA_NTP_CLIENT_H

/**
 * @file ntp_client.h
 * @brief SNTP queries and packet arithmetic (ntp.library).
 *
 * astra_ntp_query() asks one server for the time. The packet functions are
 * the same arithmetic for a caller that runs its own transport. Setting the
 * system clock is astra_ntp_sync() in %ntp.h, through the clock service.
 */

#include <stdint.h>

#include <astra/types.h>

ASTRA_EXTERN_C_BEGIN

/** Size of an SNTP request or response without extensions. */
#define ASTRA_NTP_PACKET_SIZE 48u

/** Outcome of an SNTP query or of parsing a response. */
typedef enum AstraNtpStatus {
    ASTRA_NTP_OK = 0,       /**< A valid sample. */
    ASTRA_NTP_INVALID,      /**< Bad argument, or a malformed or unsafe reply. */
    ASTRA_NTP_RESOLVE,      /**< The server name did not resolve. */
    ASTRA_NTP_IO,           /**< Socket I/O failed. */
    ASTRA_NTP_TIMED_OUT,    /**< No reply within the retry budget. */
    ASTRA_NTP_CONFIG,       /**< Configuration unavailable or invalid. */
    ASTRA_NTP_CLOCK         /**< The system clock rejected the update. */
} AstraNtpStatus;

/** One server's answer, in Unix nanoseconds. */
typedef struct AstraNtpSample {
    /** Best estimate of the current time when the reply arrived. */
    uint64_t realtime_ns;
    /** Network round trip, excluding the server's own processing. */
    uint64_t round_trip_ns;
    /** Server time minus local time; zero when no local clock was given. */
    int64_t offset_ns;
    /** Server stratum, 1 through 15. */
    uint8_t stratum;
} AstraNtpSample;

/**
 * Convert Unix nanoseconds to a 64-bit NTP timestamp (seconds since 1900 in
 * the high word, binary fraction in the low word).
 * @param nanoseconds Unix time in nanoseconds.
 * @return The NTP timestamp.
 */
uint64_t astra_ntp_unix_ns_to_timestamp(uint64_t nanoseconds);
/**
 * Convert an NTP timestamp to Unix nanoseconds, placing era-0 timestamps
 * after 2036 so the result covers 1968 through 2104.
 * @param timestamp NTP timestamp.
 * @return Unix time in nanoseconds.
 */
uint64_t astra_ntp_timestamp_to_unix_ns(uint64_t timestamp);
/**
 * Build an NTPv4 client request.
 * @param[out] packet Receives the request.
 * @param transmit_timestamp NTP timestamp the reply must echo back.
 */
void astra_ntp_request(uint8_t packet[ASTRA_NTP_PACKET_SIZE],
                       uint64_t transmit_timestamp);
/**
 * Validate a server reply and compute a sample.
 *
 * With both realtime readings zero, the sample is the server's transmit
 * time plus half the monotonic round trip; otherwise the four-timestamp
 * offset and round trip.
 * @param packet Reply bytes.
 * @param length Reply length.
 * @param transmit_timestamp The request's transmit timestamp.
 * @param monotonic_send_ns Monotonic clock when the request was sent.
 * @param monotonic_receive_ns Monotonic clock when the reply arrived.
 * @param realtime_send_ns Local realtime when sent, or zero.
 * @param realtime_receive_ns Local realtime when received, or zero.
 * @param[out] sample Receives the result.
 * @return ASTRA_NTP_OK or ASTRA_NTP_INVALID.
 */
AstraNtpStatus astra_ntp_response(
    const uint8_t *packet, uint32_t length, uint64_t transmit_timestamp,
    uint64_t monotonic_send_ns, uint64_t monotonic_receive_ns,
    uint64_t realtime_send_ns, uint64_t realtime_receive_ns,
    AstraNtpSample *sample);
/**
 * Query one server over UDP port 123, trying each resolved address with
 * three attempts of up to three seconds. Needs the NETWORK capability.
 * @param server Host name or address.
 * @param[out] sample Receives the result.
 * @return ASTRA_NTP_OK or the failure.
 */
AstraNtpStatus astra_ntp_query(const char *server, AstraNtpSample *sample);
/**
 * Describe a status in words.
 * @param status Any status.
 * @return A static string.
 */
const char *astra_ntp_status_text(AstraNtpStatus status);

ASTRA_EXTERN_C_END

#endif
