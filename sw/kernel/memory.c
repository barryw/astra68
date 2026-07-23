#include "memory.h"

#include <stddef.h>

#define KERNEL_ALLOC_POISON 0xa110ca7eu
#define KERNEL_FREE_POISON  0xfee1deadu
#define KERNEL_BITMAP_WORDS ((KERNEL_MAX_FRAMES + 31u) / 32u)
#define KERNEL_FRAME_INDEX_NONE UINT16_MAX

#if defined(__GNUC__)
#define KERNEL_NOINIT __attribute__((section(".noinit")))
#else
#define KERNEL_NOINIT
#endif

typedef struct KernelSavedRange {
    uint32_t base;
    uint32_t size;
    uint8_t state;
    uint8_t reserved[3];
} KernelSavedRange;

typedef struct KernelOwnerLedger {
    uint32_t owner;
    uint16_t head;
    uint16_t frame_count;
} KernelOwnerLedger;

/* Metadata is valid only when the matching dynamic bit is set. */
static KernelFrameInfo frames[KERNEL_MAX_FRAMES] KERNEL_NOINIT;
static uint16_t owner_next[KERNEL_MAX_FRAMES] KERNEL_NOINIT;
static uint16_t owner_previous[KERNEL_MAX_FRAMES] KERNEL_NOINIT;
static KernelOwnerLedger owner_ledgers[KERNEL_MAX_FRAME_OWNERS] KERNEL_NOINIT;
static uint32_t blocked_bitmap[KERNEL_BITMAP_WORDS] KERNEL_NOINIT;
static uint32_t dynamic_bitmap[KERNEL_BITMAP_WORDS] KERNEL_NOINIT;
static uint32_t classified_bitmap[KERNEL_BITMAP_WORDS] KERNEL_NOINIT;
static KernelSavedRange saved_ranges[ASTRA_BOOT_MAX_MEMORY_RANGES]
    KERNEL_NOINIT;
static uint32_t saved_range_count;
static KernelMemoryStats stats;
static bool initialized;

_Static_assert(sizeof(KernelFrameInfo) == 8u,
               "frame metadata must remain compact");
_Static_assert(sizeof(KernelOwnerLedger) == 8u,
               "owner ledger must remain compact");
_Static_assert(KERNEL_MAX_FRAMES <= UINT16_MAX,
               "owner frame links must represent every frame");

static bool is_power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool is_dynamic_state(KernelFrameState state)
{
    return state >= KERNEL_FRAME_PAGE_TABLE &&
           state <= KERNEL_FRAME_DEVICE;
}

static KernelFrameState boot_state(uint32_t type)
{
    switch (type) {
    case ASTRA_MEMORY_RANGE_USABLE:
        return KERNEL_FRAME_FREE;
    case ASTRA_MEMORY_RANGE_FIRMWARE:
        return KERNEL_FRAME_FIRMWARE;
    case ASTRA_MEMORY_RANGE_EARLY_LOG:
        return KERNEL_FRAME_EARLY_LOG;
    case ASTRA_MEMORY_RANGE_KERNEL:
        return KERNEL_FRAME_KERNEL;
    case ASTRA_MEMORY_RANGE_ROM_BACKING:
        return KERNEL_FRAME_ROM_BACKING;
    default:
        return KERNEL_FRAME_UNCLASSIFIED;
    }
}

static void reset_frame(KernelFrameInfo *frame, KernelFrameState state)
{
    frame->owner = KERNEL_OWNER_NONE;
    frame->references = 0u;
    frame->pins = 0u;
    frame->state = (uint8_t)state;
}

static void reset_stats(void)
{
    stats.ram_base = 0u;
    stats.total_frames = 0u;
    stats.free_frames = 0u;
    stats.high_water_frames = 0u;
    stats.allocation_failures = 0u;
    stats.owner_slots_used = 0u;
    stats.owner_release_operations = 0u;
    stats.owner_release_frame_visits = 0u;
}

static bool bitmap_test(const uint32_t *bitmap, uint32_t index)
{
    return (bitmap[index >> 5] & (1u << (index & 31u))) != 0u;
}

static void bitmap_set(uint32_t *bitmap, uint32_t index, bool value)
{
    uint32_t mask = 1u << (index & 31u);

    if (value)
        bitmap[index >> 5] |= mask;
    else
        bitmap[index >> 5] &= ~mask;
}

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static bool find_owner_slot(uint32_t owner, uint32_t *slot)
{
    for (uint32_t index = 0u; index < KERNEL_MAX_FRAME_OWNERS; ++index) {
        if (owner_ledgers[index].owner == owner) {
            *slot = index;
            return true;
        }
    }
    return false;
}

