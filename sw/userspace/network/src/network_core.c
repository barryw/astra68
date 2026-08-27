#include <astra/network_core.h>

#include <astra/bytes.h>

int astra_network_address_valid(const AstraNetworkAddress *address,
                                int allow_unspecified)
{
    if (address == NULL || address->size != sizeof(*address))
        return 0;
    if (address->family == ASTRA_NETWORK_FAMILY_UNSPEC)
        return allow_unspecified && address->port == 0u &&
               address->scope_id == 0u &&
               astra_words_zero((const uint32_t *)(const void *)address->address,
                                4u);
    if (address->family == ASTRA_NETWORK_FAMILY_IPV4) {
        if (address->scope_id != 0u)
            return 0;
        for (uint32_t index = 4u; index < sizeof(address->address); ++index)
            if (address->address[index] != 0u)
                return 0;
        return 1;
    }
    return address->family == ASTRA_NETWORK_FAMILY_IPV6;
}

AstraNetworkStatus astra_network_status_from_syscall(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK: return ASTRA_NETWORK_OK;
    case ASTRA_SYSCALL_WOULD_BLOCK: return ASTRA_NETWORK_WOULD_BLOCK;
    case ASTRA_SYSCALL_TIMED_OUT: return ASTRA_NETWORK_TIMED_OUT;
    case ASTRA_SYSCALL_CANCELLED: return ASTRA_NETWORK_CANCELLED;
    case ASTRA_SYSCALL_ACCESS_DENIED: return ASTRA_NETWORK_ACCESS;
    case ASTRA_SYSCALL_RESOURCE_LIMIT: return ASTRA_NETWORK_RESOURCE_LIMIT;
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ASTRA_NETWORK_OUT_OF_MEMORY;
    case ASTRA_SYSCALL_PEER_DEAD: return ASTRA_NETWORK_PEER_DEAD;
    case ASTRA_SYSCALL_BUFFER_TOO_SMALL:
        return ASTRA_NETWORK_BUFFER_TOO_SMALL;
    case ASTRA_SYSCALL_UNSUPPORTED:
    case ASTRA_SYSCALL_BAD_SYSCALL: return ASTRA_NETWORK_UNSUPPORTED;
    case ASTRA_SYSCALL_INVALID_ARGUMENT:
    case ASTRA_SYSCALL_INVALID_HANDLE:
    case ASTRA_SYSCALL_BAD_ADDRESS: return ASTRA_NETWORK_INVALID;
    default: return ASTRA_NETWORK_IO;
    }
}

int astra_network_shared_initialize(void *memory, uint32_t byte_size,
                                    uint32_t generation)
{
    AstraNetworkSharedHeader *header = memory;
    uint32_t slots;

    if (memory == NULL || generation == 0u ||
        byte_size <= ASTRA_NETWORK_SHARED_METADATA_BYTES ||
        byte_size > ASTRA_AREA_SIZE_MAX)
        return 0;
    slots = (byte_size - ASTRA_NETWORK_SHARED_METADATA_BYTES) /
            ASTRA_NETWORK_SLOT_BYTES;
    if (slots < 2u || sizeof(*header) + slots *
            sizeof(AstraNetworkSharedSlot) >
            ASTRA_NETWORK_SHARED_METADATA_BYTES)
        return 0;
    (void)memset(memory, 0, ASTRA_NETWORK_SHARED_METADATA_BYTES);
    header->magic = ASTRA_NETWORK_SHARED_MAGIC;
    header->version = ASTRA_NETWORK_SHARED_VERSION;
    header->structure_size = sizeof(*header);
    header->total_size = byte_size;
    header->generation = generation;
    header->slot_size = ASTRA_NETWORK_SLOT_BYTES;
    header->slot_count = slots;
    header->tx_slot_count = slots / 2u;
    header->rx_slot_count = slots - header->tx_slot_count;
    return 1;
}

int astra_network_shared_valid(const void *memory, uint32_t byte_size,
                               uint32_t generation)
{
    const AstraNetworkSharedHeader *header = memory;
    uint32_t slots;

    if (header == NULL || byte_size > ASTRA_AREA_SIZE_MAX ||
        byte_size <= ASTRA_NETWORK_SHARED_METADATA_BYTES || generation == 0u)
        return 0;
    slots = (byte_size - ASTRA_NETWORK_SHARED_METADATA_BYTES) /
            ASTRA_NETWORK_SLOT_BYTES;
    return header->magic == ASTRA_NETWORK_SHARED_MAGIC &&
           header->version == ASTRA_NETWORK_SHARED_VERSION &&
           header->structure_size == sizeof(*header) &&
           header->total_size == byte_size &&
           header->generation == generation &&
           header->slot_size == ASTRA_NETWORK_SLOT_BYTES &&
           header->slot_count == slots && slots >= 2u &&
           header->tx_slot_count != 0u &&
           header->tx_slot_count + header->rx_slot_count == slots &&
           sizeof(*header) + slots * sizeof(AstraNetworkSharedSlot) <=
               ASTRA_NETWORK_SHARED_METADATA_BYTES;
}

AstraNetworkSharedSlot *astra_network_shared_slots(void *memory)
{
    return memory == NULL ? NULL :
        (AstraNetworkSharedSlot *)((uint8_t *)memory +
                                   sizeof(AstraNetworkSharedHeader));
}

uint8_t *astra_network_shared_slot_bytes(void *memory, uint32_t slot)
{
    AstraNetworkSharedHeader *header = memory;

    if (header == NULL || header->magic != ASTRA_NETWORK_SHARED_MAGIC ||
        header->version != ASTRA_NETWORK_SHARED_VERSION ||
        slot >= header->slot_count)
        return NULL;
    return (uint8_t *)memory + ASTRA_NETWORK_SHARED_METADATA_BYTES +
           slot * header->slot_size;
}
