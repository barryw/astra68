#ifndef ASTRA_NTP_H
#define ASTRA_NTP_H

#include <astra/message_abi.h>
#include <astra/network.h>

#define ASTRA_CAPABILITY_NTP "NTP"
#define ASTRA_NTP_CONTROL_PROTOCOL UINT32_C(0x4e545043) /* NTPC */
#define ASTRA_NTP_CONTROL_VERSION 1u
#define ASTRA_NTP_CONTROL_SYNC 1u

typedef struct AstraNtpControlRequest {
    AstraMessageHeader header;
    char server[ASTRA_NETWORK_NAME_MAX + 1u];
    uint16_t reserved;
} AstraNtpControlRequest;

typedef struct AstraNtpControlReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t realtime_hi;
    uint32_t realtime_lo;
    uint32_t round_trip_hi;
    uint32_t round_trip_lo;
    uint32_t offset_hi;
    uint32_t offset_lo;
    uint32_t stratum;
} AstraNtpControlReply;

#define ASTRA_NTP_CONTROL_REQUEST_SIZE 280u
#define ASTRA_NTP_CONTROL_REPLY_SIZE 56u
_Static_assert(sizeof(AstraNtpControlRequest) ==
                   ASTRA_NTP_CONTROL_REQUEST_SIZE,
               "NTP control request ABI changed");
_Static_assert(sizeof(AstraNtpControlReply) == ASTRA_NTP_CONTROL_REPLY_SIZE,
               "NTP control reply ABI changed");

#ifndef __ASSEMBLER__
uint32_t astra_ntp_sync(uint32_t service, const char *server,
                        AstraNtpControlReply *reply);
#endif

#endif
