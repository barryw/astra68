#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/event_catalog.h>
#include <astra/event_store.h>

#define RECORD_COUNT 64u

static AstraEventStored records[RECORD_COUNT];
static AstraEventStore store;
static AstraEventDescriptor catalog_records[2];
static AstraEventCatalog catalog;
static const uint32_t catalog_base = 0xE0000000u;

static AstraEventDrained
event_at(uint32_t sequence, uint16_t flags, uint32_t message)
{
    AstraEventDrained event;

    memset(&event, 0, sizeof(event));
    event.sequence = sequence;
    event.flags = flags;
    event.message = message;
    event.process = 0x10000011u;
    event.timestamp_low = sequence * 10u;
    return event;
}

static void reset(void)
{
    memset(records, 0, sizeof(records));
    memset(catalog_records, 0, sizeof(catalog_records));
    catalog_records[0].magic = ASTRA_EVENT_DESCRIPTOR_MAGIC;
    catalog_records[0].subsystem = ASTRA_EVENT_SUBSYSTEM_STORAGE;
    catalog_records[1].magic = ASTRA_EVENT_DESCRIPTOR_MAGIC;
    catalog_records[1].subsystem = ASTRA_EVENT_SUBSYSTEM_SHELL;
    assert(astra_event_catalog_init(&catalog, catalog_records,
                                    sizeof(catalog_records), catalog_base));
    assert(astra_event_store_init(&store, records, RECORD_COUNT, &catalog));
}

/* A level decides which ring, and the presented bit decides before it does. */
static void test_a_level_chooses_its_tier(void)
{
    AstraEventDrained event;

    reset();
    event = event_at(1u, ASTRA_EVENT_LEVEL_INFO, catalog_base);
    astra_event_store_append(&store, &event);
    event = event_at(2u, ASTRA_EVENT_LEVEL_ERROR, catalog_base);
    astra_event_store_append(&store, &event);
    event = event_at(3u, ASTRA_EVENT_LEVEL_NOTICE | ASTRA_EVENT_FLAG_PRESENTED,
                     catalog_base);
    astra_event_store_append(&store, &event);

    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_DETAIL) == 1u);
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_RECORD) == 1u);
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_PRESENTED) == 1u);
    /* The boot ring took a copy of every one of them. */
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_BOOT) == 3u);

    /*
     * debug never persists. It is a live subscription, which is what makes
     * leaving debug call sites compiled into shipping code affordable.
     */
    event = event_at(4u, ASTRA_EVENT_LEVEL_DEBUG, catalog_base);
    astra_event_store_append(&store, &event);
    assert(store.dropped_debug == 1u);
    assert(store.stored == 3u);
    for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
        for (uint32_t index = 0u;
             index < astra_event_store_count(&store, tier); ++index) {
            assert(astra_event_store_at(&store, tier, index)->event.sequence !=
                   4u);
        }
    }
}

/*
 * A tier evicts its own oldest and nobody else's. This is the whole reason
 * there are four rings: a subsystem in a retry loop at `info` must not be able
 * to reach the error you were looking for.
 */
static void test_a_loud_tier_cannot_reach_another(void)
{
    AstraEventDrained event;
    uint32_t detail_capacity;
    uint32_t sequence;

    reset();
    event = event_at(1u, ASTRA_EVENT_LEVEL_ERROR, catalog_base);
    astra_event_store_append(&store, &event);

    detail_capacity = store.tiers[ASTRA_EVENT_TIER_DETAIL].capacity;
    for (sequence = 2u; sequence < detail_capacity * 3u; ++sequence) {
        event = event_at(sequence, ASTRA_EVENT_LEVEL_INFO, catalog_base);
        astra_event_store_append(&store, &event);
    }

    /* The error is still there, and it is the oldest thing in its ring. */
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_RECORD) == 1u);
    assert(astra_event_store_at(&store, ASTRA_EVENT_TIER_RECORD,
                                0u)->event.sequence == 1u);

    /* The detail ring is full, evicted, and says by how much and whose. */
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_DETAIL) ==
           detail_capacity);
    assert(store.tiers[ASTRA_EVENT_TIER_DETAIL].evicted > 0u);
    assert(store.evicted_by_subsystem[ASTRA_EVENT_SUBSYSTEM_STORAGE] ==
           store.tiers[ASTRA_EVENT_TIER_DETAIL].evicted);
    assert(store.evicted_unattributed == 0u);

    /* Oldest first, and contiguous: a page through a ring skips nothing. */
    for (uint32_t index = 1u; index < detail_capacity; ++index) {
        const AstraEventStored *previous =
            astra_event_store_at(&store, ASTRA_EVENT_TIER_DETAIL, index - 1u);
        const AstraEventStored *current =
            astra_event_store_at(&store, ASTRA_EVENT_TIER_DETAIL, index);

        assert(current->event.sequence == previous->event.sequence + 1u);
    }
    assert(astra_event_store_at(&store, ASTRA_EVENT_TIER_DETAIL,
                                detail_capacity) == NULL);
}

