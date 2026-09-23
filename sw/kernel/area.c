#include "area.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"

#include <stddef.h>

/* Object tables live above the frame metadata; see kernel.ld. */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif

#define AREA_FRAME_OWNER_PREFIX 0x40000000u
#define AREA_SLOT_BITS 16u
#define AREA_GENERATION_MASK \
    ((AREA_FRAME_OWNER_PREFIX - 1u) >> AREA_SLOT_BITS)
#define AREA_LEAF_BITS 3u
#define AREA_LEAF_ENTRIES (1u << AREA_LEAF_BITS)
#define AREA_LEAF_COUNT \
    ((KERNEL_AREA_MAX + AREA_LEAF_ENTRIES - 1u) / AREA_LEAF_ENTRIES)
#define AREA_PAGE_LEAF_ENTRIES (KERNEL_PAGE_SIZE / sizeof(uint32_t))
#define AREA_PAGE_LEAF_COUNT \
    ((KERNEL_AREA_PAGE_MAX + AREA_PAGE_LEAF_ENTRIES - 1u) / \
     AREA_PAGE_LEAF_ENTRIES)

/*
 * What an uncommitted page holds. Zero would have been the obvious marker and
 * it is the wrong one: the frame allocator's addresses are relative to a RAM
 * base the boot info supplies, so whether physical zero can ever be a real
 * frame is a property of the machine rather than of this file. A value with
 * its low bits set can never be a page-aligned frame on any of them.
 */
#define AREA_PAGE_ABSENT 0xffffffffu

typedef enum KernelAreaState {
    KERNEL_AREA_FREE = 0,
    KERNEL_AREA_RESERVED,
    KERNEL_AREA_LIVE,
    KERNEL_AREA_CLOSING
} KernelAreaState;

typedef struct KernelAreaMapping {
    KernelAddressSpace *space;
    KernelArea *area;
    uint32_t process_id;
    uint32_t virtual_base;
    uint32_t permissions;
    uint8_t active;
    uint8_t reserved[3];
} KernelAreaMapping;

typedef struct KernelAreaMappingBlock {
    struct KernelAreaMappingBlock *next;
    uint32_t physical;
    uint16_t capacity;
    uint16_t reserved;
    KernelAreaMapping records[];
} KernelAreaMappingBlock;

typedef struct KernelAreaMappingCursor {
    KernelAreaMappingBlock *block;
    uint16_t index;
} KernelAreaMappingCursor;

struct KernelArea {
    uint32_t *page_directory[AREA_PAGE_LEAF_COUNT];
    uint32_t page_directory_physical[AREA_PAGE_LEAF_COUNT];
    uint32_t creator;
    uint32_t frame_owner;
    uint32_t generation;
    uint32_t byte_size;
    uint32_t terminal_result;
    uint32_t page_count;
    uint32_t committed_pages;
    uint16_t handle_references;
    uint16_t child_references;
    uint16_t mapping_references;
    uint16_t slot;
    uint8_t state;
    uint8_t frames_released;
    uint8_t reserved_form;
};

#define AREA_LEAF_FRAMES \
    ((AREA_LEAF_ENTRIES * sizeof(KernelArea) + KERNEL_PAGE_SIZE - 1u) / \
     KERNEL_PAGE_SIZE)

#if defined(__m68k__)
_Static_assert(sizeof(KernelAreaMapping) == 24u,
               "area mapping size changed; update the memory budget");
_Static_assert(sizeof(KernelArea) <= 304u,
               "area record memory budget changed");
#endif

static KernelArea *area_directory[AREA_LEAF_COUNT] KERNEL_TABLES;
static uint32_t area_directory_physical[AREA_LEAF_COUNT] KERNEL_TABLES;
static uint32_t area_backed_limit;
static uint16_t next_area_slot;
static KernelAreaMappingBlock *mapping_blocks;
static KernelAreaMappingBlock *mapping_blocks_tail;
static KernelAreaPoolStats pool_stats;
static uint8_t pool_corrupt;

_Static_assert(KERNEL_AREA_MAX <= (1u << AREA_SLOT_BITS) - 1u,
               "area frame-owner identity has too few slot bits");

#if defined(KERNEL_AREA_HOST_TEST)
static uint8_t *host_memory;
static uint32_t host_memory_base;
static uint32_t host_memory_size;
static KernelAreaTestFault next_test_fault;

void kernel_area_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                           uint32_t size)
{
    host_memory = memory;
    host_memory_base = base;
    host_memory_size = size;
}

void kernel_area_test_fail_next(KernelAreaTestFault fault)
{
    next_test_fault = fault < KERNEL_AREA_TEST_FAULT_COUNT ?
        fault : KERNEL_AREA_TEST_FAULT_NONE;
}

static bool consume_test_fault(KernelAreaTestFault fault)
{
    if (next_test_fault != fault)
        return false;
    next_test_fault = KERNEL_AREA_TEST_FAULT_NONE;
    return true;
}
#endif

static KernelArea *area_at(uint32_t slot)
{
    KernelArea *leaf;

    if (slot >= KERNEL_AREA_MAX)
        return NULL;
    leaf = area_directory[slot >> AREA_LEAF_BITS];
    return leaf != NULL ? &leaf[slot & (AREA_LEAF_ENTRIES - 1u)] : NULL;
}

static bool valid_area(const KernelArea *area)
{
    return area != NULL && area->slot < KERNEL_AREA_MAX &&
           area_at(area->slot) == area && area->generation != 0u &&
           area->generation <= AREA_GENERATION_MASK &&
           area->state >= KERNEL_AREA_RESERVED &&
           area->state <= KERNEL_AREA_CLOSING;
}

static void reset_mapping(KernelAreaMapping *mapping)
{
    mapping->space = NULL;
    mapping->area = NULL;
    mapping->process_id = 0u;
    mapping->virtual_base = 0u;
    mapping->permissions = 0u;
    mapping->active = 0u;
    mapping->reserved[0] = 0u;
    mapping->reserved[1] = 0u;
    mapping->reserved[2] = 0u;
}

static KernelAreaMapping *next_mapping(KernelAreaMappingCursor *cursor)
{
    while (cursor->block != NULL &&
           cursor->index >= cursor->block->capacity) {
        cursor->block = cursor->block->next;
        cursor->index = 0u;
    }
    if (cursor->block == NULL)
        return NULL;
    return &cursor->block->records[cursor->index++];
}

