#include "area.h"

#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "object_cache.h"

#include <stddef.h>

#define AREA_FRAME_OWNER_PREFIX 0x40000000u
#define AREA_GENERATION_MASK 0x000fffffu

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

struct KernelArea {
    uint32_t physical_pages[KERNEL_AREA_PAGE_MAX];
    uint32_t creator;
    uint32_t frame_owner;
    uint32_t generation;
    uint32_t byte_size;
    uint32_t virtual_base;
    uint32_t terminal_result;
    uint16_t handle_references;
    uint16_t child_references;
    uint16_t mapping_references;
    uint16_t page_count;
    uint8_t slot;
    uint8_t state;
    uint8_t frames_released;
    uint8_t reserved[2];
};

#if defined(__m68k__)
_Static_assert(sizeof(KernelArea) <= 2088u,
               "area record exceeds its memory budget");
_Static_assert(sizeof(KernelAreaMapping) == 24u,
               "area mapping size changed; update the memory budget");
#endif

static KernelArea areas[KERNEL_AREA_MAX];
static KernelAreaMapping mappings[KERNEL_AREA_MAPPING_MAX];
static KernelObjectCache area_cache;
static KernelObjectCache mapping_cache;
static uint32_t area_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_AREA_MAX)];
static uint32_t mapping_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_AREA_MAPPING_MAX)];
static KernelAreaPoolStats pool_stats;
static uint8_t pool_corrupt;

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