static bool owner_slot_for_allocation(uint32_t owner, uint32_t frame_count,
                                      uint32_t *slot)
{
    uint32_t empty = KERNEL_MAX_FRAME_OWNERS;

    for (uint32_t index = 0u; index < KERNEL_MAX_FRAME_OWNERS; ++index) {
        if (owner_ledgers[index].owner == owner) {
            uint32_t available =
                (uint32_t)UINT16_MAX - owner_ledgers[index].frame_count;

            if (frame_count > available)
                return false;
            *slot = index;
            return true;
        }
        if (empty == KERNEL_MAX_FRAME_OWNERS &&
            owner_ledgers[index].owner == KERNEL_OWNER_NONE)
            empty = index;
    }
    if (empty == KERNEL_MAX_FRAME_OWNERS)
        return false;
    *slot = empty;
    return true;
}

static void link_owner_frame(uint32_t slot, uint32_t frame_index,
                             uint32_t owner)
{
    KernelOwnerLedger *ledger = &owner_ledgers[slot];
    uint16_t old_head;

    if (ledger->owner == KERNEL_OWNER_NONE) {
        ledger->owner = owner;
        ledger->head = KERNEL_FRAME_INDEX_NONE;
        ledger->frame_count = 0u;
        ++stats.owner_slots_used;
    }
    old_head = ledger->head;
    owner_previous[frame_index] = KERNEL_FRAME_INDEX_NONE;
    owner_next[frame_index] = old_head;
    if (old_head != KERNEL_FRAME_INDEX_NONE)
        owner_previous[old_head] = (uint16_t)frame_index;
    ledger->head = (uint16_t)frame_index;
    ++ledger->frame_count;
}

static bool unlink_owner_frame(uint32_t slot, uint32_t frame_index)
{
    KernelOwnerLedger *ledger = &owner_ledgers[slot];
    uint16_t previous;
    uint16_t next;

    if (frame_index >= stats.total_frames || ledger->frame_count == 0u ||
        ledger->owner == KERNEL_OWNER_NONE)
        return false;
    previous = owner_previous[frame_index];
    next = owner_next[frame_index];
    if (previous == KERNEL_FRAME_INDEX_NONE) {
        if (ledger->head != frame_index)
            return false;
    } else {
        if (previous >= stats.total_frames ||
            owner_next[previous] != frame_index)
            return false;
    }
    if (next != KERNEL_FRAME_INDEX_NONE) {
        if (next >= stats.total_frames ||
            owner_previous[next] != frame_index)
            return false;
    }
    if (previous == KERNEL_FRAME_INDEX_NONE)
        ledger->head = next;
    else
        owner_next[previous] = next;
    if (next != KERNEL_FRAME_INDEX_NONE)
        owner_previous[next] = previous;
    owner_previous[frame_index] = KERNEL_FRAME_INDEX_NONE;
    owner_next[frame_index] = KERNEL_FRAME_INDEX_NONE;
    --ledger->frame_count;
    if (ledger->frame_count == 0u) {
        ledger->owner = KERNEL_OWNER_NONE;
        ledger->head = KERNEL_FRAME_INDEX_NONE;
        --stats.owner_slots_used;
    }
    return true;
}

static uint32_t bitmap_mask(uint32_t bit, uint32_t count)
{
    if (count == 32u)
        return UINT32_MAX;
    return ((1u << count) - 1u) << bit;
}

static void bitmap_assign_range(uint32_t *bitmap, uint32_t first,
                                uint32_t count, bool value)
{
    uint32_t end = first + count;

    while (first < end) {
        uint32_t word = first >> 5;
        uint32_t bit = first & 31u;
        uint32_t chunk = 32u - bit;
        uint32_t mask;

        if (chunk > end - first)
            chunk = end - first;
        mask = bitmap_mask(bit, chunk);
        if (value)
            bitmap[word] |= mask;
        else
            bitmap[word] &= ~mask;
        first += chunk;
    }
}