static KernelAreaStatus claim_mapping(uint32_t owner,
                                      KernelAreaMapping **result)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;

    if (result == NULL)
        return KERNEL_AREA_INVALID_ARGUMENT;
    *result = NULL;
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_AREA_MAPPING,
                                   owner))
        return KERNEL_AREA_NO_SLOT;
    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active == 0u && mapping->space == NULL &&
            mapping->area == NULL && mapping->process_id == 0u) {
            *result = mapping;
            break;
        }
    }
    if (*result == NULL) {
        uint32_t physical;
        KernelAreaMappingBlock *block;

        if (kernel_memory_alloc_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA, 1u, 1u,
                KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
            KERNEL_MEMORY_OK) {
            kernel_allocation_fail(KERNEL_ALLOCATION_SITE_AREA_MAPPING,
                                   owner);
            return KERNEL_AREA_OUT_OF_MEMORY;
        }
        block = kernel_memory_access(physical, KERNEL_PAGE_SIZE);
        if (block == NULL) {
            (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
            kernel_allocation_fail(KERNEL_ALLOCATION_SITE_AREA_MAPPING,
                                   owner);
            return KERNEL_AREA_CORRUPT;
        }
        block->physical = physical;
        block->capacity = (uint16_t)((KERNEL_PAGE_SIZE - sizeof(*block)) /
                                     sizeof(block->records[0]));
        if (block->capacity == 0u) {
            (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
            kernel_allocation_fail(KERNEL_ALLOCATION_SITE_AREA_MAPPING,
                                   owner);
            return KERNEL_AREA_CORRUPT;
        }
        if (mapping_blocks_tail == NULL)
            mapping_blocks = block;
        else
            mapping_blocks_tail->next = block;
        mapping_blocks_tail = block;
        *result = &block->records[0];
    }
    if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_AREA_MAPPING, 1u,
                                  sizeof(**result), owner))
        return KERNEL_AREA_CORRUPT;
    return KERNEL_AREA_OK;
}

static bool release_mapping(KernelAreaMapping *mapping)
{
    reset_mapping(mapping);
    return kernel_allocation_release(KERNEL_ALLOCATION_SITE_AREA_MAPPING,
                                     1u, sizeof(*mapping));
}

static void reset_area(KernelArea *area, uint16_t slot)
{
    uint32_t generation = area->generation;

    kernel_bytes_clear(area, sizeof(*area));
    area->generation = generation;
    area->slot = slot;
    area->state = KERNEL_AREA_FREE;
    area->frames_released = 1u;
}

static bool ensure_area_leaf(uint32_t slot)
{
    uint32_t leaf_index = slot >> AREA_LEAF_BITS;
    uint32_t physical;
    KernelArea *leaf;

    if (area_directory[leaf_index] != NULL)
        return true;
    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA, AREA_LEAF_FRAMES,
            1u, KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return false;
    leaf = kernel_memory_access(physical,
                                AREA_LEAF_FRAMES * KERNEL_PAGE_SIZE);
    if (leaf == NULL) {
        (void)kernel_memory_release(physical, AREA_LEAF_FRAMES,
                                    KERNEL_OWNER_CORE);
        return false;
    }
    area_directory[leaf_index] = leaf;
    area_directory_physical[leaf_index] = physical;
    for (uint32_t index = 0u; index < AREA_LEAF_ENTRIES; ++index) {
        uint32_t record_slot = leaf_index * AREA_LEAF_ENTRIES + index;

        leaf[index].generation = 1u;
        reset_area(&leaf[index], record_slot < KERNEL_AREA_MAX ?
                   (uint16_t)record_slot : UINT16_MAX);
    }
    uint32_t limit = (leaf_index + 1u) * AREA_LEAF_ENTRIES;
    if (limit > KERNEL_AREA_MAX)
        limit = KERNEL_AREA_MAX;
    if (limit > area_backed_limit)
        area_backed_limit = limit;
    return true;
}

static KernelAreaStatus claim_area(uint32_t owner, KernelArea **result)
{
    if (!kernel_allocation_attempt(KERNEL_ALLOCATION_SITE_AREA_OBJECT,
                                   owner))
        return KERNEL_AREA_NO_SLOT;
    for (uint32_t offset = 0u; offset < KERNEL_AREA_MAX; ++offset) {
        uint32_t slot = (uint32_t)next_area_slot + offset;
        KernelArea *area;

        if (slot >= KERNEL_AREA_MAX)
            slot -= KERNEL_AREA_MAX;
        if (!ensure_area_leaf(slot))
            break;
        area = area_at(slot);
        if (area->state != KERNEL_AREA_FREE)
            continue;
        if (!kernel_allocation_commit(KERNEL_ALLOCATION_SITE_AREA_OBJECT,
                                      1u, sizeof(*area), owner))
            return KERNEL_AREA_CORRUPT;
        next_area_slot = slot + 1u == KERNEL_AREA_MAX ?
            0u : (uint16_t)(slot + 1u);
        *result = area;
        return KERNEL_AREA_OK;
    }
    kernel_allocation_fail(KERNEL_ALLOCATION_SITE_AREA_OBJECT, owner);
    return KERNEL_AREA_NO_SLOT;
}

static void free_area(KernelArea *area)
{
    uint16_t slot = area->slot;

    reset_area(area, slot);
    if (slot < next_area_slot)
        next_area_slot = slot;
    if (!kernel_allocation_release(KERNEL_ALLOCATION_SITE_AREA_OBJECT, 1u,
                                   sizeof(*area)))
        pool_corrupt = 1u;
}

static uint32_t *page_entries(const KernelArea *area, uint32_t page)
{
    uint32_t leaf_index;

    if (area == NULL || page >= area->page_count)
        return NULL;
    leaf_index = page / AREA_PAGE_LEAF_ENTRIES;
    return area->page_directory[leaf_index] != NULL ?
        &area->page_directory[leaf_index]
             [page % AREA_PAGE_LEAF_ENTRIES] : NULL;
}

