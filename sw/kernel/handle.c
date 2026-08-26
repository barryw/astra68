#include "handle.h"

#include "bytes.h"
#include "generation.h"
#include "object_cache.h"

#include <stddef.h>

/*
 * Object tables live in their own region above the frame metadata, not beside
 * the kernel image. Raising a limit then costs address space there and moves
 * no address anything else reads -- the trace ring especially, which crash
 * tooling finds at a fixed address. kernel.ld names this region if it fills.
 */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif


#define KERNEL_HANDLE_SLOT_BITS 8u
#define KERNEL_HANDLE_SLOT_MASK ((1u << KERNEL_HANDLE_SLOT_BITS) - 1u)
#define KERNEL_HANDLE_GENERATION_MASK 0x00ffffffu
#define KERNEL_HANDLE_BITMAP_BITS 32u
#define KERNEL_HANDLE_LAST_WORD_BITS \
    (((KERNEL_HANDLE_MAX_ENTRIES - 1u) % KERNEL_HANDLE_BITMAP_BITS) + 1u)
#define KERNEL_HANDLE_LAST_WORD_MASK \
    (UINT32_MAX >> (KERNEL_HANDLE_BITMAP_BITS - \
                    KERNEL_HANDLE_LAST_WORD_BITS))
#define KERNEL_DETACHED_SLOT_BITS 9u
#define KERNEL_DETACHED_SLOT_MASK \
    ((1u << KERNEL_DETACHED_SLOT_BITS) - 1u)
#define KERNEL_DETACHED_GENERATION_MASK 0x007fffffu

#define KERNEL_HANDLE_BATCH_EMPTY 0u
#define KERNEL_HANDLE_BATCH_PREPARED 1u
#define KERNEL_HANDLE_BATCH_COMMITTED 2u

_Static_assert(KERNEL_HANDLE_MAX_ENTRIES > 0u &&
                   KERNEL_HANDLE_MAX_ENTRIES <= 255u,
               "eight-bit handle slot requires 1..255 entries");

typedef enum KernelDetachedState {
    KERNEL_DETACHED_FREE = 0,
    KERNEL_DETACHED_RESERVED,
    KERNEL_DETACHED_LIVE
} KernelDetachedState;

typedef struct KernelHandleReleaseRecord {
    void *object;
    KernelHandleRelease release;
    void *context;
} KernelHandleReleaseRecord;

static uint8_t transfer_pool_corrupt;

static uint32_t handle_slot_word(uint32_t index)
{
    return index / KERNEL_HANDLE_BITMAP_BITS;
}

static uint32_t handle_slot_bit(uint32_t index)
{
    return 1u << (index % KERNEL_HANDLE_BITMAP_BITS);
}

static bool handle_slot_free(const KernelHandleTable *table, uint32_t index)
{
    return (table->free_slots[handle_slot_word(index)] &
            handle_slot_bit(index)) != 0u;
}

static bool claim_free_slot(KernelHandleTable *table, uint32_t *index,
                            KernelHandleEntry **entry)
{
    if (table == NULL || index == NULL || entry == NULL)
        return false;
    for (uint32_t word = 0u; word < KERNEL_HANDLE_BITMAP_WORDS; ++word) {
        uint32_t free_slots = table->free_slots[word];
        uint32_t selected;

        if (word + 1u == KERNEL_HANDLE_BITMAP_WORDS)
            free_slots &= KERNEL_HANDLE_LAST_WORD_MASK;
        if (free_slots == 0u)
            continue;
        selected = word * KERNEL_HANDLE_BITMAP_BITS +
                   (uint32_t)__builtin_ctz(free_slots);
        if (selected >= KERNEL_HANDLE_MAX_ENTRIES ||
            table->entries[selected].occupied != 0u ||
            table->entries[selected].reserved != 0u) {
            transfer_pool_corrupt = 1u;
            return false;
        }
        table->free_slots[word] &= ~handle_slot_bit(selected);
        *index = selected;
        *entry = &table->entries[selected];
        return true;
    }
    return false;
}

static bool release_free_slot(KernelHandleTable *table,
                              KernelHandleEntry *entry)
{
    uintptr_t address;
    uintptr_t first;
    uintptr_t limit;
    uint32_t index;
    uint32_t bit;
    uint32_t word;

    if (table == NULL || entry == NULL)
        return false;
    address = (uintptr_t)entry;
    first = (uintptr_t)&table->entries[0];
    limit = (uintptr_t)&table->entries[KERNEL_HANDLE_MAX_ENTRIES];
    if (address < first || address >= limit ||
        (address - first) % sizeof(table->entries[0]) != 0u)
        return false;
    index = (uint32_t)(entry - table->entries);
    word = handle_slot_word(index);
    bit = handle_slot_bit(index);
    if (entry->occupied != 0u || entry->reserved != 0u ||
        (table->free_slots[word] & bit) != 0u)
        return false;
    table->free_slots[word] |= bit;
    return true;
}

typedef struct KernelDetachedEntry {
    void *object;
    KernelHandleRetain retain;
    KernelHandleRelease release;
    void *release_context;
    uint32_t rights;
    uint32_t generation;
    uint16_t type;
    uint8_t state;
    uint8_t reserved;
} KernelDetachedEntry;

static KernelDetachedEntry detached_entries[KERNEL_HANDLE_DETACHED_MAX] KERNEL_TABLES;
static KernelObjectCache detached_cache;
static uint32_t detached_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_HANDLE_DETACHED_MAX)];
static KernelHandleTransferStats transfer_stats;

