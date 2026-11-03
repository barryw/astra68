#include <astra/network_core.h>
#include <astra/network_library.h>

#include <astra/bytes.h>

static int parse_ipv4(const char *text, uint8_t bytes[4])
{
    for (uint32_t part = 0u; part < 4u; ++part) {
        uint32_t value = 0u, digits = 0u;

        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text++ - '0');
            if (++digits > 3u || value > 255u)
                return 0;
        }
        if (digits == 0u || (part != 3u && *text != '.'))
            return 0;
        if (part != 3u)
            ++text;
        bytes[part] = (uint8_t)value;
    }
    return *text == '\0';
}

AstraNetworkStatus astra_network_address_parse(uint16_t family,
    const char *text, AstraNetworkAddress *address)
{
    AstraNetworkAddress parsed = {0};
    uint8_t *at = parsed.address, *compressed = NULL;
    const char *cursor;

    if (text == NULL || address == NULL)
        return ASTRA_NETWORK_INVALID;
    if (family != ASTRA_NETWORK_FAMILY_IPV4 &&
        family != ASTRA_NETWORK_FAMILY_IPV6)
        return ASTRA_NETWORK_UNSUPPORTED;
    parsed.size = sizeof(parsed);
    parsed.family = family;
    if (family == ASTRA_NETWORK_FAMILY_IPV4) {
        if (!parse_ipv4(text, parsed.address))
            return ASTRA_NETWORK_INVALID;
        *address = parsed;
        return ASTRA_NETWORK_OK;
    }
    cursor = text;
    if (*cursor == ':' && *++cursor != ':')
        return ASTRA_NETWORK_INVALID;
    while (*cursor != '\0') {
        const char *start = cursor;
        uint32_t value = 0u, digits = 0u;

        if (*cursor == ':') {
            if (compressed != NULL)
                return ASTRA_NETWORK_INVALID;
            compressed = at;
            ++cursor;
            if (*cursor == '\0')
                break;
            continue;
        }
        while ((*cursor >= '0' && *cursor <= '9') ||
               (*cursor >= 'a' && *cursor <= 'f') ||
               (*cursor >= 'A' && *cursor <= 'F')) {
            uint32_t digit = *cursor <= '9' ?
                (uint32_t)(*cursor - '0') :
                *cursor <= 'F' ? (uint32_t)(*cursor - 'A' + 10) :
                                 (uint32_t)(*cursor - 'a' + 10);
            value = (value << 4) | digit;
            ++cursor;
            if (++digits > 4u)
                return ASTRA_NETWORK_INVALID;
        }
        if (*cursor == '.' && at <= parsed.address + 12u) {
            uint8_t ipv4[4];

            if (!parse_ipv4(start, ipv4))
                return ASTRA_NETWORK_INVALID;
            (void)memcpy(at, ipv4, 4u);
            at += 4u;
            cursor += strlen(cursor);
            break;
        }
        if (digits == 0u || at > parsed.address + 14u)
            return ASTRA_NETWORK_INVALID;
        *at++ = (uint8_t)(value >> 8);
        *at++ = (uint8_t)value;
        if (*cursor == '\0')
            break;
        if (*cursor++ != ':')
            return ASTRA_NETWORK_INVALID;
    }
    if (compressed != NULL) {
        uint32_t tail = (uint32_t)(at - compressed);
        uint32_t gap = (uint32_t)((parsed.address + 16u) - at);

        if (gap == 0u)
            return ASTRA_NETWORK_INVALID;
        (void)memmove(compressed + gap, compressed, tail);
        (void)memset(compressed, 0, gap);
        at = parsed.address + 16u;
    }
    if (at != parsed.address + 16u)
        return ASTRA_NETWORK_INVALID;
    *address = parsed;
    return ASTRA_NETWORK_OK;
}

static int ipv4_unicast(const uint8_t address[4])
{
    return address[0] != 0u && address[0] < 224u;
}

AstraNetworkStatus astra_network_ethernet_ipv4_validate(
    const AstraNetworkEthernetIPv4 *configuration)
{
    uint32_t name_end = 0u;

    if (configuration == NULL || configuration->size !=
        sizeof(*configuration) ||
        configuration->dns_count > ASTRA_NETWORK_ETHERNET_DNS_MAX)
        return ASTRA_NETWORK_INVALID;
    while (name_end < sizeof(configuration->name) &&
           configuration->name[name_end] != '\0') {
        unsigned char character =
            (unsigned char)configuration->name[name_end++];
        if (character <= 32u || character == 127u ||
            character == '/' || character == ':')
            return ASTRA_NETWORK_INVALID;
    }
    if (name_end == 0u || name_end == sizeof(configuration->name))
        return ASTRA_NETWORK_INVALID;
    if (configuration->method == ASTRA_NETWORK_ETHERNET_IPV4_DHCP) {
        if (configuration->prefix != 0u ||
            memcmp(configuration->address, (uint8_t[4]){0},
                   sizeof(configuration->address)) != 0 ||
            memcmp(configuration->gateway, (uint8_t[4]){0},
                   sizeof(configuration->gateway)) != 0)
            return ASTRA_NETWORK_INVALID;
    } else if (configuration->method ==
               ASTRA_NETWORK_ETHERNET_IPV4_MANUAL) {
        if (!ipv4_unicast(configuration->address) ||
            configuration->prefix == 0u || configuration->prefix > 32u ||
            (memcmp(configuration->gateway, (uint8_t[4]){0},
                    sizeof(configuration->gateway)) != 0 &&
             !ipv4_unicast(configuration->gateway)))
            return ASTRA_NETWORK_INVALID;
    } else
        return ASTRA_NETWORK_INVALID;
    for (uint32_t at = 0u; at < ASTRA_NETWORK_ETHERNET_DNS_MAX; ++at) {
        if (at < configuration->dns_count ?
            !ipv4_unicast(configuration->dns[at]) :
            memcmp(configuration->dns[at], (uint8_t[4]){0},
                   sizeof(configuration->dns[at])) != 0)
            return ASTRA_NETWORK_INVALID;
    }
    return ASTRA_NETWORK_OK;
}

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
