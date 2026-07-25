#include "vm.h"

#include "memory.h"
#include "pmmu.h"
#include "thread.h"

#include <stddef.h>

#define VM_ROOT_ENTRIES 1024u
#define VM_TABLE_ENTRIES 1024u
#define VM_ROOT_INDEX(address) ((address) >> 22)
#define VM_TABLE_INDEX(address) (((address) >> 12) & 0x3ffu)

#define VM_DESC_INVALID 0x00u
#define VM_DESC_PAGE 0x01u
#define VM_DESC_TABLE 0x02u
#define VM_DESC_WRITE_PROTECT 0x04u
#define VM_DESC_CACHE_INHIBIT 0x40u
#define VM_DESC_TYPE_MASK 0x03u
#define VM_DESC_PAGE_ADDRESS 0xfffff000u
#define VM_DESC_TABLE_ADDRESS 0xfffffff0u

#define VM_KERNEL_OWNER 1u
#define VM_SDRAM_BASE 0x02000000u
#define VM_SDRAM_ROOT_INDEX VM_ROOT_INDEX(VM_SDRAM_BASE)
#define VM_MAPPING_COUNT_SHIFT 4u
#define VM_MAPPING_CLASS_MASK 0x0fu
#define VM_MAPPING_PRIVATE_CLASS 0x0fu

#if defined(KERNEL_VM_HOST_TEST)
#define VM_KERNEL_STACK_GUARD 0x02080000u
#define VM_KERNEL_WORKER_STACK_GUARD 0x02083000u
#define VM_KERNEL_THREAD_STACKS_START \
    KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE
#define VM_KERNEL_THREAD_STACKS_END \
    (VM_KERNEL_THREAD_STACKS_START + \
     KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE)
#else
extern uint8_t _kernel_stack_guard[];
extern uint8_t _kernel_worker_stack_guard[];
extern uint8_t _kernel_thread_stacks_start[];
extern uint8_t _kernel_thread_stacks_end[];
#define VM_KERNEL_STACK_GUARD ((uint32_t)(uintptr_t)_kernel_stack_guard)
#define VM_KERNEL_WORKER_STACK_GUARD \
    ((uint32_t)(uintptr_t)_kernel_worker_stack_guard)
#define VM_KERNEL_THREAD_STACKS_START \
    ((uint32_t)(uintptr_t)_kernel_thread_stacks_start)
#define VM_KERNEL_THREAD_STACKS_END \
    ((uint32_t)(uintptr_t)_kernel_thread_stacks_end)
#endif

static uint32_t kernel_root_physical;
static uint32_t kernel_low_table_physical;
static uint32_t kernel_high_table_physical;
static uint32_t empty_root_physical;
static uint32_t current_user_root;
static uint8_t mapped_user_frames[KERNEL_MAX_FRAMES];
static KernelVmStats vm_stats;
static bool initialized;
static bool enabled;

static _Alignas(8) KernelPmmuRootPointer supervisor_root;
static _Alignas(8) KernelPmmuRootPointer current_process_root;
static _Alignas(4) uint32_t tc_disabled;
static _Alignas(4) uint32_t tc_enabled;
static _Alignas(4) uint32_t transparent_disabled;

#if defined(KERNEL_VM_HOST_TEST)
static uint8_t *host_memory;
static uint32_t host_memory_base;
static uint32_t host_memory_size;
static KernelVmSharedMapFault next_shared_map_fault;

void kernel_vm_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                         uint32_t size)
{
    host_memory = memory;
    host_memory_base = base;
    host_memory_size = size;
}

void kernel_vm_test_fail_next_shared_map(KernelVmSharedMapFault fault)
{
    next_shared_map_fault =
        fault < KERNEL_VM_SHARED_MAP_FAULT_COUNT ?
            fault : KERNEL_VM_SHARED_MAP_FAULT_NONE;
}

static bool consume_shared_map_fault(KernelVmSharedMapFault fault)
{
    if (next_shared_map_fault != fault)
        return false;
    next_shared_map_fault = KERNEL_VM_SHARED_MAP_FAULT_NONE;
    return true;
}
#endif

static volatile uint32_t *physical_words(uint32_t physical_address)
{
#if defined(KERNEL_VM_HOST_TEST)
    uint32_t offset;

    if (host_memory == NULL || host_memory_size < KERNEL_PAGE_SIZE ||
        physical_address < host_memory_base)
        return NULL;
    offset = physical_address - host_memory_base;
    if (offset > host_memory_size - KERNEL_PAGE_SIZE)
        return NULL;
    return (volatile uint32_t *)(void *)(host_memory + offset);
#else
    return (volatile uint32_t *)(uintptr_t)physical_address;
#endif
}