static bool valid_area(const KernelArea *area)
{
    return area != NULL && area >= &areas[0] && area < &areas[KERNEL_AREA_MAX] &&
           area->slot == (uint8_t)(area - areas) && area->generation != 0u &&
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

static void reset_area(KernelArea *area, uint8_t slot)
{
    uint32_t generation = area->generation;

    kernel_bytes_clear(area, sizeof(*area));
    area->generation = generation;
    area->slot = slot;
    area->state = KERNEL_AREA_FREE;
    area->frames_released = 1u;
}

static uint32_t make_frame_owner(uint32_t generation, uint32_t slot)
{
    return AREA_FRAME_OWNER_PREFIX | (generation << 4u) | (slot + 1u);
}

static void creator_usage(uint32_t creator, uint32_t *area_count,
                          uint32_t *page_count)
{
    uint32_t objects = 0u;
    uint32_t pages = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAX; ++slot) {
        const KernelArea *area = &areas[slot];

        if (area->state == KERNEL_AREA_FREE || area->creator != creator)
            continue;
        ++objects;
        if (area->frames_released == 0u)
            pages += area->page_count;
    }
    *area_count = objects;
    *page_count = pages;
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
    if (kernel_vm_unmap_shared_range(
            mapping->space, mapping->virtual_base, area->physical_pages,
            area->page_count, area->frame_owner) != KERNEL_VM_OK)
        return KERNEL_AREA_CORRUPT;
    if (area->mapping_references == 0u || pool_stats.active_mappings == 0u)
        return KERNEL_AREA_CORRUPT;
    --area->mapping_references;
    --pool_stats.active_mappings;
    ++pool_stats.unmap_operations;
    if (revoked)
        ++pool_stats.revoked_mappings;
    reset_mapping(mapping);
    if (kernel_object_cache_release(&mapping_cache, mapping) !=
        KERNEL_OBJECT_CACHE_OK)
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
    reset_area(area, area->slot);
    if (kernel_object_cache_release(&area_cache, area) !=
        KERNEL_OBJECT_CACHE_OK)
        pool_corrupt = 1u;
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

    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot) {
        if (mappings[slot].active != 0u && mappings[slot].area == area &&
            unmap_record(&mappings[slot], true) != KERNEL_AREA_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
    }
    if (area->mapping_references != 0u || area->frames_released != 0u ||
        pool_stats.committed_pages < area->page_count) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    for (uint32_t page = 0u; page < area->page_count; ++page) {
        if (kernel_memory_release(area->physical_pages[page], 1u,
                                  area->frame_owner) != KERNEL_MEMORY_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        area->physical_pages[page] = 0u;
    }
    pool_stats.committed_pages -= area->page_count;
    area->frames_released = 1u;
    maybe_free(area);
    return pool_corrupt == 0u ? KERNEL_AREA_OK : KERNEL_AREA_CORRUPT;
}

void kernel_area_pool_init(void)
{
    if (!kernel_object_cache_init(
            &area_cache, areas, sizeof(areas[0]), KERNEL_AREA_MAX,
            area_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_AREA_MAX),
            KERNEL_ALLOCATION_SITE_AREA_OBJECT) ||
        !kernel_object_cache_init(
            &mapping_cache, mappings, sizeof(mappings[0]),
            KERNEL_AREA_MAPPING_MAX, mapping_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_AREA_MAPPING_MAX),
            KERNEL_ALLOCATION_SITE_AREA_MAPPING)) {
        pool_corrupt = 1u;
        return;
    }
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAX; ++slot) {
        uint32_t generation = areas[slot].generation;

        reset_area(&areas[slot], (uint8_t)slot);
        areas[slot].generation = generation == 0u ? 1u : generation;
    }
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot)
        reset_mapping(&mappings[slot]);
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
                                    KernelArea **result)
{
    KernelArea *area = NULL;
    void *raw_area;
    uint16_t area_slot;
    KernelObjectCacheStatus cache_status;
    uint32_t creator_areas;
    uint32_t creator_pages;
    uint32_t page_count;

    if (creator == 0u || byte_size == 0u || result == NULL)
        return KERNEL_AREA_INVALID_ARGUMENT;
    *result = NULL;
    if (byte_size > KERNEL_AREA_PAGE_MAX * KERNEL_PAGE_SIZE)
        return KERNEL_AREA_INVALID_ARGUMENT;
    page_count = (byte_size + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
    creator_usage(creator, &creator_areas, &creator_pages);
    if (creator_areas >= KERNEL_AREA_OWNER_MAX ||
        page_count > KERNEL_AREA_OWNER_PAGE_MAX - creator_pages ||
        page_count > KERNEL_AREA_SYSTEM_PAGE_MAX -
                         pool_stats.committed_pages) {
        ++pool_stats.quota_failures;
        return KERNEL_AREA_QUOTA_EXCEEDED;
    }
    cache_status = kernel_object_cache_claim(
        &area_cache, creator, &raw_area, &area_slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE) {
        ++pool_stats.allocation_failures;
        return KERNEL_AREA_NO_SLOT;
    }
    if (cache_status != KERNEL_OBJECT_CACHE_OK ||
        area_slot >= KERNEL_AREA_MAX) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    area = raw_area;
    if (area->state != KERNEL_AREA_FREE || area->slot != area_slot) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }

    area->generation = kernel_generation_next_masked(
        area->generation, AREA_GENERATION_MASK);
    area->creator = creator;
    area->frame_owner = make_frame_owner(area->generation, area->slot);
    area->byte_size = page_count * KERNEL_PAGE_SIZE;
    area->virtual_base = KERNEL_VM_AREA_BASE +
                         (uint32_t)area->slot * KERNEL_VM_AREA_SLOT_SIZE;
    area->terminal_result = 0u;
    area->handle_references = 1u;
    area->child_references = 0u;
    area->mapping_references = 0u;
    area->page_count = (uint16_t)page_count;
    area->state = KERNEL_AREA_RESERVED;
    area->frames_released = 1u;
#if defined(KERNEL_AREA_HOST_TEST)
    if (consume_test_fault(KERNEL_AREA_TEST_FAULT_CREATE_AFTER_RESERVE)) {
        reset_area(area, area->slot);
        if (kernel_object_cache_release(&area_cache, area) !=
            KERNEL_OBJECT_CACHE_OK)
            pool_corrupt = 1u;
        ++pool_stats.allocation_failures;
        return KERNEL_AREA_OUT_OF_MEMORY;
    }
#endif
    if (kernel_memory_alloc_pages_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_AREA_PAGES, page_count,
            KERNEL_FRAME_SHARED, area->frame_owner,
            area->physical_pages) != KERNEL_MEMORY_OK) {
        reset_area(area, area->slot);
        if (kernel_object_cache_release(&area_cache, area) !=
            KERNEL_OBJECT_CACHE_OK)
            pool_corrupt = 1u;
        ++pool_stats.allocation_failures;
        return KERNEL_AREA_OUT_OF_MEMORY;
    }
    area->frames_released = 0u;
#if defined(KERNEL_AREA_HOST_TEST)
    if (consume_test_fault(
            KERNEL_AREA_TEST_FAULT_CREATE_AFTER_FRAME_ALLOCATE)) {
        bool cleanup_failed = false;

        for (uint32_t page = 0u; page < page_count; ++page) {
            if (kernel_memory_release(area->physical_pages[page], 1u,
                                      area->frame_owner) !=
                KERNEL_MEMORY_OK)
                cleanup_failed = true;
            area->physical_pages[page] = 0u;
        }
        reset_area(area, area->slot);
        if (kernel_object_cache_release(&area_cache, area) !=
            KERNEL_OBJECT_CACHE_OK)
            cleanup_failed = true;
        ++pool_stats.allocation_failures;
        if (cleanup_failed) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        return KERNEL_AREA_OUT_OF_MEMORY;
    }