static bool classify_range(uint32_t first, uint32_t count, bool usable)
{
    uint32_t end = first + count;

    while (first < end) {
        uint32_t word = first >> 5;
        uint32_t bit = first & 31u;
        uint32_t chunk = 32u - bit;
        uint32_t mask;

        if (chunk > end - first)
            chunk = end - first;
        mask = bitmap_mask(bit, chunk);
        if ((classified_bitmap[word] & mask) != 0u)
            return false;
        classified_bitmap[word] |= mask;
        if (usable)
            blocked_bitmap[word] &= ~mask;
        first += chunk;
    }
    return true;
}

static bool all_frames_classified(void)
{
    uint32_t full_words = stats.total_frames >> 5;
    uint32_t tail = stats.total_frames & 31u;

    for (uint32_t word = 0u; word < full_words; ++word) {
        if (classified_bitmap[word] != UINT32_MAX)
            return false;
    }
    if (tail != 0u) {
        uint32_t mask = (1u << tail) - 1u;
        if ((classified_bitmap[full_words] & mask) != mask)
            return false;
    }
    return true;
}

static bool address_to_index(uint32_t physical_address, uint32_t *index)
{
    uint32_t offset;

    if (!initialized || physical_address < stats.ram_base)
        return false;
    offset = physical_address - stats.ram_base;
    if ((offset & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        offset / KERNEL_PAGE_SIZE >= stats.total_frames)
        return false;
    *index = offset / KERNEL_PAGE_SIZE;
    return true;
}

static bool frame_range(uint32_t physical_base, uint32_t frame_count,
                        uint32_t *first)
{
    uint32_t index;

    if (frame_count == 0u || !address_to_index(physical_base, &index) ||
        frame_count > stats.total_frames - index)
        return false;
    *first = index;
    return true;
}

static bool byte_range(uint32_t physical_base, uint32_t byte_count,
                       uint32_t *first, uint32_t *count)
{
    uint64_t end;
    uint32_t first_index;
    uint32_t last_index;

    if (!initialized || byte_count == 0u ||
        physical_base < stats.ram_base)
        return false;
    end = (uint64_t)physical_base + byte_count;
    if (end > (uint64_t)stats.ram_base +
                  (uint64_t)stats.total_frames * KERNEL_PAGE_SIZE)
        return false;
    first_index = (physical_base - stats.ram_base) >> KERNEL_PAGE_SHIFT;
    last_index = (uint32_t)(end - 1u - stats.ram_base) >> KERNEL_PAGE_SHIFT;
    *first = first_index;
    *count = last_index - first_index + 1u;
    return true;
}

static void poison(uint32_t first, uint32_t count, uint32_t value)
{
#if !defined(KERNEL_MEMORY_NO_POISON) || KERNEL_MEMORY_NO_POISON == 0
    for (uint32_t frame = 0u; frame < count; ++frame) {
        uintptr_t address = (uintptr_t)stats.ram_base +
            (uintptr_t)(first + frame) * KERNEL_PAGE_SIZE;
        volatile uint32_t *word = (volatile uint32_t *)address;
        for (uint32_t index = 0u;
             index < KERNEL_PAGE_SIZE / sizeof(*word); ++index)
            word[index] = value;
    }
#else
    (void)first;
    (void)count;
    (void)value;
#endif
}

static KernelFrameState frame_state(uint32_t index)
{
    uint32_t address;

    if (bitmap_test(dynamic_bitmap, index))
        return (KernelFrameState)frames[index].state;
    if (!bitmap_test(blocked_bitmap, index))
        return KERNEL_FRAME_FREE;

    address = stats.ram_base + index * KERNEL_PAGE_SIZE;
    for (uint32_t range = 0u; range < saved_range_count; ++range) {
        uint32_t end = saved_ranges[range].base + saved_ranges[range].size;
        if (address >= saved_ranges[range].base && address < end)
            return (KernelFrameState)saved_ranges[range].state;
    }
    return KERNEL_FRAME_UNCLASSIFIED;
}

static bool range_has_state(uint32_t base, uint32_t size,
                            KernelFrameState state)
{
    uint32_t first;
    uint32_t count;

    if ((base & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (size & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        !byte_range(base, size, &first, &count))
        return false;
    for (uint32_t index = 0u; index < count; ++index) {
        if (frame_state(first + index) != state)
            return false;
    }
    return true;
}

KernelMemoryStatus kernel_memory_init(const AstraBootInfo *info)
{
    uint64_t ram_end;

    initialized = false;
    saved_range_count = 0u;
    reset_stats();

    if (info == NULL || astra_boot_info_validate(info) != ASTRA_BOOT_VALID)
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    if ((info->ram_base & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (info->ram_size & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        info->ram_size == 0u ||
        info->ram_size / KERNEL_PAGE_SIZE > KERNEL_MAX_FRAMES)
        return KERNEL_MEMORY_INVALID_MAP;

    stats.ram_base = info->ram_base;
    stats.total_frames = info->ram_size / KERNEL_PAGE_SIZE;
    for (uint32_t index = 0u; index < KERNEL_MAX_FRAMES; ++index) {
        owner_next[index] = KERNEL_FRAME_INDEX_NONE;
        owner_previous[index] = KERNEL_FRAME_INDEX_NONE;
    }
    for (uint32_t index = 0u; index < KERNEL_MAX_FRAME_OWNERS; ++index) {
        owner_ledgers[index].owner = KERNEL_OWNER_NONE;
        owner_ledgers[index].head = KERNEL_FRAME_INDEX_NONE;
        owner_ledgers[index].frame_count = 0u;
    }
    for (uint32_t word = 0u; word < KERNEL_BITMAP_WORDS; ++word) {
        blocked_bitmap[word] = UINT32_MAX;
        dynamic_bitmap[word] = 0u;
        classified_bitmap[word] = 0u;
    }
    initialized = true;
    ram_end = (uint64_t)info->ram_base + info->ram_size;

    for (uint32_t range_index = 0u;
         range_index < info->memory_range_count; ++range_index) {
        const AstraBootMemoryRange *range = &info->memory_ranges[range_index];
        uint64_t end = (uint64_t)range->base + range->size;
        KernelFrameState state = boot_state(range->type);
        uint32_t first;
        uint32_t count;

        if (state == KERNEL_FRAME_UNCLASSIFIED) {
            initialized = false;
            return KERNEL_MEMORY_INVALID_MAP;
        }
        if (end <= info->ram_base || range->base >= ram_end)
            continue;
        if (range->base < info->ram_base || end > ram_end ||
            (range->base & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
            (range->size & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
            !byte_range(range->base, range->size, &first, &count) ||
            saved_range_count >= ASTRA_BOOT_MAX_MEMORY_RANGES ||
            !classify_range(first, count, state == KERNEL_FRAME_FREE)) {
            initialized = false;
            return KERNEL_MEMORY_INVALID_MAP;
        }
        saved_ranges[saved_range_count].base = range->base;
        saved_ranges[saved_range_count].size = range->size;
        saved_ranges[saved_range_count].state = (uint8_t)state;
        saved_ranges[saved_range_count].reserved[0] = 0u;
        saved_ranges[saved_range_count].reserved[1] = 0u;
        saved_ranges[saved_range_count].reserved[2] = 0u;
        ++saved_range_count;
        if (state == KERNEL_FRAME_FREE)
            stats.free_frames += count;
    }

    if (!all_frames_classified() ||
        !range_has_state(info->kernel_base, info->kernel_memory_size,
                         KERNEL_FRAME_KERNEL) ||
        !range_has_state(info->early_log_base, info->early_log_size,
                         KERNEL_FRAME_EARLY_LOG)) {
        initialized = false;
        return KERNEL_MEMORY_INVALID_MAP;
    }
    stats.high_water_frames = stats.total_frames - stats.free_frames;
    return KERNEL_MEMORY_OK;
}

KernelMemoryStatus kernel_memory_alloc(uint32_t frame_count,
                                       uint32_t alignment_frames,
                                       KernelFrameState state,
                                       uint32_t owner,
                                       uint32_t *physical_base)
{
    uint32_t allocated;
    uint32_t owner_slot;

    if (!initialized || physical_base == NULL || frame_count == 0u ||
        frame_count > stats.total_frames ||
        !is_power_of_two(alignment_frames) || !is_dynamic_state(state) ||
        owner == KERNEL_OWNER_NONE)
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    if (!owner_slot_for_allocation(owner, frame_count, &owner_slot)) {
        ++stats.allocation_failures;
        return KERNEL_MEMORY_OUT_OF_MEMORY;
    }

    for (uint32_t first = 0u;
         first <= stats.total_frames - frame_count; ++first) {
        uint32_t physical_frame = stats.ram_base / KERNEL_PAGE_SIZE + first;
        bool available = (physical_frame & (alignment_frames - 1u)) == 0u;

        for (uint32_t index = 0u; available && index < frame_count; ++index)
            available = !bitmap_test(blocked_bitmap, first + index);
        if (!available)
            continue;

        poison(first, frame_count, KERNEL_ALLOC_POISON);
        bitmap_assign_range(blocked_bitmap, first, frame_count, true);
        bitmap_assign_range(dynamic_bitmap, first, frame_count, true);
        for (uint32_t index = 0u; index < frame_count; ++index) {
            KernelFrameInfo *frame = &frames[first + index];
            frame->owner = owner;
            frame->references = 1u;
            frame->pins = 0u;
            frame->state = (uint8_t)state;
            link_owner_frame(owner_slot, first + index, owner);
        }
        stats.free_frames -= frame_count;
        allocated = stats.total_frames - stats.free_frames;
        if (allocated > stats.high_water_frames)
            stats.high_water_frames = allocated;
        *physical_base = stats.ram_base + first * KERNEL_PAGE_SIZE;
        return KERNEL_MEMORY_OK;
    }

    ++stats.allocation_failures;
    return KERNEL_MEMORY_OUT_OF_MEMORY;
}

KernelMemoryStatus kernel_memory_retain(uint32_t physical_base,
                                        uint32_t frame_count,
                                        uint32_t owner)
{
    uint32_t first;

    if (owner == KERNEL_OWNER_NONE ||
        !frame_range(physical_base, frame_count, &first))
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < frame_count; ++index) {
        KernelFrameInfo *frame = &frames[first + index];
        if (!bitmap_test(dynamic_bitmap, first + index) ||
            frame->owner != owner || !is_dynamic_state(frame->state))
            return KERNEL_MEMORY_NOT_OWNED;
        if (frame->references == UINT16_MAX)
            return KERNEL_MEMORY_OVERFLOW;
    }
    for (uint32_t index = 0u; index < frame_count; ++index)
        ++frames[first + index].references;
    return KERNEL_MEMORY_OK;
}

KernelMemoryStatus kernel_memory_release(uint32_t physical_base,
                                         uint32_t frame_count,
                                         uint32_t owner)
{
    uint32_t first;
    uint32_t owner_slot;

    if (owner == KERNEL_OWNER_NONE ||
        !frame_range(physical_base, frame_count, &first))
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < frame_count; ++index) {
        KernelFrameInfo *frame = &frames[first + index];
        if (!bitmap_test(dynamic_bitmap, first + index) ||
            frame->owner != owner || !is_dynamic_state(frame->state) ||
            frame->references == 0u)
            return KERNEL_MEMORY_NOT_OWNED;
        if (frame->references == 1u && frame->pins != 0u)
            return KERNEL_MEMORY_BUSY;
    }
    if (!find_owner_slot(owner, &owner_slot))
        return KERNEL_MEMORY_INVALID_MAP;
    for (uint32_t index = 0u; index < frame_count; ++index) {
        KernelFrameInfo *frame = &frames[first + index];
        --frame->references;
        if (frame->references == 0u) {
            if (!unlink_owner_frame(owner_slot, first + index))
                return KERNEL_MEMORY_INVALID_MAP;
            poison(first + index, 1u, KERNEL_FREE_POISON);
            reset_frame(frame, KERNEL_FRAME_FREE);
            bitmap_set(dynamic_bitmap, first + index, false);
            bitmap_set(blocked_bitmap, first + index, false);
            ++stats.free_frames;
        }
    }
    return KERNEL_MEMORY_OK;
}

KernelMemoryStatus kernel_memory_pin(uint32_t physical_base,
                                     uint32_t frame_count,
                                     uint32_t owner)
{
    uint32_t first;

    if (owner == KERNEL_OWNER_NONE ||
        !frame_range(physical_base, frame_count, &first))
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < frame_count; ++index) {
        KernelFrameInfo *frame = &frames[first + index];
        if (!bitmap_test(dynamic_bitmap, first + index) ||
            frame->owner != owner || !is_dynamic_state(frame->state))
            return KERNEL_MEMORY_NOT_OWNED;
        if (frame->pins == UINT8_MAX)
            return KERNEL_MEMORY_OVERFLOW;
    }
    for (uint32_t index = 0u; index < frame_count; ++index)
        ++frames[first + index].pins;
    return KERNEL_MEMORY_OK;
}

KernelMemoryStatus kernel_memory_unpin(uint32_t physical_base,
                                       uint32_t frame_count,
                                       uint32_t owner)
{
    uint32_t first;

    if (owner == KERNEL_OWNER_NONE ||
        !frame_range(physical_base, frame_count, &first))
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < frame_count; ++index) {
        KernelFrameInfo *frame = &frames[first + index];
        if (!bitmap_test(dynamic_bitmap, first + index) ||
            frame->owner != owner || !is_dynamic_state(frame->state))
            return KERNEL_MEMORY_NOT_OWNED;
        if (frame->pins == 0u)
            return KERNEL_MEMORY_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0u; index < frame_count; ++index)
        --frames[first + index].pins;
    return KERNEL_MEMORY_OK;
}

KernelMemoryStatus kernel_memory_release_owner(uint32_t owner,
                                               uint32_t *released_frames)
{
    uint32_t released = 0u;
    uint32_t owner_slot;
    uint32_t visited = 0u;
    uint16_t index;

    if (!initialized || owner == KERNEL_OWNER_NONE)
        return KERNEL_MEMORY_INVALID_ARGUMENT;
    increment_saturating(&stats.owner_release_operations);
    if (!find_owner_slot(owner, &owner_slot)) {
        if (released_frames != NULL)
            *released_frames = 0u;
        return KERNEL_MEMORY_OK;
    }

    index = owner_ledgers[owner_slot].head;
    while (index != KERNEL_FRAME_INDEX_NONE) {
        if (index >= stats.total_frames ||
            visited >= owner_ledgers[owner_slot].frame_count ||
            !bitmap_test(dynamic_bitmap, index) ||
            frames[index].owner != owner)
            return KERNEL_MEMORY_INVALID_MAP;
        increment_saturating(&stats.owner_release_frame_visits);
        ++visited;
        if (frames[index].pins != 0u)
            return KERNEL_MEMORY_BUSY;
        index = owner_next[index];
    }
    if (visited != owner_ledgers[owner_slot].frame_count)
        return KERNEL_MEMORY_INVALID_MAP;

    while (owner_ledgers[owner_slot].head != KERNEL_FRAME_INDEX_NONE) {
        index = owner_ledgers[owner_slot].head;
        if (index >= stats.total_frames ||
            !bitmap_test(dynamic_bitmap, index) ||
            frames[index].owner != owner ||
            !unlink_owner_frame(owner_slot, index))
            return KERNEL_MEMORY_INVALID_MAP;
        increment_saturating(&stats.owner_release_frame_visits);
        poison(index, 1u, KERNEL_FREE_POISON);
        reset_frame(&frames[index], KERNEL_FRAME_FREE);
        bitmap_set(dynamic_bitmap, index, false);
        bitmap_set(blocked_bitmap, index, false);
        ++released;
    }
    stats.free_frames += released;
    if (released_frames != NULL)
        *released_frames = released;
    return KERNEL_MEMORY_OK;
}

bool kernel_memory_range_owned(uint32_t physical_base, uint32_t byte_count,
                               uint32_t owner, KernelFrameState state,
                               bool require_pinned)
{
    uint32_t first;
    uint32_t count;

    if (owner == KERNEL_OWNER_NONE || !is_dynamic_state(state) ||
        !byte_range(physical_base, byte_count, &first, &count))
        return false;
    for (uint32_t index = 0u; index < count; ++index) {
        const KernelFrameInfo *frame = &frames[first + index];
        if (!bitmap_test(dynamic_bitmap, first + index) ||
            frame->owner != owner || frame->state != state ||
            frame->references == 0u ||
            (require_pinned && frame->pins == 0u))
            return false;
    }
    return true;
}

bool kernel_memory_frame_info(uint32_t physical_address,
                              KernelFrameInfo *info)
{
    uint32_t index;
    uint32_t aligned = physical_address & ~(KERNEL_PAGE_SIZE - 1u);

    if (info == NULL || !address_to_index(aligned, &index))
        return false;
    if (!bitmap_test(dynamic_bitmap, index)) {
        reset_frame(info, frame_state(index));
        return true;
    }
    info->owner = frames[index].owner;
    info->references = frames[index].references;
    info->pins = frames[index].pins;
    info->state = frames[index].state;
    return true;
}

bool kernel_memory_stats(KernelMemoryStats *result)
{
    if (!initialized || result == NULL)
        return false;
    result->ram_base = stats.ram_base;
    result->total_frames = stats.total_frames;
    result->free_frames = stats.free_frames;
    result->high_water_frames = stats.high_water_frames;
    result->allocation_failures = stats.allocation_failures;
    result->owner_slots_used = stats.owner_slots_used;
    result->owner_release_operations = stats.owner_release_operations;
    result->owner_release_frame_visits = stats.owner_release_frame_visits;
    return true;
}
