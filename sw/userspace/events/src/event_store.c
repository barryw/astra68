/*
 * The event store: four rings, four budgets, and an account of what it threw
 * away.
 *
 * This file may not emit an event. That is structural rather than careful --
 * an events service that logs about logging is how a logging subsystem takes a
 * machine down -- and it is why nothing here includes event_emit.h.
 */

#include <astra/event_store.h>

#include <stddef.h>

#include <astra/event_catalog.h>

/*
 * Sixteenths of the budget. The boot ring is small because it holds only the
 * first events of a boot; detail is largest because `info` is the loudest level
 * that is stored at all.
 */
#define PRESENTED_SHARE 2u
#define RECORD_SHARE    6u
#define DETAIL_SHARE    6u
#define TOTAL_SHARE     16u
/* The boot ring is the remainder -- its two sixteenths plus every rounding
 * loss, so the whole budget is spent and no record is unreachable. */

static void
ring_init(AstraEventRing *ring, AstraEventStored *records, uint32_t capacity)
{
    ring->records = records;
    ring->capacity = capacity;
    ring->count = 0u;
    ring->oldest = 0u;
    ring->evicted = 0u;
}

/* The record displaced by this append, or NULL when nothing was. */
static const AstraEventStored *
ring_push(AstraEventRing *ring, const AstraEventDrained *event)
{
    const AstraEventStored *displaced = NULL;
    AstraEventStored *slot;
    uint32_t at;

    if (ring->capacity == 0u) {
        return NULL;
    }
    at = (ring->oldest + ring->count) % ring->capacity;
    if (ring->count == ring->capacity) {
        displaced = &ring->records[ring->oldest];
        ring->oldest = (ring->oldest + 1u) % ring->capacity;
        ++ring->evicted;
    } else {
        ++ring->count;
    }
    slot = &ring->records[at];
    slot->event = *event;
    slot->repeat = 1u;
    slot->last_timestamp_high = event->timestamp_high;
    slot->last_timestamp_low = event->timestamp_low;
    slot->reserved = 0u;
    return displaced;
}

int
astra_event_store_init(AstraEventStore *store, AstraEventStored *records,
                       uint32_t count, const AstraEventCatalog *catalog)
{
    uint32_t presented;
    uint32_t record;
    uint32_t detail;
    uint32_t boot;
    uint32_t index;

    if (store == NULL || records == NULL) {
        return 0;
    }
    for (index = 0u; index < ASTRA_EVENT_TIER_MAX; ++index) {
        ring_init(&store->tiers[index], NULL, 0u);
    }
    for (index = 0u; index < ASTRA_EVENT_SUBSYSTEM_MAX; ++index) {
        store->evicted_by_subsystem[index] = 0u;
    }
    store->catalog = catalog;
    store->dropped_debug = 0u;
    store->lost_in_transport = 0u;
    store->evicted_unattributed = 0u;
    store->stored = 0u;

    presented = (count * PRESENTED_SHARE) / TOTAL_SHARE;
    record = (count * RECORD_SHARE) / TOTAL_SHARE;
    detail = (count * DETAIL_SHARE) / TOTAL_SHARE;
    /* The remainder to the boot ring, which is the one that must never be 0. */
    boot = count - presented - record - detail;
    if (presented == 0u || record == 0u || detail == 0u || boot == 0u) {
        return 0;
    }
    ring_init(&store->tiers[ASTRA_EVENT_TIER_PRESENTED], records, presented);
    ring_init(&store->tiers[ASTRA_EVENT_TIER_RECORD], records + presented,
              record);
    ring_init(&store->tiers[ASTRA_EVENT_TIER_DETAIL],
              records + presented + record, detail);
    ring_init(&store->tiers[ASTRA_EVENT_TIER_BOOT],
              records + presented + record + detail, boot);
    return 1;
}

static uint32_t
tier_of(const AstraEventDrained *event)
{
    if ((event->flags & ASTRA_EVENT_FLAG_PRESENTED) != 0u) {
        return ASTRA_EVENT_TIER_PRESENTED;
    }
    if ((event->flags & ASTRA_EVENT_LEVEL_MASK) <= ASTRA_EVENT_LEVEL_INFO) {
        return ASTRA_EVENT_TIER_DETAIL;
    }
    return ASTRA_EVENT_TIER_RECORD;
}

void
astra_event_store_append(AstraEventStore *store, const AstraEventDrained *event)
{
    const AstraEventStored *displaced;
    AstraEventRing *boot;
    uint32_t tier;

    if (store == NULL || event == NULL) {
        return;
    }
    if ((event->flags & ASTRA_EVENT_LEVEL_MASK) == ASTRA_EVENT_LEVEL_DEBUG) {
        /* Live subscription only, dropped at drain. Counted, never stored. */
        ++store->dropped_debug;
        return;
    }

    tier = tier_of(event);
    displaced = ring_push(&store->tiers[tier], event);
    if (displaced != NULL) {
        /*
         * Whose history this cost. A drained record carries a message id and
         * not a subsystem -- the subsystem is static context and lives in the
         * descriptor -- so the attribution is a catalog lookup. Without a
         * catalog the eviction is still counted, as unattributed: the one
         * thing a reader must not be told about a gap is nothing.
         */
        const AstraEventDescriptor *record = astra_event_catalog_lookup(
            store->catalog, displaced->event.message);

        if (record != NULL && record->subsystem < ASTRA_EVENT_SUBSYSTEM_MAX) {
            ++store->evicted_by_subsystem[record->subsystem];
        } else {
            ++store->evicted_unattributed;
        }
    }
    ++store->stored;

    /*
     * The boot ring takes a copy while it has room, and then stops. It is
     * first-N rather than last-N on purpose: boot is when a debugger cannot be
     * attached and when the most expensive failures happen, and a ring that
     * kept the newest would throw exactly those away.
     */
    boot = &store->tiers[ASTRA_EVENT_TIER_BOOT];
    if (boot->count < boot->capacity) {
        (void)ring_push(boot, event);
    }
}

void
astra_event_store_lost(AstraEventStore *store, uint32_t records)
{
    if (store == NULL) {
        return;
    }
    if (store->lost_in_transport > UINT32_MAX - records) {
        store->lost_in_transport = UINT32_MAX;
        return;
    }
    store->lost_in_transport += records;
}

uint32_t
astra_event_store_count(const AstraEventStore *store, uint32_t tier)
{
    if (store == NULL || tier >= ASTRA_EVENT_TIER_MAX) {
        return 0u;
    }
    return store->tiers[tier].count;
}

const AstraEventStored *
astra_event_store_at(const AstraEventStore *store, uint32_t tier,
                     uint32_t index)
{
    const AstraEventRing *ring;

    if (store == NULL || tier >= ASTRA_EVENT_TIER_MAX) {
        return NULL;
    }
    ring = &store->tiers[tier];
    if (index >= ring->count) {
        return NULL;
    }
    return &ring->records[(ring->oldest + index) % ring->capacity];
}