#endif
    area->state = KERNEL_AREA_LIVE;
    ++pool_stats.created_areas;
    ++pool_stats.active_areas;
    pool_stats.committed_pages += page_count;
    if (pool_stats.active_areas > pool_stats.max_active_areas)
        pool_stats.max_active_areas = pool_stats.active_areas;
    if (pool_stats.committed_pages > pool_stats.max_committed_pages)
        pool_stats.max_committed_pages = pool_stats.committed_pages;
    *result = area;
    return KERNEL_AREA_OK;
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
    void *raw_mapping;
    uint16_t mapping_slot;
    KernelObjectCacheStatus cache_status;
    uint32_t process_mappings = 0u;
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
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot) {
        KernelAreaMapping *mapping = &mappings[slot];

        if (mapping->active == 0u) {
            continue;
        }
        if (mapping->process_id == process_id)
            ++process_mappings;
        if (mapping->process_id == process_id && mapping->area == area) {
            if (mapping->space != space || mapping->permissions != permissions)
                return KERNEL_AREA_ACCESS_DENIED;
            *virtual_base = mapping->virtual_base;
            *byte_size = area->byte_size;
            return KERNEL_AREA_OK;
        }
    }
    if (process_mappings >= KERNEL_AREA_PROCESS_MAPPING_MAX)
        return KERNEL_AREA_QUOTA_EXCEEDED;
    cache_status = kernel_object_cache_claim(
        &mapping_cache, process_id, &raw_mapping, &mapping_slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
        return KERNEL_AREA_NO_SLOT;
    if (cache_status != KERNEL_OBJECT_CACHE_OK ||
        mapping_slot >= KERNEL_AREA_MAPPING_MAX) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }
    free_mapping = raw_mapping;
    if (free_mapping->active != 0u) {
        pool_corrupt = 1u;
        return KERNEL_AREA_CORRUPT;
    }

    vm_status = kernel_vm_map_shared_range(
        space, area->virtual_base, area->physical_pages, area->page_count,
        area->frame_owner, permissions);
    if (vm_status != KERNEL_VM_OK) {
        ++pool_stats.map_rollbacks;
        reset_mapping(free_mapping);
        if (kernel_object_cache_release(&mapping_cache, free_mapping) !=
            KERNEL_OBJECT_CACHE_OK) {
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
        if (kernel_vm_unmap_shared_range(
                space, area->virtual_base, area->physical_pages,
                area->page_count, area->frame_owner) != KERNEL_VM_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        reset_mapping(free_mapping);
        if (kernel_object_cache_release(&mapping_cache, free_mapping) !=
            KERNEL_OBJECT_CACHE_OK) {
            pool_corrupt = 1u;
            return KERNEL_AREA_CORRUPT;
        }
        return KERNEL_AREA_OUT_OF_MEMORY;
    }
#endif
    free_mapping->space = space;
    free_mapping->area = area;
    free_mapping->process_id = process_id;
    free_mapping->virtual_base = area->virtual_base;
    free_mapping->permissions = permissions;
    free_mapping->active = 1u;
    ++area->mapping_references;
    ++pool_stats.active_mappings;
    ++pool_stats.map_operations;
    if (pool_stats.active_mappings > pool_stats.max_active_mappings)
        pool_stats.max_active_mappings = pool_stats.active_mappings;
    *virtual_base = area->virtual_base;
    *byte_size = area->byte_size;
    return KERNEL_AREA_OK;
}

KernelAreaStatus kernel_area_unmap(uint32_t process_id,
                                   KernelAddressSpace *space,
                                   uint32_t virtual_base)
{
    if (process_id == 0u || space == NULL ||
        virtual_base < KERNEL_VM_AREA_BASE ||
        virtual_base >= KERNEL_VM_AREA_BASE +
                            KERNEL_VM_AREA_SLOT_COUNT *
                                KERNEL_VM_AREA_SLOT_SIZE ||
        (virtual_base & (KERNEL_VM_AREA_SLOT_SIZE - 1u)) != 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot) {
        KernelAreaMapping *mapping = &mappings[slot];

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
    uint32_t closed = 0u;
    uint32_t revoked = 0u;

    if (process_id == 0u)
        return KERNEL_AREA_INVALID_ARGUMENT;
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAX; ++slot) {
        KernelArea *area = &areas[slot];

        if (area->state == KERNEL_AREA_LIVE && area->creator == process_id) {
            uint32_t before = pool_stats.revoked_mappings;

            if (close_area(area, ASTRA_SYSCALL_PEER_DEAD) != KERNEL_AREA_OK)
                return KERNEL_AREA_CORRUPT;
            revoked += pool_stats.revoked_mappings - before;
            ++closed;
        }
    }
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot) {
        if (mappings[slot].active != 0u &&
            mappings[slot].process_id == process_id) {
            if (unmap_record(&mappings[slot], true) != KERNEL_AREA_OK)
                return KERNEL_AREA_CORRUPT;
            ++revoked;
        }
    }
    if (closed != 0u)
        ++pool_stats.owner_deaths;
    if (closed_areas != NULL)
        *closed_areas = closed;
    if (revoked_mappings != NULL)
        *revoked_mappings = revoked;
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
        if (!physical_pointer(area->physical_pages[page] + page_offset,
                              &output))
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
        if (!physical_pointer(area->physical_pages[page] + page_offset,
                              &input))
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
    area = &areas[slot];
    snapshot->creator = area->creator;
    snapshot->frame_owner = area->frame_owner;
    snapshot->generation = area->generation;
    snapshot->byte_size = area->byte_size;
    snapshot->virtual_base = area->virtual_base;
    snapshot->terminal_result = area->terminal_result;
    snapshot->handle_references = area->handle_references;
    snapshot->child_references = area->child_references;
    snapshot->mapping_references = area->mapping_references;
    snapshot->page_count = area->page_count;
    snapshot->state = area->state;
    snapshot->frames_released = area->frames_released;
    return true;
}