#if defined(__m68k__)
_Static_assert(sizeof(KernelHandleEntry) == 28u,
               "handle entry memory budget changed");
_Static_assert(sizeof(KernelDetachedEntry) == 28u,
               "detached handle memory budget changed");
#endif

static KernelHandle make_handle(uint32_t index, uint32_t generation)
{
    return (generation << KERNEL_HANDLE_SLOT_BITS) | (index + 1u);
}

static KernelDetachedHandle make_detached_handle(uint32_t index,
                                                 uint32_t generation)
{
    return (generation << KERNEL_DETACHED_SLOT_BITS) | (index + 1u);
}

static void clear_detached_entry(KernelDetachedEntry *entry)
{
    uint32_t generation = entry->generation;

    entry->object = NULL;
    entry->retain = NULL;
    entry->release = NULL;
    entry->release_context = NULL;
    entry->rights = 0u;
    entry->generation = generation;
    entry->type = KERNEL_OBJECT_NONE;
    entry->state = KERNEL_DETACHED_FREE;
    entry->reserved = 0u;
}

static void free_detached_entry(KernelDetachedEntry *entry)
{
    entry->generation = kernel_generation_next_masked(
        entry->generation, KERNEL_DETACHED_GENERATION_MASK);
    clear_detached_entry(entry);
    if (kernel_object_cache_release(&detached_cache, entry) !=
        KERNEL_OBJECT_CACHE_OK)
        transfer_pool_corrupt = 1u;
}

static KernelHandleStatus find_detached_entry(
    KernelDetachedHandle handle, uint8_t required_state,
    KernelDetachedEntry **entry)
{
    uint32_t slot = handle & KERNEL_DETACHED_SLOT_MASK;
    uint32_t generation = handle >> KERNEL_DETACHED_SLOT_BITS;

    if (entry == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *entry = NULL;
    if (slot == 0u || slot > KERNEL_HANDLE_DETACHED_MAX || generation == 0u)
        return KERNEL_HANDLE_INVALID_HANDLE;
    --slot;
    if (detached_entries[slot].generation != generation ||
        detached_entries[slot].state != required_state)
        return KERNEL_HANDLE_INVALID_HANDLE;
    *entry = &detached_entries[slot];
    return KERNEL_HANDLE_OK;
}

static uint32_t detached_live_count(void)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < KERNEL_HANDLE_DETACHED_MAX; ++index) {
        if (detached_entries[index].state == KERNEL_DETACHED_LIVE)
            ++count;
    }
    return count;
}

static uint32_t detached_reserved_count(void)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < KERNEL_HANDLE_DETACHED_MAX; ++index) {
        if (detached_entries[index].state == KERNEL_DETACHED_RESERVED)
            ++count;
    }
    return count;
}

static KernelHandleStatus find_entry(const KernelHandleTable *table,
                                     KernelHandle handle,
                                     const KernelHandleEntry **entry)
{
    uint32_t slot;
    uint32_t generation;

    if (table == NULL || entry == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    slot = handle & KERNEL_HANDLE_SLOT_MASK;
    generation = handle >> KERNEL_HANDLE_SLOT_BITS;
    if (slot == 0u || slot > KERNEL_HANDLE_MAX_ENTRIES || generation == 0u)
        return KERNEL_HANDLE_INVALID_HANDLE;
    --slot;
    if (table->entries[slot].occupied != 0u &&
        handle_slot_free(table, slot)) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    if (table->entries[slot].occupied == 0u ||
        table->entries[slot].reserved != 0u ||
        table->entries[slot].generation != generation)
        return KERNEL_HANDLE_INVALID_HANDLE;
    *entry = &table->entries[slot];
    return KERNEL_HANDLE_OK;
}

/*
 * find_entry for callers that are going to mutate what they find.
 *
 * The lookup and every validation it performs stay in find_entry and are not
 * duplicated here. Two call sites used to take the const result and cast the
 * qualifier away, which reads as "the const was inconvenient" and trains the
 * next reader to ignore it -- on the table that enforces capability rights,
 * which is the last place that should be true. Recovering the index from the
 * array the entry demonstrably came from needs no cast at all, so the compiler
 * keeps checking what the cast used to hide.
 */
static KernelHandleStatus find_entry_mutable(KernelHandleTable *table,
                                             KernelHandle handle,
                                             KernelHandleEntry **entry)
{
    const KernelHandleEntry *found = NULL;
    KernelHandleStatus status;

    if (table == NULL || entry == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    status = find_entry(table, handle, &found);
    if (status != KERNEL_HANDLE_OK)
        return status;
    *entry = &table->entries[found - table->entries];
    return KERNEL_HANDLE_OK;
}

static void invalidate_entry(KernelHandleTable *table,
                             KernelHandleEntry *entry,
                             KernelHandleReleaseRecord *record)
{
    if (record != NULL) {
        record->object = entry->object;
        record->release = entry->release;
        record->context = entry->release_context;
    }
    entry->object = NULL;
    entry->retain = NULL;
    entry->release = NULL;
    entry->release_context = NULL;
    entry->rights = 0u;
    entry->type = KERNEL_OBJECT_NONE;
    entry->occupied = 0u;
    entry->reserved = 0u;
    entry->generation = kernel_generation_next_masked(
        entry->generation, KERNEL_HANDLE_GENERATION_MASK);
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u,
                                   sizeof(*entry)))
        transfer_pool_corrupt = 1u;
    if (!release_free_slot(table, entry))
        transfer_pool_corrupt = 1u;
}

