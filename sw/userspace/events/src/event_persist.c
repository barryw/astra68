/* The event store's versioned, checksummed Astra-native disk form. */

#include <astra/event_persist.h>

#include <stddef.h>

#include <astra/crc32.h>

#define SNAPSHOT_MAGIC       0x41455654u /* "AEVT" */
#define SNAPSHOT_VERSION     1u
#define SNAPSHOT_HEADER_SIZE 108u
#define SNAPSHOT_CRC_OFFSET  104u

_Static_assert(sizeof(AstraEventStored) == 72u,
               "durable event record size");

typedef struct SnapshotHeader {
    uint32_t boot;
    uint32_t generation;
    uint32_t count[ASTRA_EVENT_TIER_MAX];
    uint32_t catalog_crc;
    uint32_t evicted[ASTRA_EVENT_TIER_MAX];
    uint32_t evicted_by_subsystem[ASTRA_EVENT_SUBSYSTEM_MAX];
    uint32_t evicted_unattributed;
    uint32_t dropped_debug;
    uint32_t lost_in_transport;
    uint32_t stored;
    uint32_t crc;
} SnapshotHeader;

static uint16_t
get16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t
get32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void
put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void
put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t
catalog_crc(const AstraEventCatalog *catalog)
{
    return catalog != NULL ?
        astra_crc32(catalog->records,
                    catalog->count * (uint32_t)sizeof(AstraEventDescriptor)) :
        0u;
}

static void
encode_header(const AstraEventStore *store, uint32_t boot,
              uint32_t generation, uint32_t crc,
              uint8_t bytes[SNAPSHOT_HEADER_SIZE])
{
    uint32_t count = 0u;

    put32(bytes + 0u, SNAPSHOT_MAGIC);
    put16(bytes + 4u, SNAPSHOT_VERSION);
    put16(bytes + 6u, SNAPSHOT_HEADER_SIZE);
    put32(bytes + 8u, boot);
    put32(bytes + 12u, generation);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        count += store->tiers[tier].count;
    }
    put32(bytes + 16u, SNAPSHOT_HEADER_SIZE +
                         count * (uint32_t)sizeof(AstraEventStored));
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        put32(bytes + 20u + tier * 4u, store->tiers[tier].count);
    }
    put32(bytes + 36u, catalog_crc(store->catalog));
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        put32(bytes + 40u + tier * 4u, store->tiers[tier].evicted);
    }
    for (uint32_t subsystem = 0u;
         subsystem < ASTRA_EVENT_SUBSYSTEM_MAX; ++subsystem) {
        put32(bytes + 56u + subsystem * 4u,
              store->evicted_by_subsystem[subsystem]);
    }
    put32(bytes + 88u, store->evicted_unattributed);
    put32(bytes + 92u, store->dropped_debug);
    put32(bytes + 96u, store->lost_in_transport);
    put32(bytes + 100u, store->stored);
    put32(bytes + SNAPSHOT_CRC_OFFSET, crc);
}

static int
decode_header(SnapshotHeader *header,
              const uint8_t bytes[SNAPSHOT_HEADER_SIZE], uint32_t size)
{
    uint32_t count = 0u;
    uint32_t records;

    if (size < SNAPSHOT_HEADER_SIZE || get32(bytes + 0u) != SNAPSHOT_MAGIC ||
        get16(bytes + 4u) != SNAPSHOT_VERSION ||
        get16(bytes + 6u) != SNAPSHOT_HEADER_SIZE ||
        get32(bytes + 16u) != size ||
        (size - SNAPSHOT_HEADER_SIZE) % sizeof(AstraEventStored) != 0u) {
        return 0;
    }
    records = (size - SNAPSHOT_HEADER_SIZE) / sizeof(AstraEventStored);
    header->boot = get32(bytes + 8u);
    header->generation = get32(bytes + 12u);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        header->count[tier] = get32(bytes + 20u + tier * 4u);
        if (header->count[tier] > records - count) {
            return 0;
        }
        count += header->count[tier];
    }
    header->catalog_crc = get32(bytes + 36u);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        header->evicted[tier] = get32(bytes + 40u + tier * 4u);
    }
    for (uint32_t subsystem = 0u;
         subsystem < ASTRA_EVENT_SUBSYSTEM_MAX; ++subsystem) {
        header->evicted_by_subsystem[subsystem] =
            get32(bytes + 56u + subsystem * 4u);
    }
    header->evicted_unattributed = get32(bytes + 88u);
    header->dropped_debug = get32(bytes + 92u);
    header->lost_in_transport = get32(bytes + 96u);
    header->stored = get32(bytes + 100u);
    header->crc = get32(bytes + SNAPSHOT_CRC_OFFSET);
    return count == records && header->boot != 0u &&
           header->generation != 0u;
}

