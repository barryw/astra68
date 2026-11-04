#ifndef ASTRA_NETWORK_CORE_H
#define ASTRA_NETWORK_CORE_H

#include <stdint.h>

#include <astra/network.h>

int astra_network_address_valid(const AstraNetworkAddress *address,
                                int allow_unspecified);
AstraNetworkStatus astra_network_ethernet_ipv4_validate(
    const AstraNetworkEthernetIPv4 *configuration);
#define ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX 112u
int astra_network_ethernet_profile_encode(
    const AstraNetworkEthernetIPv4 *profile,
    char text[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX]);
int astra_network_ethernet_profile_decode(
    const char *text, AstraNetworkEthernetIPv4 *profile);
int astra_network_mapping_valid(const AstraNetworkMapping *mapping,
                                int allow_new, int allow_status);
#define ASTRA_NETWORK_MAPPING_TEXT_MAX (6u + sizeof(AstraNetworkMapping) * 2u)
int astra_network_mapping_encode(const AstraNetworkMapping *mapping,
    char text[ASTRA_NETWORK_MAPPING_TEXT_MAX]);
int astra_network_mapping_decode(const char *text,
                                 AstraNetworkMapping *mapping);
AstraNetworkStatus astra_network_status_from_syscall(uint32_t status);
int astra_network_shared_initialize(void *memory, uint32_t byte_size,
                                    uint32_t generation);
int astra_network_shared_valid(const void *memory, uint32_t byte_size,
                               uint32_t generation);
AstraNetworkSharedSlot *astra_network_shared_slots(void *memory);
uint8_t *astra_network_shared_slot_bytes(void *memory, uint32_t slot);

#endif