void kernel_handle_transfer_pool_init(void)
{
    if (!kernel_object_cache_init(
            &detached_cache, detached_entries,
            sizeof(detached_entries[0]), KERNEL_HANDLE_DETACHED_MAX,
            detached_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_HANDLE_DETACHED_MAX),
            KERNEL_ALLOCATION_SITE_DETACHED_HANDLE)) {
        transfer_pool_corrupt = 1u;
        return;
    }
    for (uint32_t index = 0u; index < KERNEL_HANDLE_DETACHED_MAX; ++index) {
        uint32_t generation = detached_entries[index].generation;

        clear_detached_entry(&detached_entries[index]);
        detached_entries[index].generation = generation == 0u ? 1u : generation;
    }
    kernel_bytes_clear(&transfer_stats, sizeof(transfer_stats));
    transfer_pool_corrupt = 0u;
}

void kernel_handle_table_init(KernelHandleTable *table)
{
    if (table == NULL)
        return;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        KernelHandleEntry *entry = &table->entries[index];

        entry->object = NULL;
        entry->retain = NULL;
        entry->release = NULL;
        entry->release_context = NULL;
        entry->rights = 0u;
        entry->generation = 1u;
        entry->type = KERNEL_OBJECT_NONE;
        entry->occupied = 0u;
        entry->reserved = 0u;
    }
    table->owner = 0u;
    /*
     * Whole words, then the tail. Not a conditional inside the loop: the mask
     * is all ones when the entry count is a multiple of the word size, and the
     * compiler calls that a branch with identical arms. Not a fill of every
     * word either, which it turns into a memset the kernel does not link.
     */
    for (uint32_t word = 0u; word + 1u < KERNEL_HANDLE_BITMAP_WORDS; ++word)
        table->free_slots[word] = UINT32_MAX;
    table->free_slots[KERNEL_HANDLE_BITMAP_WORDS - 1u] =
        KERNEL_HANDLE_LAST_WORD_MASK;
}

bool kernel_handle_table_set_owner(KernelHandleTable *table, uint32_t owner)
{
    if (table == NULL || owner == 0u || !kernel_handle_table_valid(table) ||
        kernel_handle_count(table) != 0u)
        return false;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        if (table->entries[index].reserved != 0u)
            return false;
    }
    table->owner = owner;
    return true;
}

KernelHandleStatus kernel_handle_clone_table(
    const KernelHandleTable *source, KernelHandleTable *destination)
{
    if (source == NULL || destination == NULL ||
        !kernel_handle_table_valid(source) ||
        !kernel_handle_table_valid(destination) ||
        destination->owner == 0u || kernel_handle_count(destination) != 0u)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        const KernelHandleEntry *from = &source->entries[index];
        KernelHandleEntry *to = &destination->entries[index];

        if (from->occupied == 0u || from->retain == NULL)
            continue;
        if (!handle_slot_free(destination, index) || to->occupied != 0u ||
            to->reserved != 0u ||
            !kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                                       destination->owner))
            goto full;
        destination->free_slots[handle_slot_word(index)] &=
            ~handle_slot_bit(index);
        if (!from->retain(from->object, from->release_context)) {
            destination->free_slots[handle_slot_word(index)] |=
                handle_slot_bit(index);
            kernel_allocation_fail(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                                   destination->owner);
            goto invalid;
        }
        to->object = from->object;
        to->retain = from->retain;
        to->release = from->release;
        to->release_context = from->release_context;
        to->rights = from->rights;
        to->generation = from->generation;
        to->type = from->type;
        to->occupied = 1u;
        to->reserved = 0u;
        if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                                      1u, sizeof(*to),
                                      destination->owner)) {
            to->occupied = 0u;
            destination->free_slots[handle_slot_word(index)] |=
                handle_slot_bit(index);
            if (from->release != NULL)
                from->release(from->object, from->release_context);
            goto corrupt;
        }
    }
    return KERNEL_HANDLE_OK;

full:
    (void)kernel_handle_close_all(destination);
    return KERNEL_HANDLE_TABLE_FULL;
invalid:
    (void)kernel_handle_close_all(destination);
    return KERNEL_HANDLE_INVALID_STATE;
corrupt:
    (void)kernel_handle_close_all(destination);
    return KERNEL_HANDLE_CORRUPT;
}