bool kernel_area_pool_healthy(void)
{
    return pool_corrupt == 0u;
}

bool kernel_area_pool_valid(void)
{
    uint32_t active = 0u;
    uint32_t closing = 0u;
    uint32_t committed = 0u;
    uint32_t active_mappings = 0u;

    if (!kernel_area_pool_healthy() ||
        !kernel_object_cache_valid(&area_cache) ||
        !kernel_object_cache_valid(&mapping_cache))
        return false;
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAX; ++slot) {
        const KernelArea *area = &areas[slot];
        uint32_t mapping_count = 0u;
        bool claimed = kernel_object_cache_slot_claimed(
            &area_cache, (uint16_t)slot);

        if (area->slot != slot || area->generation == 0u ||
            area->generation > AREA_GENERATION_MASK)
            return false;
        if (area->state == KERNEL_AREA_FREE) {
            if (claimed || area->creator != 0u || area->frame_owner != 0u ||
                area->handle_references != 0u ||
                area->child_references != 0u ||
                area->mapping_references != 0u ||
                area->frames_released == 0u)
                return false;
            continue;
        }
        if (!claimed)
            return false;
        if (!valid_area(area) || area->creator == 0u ||
            area->frame_owner == 0u || area->page_count == 0u ||
            area->page_count > KERNEL_AREA_PAGE_MAX ||
            area->byte_size != area->page_count * KERNEL_PAGE_SIZE)
            return false;
        ++active;
        if (area->state == KERNEL_AREA_CLOSING)
            ++closing;
        if (area->frames_released == 0u)
            committed += area->page_count;
        for (uint32_t map = 0u; map < KERNEL_AREA_MAPPING_MAX; ++map) {
            if (mappings[map].active != 0u && mappings[map].area == area)
                ++mapping_count;
        }
        if (mapping_count != area->mapping_references)
            return false;
    }
    for (uint32_t slot = 0u; slot < KERNEL_AREA_MAPPING_MAX; ++slot) {
        const KernelAreaMapping *mapping = &mappings[slot];
        bool claimed = kernel_object_cache_slot_claimed(
            &mapping_cache, (uint16_t)slot);

        if (mapping->active == 0u) {
            if (claimed || mapping->space != NULL || mapping->area != NULL ||
                mapping->process_id != 0u || mapping->virtual_base != 0u ||
                mapping->permissions != 0u)
                return false;
            continue;
        }
        if (!claimed)
            return false;
        if (!valid_area(mapping->area) ||
            mapping->area->state != KERNEL_AREA_LIVE ||
            mapping->space == NULL || mapping->process_id == 0u ||
            mapping->virtual_base != mapping->area->virtual_base)
            return false;
        ++active_mappings;
    }
    return active == pool_stats.active_areas &&
           closing == pool_stats.closing_areas &&
           committed == pool_stats.committed_pages &&
           active_mappings == pool_stats.active_mappings &&
           active <= KERNEL_AREA_MAX &&
           committed <= KERNEL_AREA_SYSTEM_PAGE_MAX &&
           active_mappings <= KERNEL_AREA_MAPPING_MAX;
}

bool kernel_area_pool_stats(KernelAreaPoolStats *stats)
{
    if (stats == NULL || !kernel_area_pool_valid())
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    return true;
}