/*
 * The boot ring keeps the *first* events of a boot, not the newest. Boot is
 * when a debugger cannot be attached, and a ring that kept the newest would
 * throw away exactly the events that explain a machine that did not come up.
 */
static void test_the_boot_ring_keeps_the_earliest(void)
{
    AstraEventDrained event;
    uint32_t capacity;
    uint32_t sequence;

    reset();
    capacity = store.tiers[ASTRA_EVENT_TIER_BOOT].capacity;
    for (sequence = 1u; sequence <= capacity * 4u; ++sequence) {
        event = event_at(sequence, ASTRA_EVENT_LEVEL_NOTICE, catalog_base);
        astra_event_store_append(&store, &event);
    }
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_BOOT) == capacity);
    assert(store.tiers[ASTRA_EVENT_TIER_BOOT].evicted == 0u);
    for (uint32_t index = 0u; index < capacity; ++index) {
        assert(astra_event_store_at(&store, ASTRA_EVENT_TIER_BOOT,
                                    index)->event.sequence == index + 1u);
    }
}

/* An eviction nobody can name is still an eviction, and still counted. */
static void test_loss_is_never_silent(void)
{
    AstraEventDrained event;
    uint32_t capacity;
    uint32_t sequence;

    reset();
    store.catalog = NULL;
    capacity = store.tiers[ASTRA_EVENT_TIER_RECORD].capacity;
    for (sequence = 1u; sequence <= capacity + 3u; ++sequence) {
        event = event_at(sequence, ASTRA_EVENT_LEVEL_WARNING, catalog_base);
        astra_event_store_append(&store, &event);
    }
    assert(store.evicted_unattributed == 3u);
    for (uint32_t index = 0u; index < ASTRA_EVENT_SUBSYSTEM_MAX; ++index) {
        assert(store.evicted_by_subsystem[index] == 0u);
    }

    /* What the kernel ring displaced before a drain reached it. */
    astra_event_store_lost(&store, 5u);
    astra_event_store_lost(&store, 7u);
    assert(store.lost_in_transport == 12u);
    astra_event_store_lost(&store, UINT32_MAX);
    assert(store.lost_in_transport == UINT32_MAX);
}

/* A budget that cannot give every ring a record is refused, not rounded. */
static void test_a_budget_too_small_is_refused(void)
{
    assert(!astra_event_store_init(&store, records, 4u, NULL));
    assert(astra_event_store_count(&store, ASTRA_EVENT_TIER_RECORD) == 0u);
    assert(astra_event_store_init(&store, records, RECORD_COUNT, NULL));
    /* Every record is reachable: the rings partition the array exactly. */
    {
        uint32_t total = 0u;

        for (uint32_t tier = 0u; tier < ASTRA_EVENT_TIER_MAX; ++tier) {
            total += store.tiers[tier].capacity;
        }
        assert(total == RECORD_COUNT);
    }
}

int main(void)
{
    test_a_level_chooses_its_tier();
    test_a_loud_tier_cannot_reach_another();
    test_the_boot_ring_keeps_the_earliest();
    test_loss_is_never_silent();
    test_a_budget_too_small_is_refused();
    puts("astra event store: PASS");
    return 0;
}