static KernelHandleStatus install(KernelHandleTable *table,
                                  KernelObjectType type, uint32_t rights,
                                  void *object, KernelHandleRetain retain,
                                  KernelHandleRelease release,
                                  void *release_context,
                                  KernelHandle *handle)
{
    KernelHandleEntry *entry;
    uint32_t index;

    if (table == NULL || object == NULL || handle == NULL ||
        type == KERNEL_OBJECT_NONE || rights == 0u)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *handle = KERNEL_HANDLE_INVALID;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                                   table->owner))
        return KERNEL_HANDLE_TABLE_FULL;
    if (!claim_free_slot(table, &index, &entry)) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                               table->owner);
        return transfer_pool_corrupt != 0u ? KERNEL_HANDLE_CORRUPT :
                                             KERNEL_HANDLE_TABLE_FULL;
    }
    if (entry->generation == 0u ||
        entry->generation > KERNEL_HANDLE_GENERATION_MASK)
        entry->generation = 1u;
    entry->object = object;
    entry->retain = retain;
    entry->release = release;
    entry->release_context = release_context;
    entry->rights = rights;
    entry->type = (uint16_t)type;
    entry->occupied = 1u;
    entry->reserved = 0u;
    if (!kernel_allocation_commit(
            KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u, sizeof(*entry),
            table->owner)) {
        entry->occupied = 0u;
        if (!release_free_slot(table, entry))
            transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    *handle = make_handle(index, entry->generation);
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_install(KernelHandleTable *table,
                                         KernelObjectType type,
                                         uint32_t rights, void *object,
                                         KernelHandleRelease release,
                                         void *release_context,
                                         KernelHandle *handle)
{
    return install(table, type, rights, object, NULL, release,
                   release_context, handle);
}

KernelHandleStatus kernel_handle_install_cloneable(
    KernelHandleTable *table, KernelObjectType type, uint32_t rights,
    void *object, KernelHandleRetain retain, KernelHandleRelease release,
    void *release_context, KernelHandle *handle)
{
    if (retain == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    return install(table, type, rights, object, retain, release,
                   release_context, handle);
}

/*
 * One object, two handles, and the second one may be in another process.
 *
 * This is the copy a launch is made of: the parent keeps what it had and the
 * child receives the same object with rights that are a subset. The two rules
 * that make that safe were already here for the same-table case and are the
 * whole of §1.1 of the launch design -- the source must carry TRANSFER, and
 * the requested rights must be a subset of the source's. A launch therefore
 * cannot produce authority that did not exist a moment earlier, and it cannot
 * do it by accident either.
 *
 * The allocation is charged to the destination table's owner, because that is
 * whose slot it is.
 */
KernelHandleStatus kernel_handle_duplicate_into(
    const KernelHandleTable *source_table, KernelHandle source,
    uint32_t rights, KernelHandleTable *table, KernelHandle *duplicate)
{
    const KernelHandleEntry *source_entry;
    KernelHandleEntry *destination;
    uint32_t destination_index = 0u;
    KernelHandleStatus status;

    if (source_table == NULL || table == NULL || duplicate == NULL ||
        rights == 0u)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *duplicate = KERNEL_HANDLE_INVALID;
    status = find_entry(source_table, source, &source_entry);
    if (status != KERNEL_HANDLE_OK)
        return status;
    if ((source_entry->rights & (1u << 5)) == 0u ||
        (rights & ~source_entry->rights) != 0u ||
        source_entry->retain == NULL)
        return KERNEL_HANDLE_ACCESS_DENIED;

    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                                   table->owner))
        return KERNEL_HANDLE_TABLE_FULL;

    if (!claim_free_slot(table, &destination_index, &destination)) {
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                               table->owner);
        return transfer_pool_corrupt != 0u ? KERNEL_HANDLE_CORRUPT :
                                             KERNEL_HANDLE_TABLE_FULL;
    }
    if (!source_entry->retain(source_entry->object,
                              source_entry->release_context)) {
        if (!release_free_slot(table, destination))
            transfer_pool_corrupt = 1u;
        kernel_allocation_fail(KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                               table->owner);
        return KERNEL_HANDLE_INVALID_STATE;
    }
    if (destination->generation == 0u ||
        destination->generation > KERNEL_HANDLE_GENERATION_MASK)
        destination->generation = 1u;
    destination->object = source_entry->object;
    destination->retain = source_entry->retain;
    destination->release = source_entry->release;
    destination->release_context = source_entry->release_context;
    destination->rights = rights;
    destination->type = source_entry->type;
    destination->occupied = 1u;
    destination->reserved = 0u;
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u,
                                  sizeof(*destination), table->owner)) {
        destination->occupied = 0u;
        if (!release_free_slot(table, destination))
            transfer_pool_corrupt = 1u;
        if (source_entry->release != NULL)
            source_entry->release(source_entry->object,
                                  source_entry->release_context);
        return KERNEL_HANDLE_CORRUPT;
    }
    *duplicate = make_handle(destination_index, destination->generation);
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_duplicate(KernelHandleTable *table,
                                           KernelHandle source,
                                           uint32_t rights,
                                           KernelHandle *duplicate)
{
    return kernel_handle_duplicate_into(table, source, rights, table,
                                        duplicate);
}

