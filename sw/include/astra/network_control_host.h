#ifndef ASTRA_NETWORK_CONTROL_HOST_H
#define ASTRA_NETWORK_CONTROL_HOST_H

#include <astra/network.h>
#include <stdint.h>

#define ASTRA_NETWORK_CONTROL_VERSION 1u

enum {
    ASTRA_NETWORK_CONTROL_PROBE = 1u,
    ASTRA_NETWORK_CONTROL_STAGE = 2u,
    ASTRA_NETWORK_CONTROL_CONFIRM = 3u,
    ASTRA_NETWORK_CONTROL_REVERT = 4u
};

/* Private AArch64 host socket protocol. No guest ABI or host pointer crosses it. */
typedef struct AstraNetworkControlMessage {
    uint32_t version;
    uint32_t operation;
    AstraNetworkEthernetIPv4 profile;
    uint32_t rollback_seconds;
    uint32_t error;
} AstraNetworkControlMessage;

_Static_assert(sizeof(AstraNetworkControlMessage) == 68u,
               "network control socket message changed");

#endif
