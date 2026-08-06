#ifndef ASTRA_EVENT_STORE_H
#define ASTRA_EVENT_STORE_H

#include <stdint.h>

#include <astra/event.h>
#include <astra/event_catalog.h>

/*
 * What the events service keeps, and what it throws away first.
 *
 * A single bounded ring with drop-oldest solves the disk filling and leaves the
 * worse problem: one subsystem in a retry loop evicting the failure you were
 * looking for. So the store is four rings with four budgets, and a subsystem
 * screaming at `info` can evict only `info`.
 *
 * Level decides the ring, not time. Time-based expiry with no size bound fills
 * disks; a size bound with no structure loses signal to noise.
 *
 *   presented  what the person was shown -- the notification history
 *   record     notice, warning and error: the working history
 *   detail     info: the largest volume, the shortest life
 *   boot       the first events of this boot, whatever their level
 *
 * `debug` is not stored at all. It is a live subscription and is dropped at
 * drain, which is what makes leaving debug call sites compiled in affordable.
 *
 * An event lands in exactly one ring, and the presented bit wins over the
 * level: an event the person was shown is a notification first. The boot ring
 * is the deliberate exception and takes a copy, because the earliest events are
 * the most valuable and a first-in-first-out store evicts them first.
 */

#define ASTRA_EVENT_TIER_PRESENTED 0u
#define ASTRA_EVENT_TIER_RECORD    1u
#define ASTRA_EVENT_TIER_DETAIL    2u
#define ASTRA_EVENT_TIER_BOOT      3u
#define ASTRA_EVENT_TIER_MAX       4u

/*
 * One stored record: the drained event, plus the two fields coalescing needs.
 * They are written now and stay at one and zero until it exists, so the record
 * does not change shape on the day it does.
 */
typedef struct AstraEventStored {
    AstraEventDrained event;
    uint32_t repeat;             /* occurrences folded into this one */
    uint32_t last_timestamp_high;
    uint32_t last_timestamp_low;
    uint32_t reserved;
} AstraEventStored;

typedef struct AstraEventRing {
    AstraEventStored *records;
    uint32_t capacity;
    uint32_t count;
    uint32_t oldest;             /* index of the oldest record held */
    uint32_t evicted;            /* records this ring has dropped, ever */
} AstraEventRing;

typedef struct AstraEventStore {
    AstraEventRing tiers[ASTRA_EVENT_TIER_MAX];
    /*
     * Held to attribute an eviction. A drained record carries a message id;
     * which subsystem emitted it is static context, in the descriptor, so the
     * store needs the catalog to say whose history it just dropped. May be
     * NULL, and then evictions count as unattributed rather than not at all.
     */
    const AstraEventCatalog *catalog;
    /*
     * What was dropped and by whom. Eviction accounting is what turns the
     * provisional tier fractions and rate limits into a decision made from
     * evidence rather than from taste, so it is collected before anything
     * needs it.
     */
    uint32_t evicted_by_subsystem[ASTRA_EVENT_SUBSYSTEM_MAX];
    uint32_t evicted_unattributed; /* dropped, with no catalog to name whose */
    uint32_t dropped_debug;      /* never stored, by design; counted anyway */
    uint32_t lost_in_transport;  /* what the ring displaced before a drain */
    uint32_t stored;             /* records accepted, ever */
} AstraEventStore;

/*
 * Splits one caller-owned array into the four rings. The fractions are fixed --
 * a person sets one number and cannot produce an incoherent split, and a tiny
 * record tier beside a huge detail tier looks reasonable right up to the moment
 * you need yesterday's error.
 *
 * Returns 1, or 0 when `count` cannot give every ring at least one record: a
 * ring with no room is a tier that silently holds nothing.
 */
int astra_event_store_init(AstraEventStore *store, AstraEventStored *records,
                           uint32_t count, const AstraEventCatalog *catalog);

/* Files one drained event. Always accepted; what it displaces is counted. */
void astra_event_store_append(AstraEventStore *store,
                              const AstraEventDrained *event);

/* What a drain reported as displaced before it got there -- §6.2's loss. */
void astra_event_store_lost(AstraEventStore *store, uint32_t records);

uint32_t astra_event_store_count(const AstraEventStore *store, uint32_t tier);

/*
 * The `index`-th record of a tier, oldest first, or NULL past the end. Oldest
 * first because a story is read forwards, and because a reader that pages
 * through a growing ring should see what it already saw in the same order.
 */
const AstraEventStored *astra_event_store_at(const AstraEventStore *store,
                                             uint32_t tier, uint32_t index);

#endif
