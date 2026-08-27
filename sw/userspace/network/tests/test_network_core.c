#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <astra/network_core.h>

static void address_contract(void)
{
    AstraNetworkAddress address = {0};

    assert(!astra_network_address_valid(NULL, 0));
    address.size = sizeof(address);
    assert(!astra_network_address_valid(&address, 0));
    assert(astra_network_address_valid(&address, 1));
    address.family = ASTRA_NETWORK_FAMILY_IPV4;
    address.address[0] = 127u;
    address.address[3] = 1u;
    assert(astra_network_address_valid(&address, 0));
    address.scope_id = 1u;
    assert(!astra_network_address_valid(&address, 0));
    address.scope_id = 0u;
    address.address[15] = 1u;
    assert(!astra_network_address_valid(&address, 0));
    address.family = ASTRA_NETWORK_FAMILY_IPV6;
    assert(astra_network_address_valid(&address, 0));
}

static void shared_layout(void)
{
    uint8_t *memory = calloc(1u, ASTRA_AREA_SIZE_MAX);
    AstraNetworkSharedHeader *header = (void *)memory;
    AstraNetworkSharedSlot *slots;

    assert(memory != NULL);
    assert(astra_network_shared_initialize(memory, ASTRA_AREA_SIZE_MAX, 7u));
    assert(astra_network_shared_valid(memory, ASTRA_AREA_SIZE_MAX, 7u));
    assert(header->slot_count == 63u);
    assert(header->tx_slot_count + header->rx_slot_count ==
           header->slot_count);
    slots = astra_network_shared_slots(memory);
    assert(slots != NULL && slots[62].state == ASTRA_NETWORK_SLOT_FREE);
    assert(astra_network_shared_slot_bytes(memory, 0u) == memory + 4096u);
    assert(astra_network_shared_slot_bytes(memory, 62u) +
               ASTRA_NETWORK_SLOT_BYTES <= memory + ASTRA_AREA_SIZE_MAX);
    assert(astra_network_shared_slot_bytes(memory, 63u) == NULL);
    header->slot_count = 64u;
    assert(!astra_network_shared_valid(memory, ASTRA_AREA_SIZE_MAX, 7u));
    header->slot_count = 63u;
    header->tx_slot_count = 0u;
    assert(!astra_network_shared_valid(memory, ASTRA_AREA_SIZE_MAX, 7u));
    free(memory);
}

static void status_contract(void)
{
    assert(astra_network_status_from_syscall(ASTRA_SYSCALL_OK) ==
           ASTRA_NETWORK_OK);
    assert(astra_network_status_from_syscall(ASTRA_SYSCALL_WOULD_BLOCK) ==
           ASTRA_NETWORK_WOULD_BLOCK);
    assert(astra_network_status_from_syscall(ASTRA_SYSCALL_PEER_DEAD) ==
           ASTRA_NETWORK_PEER_DEAD);
    assert(astra_network_status_from_syscall(0xffffffffu) ==
           ASTRA_NETWORK_IO);
}

int main(void)
{
    address_contract();
    shared_layout();
    status_contract();
    return 0;
}