KernelHandleStatus kernel_handle_lookup(const KernelHandleTable *table,
                                        KernelHandle handle,
                                        KernelObjectType required_type,
                                        uint32_t required_rights,
                                        void **object)
{
    const KernelHandleEntry *entry;
    KernelHandleStatus status;

    if (object == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *object = NULL;
    status = find_entry(table, handle, &entry);
    if (status != KERNEL_HANDLE_OK)
        return status;
    if (required_type != KERNEL_OBJECT_NONE &&
        entry->type != (uint16_t)required_type)
        return KERNEL_HANDLE_TYPE_MISMATCH;
    if ((entry->rights & required_rights) != required_rights)
        return KERNEL_HANDLE_ACCESS_DENIED;
    *object = entry->object;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_lookup_any(const KernelHandleTable *table,
                                            KernelHandle handle,
                                            uint32_t required_rights,
                                            KernelObjectType *type,
                                            void **object)
{
    const KernelHandleEntry *entry;
    KernelHandleStatus status;

    if (type == NULL || object == NULL)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    *type = KERNEL_OBJECT_NONE;
    *object = NULL;
    status = find_entry(table, handle, &entry);
    if (status != KERNEL_HANDLE_OK)
        return status;
    if ((entry->rights & required_rights) != required_rights)
        return KERNEL_HANDLE_ACCESS_DENIED;
    if (entry->type == KERNEL_OBJECT_NONE || entry->object == NULL)
        return KERNEL_HANDLE_INVALID_HANDLE;
    *type = (KernelObjectType)entry->type;
    *object = entry->object;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_close(KernelHandleTable *table,
                                       KernelHandle handle)
{
    KernelHandleEntry *found;
    KernelHandleReleaseRecord record;
    KernelHandleStatus status = find_entry_mutable(table, handle, &found);

    if (status != KERNEL_HANDLE_OK)
        return status;
    invalidate_entry(table, found, &record);
    if (record.release != NULL)
        record.release(record.object, record.context);
    return KERNEL_HANDLE_OK;
}

uint32_t kernel_handle_close_all(KernelHandleTable *table)
{
    KernelHandleReleaseRecord records[KERNEL_HANDLE_MAX_ENTRIES];
    uint32_t count = 0u;

    if (table == NULL)
        return 0u;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        if (table->entries[index].occupied == 0u) {
            if (table->entries[index].reserved != 0u &&
                !kernel_allocation_release(
                    KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u,
                    sizeof(table->entries[index])))
                transfer_pool_corrupt = 1u;
            if (table->entries[index].reserved != 0u) {
                table->entries[index].reserved = 0u;
                if (!release_free_slot(table, &table->entries[index]))
                    transfer_pool_corrupt = 1u;
            }
            continue;
        }
        invalidate_entry(table, &table->entries[index], &records[count]);
        ++count;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        if (records[index].release != NULL)
            records[index].release(records[index].object,
                                   records[index].context);
    }
    return count;
}

uint32_t kernel_handle_close_type(KernelHandleTable *table,
                                  KernelObjectType type)
{
    uint32_t closed = 0u;

    if (table == NULL || type == KERNEL_OBJECT_NONE)
        return 0u;
    for (uint32_t slot = 0u; slot < KERNEL_HANDLE_MAX_ENTRIES; ++slot) {
        KernelHandleEntry *entry = &table->entries[slot];
        KernelHandle handle;

        if (entry->occupied == 0u || entry->type != (uint16_t)type)
            continue;
        handle = make_handle(slot, entry->generation);
        if (kernel_handle_close(table, handle) != KERNEL_HANDLE_OK)
            break;
        ++closed;
    }
    return closed;
}

uint32_t kernel_handle_count(const KernelHandleTable *table)
{
    uint32_t count = 0u;

    if (table == NULL)
        return 0u;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        if (table->entries[index].occupied != 0u)
            ++count;
    }
    return count;
}

uint32_t kernel_handle_available(const KernelHandleTable *table)
{
    uint32_t count = 0u;

    if (table == NULL)
        return 0u;
    for (uint32_t word = 0u; word < KERNEL_HANDLE_BITMAP_WORDS; ++word) {
        uint32_t free_slots = table->free_slots[word];

        if (word + 1u == KERNEL_HANDLE_BITMAP_WORDS)
            free_slots &= KERNEL_HANDLE_LAST_WORD_MASK;
        while (free_slots != 0u) {
            free_slots &= free_slots - 1u;
            ++count;
        }
    }
    return count;
}

bool kernel_handle_table_valid(const KernelHandleTable *table)
{
    /*
     * Cleared by the kernel's own helper rather than an initialiser: at four
     * words the compiler turns `= {0}` into a memset, and the kernel links no
     * C library to provide one.
     */
    uint32_t expected_free[KERNEL_HANDLE_BITMAP_WORDS];

    kernel_bytes_clear(expected_free, (uint32_t)sizeof(expected_free));
    if (table == NULL ||
        (table->free_slots[KERNEL_HANDLE_BITMAP_WORDS - 1u] &
         ~KERNEL_HANDLE_LAST_WORD_MASK) != 0u)
        return false;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_MAX_ENTRIES; ++index) {
        const KernelHandleEntry *entry = &table->entries[index];

        if (entry->occupied != 0u && entry->reserved != 0u)
            return false;
        if (entry->occupied == 0u && entry->reserved == 0u)
            expected_free[handle_slot_word(index)] |= handle_slot_bit(index);
    }
    for (uint32_t word = 0u; word < KERNEL_HANDLE_BITMAP_WORDS; ++word)
        if (table->free_slots[word] != expected_free[word])
            return false;
    return true;
}

static void reset_transfer_batch(KernelHandleTransferBatch *batch)
{
    for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index) {
        batch->detached[index] = 0u;
        batch->source[index] = KERNEL_HANDLE_INVALID;
    }
    batch->count = 0u;
    batch->state = KERNEL_HANDLE_BATCH_EMPTY;
    batch->reserved[0] = 0u;
    batch->reserved[1] = 0u;
}

