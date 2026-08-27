#ifndef ASTRA_NETWORK_CORE_H
#define ASTRA_NETWORK_CORE_H

#include <stdint.h>

#include <astra/network.h>

int astra_network_address_valid(const AstraNetworkAddress *address,
                                int allow_unspecified);
AstraNetworkStatus astra_network_status_from_syscall(uint32_t status);
int astra_network_shared_initialize(void *memory, uint32_t byte_size,
                                    uint32_t generation);
int astra_network_shared_valid(const void *memory, uint32_t byte_size,
                               uint32_t generation);
AstraNetworkSharedSlot *astra_network_shared_slots(void *memory);
uint8_t *astra_network_shared_slot_bytes(void *memory, uint32_t slot);

#endif