static bool ensure_page_leaf(KernelArea *area, uint32_t page)
{
    uint32_t leaf_index;
    uint32_t physical;
    uint32_t *leaf;

    if (area == NULL || page >= area->page_count)
        return false;
    leaf_index = page / AREA_PAGE_LEAF_ENTRIES;
    if (area->page_directory[leaf_index] != NULL)
        return true;
    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA, 1u, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return false;
    leaf = kernel_memory_access(physical, KERNEL_PAGE_SIZE);
    if (leaf == NULL) {
        (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
        return false;
    }
    for (uint32_t index = 0u; index < AREA_PAGE_LEAF_ENTRIES; ++index)
        leaf[index] = AREA_PAGE_ABSENT;
    area->page_directory[leaf_index] = leaf;
    area->page_directory_physical[leaf_index] = physical;
    return true;
}

static bool release_page_metadata(KernelArea *area)
{
    bool released = true;

    for (uint32_t leaf = 0u; leaf < AREA_PAGE_LEAF_COUNT; ++leaf) {
        if (area->page_directory[leaf] == NULL)
            continue;
        if (area->page_directory_physical[leaf] == 0u ||
            kernel_memory_release(area->page_directory_physical[leaf], 1u,
                                  KERNEL_OWNER_CORE) != KERNEL_MEMORY_OK)
            released = false;
        area->page_directory[leaf] = NULL;
        area->page_directory_physical[leaf] = 0u;
    }
    return released;
}

static bool page_committed(const KernelArea *area, uint32_t page)
{
    uint32_t *entry = page_entries(area, page);

    return entry != NULL && *entry != AREA_PAGE_ABSENT;
}

static uint32_t area_page_address(uint32_t virtual_base, uint32_t page)
{
    return virtual_base + page * KERNEL_PAGE_SIZE;
}

/*
 * Publishes an area's committed pages into one address space. They are an
 * arbitrary set rather than a prefix -- a program may touch the far end of a
 * reserved area first -- so the set is walked as maximal contiguous runs and
 * each run is one call. An area with nothing committed maps nothing and
 * succeeds, which is what makes mapping a fresh reserved area free.
 */
static KernelVmStatus map_committed_runs(const KernelArea *area,
                                         KernelAddressSpace *space,
                                         uint32_t virtual_base,
                                         uint32_t permissions,
                                         uint32_t *mapped_pages)
{
    uint32_t page = 0u;

    *mapped_pages = 0u;
    while (page < area->page_count) {
        uint32_t run;
        KernelVmStatus status;

        if (!page_committed(area, page)) {
            ++page;
            continue;
        }
        run = 0u;
        while (run < AREA_PAGE_LEAF_ENTRIES -
                         page % AREA_PAGE_LEAF_ENTRIES &&
               page_committed(area, page + run))
            ++run;
        status = kernel_vm_map_shared_range(
            space, area_page_address(virtual_base, page),
            page_entries(area, page),
            run, area->frame_owner, permissions);
        if (status != KERNEL_VM_OK)
            return status;
        *mapped_pages += run;
        page += run;
    }
    return KERNEL_VM_OK;
}

/*
 * The inverse, and it takes a page ceiling so that a failed map can withdraw
 * exactly the runs it published rather than every run the area has.
 */
static KernelVmStatus unmap_committed_runs(const KernelArea *area,
                                           KernelAddressSpace *space,
                                           uint32_t virtual_base,
                                           uint32_t page_limit)
{
    uint32_t page = 0u;
    uint32_t unmapped = 0u;

    while (page < area->page_count && unmapped < page_limit) {
        uint32_t run;
        KernelVmStatus status;

        if (!page_committed(area, page)) {
            ++page;
            continue;
        }
        run = 0u;
        while (run < AREA_PAGE_LEAF_ENTRIES -
                         page % AREA_PAGE_LEAF_ENTRIES &&
               page_committed(area, page + run) &&
               unmapped + run < page_limit)
            ++run;
        status = kernel_vm_unmap_shared_range(
            space, area_page_address(virtual_base, page),
            page_entries(area, page),
            run, area->frame_owner);
        if (status != KERNEL_VM_OK)
            return status;
        unmapped += run;
        page += run;
    }
    return KERNEL_VM_OK;
}

static uint32_t make_frame_owner(uint32_t generation, uint32_t slot)
{
    return AREA_FRAME_OWNER_PREFIX | (generation << AREA_SLOT_BITS) | slot;
}

static bool physical_pointer(uint32_t physical, volatile uint8_t **pointer)
{
#if defined(KERNEL_AREA_HOST_TEST)
    uint32_t offset;

    if (pointer == NULL || host_memory == NULL || physical < host_memory_base)
        return false;
    offset = physical - host_memory_base;
    if (offset >= host_memory_size)
        return false;
    *pointer = host_memory + offset;
    return true;
#else
    if (pointer == NULL)
        return false;
    *pointer = (volatile uint8_t *)(uintptr_t)physical;
    return true;
#endif
}

static bool range_valid(const KernelArea *area, uint32_t offset,
                        uint32_t size)
{
    return valid_area(area) && area->state == KERNEL_AREA_LIVE &&
           offset <= area->byte_size && size <= area->byte_size - offset;
}

static KernelAreaStatus unmap_record(KernelAreaMapping *mapping,
                                     bool revoked)
{
    KernelArea *area;

    if (mapping == NULL || mapping->active == 0u ||
        !valid_area(mapping->area) || mapping->space == NULL)
        return KERNEL_AREA_CORRUPT;
    area = mapping->area;
    if (unmap_committed_runs(area, mapping->space, mapping->virtual_base,
                             area->page_count) !=
        KERNEL_VM_OK)
        return KERNEL_AREA_CORRUPT;
    if (area->mapping_references == 0u || pool_stats.active_mappings == 0u)
        return KERNEL_AREA_CORRUPT;
    --area->mapping_references;
    --pool_stats.active_mappings;
    ++pool_stats.unmap_operations;
    if (revoked)
        ++pool_stats.revoked_mappings;
    if (!release_mapping(mapping))
        return KERNEL_AREA_CORRUPT;
    return KERNEL_AREA_OK;
}

static void maybe_free(KernelArea *area)
{
    if (!valid_area(area) || area->state != KERNEL_AREA_CLOSING ||
        area->handle_references != 0u || area->child_references != 0u ||
        area->mapping_references != 0u || area->frames_released == 0u)
        return;
    if (pool_stats.active_areas == 0u || pool_stats.closing_areas == 0u) {
        pool_corrupt = 1u;
        return;
    }
    --pool_stats.active_areas;
    --pool_stats.closing_areas;
    free_area(area);
}

static bool release_area_storage(KernelArea *area)
{
    bool released = true;

    for (uint32_t page = 0u; page < area->page_count; ++page) {
        uint32_t *entry;

        if (!page_committed(area, page))
            continue;
        entry = page_entries(area, page);
        if (entry == NULL ||
            kernel_memory_release(*entry, 1u, area->frame_owner) !=
                KERNEL_MEMORY_OK)
            released = false;
        else
            *entry = AREA_PAGE_ABSENT;
    }
    if (!kernel_memory_unprotect_owner(area->frame_owner))
        released = false;
    if (!release_page_metadata(area))
        released = false;
    area->committed_pages = 0u;
    area->frames_released = 1u;
    return released;
}

static KernelAreaStatus close_area(KernelArea *area, uint32_t terminal_result)
{
    if (!valid_area(area))
        return KERNEL_AREA_INVALID_ARGUMENT;
    if (area->state == KERNEL_AREA_CLOSING)
        return KERNEL_AREA_OK;
    if (area->state != KERNEL_AREA_LIVE)
        return KERNEL_AREA_INVALID_STATE;
    area->state = KERNEL_AREA_CLOSING;
    area->terminal_result = terminal_result;
    ++pool_stats.closing_areas;

    {
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while ((mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active == 0u || mapping->area != area)
                continue;
            if (unmap_record(mapping, true) == KERNEL_AREA_OK)
                continue;
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
    }
    if (area->mapping_references != 0u || area->frames_released != 0u ||
        pool_stats.committed_pages < area->committed_pages) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    pool_stats.committed_pages -= area->committed_pages;
    if (!release_area_storage(area)) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    maybe_free(area);
    return pool_corrupt == 0u ? KERNEL_AREA_OK : KERNEL_AREA_CORRUPT;
}

void kernel_area_pool_init(void)
{
    for (uint32_t leaf = 0u; leaf < AREA_LEAF_COUNT; ++leaf) {
        area_directory[leaf] = NULL;
        area_directory_physical[leaf] = 0u;
    }
    area_backed_limit = 0u;
    next_area_slot = 0u;
    mapping_blocks = NULL;
    mapping_blocks_tail = NULL;
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
#if defined(KERNEL_AREA_HOST_TEST)
    host_memory = NULL;
    host_memory_base = 0u;
    host_memory_size = 0u;
    next_test_fault = KERNEL_AREA_TEST_FAULT_NONE;
#endif
}

KernelAreaStatus kernel_area_create(uint32_t creator, uint32_t byte_size,
                                    uint32_t flags, KernelArea **result)
{
    KernelArea *area = NULL;
    KernelAreaStatus claim_status;
    uint32_t page_count;
    bool reserved_form;

    if (creator == 0u || byte_size == 0u || result == NULL ||
        (flags & ~KERNEL_AREA_CREATE_FLAGS) != 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    *result = NULL;
    if (byte_size > KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE)
        return KERNEL_AREA_INVALID_ARGUMENT;
    reserved_form = (flags & KERNEL_AREA_CREATE_RESERVED) != 0u;
    page_count = (byte_size + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
    claim_status = claim_area(creator, &area);
    if (claim_status == KERNEL_AREA_NO_SLOT) {
        ++pool_stats.allocation_failures;
        return KERNEL_AREA_NO_SLOT;
    }
    if (claim_status != KERNEL_AREA_OK) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    if (area->state != KERNEL_AREA_FREE || area->slot >= KERNEL_AREA_MAX) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }

    area->generation = kernel_generation_next_masked(
        area->generation, AREA_GENERATION_MASK);
    area->creator = creator;
    area->frame_owner = make_frame_owner(area->generation, area->slot);
    if (kernel_memory_owner_protected(creator) &&
        !kernel_memory_protect_owner(area->frame_owner)) {
        free_area(area);
        ++pool_stats.allocation_failures;
        return KERNEL_AREA_NO_SLOT;
    }
    area->byte_size = page_count * KERNEL_PAGE_SIZE;
    area->terminal_result = 0u;
    area->handle_references = 1u;
    area->child_references = 0u;
    area->mapping_references = 0u;
    area->page_count = page_count;
    area->committed_pages = 0u;
    area->reserved_form = reserved_form ? 1u : 0u;
    area->state = KERNEL_AREA_RESERVED;
    area->frames_released = 1u;
#if defined(KERNEL_AREA_HOST_TEST)
    if (consume_test_fault(KERNEL_AREA_TEST_FAULT_CREATE_AFTER_RESERVE))
        goto allocation_failed;
#endif
    if (!reserved_form) {
        for (uint32_t page = 0u; page < page_count;) {
            uint32_t count = AREA_PAGE_LEAF_ENTRIES -
                             page % AREA_PAGE_LEAF_ENTRIES;
            uint32_t *entries;

            if (count > page_count - page)
                count = page_count - page;
            if (!ensure_page_leaf(area, page))
                goto allocation_failed;
            entries = page_entries(area, page);
            if (entries == NULL ||
                kernel_memory_alloc_pages_zeroed_tagged(
                    KERNEL_ALLOCATION_SITE_AREA_PAGES, count,
                    KERNEL_FRAME_SHARED, area->frame_owner, entries) !=
                    KERNEL_MEMORY_OK) {
                if (entries != NULL) {
                    for (uint32_t index = 0u; index < count; ++index)
                        entries[index] = AREA_PAGE_ABSENT;
                }
                goto allocation_failed;
            }
            area->committed_pages += count;
            page += count;
        }
    }
#if defined(KERNEL_AREA_HOST_TEST)
    if (consume_test_fault(KERNEL_AREA_TEST_FAULT_CREATE_AFTER_FRAME_ALLOCATE))
        goto allocation_failed;
#endif
    area->state = KERNEL_AREA_LIVE;
    /*
     * Zero from here on means "close_area still owes this area's frames back",
     * not "it holds at least one". A reserved area owes nothing yet and will
     * still be walked on close, which is what lets it be closed at all.
     */
    area->frames_released = 0u;
    ++pool_stats.created_areas;
    ++pool_stats.active_areas;
    pool_stats.committed_pages += area->committed_pages;
    if (pool_stats.active_areas > pool_stats.max_active_areas)
        pool_stats.max_active_areas = pool_stats.active_areas;
    if (pool_stats.committed_pages > pool_stats.max_committed_pages)
        pool_stats.max_committed_pages = pool_stats.committed_pages;
    *result = area;
    return KERNEL_AREA_OK;

allocation_failed:
    ++pool_stats.allocation_failures;
    if (!release_area_storage(area))
        pool_corrupt = 1u;
    free_area(area);
    return pool_corrupt == 0u ? KERNEL_AREA_OUT_OF_MEMORY :
                               KERNEL_AREA_CORRUPT;
}

void kernel_area_abandon_unpublished(KernelArea *area)
{
    if (!valid_area(area) || area->state != KERNEL_AREA_LIVE ||
        area->handle_references != 1u || area->child_references != 0u ||
        area->mapping_references != 0u) {
        pool_corrupt = 1u;
        return;
    }
    area->handle_references = 0u;
    if (close_area(area, 0u) != KERNEL_AREA_OK)
        pool_corrupt = 1u;
}

bool kernel_area_handle_retain(void *object, void *context)
{
    KernelArea *area = object;

    (void)context;
    if (!valid_area(area) || area->state != KERNEL_AREA_LIVE ||
        area->handle_references == UINT16_MAX)
        return false;
    ++area->handle_references;
    return true;
}

void kernel_area_handle_release(void *object, void *context)
{
    KernelArea *area = object;

    (void)context;
    if (!valid_area(area) || area->handle_references == 0u) {
        pool_corrupt = 1u;
        return;
    }
    --area->handle_references;
    if (area->state == KERNEL_AREA_LIVE && area->handle_references == 0u &&
        area->child_references == 0u && close_area(area, 0u) != KERNEL_AREA_OK)
        pool_corrupt = 1u;
    else
        maybe_free(area);
}

KernelAreaStatus kernel_area_child_retain(KernelArea *area)
{
    if (!valid_area(area))
        return KERNEL_AREA_INVALID_ARGUMENT;
    if (area->state != KERNEL_AREA_LIVE)
        return KERNEL_AREA_PEER_DEAD;
    if (area->child_references == UINT16_MAX)
        return KERNEL_AREA_CORRUPT;
    ++area->child_references;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_child_release(KernelArea *area)
{
    if (!valid_area(area) || area->child_references == 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    --area->child_references;
    if (area->state == KERNEL_AREA_LIVE && area->handle_references == 0u &&
        area->child_references == 0u && close_area(area, 0u) != KERNEL_AREA_OK)
        return KERNEL_AREA_CORRUPT;
    maybe_free(area);
    return pool_corrupt == 0u ? KERNEL_AREA_OK : KERNEL_AREA_CORRUPT;
}

KernelAreaStatus kernel_area_map(KernelArea *area, uint32_t process_id,
                                 KernelAddressSpace *space,
                                 uint32_t permissions,
                                 uint32_t *virtual_base,
                                 uint32_t *byte_size)
{
    KernelAreaMapping *free_mapping = NULL;
    KernelAreaStatus area_status;
    uint32_t selected_base;
    uint32_t selected_span;
    uint32_t mapped_pages = 0u;
    KernelVmStatus vm_status;

    if (!valid_area(area) || process_id == 0u || space == NULL ||
        virtual_base == NULL || byte_size == NULL ||
        (permissions & KERNEL_VM_READ) == 0u ||
        (permissions & KERNEL_VM_EXEC) != 0u ||
        (permissions & ~(KERNEL_VM_READ | KERNEL_VM_WRITE)) != 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    *virtual_base = 0u;
    *byte_size = 0u;
    if (area->state != KERNEL_AREA_LIVE)
        return KERNEL_AREA_PEER_DEAD;
    {
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while ((mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active == 0u)
                continue;
            if (mapping->process_id == process_id) {
                if (mapping->space != space)
                    return KERNEL_AREA_ACCESS_DENIED;
                if (mapping->virtual_base < KERNEL_VM_AREA_BASE ||
                    mapping->virtual_base - KERNEL_VM_AREA_BASE >=
                        KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE)
                    return KERNEL_AREA_CORRUPT;
            }
            if (mapping->process_id == process_id && mapping->area == area) {
                if (mapping->space != space ||
                    mapping->permissions != permissions)
                    return KERNEL_AREA_ACCESS_DENIED;
                *virtual_base = mapping->virtual_base;
                *byte_size = area->byte_size;
                return KERNEL_AREA_OK;
            }
        }
    }
    selected_span = area->page_count * KERNEL_PAGE_SIZE;
    selected_base = KERNEL_VM_AREA_BASE;
    for (;;) {
        uint32_t next = selected_base;
        uint32_t window_offset = selected_base - KERNEL_VM_AREA_BASE;

        if (window_offset >
                KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE ||
            selected_span >
                KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE -
                    window_offset)
            return KERNEL_AREA_QUOTA_EXCEEDED;
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while ((mapping = next_mapping(&cursor)) != NULL) {
            uint32_t mapping_span;
            uint32_t mapping_end;

            if (mapping->active == 0u ||
                mapping->process_id != process_id)
                continue;
            mapping_span = mapping->area->page_count * KERNEL_PAGE_SIZE;
            mapping_end = mapping->virtual_base + mapping_span;
            if (selected_base < mapping_end &&
                mapping->virtual_base < selected_base + selected_span &&
                mapping_end > next)
                next = mapping_end;
        }
        if (next == selected_base)
            break;
        selected_base = next;
    }
    area_status = claim_mapping(process_id, &free_mapping);
    if (area_status != KERNEL_AREA_OK)
        return area_status;
    if (free_mapping->active != 0u || free_mapping->space != NULL ||
        free_mapping->area != NULL) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }

    vm_status = map_committed_runs(area, space, selected_base, permissions,
                                   &mapped_pages);
    if (vm_status != KERNEL_VM_OK) {
        if (mapped_pages != 0u &&
            unmap_committed_runs(area, space, selected_base, mapped_pages) !=
                KERNEL_VM_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        ++pool_stats.map_rollbacks;
        if (!release_mapping(free_mapping)) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
            return KERNEL_AREA_OUT_OF_MEMORY;
        if (vm_status == KERNEL_VM_ALREADY_MAPPED)
            return KERNEL_AREA_ALREADY_MAPPED;
        if (vm_status == KERNEL_VM_CACHE_ALIAS)
            return KERNEL_AREA_ACCESS_DENIED;
        return KERNEL_AREA_CORRUPT;
    }
#if defined(KERNEL_AREA_HOST_TEST)
    if (consume_test_fault(
            KERNEL_AREA_TEST_FAULT_MAP_AFTER_VM_PUBLISH)) {
        ++pool_stats.map_rollbacks;
        if (unmap_committed_runs(area, space, selected_base,
                                 area->page_count) !=
            KERNEL_VM_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        if (!release_mapping(free_mapping)) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        return KERNEL_AREA_OUT_OF_MEMORY;
    }
#endif
    free_mapping->space = space;
    free_mapping->area = area;
    free_mapping->process_id = process_id;
    free_mapping->virtual_base = selected_base;
    free_mapping->permissions = permissions;
    free_mapping->active = 1u;
    ++area->mapping_references;
    ++pool_stats.active_mappings;
    ++pool_stats.map_operations;
    if (pool_stats.active_mappings > pool_stats.max_active_mappings)
        pool_stats.max_active_mappings = pool_stats.active_mappings;
    *virtual_base = selected_base;
    *byte_size = area->byte_size;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_clone_process(
    uint32_t source_process_id, const KernelAddressSpace *source_space,
    uint32_t destination_process_id, KernelAddressSpace *destination_space)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *source;
    KernelAreaStatus failure = KERNEL_AREA_NO_SLOT;

    if (source_process_id == 0u || source_space == NULL ||
        destination_process_id == 0u || destination_space == NULL ||
        source_process_id == destination_process_id ||
        source_space == destination_space)
        return KERNEL_AREA_INVALID_ARGUMENT;
    while ((source = next_mapping(&cursor)) != NULL) {
        KernelAreaMapping *destination;
        KernelAreaStatus status;

        if (source->active != 0u &&
            source->process_id == destination_process_id)
            return KERNEL_AREA_INVALID_STATE;
        if (source->active == 0u ||
            source->process_id != source_process_id)
            continue;
        if (source->space != source_space || !valid_area(source->area))
            goto corrupt;
        status = claim_mapping(destination_process_id, &destination);
        if (status != KERNEL_AREA_OK) {
            failure = status;
            goto unavailable;
        }
        if (destination->active != 0u || destination->space != NULL ||
            destination->area != NULL)
            goto corrupt;
        destination->space = destination_space;
        destination->area = source->area;
        destination->process_id = destination_process_id;
        destination->virtual_base = source->virtual_base;
        destination->permissions = source->permissions;
        destination->active = 0u;
    }
    cursor.block = mapping_blocks;
    cursor.index = 0u;
    while ((source = next_mapping(&cursor)) != NULL) {

        if (source->active != 0u ||
            source->process_id != destination_process_id)
            continue;
        source->active = 1u;
        ++source->area->mapping_references;
        ++pool_stats.active_mappings;
        ++pool_stats.map_operations;
    }
    if (pool_stats.active_mappings > pool_stats.max_active_mappings)
        pool_stats.max_active_mappings = pool_stats.active_mappings;
    return KERNEL_AREA_OK;

unavailable:
    cursor.block = mapping_blocks;
    cursor.index = 0u;
    while ((source = next_mapping(&cursor)) != NULL) {

        if (source->active != 0u ||
            source->process_id != destination_process_id)
            continue;
        if (!release_mapping(source))
            goto corrupt;
    }
    return failure;

corrupt:
    cursor.block = mapping_blocks;
    cursor.index = 0u;
    while ((source = next_mapping(&cursor)) != NULL) {

        if (source->active != 0u ||
            source->process_id != destination_process_id)
            continue;
        (void)release_mapping(source);
    }
    pool_corrupt = 1u;
    return KERNEL_AREA_CORRUPT;
}

/*
 * Finds the area a user address falls in, and proves the caller holds it.
 * Both the fault path and the decommit path need exactly this, and needing it
 * twice is what makes it a function rather than two copies that drift.
 */
static KernelAreaMapping *authorised_mapping(
    uint32_t process_id, const KernelAddressSpace *space, uint32_t address)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;

    if (process_id == 0u || space == NULL || address < KERNEL_VM_AREA_BASE)
        return NULL;
    if (address >= KERNEL_VM_AREA_BASE +
                       KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE)
        return NULL;
    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active != 0u && mapping->process_id == process_id &&
            mapping->space == space && valid_area(mapping->area) &&
            mapping->area->state == KERNEL_AREA_LIVE &&
            address >= mapping->virtual_base &&
            address - mapping->virtual_base < mapping->area->byte_size)
            return mapping;
    }
    return NULL;
}

/*
 * Commits one cluster of a reserved area and publishes it everywhere the area
 * is already mapped.
 *
 * Publishing into every mapping rather than only the faulting one is what
 * keeps an area a single object: two processes sharing one have to see the
 * same bytes at the same offset, so a page that exists for one of them exists
 * for all of them. The alternative -- letting each address space fault its own
 * pages in -- would need per-mapping page state and would buy nothing, because
 * the frame is committed either way the moment anybody touches it.
 *
 * Everything is checked before anything is published, and a failure withdraws
 * what it managed, so a refused commit leaves the area exactly as it was.
 */
static bool commit_cluster(KernelArea *area, uint32_t page)
{
    uint32_t aligned;
    uint32_t limit;
    uint32_t first;
    uint32_t count = 0u;
    uint32_t allocated = 0u;
    uint32_t published = 0u;
    uint32_t *entries;
    uint32_t frames[KERNEL_AREA_COMMIT_CLUSTER_PAGES];

    /*
     * The run is grown outward from the faulting page rather than forward
     * from the cluster's first page, and the difference is not cosmetic:
     * decommit can punch a hole into the middle of a committed cluster, and
     * scanning from the base would find the base occupied, commit nothing,
     * and leave the fault unanswered -- which retires the process for
     * touching an address the reservation says is its own.
     *
     * It stops at the cluster's bounds, at the end of the area, and at any
     * page somebody already committed, so a fault always gets the maximal
     * absent run containing its own address and never re-does work.
     */
    aligned = page - (page % KERNEL_AREA_COMMIT_CLUSTER_PAGES);
    limit = aligned + KERNEL_AREA_COMMIT_CLUSTER_PAGES;
    if (limit > area->page_count)
        limit = area->page_count;
    if (page >= limit || page_committed(area, page))
        return false;
    first = page;
    while (first > aligned && !page_committed(area, first - 1u))
        --first;
    while (first + count < limit && !page_committed(area, first + count))
        ++count;
    if (count == 0u)
        return false;
    if (!ensure_page_leaf(area, first)) {
        ++pool_stats.allocation_failures;
        ++pool_stats.commit_failures;
        return false;
    }
    entries = page_entries(area, first);
    if (entries == NULL ||
        first / AREA_PAGE_LEAF_ENTRIES !=
            (first + count - 1u) / AREA_PAGE_LEAF_ENTRIES) {
        pool_corrupt = 1u;
        ++pool_stats.commit_failures;
        return false;
    }
    while (allocated < count) {
        if (kernel_memory_alloc_pages_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_AREA_PAGES, 1u, KERNEL_FRAME_SHARED,
                area->frame_owner, &frames[allocated]) != KERNEL_MEMORY_OK)
            break;
        ++allocated;
    }
    if (allocated != count) {
        while (allocated != 0u) {
            --allocated;
            (void)kernel_memory_release(frames[allocated], 1u,
                                        area->frame_owner);
        }
        ++pool_stats.allocation_failures;
        ++pool_stats.commit_failures;
        return false;
    }
    for (uint32_t index = 0u; index < count; ++index)
        entries[index] = frames[index];

    {
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while ((mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active == 0u || mapping->area != area)
                continue;
            if (kernel_vm_map_shared_range(
                    mapping->space,
                    area_page_address(mapping->virtual_base, first),
                    entries, count, area->frame_owner,
                    mapping->permissions) != KERNEL_VM_OK)
                break;
            ++published;
        }
    }
    if (published != area->mapping_references) {
        uint32_t withdrawn = 0u;
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while (withdrawn < published &&
               (mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active == 0u || mapping->area != area)
                continue;
            if (kernel_vm_unmap_shared_range(
                    mapping->space,
                    area_page_address(mapping->virtual_base, first),
                    entries, count, area->frame_owner) != KERNEL_VM_OK) {
                pool_corrupt = 1u;
                return false;
            }
            ++withdrawn;
        }
        for (uint32_t index = 0u; index < count; ++index) {
            if (kernel_memory_release(frames[index], 1u, area->frame_owner) !=
                KERNEL_MEMORY_OK)
                pool_corrupt = 1u;
            entries[index] = AREA_PAGE_ABSENT;
        }
        ++pool_stats.commit_failures;
        return false;
    }
    area->committed_pages += count;
    pool_stats.committed_pages += count;
    if (pool_stats.committed_pages > pool_stats.max_committed_pages)
        pool_stats.max_committed_pages = pool_stats.committed_pages;
    ++pool_stats.commit_faults;
    pool_stats.commit_pages += count;
    return true;
}

bool kernel_area_fault(uint32_t process_id, KernelAddressSpace *space,
                       uint32_t address)
{
    uint32_t page;
    KernelAreaMapping *mapping;
    /*
     * Authority, and the reason this is not simply an address test: the
     * faulting process must already hold a mapping of this area. Without the
     * check any process could spend an area owner's frames by reading into a
     * window it was never given.
     */
    KernelArea *area;

    mapping = authorised_mapping(process_id, space, address);
    if (mapping == NULL || mapping->area->reserved_form == 0u)
        return false;
    area = mapping->area;
    page = (address - mapping->virtual_base) / KERNEL_PAGE_SIZE;
    if (page >= area->page_count || page_committed(area, page))
        return false;
    return commit_cluster(area, page);
}


/*
 * Withdraws one page from every address space holding the area, then releases
 * the frame. The order matters: a frame released while a descriptor still
 * pointed at it would be a page the owner could read after it belonged to
 * somebody else.
 */
static bool drop_page(KernelArea *area, uint32_t page)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;
    uint32_t *entry = page_entries(area, page);
    uint32_t withdrawn = 0u;

    if (entry == NULL || *entry == AREA_PAGE_ABSENT) {
        pool_corrupt = 1u;
        return false;
    }

    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active == 0u || mapping->area != area)
            continue;
        if (kernel_vm_unmap_shared_range(
                mapping->space,
                area_page_address(mapping->virtual_base, page),
                entry, 1u, area->frame_owner) != KERNEL_VM_OK) {
            pool_corrupt = 1u;
            return false;
        }
        ++withdrawn;
    }
    if (withdrawn != area->mapping_references) {
        pool_corrupt = 1u;
        return false;
    }
    if (kernel_memory_release(*entry, 1u,
                              area->frame_owner) != KERNEL_MEMORY_OK) {
        pool_corrupt = 1u;
        return false;
    }
    *entry = AREA_PAGE_ABSENT;
    if (area->committed_pages == 0u || pool_stats.committed_pages == 0u) {
        pool_corrupt = 1u;
        return false;
    }
    --area->committed_pages;
    --pool_stats.committed_pages;
    return true;
}