static int
valid_record(const AstraEventStored *record)
{
    return record->event.payload_length <= ASTRA_EVENT_ARGUMENT_MAX &&
           (record->event.flags & ~ASTRA_EVENT_FLAG_MASK) == 0u &&
           record->event.reserved == 0u && record->reserved == 0u;
}

int
astra_event_snapshot_probe(AstraEventSnapshotRead read, void *context,
                           uint32_t size, AstraEventSnapshotInfo *info)
{
    uint8_t header_bytes[SNAPSHOT_HEADER_SIZE];
    AstraEventStored record;
    SnapshotHeader header;
    uint32_t crc;
    uint32_t offset = SNAPSHOT_HEADER_SIZE;

    if (read == NULL || info == NULL ||
        !read(context, 0u, header_bytes, sizeof(header_bytes)) ||
        !decode_header(&header, header_bytes, size)) {
        return 0;
    }
    crc = astra_crc32_update(0xffffffffu, header_bytes,
                             SNAPSHOT_CRC_OFFSET);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        for (uint32_t index = 0u; index < header.count[tier]; ++index) {
            if (!read(context, offset, &record, sizeof(record)) ||
                !valid_record(&record)) {
                return 0;
            }
            crc = astra_crc32_update(crc, &record, sizeof(record));
            offset += sizeof(record);
        }
    }
    if (~crc != header.crc) {
        return 0;
    }
    info->boot = header.boot;
    info->generation = header.generation;
    info->catalog_crc = header.catalog_crc;
    return 1;
}

int
astra_event_snapshot_load(AstraEventStore *store, AstraEventSnapshotRead read,
                          void *context, uint32_t size,
                          AstraEventSnapshotInfo *info)
{
    uint8_t header_bytes[SNAPSHOT_HEADER_SIZE];
    SnapshotHeader header;
    AstraEventSnapshotInfo proven;
    uint32_t offset = SNAPSHOT_HEADER_SIZE;

    if (store == NULL || !astra_event_snapshot_probe(read, context, size,
                                                      &proven) ||
        !read(context, 0u, header_bytes, sizeof(header_bytes)) ||
        !decode_header(&header, header_bytes, size)) {
        return 0;
    }
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        AstraEventRing *ring = &store->tiers[tier];

        if (header.count[tier] > ring->capacity) {
            return 0;
        }
        ring->oldest = 0u;
        ring->count = header.count[tier];
        for (uint32_t index = 0u; index < ring->count; ++index) {
            if (!read(context, offset, &ring->records[index],
                      sizeof(AstraEventStored))) {
                return 0;
            }
            offset += sizeof(AstraEventStored);
        }
        ring->evicted = header.evicted[tier];
    }
    for (uint32_t subsystem = 0u;
         subsystem < ASTRA_EVENT_SUBSYSTEM_MAX; ++subsystem) {
        store->evicted_by_subsystem[subsystem] =
            header.evicted_by_subsystem[subsystem];
    }
    store->evicted_unattributed = header.evicted_unattributed;
    store->dropped_debug = header.dropped_debug;
    store->lost_in_transport = header.lost_in_transport;
    store->stored = header.stored;
    if (catalog_crc(store->catalog) != header.catalog_crc) {
        /* Honest numeric ids beat new-build text attached to old-build ids. */
        store->catalog = NULL;
    }
    if (info != NULL) {
        *info = proven;
    }
    return 1;
}

int
astra_event_snapshot_save(const AstraEventStore *store, uint32_t boot,
                          uint32_t generation, AstraEventSnapshotWrite write,
                          void *context)
{
    uint8_t header_bytes[SNAPSHOT_HEADER_SIZE];
    uint32_t crc;
    uint32_t offset = SNAPSHOT_HEADER_SIZE;

    if (store == NULL || write == NULL || boot == 0u || generation == 0u) {
        return 0;
    }
    encode_header(store, boot, generation, 0u, header_bytes);
    crc = astra_crc32_update(0xffffffffu, header_bytes,
                             SNAPSHOT_CRC_OFFSET);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        for (uint32_t index = 0u; index < store->tiers[tier].count; ++index) {
            crc = astra_crc32_update(
                crc, astra_event_store_at(store, tier, index),
                sizeof(AstraEventStored));
        }
    }
    encode_header(store, boot, generation, ~crc, header_bytes);
    if (!write(context, 0u, header_bytes, sizeof(header_bytes))) {
        return 0;
    }
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        for (uint32_t index = 0u; index < store->tiers[tier].count; ++index) {
            const AstraEventStored *record =
                astra_event_store_at(store, tier, index);

            if (!write(context, offset, record, sizeof(*record))) {
                return 0;
            }
            offset += sizeof(*record);
        }
    }
    return 1;
}