#if defined(KERNEL_VM_HOST_TEST)
bool kernel_vm_test_translate_current(uint32_t virtual_address, bool write,
                                      uint32_t *physical_address)
{
    volatile uint32_t *root;
    volatile uint32_t *table;
    uint32_t root_descriptor;
    uint32_t page_descriptor;

    if (physical_address == NULL || current_user_root == 0u ||
        virtual_address < KERNEL_VM_USER_MIN ||
        virtual_address > KERNEL_VM_USER_MAX)
        return false;
    root = physical_words(current_user_root);
    if (root == NULL)
        return false;
    root_descriptor = root[VM_ROOT_INDEX(virtual_address)];
    if ((root_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return false;
    table = physical_words(root_descriptor & VM_DESC_TABLE_ADDRESS);
    if (table == NULL)
        return false;
    page_descriptor = table[VM_TABLE_INDEX(virtual_address)];
    if ((page_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE ||
        (write && (page_descriptor & VM_DESC_WRITE_PROTECT) != 0u))
        return false;
    *physical_address = (page_descriptor & VM_DESC_PAGE_ADDRESS) |
                        (virtual_address & (KERNEL_PAGE_SIZE - 1u));
    return *physical_address >= host_memory_base &&
           *physical_address - host_memory_base < host_memory_size;
}
#endif

static void reset_stats(void)
{
    vm_stats.kernel_root_physical = 0u;
    vm_stats.empty_root_physical = 0u;
    vm_stats.kernel_stack_guard = 0u;
    vm_stats.kernel_worker_stack_guard = 0u;
    vm_stats.kernel_thread_stack_arena = 0u;
    vm_stats.kernel_thread_stack_arena_end = 0u;
    vm_stats.kernel_thread_stack_guards = 0u;
    vm_stats.supervisor_table_pages = 0u;
    vm_stats.address_spaces = 0u;
    vm_stats.user_mappings = 0u;
    vm_stats.user_table_pages = 0u;
    vm_stats.flushes = 0u;
    vm_stats.cache_invalidations = 0u;
    vm_stats.switches = 0u;
}

static uint32_t frame_index(uint32_t physical_address)
{
    return (physical_address - VM_SDRAM_BASE) >> KERNEL_PAGE_SHIFT;
}

static bool frame_is_user_mapped(uint32_t physical_address)
{
    if ((physical_address & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        physical_address < VM_SDRAM_BASE ||
        physical_address - VM_SDRAM_BASE >=
            KERNEL_MAX_FRAMES * KERNEL_PAGE_SIZE)
        return false;
    return (mapped_user_frames[frame_index(physical_address)] >>
            VM_MAPPING_COUNT_SHIFT) != 0u;
}

static bool shared_mapping_class(uint32_t virtual_address,
                                 uint8_t *mapping_class)
{
    uint32_t offset;

    if (mapping_class == NULL || virtual_address < KERNEL_VM_AREA_BASE)
        return false;
    offset = virtual_address - KERNEL_VM_AREA_BASE;
    if (offset >= KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE)
        return false;
    *mapping_class = (uint8_t)(offset / KERNEL_VM_AREA_SLOT_SIZE);
    return true;
}

static bool frame_mapping_can_add(uint32_t physical_address,
                                  KernelFrameState state,
                                  uint32_t virtual_address)
{
    uint8_t encoded = mapped_user_frames[frame_index(physical_address)];
    uint8_t count = encoded >> VM_MAPPING_COUNT_SHIFT;
    uint8_t mapping_class;

    if (state == KERNEL_FRAME_PROCESS)
        return count == 0u;
    if (state != KERNEL_FRAME_SHARED ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        count >= KERNEL_VM_SHARED_ALIAS_MAX)
        return false;
    return count == 0u ||
           (encoded & VM_MAPPING_CLASS_MASK) == mapping_class;
}

static bool frame_mapping_add(uint32_t physical_address,
                              KernelFrameState state,
                              uint32_t virtual_address)
{
    uint32_t index = frame_index(physical_address);
    uint8_t encoded = mapped_user_frames[index];
    uint8_t count = encoded >> VM_MAPPING_COUNT_SHIFT;
    uint8_t mapping_class;

    if (!frame_mapping_can_add(physical_address, state, virtual_address))
        return false;
    if (state == KERNEL_FRAME_PROCESS) {
        mapped_user_frames[index] =
            (uint8_t)((1u << VM_MAPPING_COUNT_SHIFT) |
                      VM_MAPPING_PRIVATE_CLASS);
        return true;
    }
    if (!shared_mapping_class(virtual_address, &mapping_class))
        return false;
    mapped_user_frames[index] =
        (uint8_t)(((count + 1u) << VM_MAPPING_COUNT_SHIFT) | mapping_class);
    return true;
}

static bool frame_mapping_remove(uint32_t physical_address,
                                 KernelFrameState state,
                                 uint32_t virtual_address)
{
    uint32_t index = frame_index(physical_address);
    uint8_t encoded = mapped_user_frames[index];
    uint8_t count = encoded >> VM_MAPPING_COUNT_SHIFT;
    uint8_t mapping_class;

    if (state == KERNEL_FRAME_PROCESS) {
        if (count != 1u ||
            (encoded & VM_MAPPING_CLASS_MASK) != VM_MAPPING_PRIVATE_CLASS)
            return false;
        mapped_user_frames[index] = 0u;
        return true;
    }
    if (state != KERNEL_FRAME_SHARED || count == 0u ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        (encoded & VM_MAPPING_CLASS_MASK) != mapping_class)
        return false;
    --count;
    mapped_user_frames[index] = count == 0u ? 0u :
        (uint8_t)((count << VM_MAPPING_COUNT_SHIFT) | mapping_class);
    return true;
}

static void clear_space(KernelAddressSpace *space)
{
    space->owner = 0u;
    space->root_physical = 0u;
    space->mapped_pages = 0u;
    space->table_pages = 0u;
    space->initialized = 0u;
    space->reserved[0] = 0u;
    space->reserved[1] = 0u;
    space->reserved[2] = 0u;
}

static void flush_all(void)
{
    if (enabled) {
        kernel_pmmu_flush_all();
        ++vm_stats.flushes;
    }
}

static void invalidate_caches(void)
{
    if (enabled) {
        kernel_cache_invalidate_all();
        ++vm_stats.cache_invalidations;
    }
}

static KernelVmStatus allocate_table(uint32_t owner, uint32_t *physical)
{
    volatile uint32_t *words;
    KernelMemoryStatus status = kernel_memory_alloc(
        1u, 1u, KERNEL_FRAME_PAGE_TABLE, owner, physical);

    if (status != KERNEL_MEMORY_OK)
        return status == KERNEL_MEMORY_OUT_OF_MEMORY ?
            KERNEL_VM_OUT_OF_MEMORY : KERNEL_VM_CORRUPT;
    words = physical_words(*physical);
    if (words == NULL) {
        (void)kernel_memory_release(*physical, 1u, owner);
        return KERNEL_VM_CORRUPT;
    }
    for (uint32_t index = 0u; index < VM_TABLE_ENTRIES; ++index)
        words[index] = VM_DESC_INVALID;
    return KERNEL_VM_OK;
}

static void set_mmio_page(volatile uint32_t *table, uint32_t address)
{
    table[VM_TABLE_INDEX(address)] =
        (address & VM_DESC_PAGE_ADDRESS) |
        VM_DESC_CACHE_INHIBIT | VM_DESC_PAGE;
}

static void set_mmio_range(volatile uint32_t *table, uint32_t base,
                           uint32_t page_count)
{
    for (uint32_t page = 0u; page < page_count; ++page)
        set_mmio_page(table, base + page * KERNEL_PAGE_SIZE);
}

static bool is_thread_stack_guard(uint32_t address)
{
    if (address < VM_KERNEL_THREAD_STACKS_START ||
        address >= VM_KERNEL_THREAD_STACKS_END)
        return false;
    return (address - VM_KERNEL_THREAD_STACKS_START) %
               KERNEL_THREAD_SUPERVISOR_SLOT_SIZE == 0u;
}

static bool is_supervisor_guard(uint32_t address)
{
    return address == VM_KERNEL_STACK_GUARD ||
           address == VM_KERNEL_WORKER_STACK_GUARD ||
           is_thread_stack_guard(address);
}

static KernelVmStatus build_supervisor_root(void)
{
    volatile uint32_t *root;
    volatile uint32_t *low_table;
    volatile uint32_t *high_table;
    KernelVmStatus status;

    if ((VM_KERNEL_STACK_GUARD & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_WORKER_STACK_GUARD & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_THREAD_STACKS_START & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_THREAD_STACKS_END & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        VM_KERNEL_THREAD_STACKS_END - VM_KERNEL_THREAD_STACKS_START !=
            KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE ||
        VM_KERNEL_STACK_GUARD == VM_KERNEL_WORKER_STACK_GUARD ||
        is_thread_stack_guard(VM_KERNEL_STACK_GUARD) ||
        is_thread_stack_guard(VM_KERNEL_WORKER_STACK_GUARD) ||
        VM_ROOT_INDEX(VM_KERNEL_STACK_GUARD) != VM_SDRAM_ROOT_INDEX ||
        VM_ROOT_INDEX(VM_KERNEL_WORKER_STACK_GUARD) !=
            VM_SDRAM_ROOT_INDEX ||
        VM_ROOT_INDEX(VM_KERNEL_THREAD_STACKS_START) !=
            VM_SDRAM_ROOT_INDEX ||
        VM_ROOT_INDEX(VM_KERNEL_THREAD_STACKS_END - 1u) !=
            VM_SDRAM_ROOT_INDEX)
        return KERNEL_VM_CORRUPT;
    status = allocate_table(VM_KERNEL_OWNER, &kernel_root_physical);
    if (status != KERNEL_VM_OK)
        return status;
    status = allocate_table(VM_KERNEL_OWNER, &kernel_low_table_physical);
    if (status != KERNEL_VM_OK) {
        (void)kernel_memory_release(kernel_root_physical, 1u,
                                    VM_KERNEL_OWNER);
        kernel_root_physical = 0u;
        return status;
    }
    status = allocate_table(VM_KERNEL_OWNER, &kernel_high_table_physical);
    if (status != KERNEL_VM_OK) {
        (void)kernel_memory_release(kernel_low_table_physical, 1u,
                                    VM_KERNEL_OWNER);
        (void)kernel_memory_release(kernel_root_physical, 1u,
                                    VM_KERNEL_OWNER);
        kernel_low_table_physical = 0u;
        kernel_root_physical = 0u;
        return status;
    }
    root = physical_words(kernel_root_physical);
    low_table = physical_words(kernel_low_table_physical);
    high_table = physical_words(kernel_high_table_physical);
    if (root == NULL || low_table == NULL || high_table == NULL) {
        (void)kernel_memory_release(kernel_high_table_physical, 1u,
                                    VM_KERNEL_OWNER);
        (void)kernel_memory_release(kernel_low_table_physical, 1u,
                                    VM_KERNEL_OWNER);
        (void)kernel_memory_release(kernel_root_physical, 1u,
                                    VM_KERNEL_OWNER);
        kernel_high_table_physical = 0u;
        kernel_low_table_physical = 0u;
        kernel_root_physical = 0u;
        return KERNEL_VM_CORRUPT;
    }

    for (uint32_t index = 0u; index < VM_TABLE_ENTRIES; ++index) {
        uint32_t address = VM_SDRAM_BASE + index * KERNEL_PAGE_SIZE;

        if (!is_supervisor_guard(address))
            low_table[index] = address | VM_DESC_PAGE;
    }
    root[VM_SDRAM_ROOT_INDEX] =
        kernel_low_table_physical | VM_DESC_TABLE;
    for (uint32_t index = VM_SDRAM_ROOT_INDEX + 1u; index <= 15u; ++index)
        root[index] = (index << 22) | VM_DESC_PAGE;
    root[1023] = kernel_high_table_physical | VM_DESC_TABLE;

    set_mmio_range(high_table, 0xfff00000u, 16u);
    set_mmio_range(high_table, 0xfff10000u, 16u);
    set_mmio_range(high_table, 0xfff20000u, 16u);
    set_mmio_range(high_table, 0xfff40000u, 1u);
    return KERNEL_VM_OK;
}

static bool valid_user_page(uint32_t virtual_address)
{
    return (virtual_address & (KERNEL_PAGE_SIZE - 1u)) == 0u &&
           virtual_address >= KERNEL_VM_USER_MIN &&
           virtual_address <= KERNEL_VM_USER_MAX - (KERNEL_PAGE_SIZE - 1u);
}

static bool table_empty(const volatile uint32_t *table)
{
    for (uint32_t index = 0u; index < VM_TABLE_ENTRIES; ++index) {
        if ((table[index] & VM_DESC_TYPE_MASK) != VM_DESC_INVALID)
            return false;
    }
    return true;
}

KernelVmStatus kernel_vm_init(void)
{
    KernelVmStatus status;

    initialized = false;
    enabled = false;
    kernel_root_physical = 0u;
    kernel_low_table_physical = 0u;
    kernel_high_table_physical = 0u;
    empty_root_physical = 0u;
    current_user_root = 0u;
#if defined(KERNEL_VM_HOST_TEST)
    next_shared_map_fault = KERNEL_VM_SHARED_MAP_FAULT_NONE;
#endif
    for (uint32_t index = 0u; index < KERNEL_MAX_FRAMES; ++index)
        mapped_user_frames[index] = 0u;
    reset_stats();

    status = build_supervisor_root();
    if (status != KERNEL_VM_OK)
        return status;
    status = allocate_table(VM_KERNEL_OWNER, &empty_root_physical);
    if (status != KERNEL_VM_OK) {
        (void)kernel_memory_release(kernel_high_table_physical, 1u,
                                    VM_KERNEL_OWNER);
        (void)kernel_memory_release(kernel_low_table_physical, 1u,
                                    VM_KERNEL_OWNER);
        (void)kernel_memory_release(kernel_root_physical, 1u,
                                    VM_KERNEL_OWNER);
        kernel_high_table_physical = 0u;
        kernel_low_table_physical = 0u;
        kernel_root_physical = 0u;
        return status;
    }

    supervisor_root.limit_descriptor = KERNEL_PMMU_ROOT_LIMIT_SHORT;
    supervisor_root.table_address = kernel_root_physical;
    current_process_root.limit_descriptor = KERNEL_PMMU_ROOT_LIMIT_SHORT;
    current_process_root.table_address = empty_root_physical;
    tc_disabled = 0u;
    tc_enabled = KERNEL_PMMU_TC_4K_10_10_SRE;
    transparent_disabled = 0u;
    current_user_root = empty_root_physical;
    vm_stats.kernel_root_physical = kernel_root_physical;
    vm_stats.empty_root_physical = empty_root_physical;
    vm_stats.kernel_stack_guard = VM_KERNEL_STACK_GUARD;
    vm_stats.kernel_worker_stack_guard = VM_KERNEL_WORKER_STACK_GUARD;
    vm_stats.kernel_thread_stack_arena = VM_KERNEL_THREAD_STACKS_START;
    vm_stats.kernel_thread_stack_arena_end = VM_KERNEL_THREAD_STACKS_END;
    vm_stats.kernel_thread_stack_guards = KERNEL_THREAD_MAX;
    vm_stats.supervisor_table_pages = 3u;
    initialized = true;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_enable(void)
{
    if (!initialized)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (enabled)
        return KERNEL_VM_OK;

    kernel_pmmu_load_tc(&tc_disabled);
    kernel_pmmu_load_tt0(&transparent_disabled);
    kernel_pmmu_load_tt1(&transparent_disabled);
    kernel_pmmu_load_srp(&supervisor_root);
    kernel_pmmu_load_crp(&current_process_root);
    kernel_pmmu_set_user_function_codes();
    kernel_pmmu_flush_all();
    kernel_cache_invalidate_all();
    kernel_pmmu_load_tc(&tc_enabled);
    enabled = true;
    ++vm_stats.flushes;
    ++vm_stats.cache_invalidations;
    return KERNEL_VM_OK;
}

bool kernel_vm_enabled(void)
{
    return enabled;
}

KernelVmStatus kernel_vm_create_address_space(uint32_t owner,
                                              KernelAddressSpace *space)
{
    KernelVmStatus status;

    if (!initialized || owner == 0u || owner == VM_KERNEL_OWNER ||
        space == NULL || space->initialized != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    clear_space(space);
    status = allocate_table(owner, &space->root_physical);
    if (status != KERNEL_VM_OK)
        return status;
    space->owner = owner;
    space->table_pages = 1u;
    space->initialized = 1u;
    ++vm_stats.address_spaces;
    ++vm_stats.user_table_pages;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_map_page(KernelAddressSpace *space,
                                  uint32_t virtual_address,
                                  uint32_t physical_address,
                                  uint32_t permissions)
{
    volatile uint32_t *root;
    volatile uint32_t *table;
    KernelFrameInfo frame;
    uint32_t root_index;
    uint32_t table_index;
    uint32_t table_physical;
    bool allocated_table = false;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address) ||
        (physical_address & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (permissions & KERNEL_VM_READ) == 0u ||
        (permissions & (KERNEL_VM_WRITE | KERNEL_VM_EXEC)) ==
            (KERNEL_VM_WRITE | KERNEL_VM_EXEC) ||
        (permissions & ~(KERNEL_VM_READ | KERNEL_VM_WRITE |
                         KERNEL_VM_EXEC)) != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (!kernel_memory_frame_info(physical_address, &frame) ||
        frame.owner != space->owner || frame.state != KERNEL_FRAME_PROCESS ||
        frame.references == 0u)
        return KERNEL_VM_NOT_OWNED;

    root = physical_words(space->root_physical);
    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    root_index = VM_ROOT_INDEX(virtual_address);
    table_index = VM_TABLE_INDEX(virtual_address);
    if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID) {
        KernelVmStatus status = allocate_table(space->owner,
                                               &table_physical);
        if (status != KERNEL_VM_OK)
            return status;
        root[root_index] = table_physical | VM_DESC_TABLE;
        ++space->table_pages;
        ++vm_stats.user_table_pages;
        allocated_table = true;
    } else if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_TABLE) {
        table_physical = root[root_index] & VM_DESC_TABLE_ADDRESS;
    } else {
        return KERNEL_VM_CORRUPT;
    }

    table = physical_words(table_physical);
    if (table == NULL) {
        if (allocated_table) {
            root[root_index] = VM_DESC_INVALID;
            (void)kernel_memory_release(table_physical, 1u, space->owner);
            --space->table_pages;
            --vm_stats.user_table_pages;
        }
        return KERNEL_VM_CORRUPT;
    }
    if ((table[table_index] & VM_DESC_TYPE_MASK) != VM_DESC_INVALID) {
        if (allocated_table) {
            root[root_index] = VM_DESC_INVALID;
            (void)kernel_memory_release(table_physical, 1u, space->owner);
            --space->table_pages;
            --vm_stats.user_table_pages;
        }
        return KERNEL_VM_ALREADY_MAPPED;
    }
    if (!frame_mapping_can_add(physical_address, KERNEL_FRAME_PROCESS,
                               virtual_address)) {
        if (allocated_table) {
            root[root_index] = VM_DESC_INVALID;
            (void)kernel_memory_release(table_physical, 1u, space->owner);
            --space->table_pages;
            --vm_stats.user_table_pages;
        }
        return KERNEL_VM_CACHE_ALIAS;
    }
    if (kernel_memory_retain(physical_address, 1u, space->owner) !=
        KERNEL_MEMORY_OK) {
        if (allocated_table) {
            root[root_index] = VM_DESC_INVALID;
            (void)kernel_memory_release(table_physical, 1u, space->owner);
            --space->table_pages;
            --vm_stats.user_table_pages;
        }
        return KERNEL_VM_CORRUPT;
    }

    if (!frame_mapping_add(physical_address, KERNEL_FRAME_PROCESS,
                           virtual_address)) {
        (void)kernel_memory_release(physical_address, 1u, space->owner);
        if (allocated_table) {
            root[root_index] = VM_DESC_INVALID;
            (void)kernel_memory_release(table_physical, 1u, space->owner);
            --space->table_pages;
            --vm_stats.user_table_pages;
        }
        return KERNEL_VM_CORRUPT;
    }
    table[table_index] = physical_address | VM_DESC_PAGE |
        ((permissions & KERNEL_VM_WRITE) != 0u ? 0u :
         VM_DESC_WRITE_PROTECT);
    invalidate_caches();
    flush_all();
    ++space->mapped_pages;
    ++vm_stats.user_mappings;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_unmap_page(KernelAddressSpace *space,
                                    uint32_t virtual_address)
{
    volatile uint32_t *root;
    volatile uint32_t *table;
    uint32_t root_index;
    uint32_t table_index;
    uint32_t table_physical;
    uint32_t page_physical;
    uint32_t frame_owner;
    KernelFrameInfo frame;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    root = physical_words(space->root_physical);
    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    root_index = VM_ROOT_INDEX(virtual_address);
    table_index = VM_TABLE_INDEX(virtual_address);
    if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_NOT_MAPPED;
    table_physical = root[root_index] & VM_DESC_TABLE_ADDRESS;
    table = physical_words(table_physical);
    if (table == NULL ||
        (table[table_index] & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_NOT_MAPPED;

    page_physical = table[table_index] & VM_DESC_PAGE_ADDRESS;
    if (!kernel_memory_frame_info(page_physical, &frame) ||
        (frame.state != KERNEL_FRAME_PROCESS &&
         frame.state != KERNEL_FRAME_SHARED) ||
        !frame_is_user_mapped(page_physical))
        return KERNEL_VM_CORRUPT;
    frame_owner = frame.owner;
    table[table_index] = VM_DESC_INVALID;
    invalidate_caches();
    flush_all();
    if (!frame_mapping_remove(page_physical,
                              (KernelFrameState)frame.state,
                              virtual_address) ||
        kernel_memory_release(page_physical, 1u, frame_owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    --space->mapped_pages;
    --vm_stats.user_mappings;

    if (table_empty(table)) {
        root[root_index] = VM_DESC_INVALID;
        flush_all();
        if (kernel_memory_release(table_physical, 1u, space->owner) !=
            KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
        --space->table_pages;
        --vm_stats.user_table_pages;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_map_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner, uint32_t permissions)
{
    volatile uint32_t *root;
    volatile uint32_t *table = NULL;
    uint32_t root_index;
    uint32_t table_physical = 0u;
    uint32_t retained = 0u;
    uint32_t mapping_count = 0u;
    uint32_t descriptor_count = 0u;
    bool allocated_table = false;
    bool root_published = false;
    bool cleanup_failed = false;
    KernelVmStatus result = KERNEL_VM_CORRUPT;
    uint8_t mapping_class;

    if (!initialized || space == NULL || space->initialized == 0u ||
        physical_pages == NULL || page_count == 0u ||
        page_count > KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE ||
        frame_owner == KERNEL_OWNER_NONE ||
        (virtual_address & (KERNEL_VM_AREA_SLOT_SIZE - 1u)) != 0u ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        (permissions & KERNEL_VM_READ) == 0u ||
        (permissions & KERNEL_VM_EXEC) != 0u ||
        (permissions & ~(KERNEL_VM_READ | KERNEL_VM_WRITE)) != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    (void)mapping_class;

    root = physical_words(space->root_physical);
    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    root_index = VM_ROOT_INDEX(virtual_address);
    if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID) {
        KernelVmStatus status = allocate_table(space->owner, &table_physical);

        if (status != KERNEL_VM_OK)
            return status;
        table = physical_words(table_physical);
        allocated_table = true;
    } else if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_TABLE) {
        table_physical = root[root_index] & VM_DESC_TABLE_ADDRESS;
        table = physical_words(table_physical);
    } else {
        return KERNEL_VM_CORRUPT;
    }
    if (table == NULL) {
        result = KERNEL_VM_CORRUPT;
        goto rollback;
    }
#if defined(KERNEL_VM_HOST_TEST)
    if (allocated_table && consume_shared_map_fault(
            KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE)) {
        result = KERNEL_VM_OUT_OF_MEMORY;
        goto rollback;
    }
#endif

    for (uint32_t page = 0u; page < page_count; ++page) {
        KernelFrameInfo frame;
        uint32_t physical = physical_pages[page];
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;
        uint32_t table_index = VM_TABLE_INDEX(address);

        if ((physical & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
            !kernel_memory_frame_info(physical, &frame)) {
            result = KERNEL_VM_INVALID_ARGUMENT;
            goto rollback;
        }
        if ((table[table_index] & VM_DESC_TYPE_MASK) != VM_DESC_INVALID) {
            result = KERNEL_VM_ALREADY_MAPPED;
            goto rollback;
        }
        if (frame.owner != frame_owner ||
            frame.state != KERNEL_FRAME_SHARED || frame.references == 0u) {
            result = KERNEL_VM_NOT_OWNED;
            goto rollback;
        }
        if (!frame_mapping_can_add(physical, KERNEL_FRAME_SHARED, address)) {
            result = KERNEL_VM_CACHE_ALIAS;
            goto rollback;
        }
        for (uint32_t prior = 0u; prior < page; ++prior) {
            if (physical_pages[prior] == physical) {
                result = KERNEL_VM_INVALID_ARGUMENT;
                goto rollback;
            }
        }
    }

    for (; retained < page_count; ++retained) {
        if (kernel_memory_retain(physical_pages[retained], 1u,
                                 frame_owner) != KERNEL_MEMORY_OK)
            break;
    }
    if (retained != page_count) {
        result = KERNEL_VM_CORRUPT;
        goto rollback;
    }
#if defined(KERNEL_VM_HOST_TEST)
    if (consume_shared_map_fault(
            KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN)) {
        result = KERNEL_VM_OUT_OF_MEMORY;
        goto rollback;
    }
#endif

    for (; mapping_count < page_count; ++mapping_count) {
        uint32_t address =
            virtual_address + mapping_count * KERNEL_PAGE_SIZE;

        if (!frame_mapping_add(physical_pages[mapping_count],
                               KERNEL_FRAME_SHARED, address)) {
            result = KERNEL_VM_CORRUPT;
            goto rollback;
        }
    }
#if defined(KERNEL_VM_HOST_TEST)
    if (consume_shared_map_fault(
            KERNEL_VM_SHARED_MAP_FAULT_AFTER_MAPPING_METADATA)) {
        result = KERNEL_VM_OUT_OF_MEMORY;
        goto rollback;
    }
#endif

    for (; descriptor_count < page_count; ++descriptor_count) {
        uint32_t address =
            virtual_address + descriptor_count * KERNEL_PAGE_SIZE;

        table[VM_TABLE_INDEX(address)] =
            physical_pages[descriptor_count] | VM_DESC_PAGE |
            ((permissions & KERNEL_VM_WRITE) != 0u ? 0u :
             VM_DESC_WRITE_PROTECT);
    }
#if defined(KERNEL_VM_HOST_TEST)
    if (consume_shared_map_fault(
            KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH)) {
        result = KERNEL_VM_OUT_OF_MEMORY;
        goto rollback;
    }
#endif

    if (allocated_table) {
        root[root_index] = table_physical | VM_DESC_TABLE;
        root_published = true;
    }
#if defined(KERNEL_VM_HOST_TEST)
    if (root_published && consume_shared_map_fault(
            KERNEL_VM_SHARED_MAP_FAULT_AFTER_ROOT_PUBLISH)) {
        result = KERNEL_VM_OUT_OF_MEMORY;
        goto rollback;
    }
#endif

    invalidate_caches();
    flush_all();
    if (allocated_table) {
        ++space->table_pages;
        ++vm_stats.user_table_pages;
    }
    space->mapped_pages += page_count;
    vm_stats.user_mappings += page_count;
    return KERNEL_VM_OK;

rollback:
    if (root_published)
        root[root_index] = VM_DESC_INVALID;
    while (descriptor_count != 0u) {
        uint32_t address;

        --descriptor_count;
        address = virtual_address + descriptor_count * KERNEL_PAGE_SIZE;
        table[VM_TABLE_INDEX(address)] = VM_DESC_INVALID;
    }
    if (root_published || (!allocated_table && mapping_count != 0u)) {
        invalidate_caches();
        flush_all();
    }
    while (mapping_count != 0u) {
        uint32_t address;

        --mapping_count;
        address = virtual_address + mapping_count * KERNEL_PAGE_SIZE;
        if (!frame_mapping_remove(physical_pages[mapping_count],
                                  KERNEL_FRAME_SHARED, address))
            cleanup_failed = true;
    }
    while (retained != 0u) {
        --retained;
        if (kernel_memory_release(physical_pages[retained], 1u,
                                  frame_owner) != KERNEL_MEMORY_OK)
            cleanup_failed = true;
    }
    if (allocated_table &&
        kernel_memory_release(table_physical, 1u, space->owner) !=
            KERNEL_MEMORY_OK)
        cleanup_failed = true;
    return cleanup_failed ? KERNEL_VM_CORRUPT : result;
}

KernelVmStatus kernel_vm_unmap_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner)
{
    volatile uint32_t *root;
    volatile uint32_t *table;
    uint32_t root_index;
    uint32_t table_physical;
    bool release_table;
    uint8_t mapping_class;

    if (!initialized || space == NULL || space->initialized == 0u ||
        physical_pages == NULL || page_count == 0u ||
        page_count > KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE ||
        frame_owner == KERNEL_OWNER_NONE ||
        (virtual_address & (KERNEL_VM_AREA_SLOT_SIZE - 1u)) != 0u ||
        !shared_mapping_class(virtual_address, &mapping_class))
        return KERNEL_VM_INVALID_ARGUMENT;
    (void)mapping_class;
    root = physical_words(space->root_physical);
    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    root_index = VM_ROOT_INDEX(virtual_address);
    if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_NOT_MAPPED;
    table_physical = root[root_index] & VM_DESC_TABLE_ADDRESS;
    table = physical_words(table_physical);
    if (table == NULL)
        return KERNEL_VM_CORRUPT;

    for (uint32_t page = 0u; page < page_count; ++page) {
        KernelFrameInfo frame;
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;
        uint32_t descriptor = table[VM_TABLE_INDEX(address)];

        if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE ||
            (descriptor & VM_DESC_PAGE_ADDRESS) != physical_pages[page])
            return KERNEL_VM_NOT_MAPPED;
        if (!kernel_memory_frame_info(physical_pages[page], &frame) ||
            frame.owner != frame_owner || frame.state != KERNEL_FRAME_SHARED ||
            !frame_is_user_mapped(physical_pages[page]))
            return KERNEL_VM_CORRUPT;
    }

    for (uint32_t page = 0u; page < page_count; ++page) {
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;

        table[VM_TABLE_INDEX(address)] = VM_DESC_INVALID;
    }
    release_table = table_empty(table);
    if (release_table)
        root[root_index] = VM_DESC_INVALID;
    invalidate_caches();
    flush_all();

    for (uint32_t page = 0u; page < page_count; ++page) {
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;

        if (!frame_mapping_remove(physical_pages[page], KERNEL_FRAME_SHARED,
                                  address) ||
            kernel_memory_release(physical_pages[page], 1u, frame_owner) !=
                KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
    }
    space->mapped_pages -= page_count;
    vm_stats.user_mappings -= page_count;
    if (release_table) {
        if (kernel_memory_release(table_physical, 1u, space->owner) !=
            KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
        --space->table_pages;
        --vm_stats.user_table_pages;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_destroy_address_space(KernelAddressSpace *space)
{
    volatile uint32_t *root;

    if (!initialized || space == NULL || space->initialized == 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (space->root_physical == current_user_root)
        return KERNEL_VM_BUSY;
    root = physical_words(space->root_physical);
    if (root == NULL)
        return KERNEL_VM_CORRUPT;

    if (space->mapped_pages != 0u)
        invalidate_caches();

    for (uint32_t root_index = 0u; root_index < VM_ROOT_ENTRIES;
         ++root_index) {
        volatile uint32_t *table;
        uint32_t table_physical;

        if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
            continue;
        if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
            return KERNEL_VM_CORRUPT;
        table_physical = root[root_index] & VM_DESC_TABLE_ADDRESS;
        table = physical_words(table_physical);
        if (table == NULL)
            return KERNEL_VM_CORRUPT;
        for (uint32_t table_index = 0u; table_index < VM_TABLE_ENTRIES;
             ++table_index) {
            uint32_t page_physical;
            uint32_t virtual_address;
            KernelFrameInfo frame;

            if ((table[table_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
                continue;
            if ((table[table_index] & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
                return KERNEL_VM_CORRUPT;
            page_physical = table[table_index] & VM_DESC_PAGE_ADDRESS;
            virtual_address = (root_index << 22) |
                              (table_index << KERNEL_PAGE_SHIFT);
            if (!kernel_memory_frame_info(page_physical, &frame) ||
                (frame.state != KERNEL_FRAME_PROCESS &&
                 frame.state != KERNEL_FRAME_SHARED) ||
                !frame_is_user_mapped(page_physical))
                return KERNEL_VM_CORRUPT;
            table[table_index] = VM_DESC_INVALID;
            flush_all();
            if (!frame_mapping_remove(page_physical,
                                      (KernelFrameState)frame.state,
                                      virtual_address) ||
                kernel_memory_release(page_physical, 1u, frame.owner) !=
                KERNEL_MEMORY_OK)
                return KERNEL_VM_CORRUPT;
            --space->mapped_pages;
            --vm_stats.user_mappings;
        }
        root[root_index] = VM_DESC_INVALID;
        flush_all();
        if (kernel_memory_release(table_physical, 1u, space->owner) !=
            KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
        --space->table_pages;
        --vm_stats.user_table_pages;
    }
    flush_all();
    if (kernel_memory_release(space->root_physical, 1u, space->owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    --space->table_pages;
    --vm_stats.user_table_pages;
    --vm_stats.address_spaces;
    clear_space(space);
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_switch(const KernelAddressSpace *space)
{
    if (!initialized || !enabled || space == NULL ||
        space->initialized == 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (current_user_root == space->root_physical)
        return KERNEL_VM_OK;
    invalidate_caches();
    current_process_root.limit_descriptor = KERNEL_PMMU_ROOT_LIMIT_SHORT;
    current_process_root.table_address = space->root_physical;
    kernel_pmmu_load_crp(&current_process_root);
    kernel_pmmu_flush_all();
    current_user_root = space->root_physical;
    ++vm_stats.flushes;
    ++vm_stats.switches;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_switch_to_empty(void)
{
    if (!initialized || !enabled)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (current_user_root == empty_root_physical)
        return KERNEL_VM_OK;
    invalidate_caches();
    current_process_root.limit_descriptor = KERNEL_PMMU_ROOT_LIMIT_SHORT;
    current_process_root.table_address = empty_root_physical;
    kernel_pmmu_load_crp(&current_process_root);
    kernel_pmmu_flush_all();
    current_user_root = empty_root_physical;
    ++vm_stats.flushes;
    ++vm_stats.switches;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_sync_shared_aliases(void)
{
    if (!initialized)
        return KERNEL_VM_INVALID_ARGUMENT;
    invalidate_caches();
    return KERNEL_VM_OK;
}

bool kernel_vm_stats(KernelVmStats *result)
{
    if (!initialized || result == NULL)
        return false;
    result->kernel_root_physical = vm_stats.kernel_root_physical;
    result->empty_root_physical = vm_stats.empty_root_physical;
    result->kernel_stack_guard = vm_stats.kernel_stack_guard;
    result->kernel_worker_stack_guard =
        vm_stats.kernel_worker_stack_guard;
    result->kernel_thread_stack_arena =
        vm_stats.kernel_thread_stack_arena;
    result->kernel_thread_stack_arena_end =
        vm_stats.kernel_thread_stack_arena_end;
    result->kernel_thread_stack_guards =
        vm_stats.kernel_thread_stack_guards;
    result->supervisor_table_pages = vm_stats.supervisor_table_pages;
    result->address_spaces = vm_stats.address_spaces;
    result->user_mappings = vm_stats.user_mappings;
    result->user_table_pages = vm_stats.user_table_pages;
    result->flushes = vm_stats.flushes;
    result->cache_invalidations = vm_stats.cache_invalidations;
    result->switches = vm_stats.switches;
    return true;
}
