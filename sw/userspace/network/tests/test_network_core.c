#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    uint8_t *memory = calloc(1u, ASTRA_NETWORK_SHARED_BYTES_MAX +
                                 ASTRA_NETWORK_SLOT_BYTES);
    AstraNetworkSharedHeader *header = (void *)memory;
    AstraNetworkSharedSlot *slots;

    assert(memory != NULL);
    assert(astra_network_shared_initialize(
        memory, ASTRA_NETWORK_SHARED_BYTES_MAX, 7u));
    assert(astra_network_shared_valid(
        memory, ASTRA_NETWORK_SHARED_BYTES_MAX, 7u));
    assert(header->slot_count == ASTRA_NETWORK_SHARED_SLOT_MAX);
    assert(header->tx_slot_count + header->rx_slot_count ==
           header->slot_count);
    slots = astra_network_shared_slots(memory);
    assert(slots != NULL &&
           slots[ASTRA_NETWORK_SHARED_SLOT_MAX - 1u].state ==
               ASTRA_NETWORK_SLOT_FREE);
    assert(astra_network_shared_slot_bytes(memory, 0u) == memory + 4096u);
    assert(astra_network_shared_slot_bytes(
               memory, ASTRA_NETWORK_SHARED_SLOT_MAX - 1u) +
               ASTRA_NETWORK_SLOT_BYTES <=
           memory + ASTRA_NETWORK_SHARED_BYTES_MAX);
    assert(astra_network_shared_slot_bytes(
               memory, ASTRA_NETWORK_SHARED_SLOT_MAX) == NULL);

    /* One more packet slot has no metadata descriptor and must be rejected. */
    assert(!astra_network_shared_initialize(
        memory, ASTRA_NETWORK_SHARED_BYTES_MAX + ASTRA_NETWORK_SLOT_BYTES,
        8u));

    header->slot_count = ASTRA_NETWORK_SHARED_SLOT_MAX + 1u;
    assert(!astra_network_shared_valid(
        memory, ASTRA_NETWORK_SHARED_BYTES_MAX, 7u));
    header->slot_count = ASTRA_NETWORK_SHARED_SLOT_MAX;
    header->tx_slot_count = 0u;
    assert(!astra_network_shared_valid(
        memory, ASTRA_NETWORK_SHARED_BYTES_MAX, 7u));
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

static void ethernet_profile_contract(void)
{
    AstraNetworkEthernetIPv4 before = {.size = sizeof(before),
        .name = "eth0", .method = ASTRA_NETWORK_ETHERNET_IPV4_MANUAL,
        .address = {192u, 168u, 1u, 52u}, .prefix = 24u,
        .gateway = {192u, 168u, 1u, 1u}, .dns_count = 1u,
        .dns = {{1u, 1u, 1u, 1u}}};
    AstraNetworkEthernetIPv4 after = {0};
    char text[ASTRA_NETWORK_ETHERNET_PROFILE_TEXT_MAX];
    assert(astra_network_ethernet_profile_encode(&before, text));
    assert(astra_network_ethernet_profile_decode(text, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    text[sizeof(text) - 2u] = 'z';
    assert(!astra_network_ethernet_profile_decode(text, &after));
}

static void mapping_contract(void)
{
    AstraNetworkMapping before = {.size = sizeof(before), .id = 9u,
        .flags = ASTRA_NETWORK_MAPPING_READ_ONLY,
        .source = {.format = ASTRA_VOLUME_SMB, .server = "files.example",
                   .share = "/stories", .label = "Stories"},
        .account = "reader", .domain = "WORKGROUP"};
    AstraNetworkMapping after = {0};
    char text[ASTRA_NETWORK_MAPPING_TEXT_MAX];

    assert(astra_network_mapping_valid(&before, 0, 0));
    assert(astra_network_mapping_encode(&before, text));
    assert(strlen(text) == sizeof(text) - 1u);
    assert(astra_network_mapping_decode(text, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    text[sizeof(text) - 2u] = 'z';
    assert(!astra_network_mapping_decode(text, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    before.id = 0u;
    assert(astra_network_mapping_valid(&before, 1, 0));
    assert(!astra_network_mapping_encode(&before, text));
    before.id = 9u;
    before.flags |= ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL;
    assert(astra_network_mapping_valid(&before, 0, 1));
    assert(!astra_network_mapping_valid(&before, 0, 0));
    before.flags &= ~ASTRA_NETWORK_MAPPING_HAS_CREDENTIAL;
    before.account[7] = 'x';
    assert(!astra_network_mapping_valid(&before, 0, 0));
    before.account[7] = '\0';
    before.source.format = ASTRA_VOLUME_NFS;
    assert(!astra_network_mapping_valid(&before, 0, 0));
    memset(before.account, 0, sizeof(before.account));
    memset(before.domain, 0, sizeof(before.domain));
    before.source.share[0] = '/';
    assert(astra_network_mapping_valid(&before, 0, 0));
}

int main(void)
{
    address_contract();
    shared_layout();
    status_contract();
    ethernet_profile_contract();
    mapping_contract();
    return 0;
}