KernelAreaStatus kernel_area_decommit(uint32_t process_id,
                                      KernelAddressSpace *space,
                                      uint32_t address, uint32_t byte_size,
                                      uint32_t *released_pages)
{
    KernelAreaMapping *mapping;
    KernelArea *area;
    uint32_t first;
    uint32_t last;
    uint32_t offset;
    uint32_t released = 0u;

    if (released_pages == NULL || byte_size == 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    *released_pages = 0u;
    mapping = authorised_mapping(process_id, space, address);
    if (mapping == NULL)
        return KERNEL_AREA_NOT_MAPPED;
    area = mapping->area;
    /*
     * An ordinary area is committed by definition -- its whole extent is its
     * identity, and something is reading it. Only a reserved one has pages
     * that were always going to come and go.
     */
    if (area->reserved_form == 0u)
        return KERNEL_AREA_INVALID_STATE;
    offset = address - mapping->virtual_base;
    if (byte_size > area->byte_size || offset > area->byte_size - byte_size)
        return KERNEL_AREA_INVALID_ARGUMENT;
    /* Round inward: a partly covered page keeps whatever else is in it. */
    first = (offset + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
    last = (offset + byte_size) / KERNEL_PAGE_SIZE;
    for (uint32_t page = first; page < last; ++page) {
        if (!page_committed(area, page))
            continue;
        if (!drop_page(area, page))
            return KERNEL_AREA_CORRUPT;
        ++released;
    }
    pool_stats.decommit_operations += released != 0u ? 1u : 0u;
    pool_stats.decommit_pages += released;
    *released_pages = released;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_unmap(uint32_t process_id,
                                   KernelAddressSpace *space,
                                   uint32_t virtual_base)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;

    if (process_id == 0u || space == NULL ||
        virtual_base < KERNEL_VM_AREA_BASE ||
        virtual_base >= KERNEL_VM_AREA_BASE +
                            KERNEL_VM_AREA_SLOT_COUNT *
                                KERNEL_VM_AREA_SLOT_SIZE ||
        (virtual_base & (KERNEL_PAGE_SIZE - 1u)) != 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active != 0u && mapping->process_id == process_id &&
            mapping->space == space && mapping->virtual_base == virtual_base)
            return unmap_record(mapping, false);
    }
    return KERNEL_AREA_NOT_MAPPED;
}

KernelAreaStatus kernel_area_process_died(uint32_t process_id,
                                          uint32_t *closed_areas,
                                          uint32_t *revoked_mappings)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;
    uint32_t survived = 0u;
    uint32_t revoked = 0u;

    if (process_id == 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    for (uint32_t slot = 0u; slot < area_backed_limit; ++slot) {
        KernelArea *area = area_at(slot);

        if (area != NULL && area->state == KERNEL_AREA_LIVE &&
            area->creator == process_id)
            ++survived;
    }
    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active == 0u || mapping->process_id != process_id)
            continue;
        if (unmap_record(mapping, true) != KERNEL_AREA_OK)
            return KERNEL_AREA_CORRUPT;
        ++revoked;
    }
    if (survived != 0u)
        ++pool_stats.owner_deaths;
    if (closed_areas != NULL)
        *closed_areas = 0u;
    if (revoked_mappings != NULL)
        *revoked_mappings = revoked;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_unmap_process(uint32_t process_id,
                                           uint32_t *unmapped)
{
    KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
    KernelAreaMapping *mapping;
    uint32_t count = 0u;

    if (process_id == 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    while ((mapping = next_mapping(&cursor)) != NULL) {
        if (mapping->active == 0u || mapping->process_id != process_id)
            continue;
        if (unmap_record(mapping, false) != KERNEL_AREA_OK)
            return KERNEL_AREA_CORRUPT;
        ++count;
    }
    if (unmapped != NULL)
        *unmapped = count;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_write(KernelArea *area, uint32_t offset,
                                   const void *source, uint32_t size)
{
    const uint8_t *input = source;

    if (source == NULL || size == 0u || !range_valid(area, offset, size))
        return KERNEL_AREA_INVALID_ARGUMENT;
    while (size != 0u) {
        uint32_t page = offset / KERNEL_PAGE_SIZE;
        uint32_t page_offset = offset & (KERNEL_PAGE_SIZE - 1u);
        uint32_t chunk = KERNEL_PAGE_SIZE - page_offset;
        volatile uint8_t *output;

        if (chunk > size)
            chunk = size;
        /*
         * The kernel reached this page before the owner's own access did, so
         * the commit that access would have caused has to happen here instead
         * -- the same reversal the user stack's copy path makes. Refusing
         * would fail a write to a perfectly good offset of a reserved area.
         */
        if (!page_committed(area, page) && !commit_cluster(area, page))
            return KERNEL_AREA_OUT_OF_MEMORY;
        uint32_t *entry = page_entries(area, page);

        if (entry == NULL ||
            !physical_pointer(*entry + page_offset, &output))
            return KERNEL_AREA_CORRUPT;
        for (uint32_t index = 0u; index < chunk; ++index)
            output[index] = input[index];
        input += chunk;
        offset += chunk;
        size -= chunk;
    }
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_read(const KernelArea *area, uint32_t offset,
                                  void *destination, uint32_t size)
{
    uint8_t *output = destination;

    if (destination == NULL || size == 0u || !range_valid(area, offset, size))
        return KERNEL_AREA_INVALID_ARGUMENT;
    while (size != 0u) {
        uint32_t page = offset / KERNEL_PAGE_SIZE;
        uint32_t page_offset = offset & (KERNEL_PAGE_SIZE - 1u);
        uint32_t chunk = KERNEL_PAGE_SIZE - page_offset;
        volatile uint8_t *input;

        if (chunk > size)
            chunk = size;
        /*
         * An uncommitted page reads as zeros, because that is what it would
         * read as the instant it were committed -- every area frame is handed
         * out zeroed. Spending a frame to say so would be paying for the one
         * access that does not need the memory to exist.
         */
        if (!page_committed(area, page)) {
            for (uint32_t index = 0u; index < chunk; ++index)
                output[index] = 0u;
            output += chunk;
            offset += chunk;
            size -= chunk;
            continue;
        }
        uint32_t *entry = page_entries(area, page);

        if (entry == NULL ||
            !physical_pointer(*entry + page_offset, &input))
            return KERNEL_AREA_CORRUPT;
        for (uint32_t index = 0u; index < chunk; ++index)
            output[index] = input[index];
        output += chunk;
        offset += chunk;
        size -= chunk;
    }
    return KERNEL_AREA_OK;
}

bool kernel_area_live(const KernelArea *area)
{
    return valid_area(area) && area->state == KERNEL_AREA_LIVE;
}

uint32_t kernel_area_creator(const KernelArea *area)
{
    return valid_area(area) ? area->creator : 0u;
}

uint32_t kernel_area_generation(const KernelArea *area)
{
    return valid_area(area) ? area->generation : 0u;
}

uint32_t kernel_area_size(const KernelArea *area)
{
    return valid_area(area) ? area->byte_size : 0u;
}

bool kernel_area_snapshot(uint32_t slot, KernelAreaSnapshot *snapshot)
{
    const KernelArea *area;

    if (slot >= KERNEL_AREA_MAX || snapshot == NULL)
        return false;
    area = area_at(slot);
    if (area == NULL)
        return false;
    snapshot->creator = area->creator;
    snapshot->frame_owner = area->frame_owner;
    snapshot->generation = area->generation;
    snapshot->byte_size = area->byte_size;
    snapshot->terminal_result = area->terminal_result;
    snapshot->handle_references = area->handle_references;
    snapshot->child_references = area->child_references;
    snapshot->mapping_references = area->mapping_references;
    snapshot->page_count = area->page_count;
    snapshot->committed_pages = area->committed_pages;
    snapshot->state = area->state;
    snapshot->frames_released = area->frames_released;
    snapshot->reserved_form = area->reserved_form;
    return true;
}

bool kernel_area_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_area_pool_valid(void)
{
    KernelAllocationStats area_allocation;
    KernelAllocationStats area_metadata_allocation;
    KernelAllocationStats page_metadata_allocation;
    KernelAllocationStats mapping_allocation;
    KernelAllocationStats metadata_allocation;
    KernelAreaMappingBlock *slow = mapping_blocks;
    KernelAreaMappingBlock *fast = mapping_blocks;
    KernelAreaMappingBlock *last = NULL;
    uint32_t active = 0u;
    uint32_t closing = 0u;
    uint32_t committed = 0u;
    uint32_t active_mappings = 0u;
    uint32_t block_count = 0u;
    uint32_t area_leaves = 0u;
    uint32_t page_leaves = 0u;

    if (!kernel_area_pool_healthy() ||
        area_backed_limit > KERNEL_AREA_MAX ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_OBJECT, &area_allocation) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_OBJECT_METADATA,
            &area_metadata_allocation) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_PAGE_METADATA,
            &page_metadata_allocation) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_MAPPING, &mapping_allocation) ||
        !kernel_allocation_site_stats(
            KERNEL_ALLOCATION_SITE_AREA_MAPPING_METADATA,
            &metadata_allocation))
        return false;
    while (area_leaves < AREA_LEAF_COUNT &&
           area_directory[area_leaves] != NULL)
        ++area_leaves;
    for (uint32_t leaf = area_leaves; leaf < AREA_LEAF_COUNT; ++leaf) {
        if (area_directory[leaf] != NULL ||
            area_directory_physical[leaf] != 0u)
            return false;
    }
    {
        uint32_t expected_limit = area_leaves * AREA_LEAF_ENTRIES;

        if (expected_limit > KERNEL_AREA_MAX)
            expected_limit = KERNEL_AREA_MAX;
        if (area_backed_limit != expected_limit ||
            area_metadata_allocation.current_units !=
                area_leaves * AREA_LEAF_FRAMES ||
            area_metadata_allocation.current_bytes !=
                area_leaves * AREA_LEAF_FRAMES * KERNEL_PAGE_SIZE)
            return false;
    }
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast)
            return false;
    }
    for (KernelAreaMappingBlock *block = mapping_blocks;
         block != NULL; block = block->next) {
        if (block->physical == 0u || block->reserved != 0u ||
            block->capacity !=
                (KERNEL_PAGE_SIZE - sizeof(*block)) /
                    sizeof(block->records[0]))
            return false;
        ++block_count;
        last = block;
    }
    if (last != mapping_blocks_tail ||
        (mapping_blocks == NULL) != (mapping_blocks_tail == NULL) ||
        metadata_allocation.current_units != block_count ||
        metadata_allocation.current_bytes != block_count * KERNEL_PAGE_SIZE)
        return false;
    for (uint32_t slot = 0u; slot < area_backed_limit; ++slot) {
        const KernelArea *area = area_at(slot);
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;
        uint32_t mapping_count = 0u;

        if (area == NULL || area->slot != slot || area->generation == 0u ||
            area->generation > AREA_GENERATION_MASK)
            return false;
        if (area->state == KERNEL_AREA_FREE) {
            if (area->creator != 0u || area->frame_owner != 0u ||
                area->handle_references != 0u ||
                area->child_references != 0u ||
                area->mapping_references != 0u ||
                area->frames_released == 0u)
                return false;
            for (uint32_t leaf = 0u; leaf < AREA_PAGE_LEAF_COUNT; ++leaf) {
                if (area->page_directory[leaf] != NULL ||
                    area->page_directory_physical[leaf] != 0u)
                    return false;
            }
            continue;
        }
        if (!valid_area(area) || area->creator == 0u ||
            area->frame_owner == 0u || area->page_count == 0u ||
            area->page_count > KERNEL_AREA_PAGE_MAX ||
            area->byte_size != area->page_count * KERNEL_PAGE_SIZE)
            return false;
        ++active;
        if (area->state == KERNEL_AREA_CLOSING)
            ++closing;
        /*
         * The committed count and the page array have to agree, or every
         * quota answer derived from the count is fiction.
         */
        {
            uint32_t present = 0u;

            for (uint32_t leaf = 0u; leaf < AREA_PAGE_LEAF_COUNT; ++leaf) {
                if ((area->page_directory[leaf] == NULL) !=
                    (area->page_directory_physical[leaf] == 0u))
                    return false;
                if (area->page_directory[leaf] != NULL)
                    ++page_leaves;
            }
            for (uint32_t page = 0u; page < area->page_count; ++page) {
                if (page_committed(area, page))
                    ++present;
            }
            if (present != area->committed_pages ||
                area->committed_pages > area->page_count ||
                (area->reserved_form == 0u && area->frames_released == 0u &&
                 area->committed_pages != area->page_count))
                return false;
        }
        committed += area->committed_pages;
        while ((mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active != 0u && mapping->area == area)
                ++mapping_count;
        }
        if (mapping_count != area->mapping_references)
            return false;
    }
    {
        KernelAreaMappingCursor cursor = {mapping_blocks, 0u};
        KernelAreaMapping *mapping;

        while ((mapping = next_mapping(&cursor)) != NULL) {
            if (mapping->active == 0u) {
                if (mapping->space != NULL || mapping->area != NULL ||
                    mapping->process_id != 0u ||
                    mapping->virtual_base != 0u ||
                    mapping->permissions != 0u)
                    return false;
                continue;
            }
            if (!valid_area(mapping->area) ||
                mapping->area->state != KERNEL_AREA_LIVE ||
                mapping->space == NULL || mapping->process_id == 0u ||
                mapping->virtual_base < KERNEL_VM_AREA_BASE ||
                mapping->virtual_base >= KERNEL_VM_AREA_BASE +
                                             KERNEL_VM_AREA_SLOT_COUNT *
                                                 KERNEL_VM_AREA_SLOT_SIZE ||
                (mapping->virtual_base & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
                mapping->area->page_count * KERNEL_PAGE_SIZE >
                    KERNEL_VM_AREA_BASE +
                        KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE -
                        mapping->virtual_base)
                return false;
            {
                KernelAreaMappingCursor prior = {mapping_blocks, 0u};
                KernelAreaMapping *candidate;

                while ((candidate = next_mapping(&prior)) != mapping) {
                    if (candidate == NULL)
                        return false;
                    if (candidate->active != 0u &&
                        candidate->process_id == mapping->process_id &&
                        candidate->space == mapping->space &&
                        candidate->virtual_base <
                            mapping->virtual_base +
                                mapping->area->page_count *
                                    KERNEL_PAGE_SIZE &&
                        mapping->virtual_base <
                            candidate->virtual_base +
                                candidate->area->page_count *
                                    KERNEL_PAGE_SIZE)
                        return false;
                }
            }
            ++active_mappings;
        }
    }
    return active == pool_stats.active_areas &&
           closing == pool_stats.closing_areas &&
           committed == pool_stats.committed_pages &&
           active_mappings == pool_stats.active_mappings &&
           mapping_allocation.current_units == active_mappings &&
           mapping_allocation.current_bytes ==
               active_mappings * sizeof(KernelAreaMapping) &&
           area_allocation.current_units == active &&
           area_allocation.current_bytes == active * sizeof(KernelArea) &&
           page_metadata_allocation.current_units == page_leaves &&
           page_metadata_allocation.current_bytes ==
               page_leaves * KERNEL_PAGE_SIZE &&
           active <= KERNEL_AREA_MAX;
}

bool kernel_area_pool_stats(KernelAreaPoolStats *stats)
{
    if (stats == NULL || !kernel_area_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    return true;
}
