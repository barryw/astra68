#ifndef ASTRA_NTP_H
#define ASTRA_NTP_H

#include <astra/compiler.h>
#include <astra/message_abi.h>
#include <astra/network.h>

/**
 * @file ntp.h
 * @brief Control protocol for the resident `ntpd`'s one-shot clock sync.
 *
 * This is not the NTP wire format: that packet (`ASTRA_NTP_PACKET_SIZE`,
 * `AstraNtpSample`, `astra_ntp_request`/`astra_ntp_response`) lives in
 * %ntp_client.h and travels over the network to a time server. What is here
 * is the local control exchange between a program such as the `ntp` command
 * and the resident service that owns the clock capability: "synchronize now,
 * optionally against this server, and tell me what you measured." See
 * docs/TIME.md for where this sits in the machine's clock chain.
 */

/** Launcher capability name for a send handle to the `ntpd` control port. */
#define ASTRA_CAPABILITY_NTP "NTP"
/** Native-big-endian `NTPC` control-protocol identifier. */
#define ASTRA_NTP_CONTROL_PROTOCOL UINT32_C(0x4e545043) /* NTPC */
/** Current control-protocol wire-format version. */
#define ASTRA_NTP_CONTROL_VERSION 1u
/** The control protocol's only operation: synchronize now. */
#define ASTRA_NTP_CONTROL_SYNC 1u

/** Request: synchronize the clock now, against an optional named server. */
typedef struct AstraNtpControlRequest {
    /** Common header; `operation` is ::ASTRA_NTP_CONTROL_SYNC. */
    AstraMessageHeader header;
    /** Null-terminated server to query, or an empty string for the
     *  service's own configured server list. */
    char server[ASTRA_NETWORK_NAME_MAX + 1u];
    /** Reserved for future use; always zero. */
    uint16_t reserved;
} AstraNtpControlRequest;

/** Reply: the result of one synchronize request. */
typedef struct AstraNtpControlReply {
    /** Common header, echoing the request's ::ASTRA_NTP_CONTROL_SYNC. */
    AstraMessageHeader header;
    /** AstraNtpStatus result; the other fields are valid only on success. */
    uint32_t status;
    /** High 32 bits of the realtime sample, nanoseconds since the Unix
     *  epoch. */
    uint32_t realtime_hi;
    /** Low 32 bits of the realtime sample. */
    uint32_t realtime_lo;
    /** High 32 bits of the measured round-trip time, in nanoseconds. */
    uint32_t round_trip_hi;
    /** Low 32 bits of the measured round-trip time. */
    uint32_t round_trip_lo;
    /** High 32 bits of the signed clock offset applied, in nanoseconds
     *  (two's complement across the hi/lo pair). */
    uint32_t offset_hi;
    /** Low 32 bits of the signed clock offset applied. */
    uint32_t offset_lo;
    /** NTP stratum of the server that answered. */
    uint32_t stratum;
} AstraNtpControlReply;

/** Wire size of AstraNtpControlRequest. */
#define ASTRA_NTP_CONTROL_REQUEST_SIZE 280u
/** Wire size of AstraNtpControlReply. */
#define ASTRA_NTP_CONTROL_REPLY_SIZE 56u
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNtpControlRequest) ==
                   ASTRA_NTP_CONTROL_REQUEST_SIZE,
               "NTP control request ABI changed");
_Static_assert(sizeof(AstraNtpControlReply) == ASTRA_NTP_CONTROL_REPLY_SIZE,
               "NTP control reply ABI changed");
/** @endcond */

#ifndef __ASSEMBLER__
/**
 * Ask the resident NTP service to synchronize the clock once, and block
 * for its reply.
 *
 * Sends an ::ASTRA_NTP_CONTROL_SYNC request to the service's control port
 * and waits for the answer; this is a single blocking round trip, not a
 * standing subscription.
 *
 * @param service Send-capability handle for the `ntpd` control port
 *                (typically from the launcher's ::ASTRA_CAPABILITY_NTP
 *                capability).
 * @param server Null-terminated hostname or address to query, or NULL to
 *               use the service's own configured server list.
 * @param reply Receives the control reply. Populated only once a reply
 *              message actually arrives; left untouched if the control
 *              port could not be created or the request could not be sent.
 * @return AstraNtpStatus result: ASTRA_NTP_OK on success, the service's own
 *         failure reason if it answered but could not synchronize, or
 *         ASTRA_NTP_IO/ASTRA_NTP_INVALID if the request could not be sent
 *         or its reply could not be parsed.
 */
uint32_t astra_ntp_sync(uint32_t service, const char *server,
                        AstraNtpControlReply *reply);
#endif

#endif