KernelHandleStatus kernel_handle_transfer_prepare(
    const KernelHandleTable *source_table, const KernelHandle *source_handles,
    uint32_t count, uint32_t required_rights,
    KernelHandleTransferBatch *batch)
{
    const KernelHandleEntry *sources[KERNEL_HANDLE_TRANSFER_MAX];
    uint32_t allocated = 0u;

    if (source_table == NULL || source_handles == NULL || batch == NULL ||
        count == 0u || count > KERNEL_HANDLE_TRANSFER_MAX ||
        required_rights == 0u)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    reset_transfer_batch(batch);

    for (uint32_t index = 0u; index < count; ++index) {
        KernelHandleStatus status = find_entry(
            source_table, source_handles[index], &sources[index]);

        if (status != KERNEL_HANDLE_OK)
            return status;
        if ((sources[index]->rights & required_rights) != required_rights)
            return KERNEL_HANDLE_ACCESS_DENIED;
        if (sources[index]->object == NULL || sources[index]->release == NULL ||
            sources[index]->type == KERNEL_OBJECT_NONE)
            return KERNEL_HANDLE_CORRUPT;
        for (uint32_t prior = 0u; prior < index; ++prior) {
            if (source_handles[prior] == source_handles[index])
                return KERNEL_HANDLE_DUPLICATE;
        }
    }

    while (allocated < count) {
        void *raw_destination;
        uint16_t slot;
        KernelObjectCacheStatus cache_status = kernel_object_cache_claim(
            &detached_cache, 0u, &raw_destination, &slot);
        KernelDetachedEntry *destination;
        const KernelHandleEntry *source;

        if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
            break;
        if (cache_status != KERNEL_OBJECT_CACHE_OK ||
            slot >= KERNEL_HANDLE_DETACHED_MAX) {
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
        destination = raw_destination;
        if (destination->state != KERNEL_DETACHED_FREE) {
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
        if (destination->generation == 0u ||
            destination->generation > KERNEL_DETACHED_GENERATION_MASK)
            destination->generation = 1u;
        source = sources[allocated];
        destination->object = source->object;
        destination->retain = source->retain;
        destination->release = source->release;
        destination->release_context = source->release_context;
        destination->rights = source->rights;
        destination->type = source->type;
        destination->state = KERNEL_DETACHED_RESERVED;
        destination->reserved = 0u;
        batch->detached[allocated] = make_detached_handle(
            slot, destination->generation);
        batch->source[allocated] = source_handles[allocated];
        ++allocated;
    }
    if (allocated != count) {
        for (uint32_t index = 0u; index < allocated; ++index) {
            KernelDetachedEntry *entry = NULL;

            if (find_detached_entry(batch->detached[index],
                                    KERNEL_DETACHED_RESERVED,
                                    &entry) != KERNEL_HANDLE_OK) {
                transfer_pool_corrupt = 1u;
                return KERNEL_HANDLE_CORRUPT;
            }
            free_detached_entry(entry);
        }
        reset_transfer_batch(batch);
        ++transfer_stats.pool_exhaustions;
        return KERNEL_HANDLE_TRANSFER_POOL_FULL;
    }
    batch->count = (uint8_t)count;
    batch->state = KERNEL_HANDLE_BATCH_PREPARED;
    if (transfer_stats.reserved_detached >
        KERNEL_HANDLE_DETACHED_MAX - count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    transfer_stats.reserved_detached += count;
    ++transfer_stats.prepared_exports;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_transfer_commit_export(
    KernelHandleTable *source_table, KernelHandleTransferBatch *batch)
{
    KernelHandleEntry *sources[KERNEL_HANDLE_TRANSFER_MAX];
    KernelDetachedEntry *destinations[KERNEL_HANDLE_TRANSFER_MAX];

    if (source_table == NULL || batch == NULL ||
        batch->state != KERNEL_HANDLE_BATCH_PREPARED || batch->count == 0u ||
        batch->count > KERNEL_HANDLE_TRANSFER_MAX)
        return KERNEL_HANDLE_INVALID_ARGUMENT;

    for (uint32_t index = 0u; index < batch->count; ++index) {
        KernelHandleEntry *source = NULL;
        KernelDetachedEntry *destination = NULL;
        KernelHandleStatus status = find_entry_mutable(
            source_table, batch->source[index], &source);

        if (status != KERNEL_HANDLE_OK)
            return KERNEL_HANDLE_INVALID_STATE;
        status = find_detached_entry(batch->detached[index],
                                     KERNEL_DETACHED_RESERVED,
                                     &destination);
        if (status != KERNEL_HANDLE_OK || source->object != destination->object ||
            source->retain != destination->retain ||
            source->release != destination->release ||
            source->release_context != destination->release_context ||
            source->rights != destination->rights ||
            source->type != destination->type)
            return KERNEL_HANDLE_INVALID_STATE;
        sources[index] = source;
        destinations[index] = destination;
    }
    if (transfer_stats.reserved_detached < batch->count ||
        transfer_stats.live_detached >
            KERNEL_HANDLE_DETACHED_MAX - batch->count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }

    for (uint32_t index = 0u; index < batch->count; ++index) {
        invalidate_entry(source_table, sources[index], NULL);
        destinations[index]->state = KERNEL_DETACHED_LIVE;
    }
    transfer_stats.reserved_detached -= batch->count;
    transfer_stats.live_detached += batch->count;
    batch->state = KERNEL_HANDLE_BATCH_COMMITTED;
    ++transfer_stats.committed_exports;
    if (transfer_stats.live_detached > transfer_stats.max_live_detached)
        transfer_stats.max_live_detached = transfer_stats.live_detached;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_transfer_rollback(
    KernelHandleTransferBatch *batch)
{
    KernelDetachedEntry *entries[KERNEL_HANDLE_TRANSFER_MAX];

    if (batch == NULL || batch->state != KERNEL_HANDLE_BATCH_PREPARED ||
        batch->count == 0u || batch->count > KERNEL_HANDLE_TRANSFER_MAX)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < batch->count; ++index) {
        if (find_detached_entry(batch->detached[index],
                                KERNEL_DETACHED_RESERVED,
                                &entries[index]) != KERNEL_HANDLE_OK)
            return KERNEL_HANDLE_INVALID_STATE;
    }
    if (transfer_stats.reserved_detached < batch->count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    for (uint32_t index = 0u; index < batch->count; ++index)
        free_detached_entry(entries[index]);
    transfer_stats.reserved_detached -= batch->count;
    reset_transfer_batch(batch);
    ++transfer_stats.export_rollbacks;
    return KERNEL_HANDLE_OK;
}

static void reset_import_reservation(
    KernelHandleImportReservation *reservation)
{
    for (uint32_t index = 0u; index < KERNEL_HANDLE_TRANSFER_MAX; ++index) {
        reservation->handles[index] = KERNEL_HANDLE_INVALID;
        reservation->slots[index] = UINT8_MAX;
    }
    reservation->count = 0u;
    reservation->active = 0u;
    reservation->reserved[0] = 0u;
    reservation->reserved[1] = 0u;
}

KernelHandleStatus kernel_handle_import_reserve(
    KernelHandleTable *destination_table,
    const KernelDetachedHandle *detached, uint32_t count,
    KernelHandleImportReservation *reservation)
{
    uint32_t reserved = 0u;

    if (destination_table == NULL || reservation == NULL ||
        count > KERNEL_HANDLE_TRANSFER_MAX ||
        (count != 0u && detached == NULL))
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    reset_import_reservation(reservation);
    for (uint32_t index = 0u; index < count; ++index) {
        KernelDetachedEntry *entry = NULL;

        if (find_detached_entry(detached[index], KERNEL_DETACHED_LIVE,
                                &entry) != KERNEL_HANDLE_OK)
            return KERNEL_HANDLE_INVALID_HANDLE;
        (void)entry;
        for (uint32_t prior = 0u; prior < index; ++prior) {
            if (detached[prior] == detached[index])
                return KERNEL_HANDLE_DUPLICATE;
        }
    }
    if (kernel_handle_available(destination_table) < count)
        return KERNEL_HANDLE_TABLE_FULL;

    for (uint32_t slot = 0u;
         slot < KERNEL_HANDLE_MAX_ENTRIES && reserved < count; ++slot) {
        KernelHandleEntry *entry = &destination_table->entries[slot];

        if (entry->occupied != 0u || entry->reserved != 0u)
            continue;
        if (!handle_slot_free(destination_table, slot)) {
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
        if (!kernel_allocation_attempt(
                KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
                destination_table->owner)) {
            for (uint32_t rollback = 0u; rollback < reserved; ++rollback) {
                KernelHandleEntry *prior = &destination_table->entries[
                    reservation->slots[rollback]];

                prior->reserved = 0u;
                if (!release_free_slot(destination_table, prior))
                    transfer_pool_corrupt = 1u;
                if (!kernel_allocation_release(
                        KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u,
                        sizeof(*prior)))
                    transfer_pool_corrupt = 1u;
            }
            reset_import_reservation(reservation);
            return KERNEL_HANDLE_TABLE_FULL;
        }
        if (entry->generation == 0u ||
            entry->generation > KERNEL_HANDLE_GENERATION_MASK)
            entry->generation = 1u;
        destination_table->free_slots[handle_slot_word(slot)] &=
            ~handle_slot_bit(slot);
        entry->reserved = 1u;
        if (!kernel_allocation_commit(
                KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u, sizeof(*entry),
                destination_table->owner)) {
            entry->reserved = 0u;
            if (!release_free_slot(destination_table, entry))
                transfer_pool_corrupt = 1u;
            for (uint32_t rollback = 0u; rollback < reserved; ++rollback) {
                KernelHandleEntry *prior = &destination_table->entries[
                    reservation->slots[rollback]];

                prior->reserved = 0u;
                if (!release_free_slot(destination_table, prior) ||
                    !kernel_allocation_release(
                        KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u,
                        sizeof(*prior)))
                    transfer_pool_corrupt = 1u;
            }
            reset_import_reservation(reservation);
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
        reservation->slots[reserved] = (uint8_t)slot;
        reservation->handles[reserved] = make_handle(slot, entry->generation);
        ++reserved;
    }
    if (reserved != count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    reservation->count = (uint8_t)count;
    reservation->active = 1u;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_import_commit(
    KernelHandleTable *destination_table,
    KernelHandleImportReservation *reservation,
    const KernelDetachedHandle *detached)
{
    KernelDetachedEntry *sources[KERNEL_HANDLE_TRANSFER_MAX];

    if (destination_table == NULL || reservation == NULL ||
        reservation->active == 0u ||
        reservation->count > KERNEL_HANDLE_TRANSFER_MAX ||
        (reservation->count != 0u && detached == NULL))
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < reservation->count; ++index) {
        uint32_t slot = reservation->slots[index];
        KernelHandleEntry *destination;

        if (slot >= KERNEL_HANDLE_MAX_ENTRIES)
            return KERNEL_HANDLE_INVALID_STATE;
        destination = &destination_table->entries[slot];
        if (destination->occupied != 0u || destination->reserved != 1u ||
            handle_slot_free(destination_table, slot) ||
            reservation->handles[index] !=
                make_handle(slot, destination->generation) ||
            find_detached_entry(detached[index], KERNEL_DETACHED_LIVE,
                                &sources[index]) != KERNEL_HANDLE_OK)
            return KERNEL_HANDLE_INVALID_STATE;
    }
    if (transfer_stats.live_detached < reservation->count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }

    for (uint32_t index = 0u; index < reservation->count; ++index) {
        KernelHandleEntry *destination =
            &destination_table->entries[reservation->slots[index]];
        KernelDetachedEntry *source = sources[index];

        destination->object = source->object;
        destination->retain = source->retain;
        destination->release = source->release;
        destination->release_context = source->release_context;
        destination->rights = source->rights;
        destination->type = source->type;
        destination->occupied = 1u;
        destination->reserved = 0u;
        free_detached_entry(source);
    }
    transfer_stats.live_detached -= reservation->count;
    reset_import_reservation(reservation);
    ++transfer_stats.committed_imports;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_import_cancel(
    KernelHandleTable *destination_table,
    KernelHandleImportReservation *reservation)
{
    if (destination_table == NULL || reservation == NULL ||
        reservation->active == 0u ||
        reservation->count > KERNEL_HANDLE_TRANSFER_MAX)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < reservation->count; ++index) {
        uint32_t slot = reservation->slots[index];

        if (slot >= KERNEL_HANDLE_MAX_ENTRIES ||
            destination_table->entries[slot].occupied != 0u ||
            destination_table->entries[slot].reserved != 1u ||
            handle_slot_free(destination_table, slot))
            return KERNEL_HANDLE_INVALID_STATE;
    }
    for (uint32_t index = 0u; index < reservation->count; ++index) {
        KernelHandleEntry *entry =
            &destination_table->entries[reservation->slots[index]];

        entry->reserved = 0u;
        if (!release_free_slot(destination_table, entry)) {
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
        if (!kernel_allocation_release(
                KERNEL_ALLOCATION_SITE_HANDLE_SLOT, 1u, sizeof(*entry))) {
            transfer_pool_corrupt = 1u;
            return KERNEL_HANDLE_CORRUPT;
        }
    }
    reset_import_reservation(reservation);
    ++transfer_stats.import_rollbacks;
    return KERNEL_HANDLE_OK;
}

KernelHandleStatus kernel_handle_detached_release(
    const KernelDetachedHandle *detached, uint32_t count)
{
    KernelDetachedEntry *entries[KERNEL_HANDLE_TRANSFER_MAX];
    KernelHandleReleaseRecord releases[KERNEL_HANDLE_TRANSFER_MAX];

    if ((count != 0u && detached == NULL) ||
        count > KERNEL_HANDLE_TRANSFER_MAX)
        return KERNEL_HANDLE_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < count; ++index) {
        if (find_detached_entry(detached[index], KERNEL_DETACHED_LIVE,
                                &entries[index]) != KERNEL_HANDLE_OK)
            return KERNEL_HANDLE_INVALID_HANDLE;
        for (uint32_t prior = 0u; prior < index; ++prior) {
            if (detached[prior] == detached[index])
                return KERNEL_HANDLE_DUPLICATE;
        }
    }
    if (transfer_stats.live_detached < count) {
        transfer_pool_corrupt = 1u;
        return KERNEL_HANDLE_CORRUPT;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        releases[index].object = entries[index]->object;
        releases[index].release = entries[index]->release;
        releases[index].context = entries[index]->release_context;
        free_detached_entry(entries[index]);
    }
    transfer_stats.live_detached -= count;
    transfer_stats.released_detached += count;
    for (uint32_t index = 0u; index < count; ++index)
        releases[index].release(releases[index].object,
                                releases[index].context);
    return KERNEL_HANDLE_OK;
}

bool kernel_handle_detached_slot(KernelDetachedHandle detached,
                                 uint16_t *slot)
{
    KernelDetachedEntry *entry = NULL;
    uint32_t encoded_slot = detached & KERNEL_DETACHED_SLOT_MASK;

    if (slot == NULL ||
        find_detached_entry(detached, KERNEL_DETACHED_LIVE, &entry) !=
            KERNEL_HANDLE_OK ||
        encoded_slot == 0u || encoded_slot > KERNEL_HANDLE_DETACHED_MAX)
        return false;
    (void)entry;
    *slot = (uint16_t)(encoded_slot - 1u);
    return true;
}

bool kernel_handle_transfer_pool_healthy(void)
{
    return transfer_pool_corrupt == 0u;
}

bool kernel_handle_transfer_pool_valid(void)
{
    uint32_t live;
    uint32_t reserved;

    if (!kernel_handle_transfer_pool_healthy() ||
        !kernel_object_cache_valid(&detached_cache))
        return false;
    for (uint32_t index = 0u; index < KERNEL_HANDLE_DETACHED_MAX; ++index) {
        const KernelDetachedEntry *entry = &detached_entries[index];
        bool claimed = kernel_object_cache_slot_claimed(
            &detached_cache, (uint16_t)index);

        if (entry->generation == 0u ||
            entry->generation > KERNEL_DETACHED_GENERATION_MASK)
            return false;
        if (entry->state == KERNEL_DETACHED_FREE) {
            if (claimed || entry->object != NULL || entry->release != NULL ||
                entry->retain != NULL || entry->release_context != NULL ||
                entry->rights != 0u ||
                entry->type != KERNEL_OBJECT_NONE || entry->reserved != 0u)
                return false;
        } else if (entry->state == KERNEL_DETACHED_RESERVED ||
                   entry->state == KERNEL_DETACHED_LIVE) {
            if (!claimed || entry->object == NULL || entry->release == NULL ||
                entry->rights == 0u || entry->type == KERNEL_OBJECT_NONE ||
                entry->reserved != 0u)
                return false;
        } else {
            return false;
        }
    }
    live = detached_live_count();
    reserved = detached_reserved_count();
    return live == transfer_stats.live_detached &&
           reserved == transfer_stats.reserved_detached &&
           live <= KERNEL_HANDLE_DETACHED_MAX &&
           reserved <= KERNEL_HANDLE_DETACHED_MAX - live &&
           transfer_stats.max_live_detached >= live &&
           transfer_stats.max_live_detached <= KERNEL_HANDLE_DETACHED_MAX;
}

bool kernel_handle_transfer_stats(KernelHandleTransferStats *stats)
{
    if (stats == NULL || !kernel_handle_transfer_pool_valid())
        return false;
    kernel_bytes_copy(stats, &transfer_stats, sizeof(*stats));
    return true;
}
