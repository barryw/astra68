#include "vm.h"

#include "memory.h"
#include "pmmu.h"
#include "thread.h"

#include <astra/library.h>

#include <stddef.h>

/* Object tables live above the frame metadata; see kernel.ld. */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif

#define VM_ROOT_ENTRIES 128u
#define VM_POINTER_ENTRIES 128u
#define VM_PAGE_ENTRIES 64u
#define VM_ALLOCATED_TABLE_WORDS (KERNEL_PAGE_SIZE / sizeof(uint32_t))
#define VM_ROOT_INDEX(address) (((address) >> 25) & 0x7fu)
#define VM_POINTER_INDEX(address) (((address) >> 18) & 0x7fu)
#define VM_PAGE_INDEX(address) (((address) >> 12) & 0x3fu)

#define VM_DESC_INVALID 0x00u
#define VM_DESC_PAGE 0x01u
#define VM_DESC_TABLE 0x02u
#define VM_DESC_WRITE_PROTECT 0x04u
#define VM_DESC_CACHE_INHIBIT 0x60u
#define VM_DESC_SUPERVISOR_ONLY 0x80u
#define VM_DESC_GLOBAL 0x400u
#define VM_DESC_TYPE_MASK 0x03u
#define VM_DESC_PAGE_ADDRESS 0xfffff000u
#define VM_DESC_TABLE_ADDRESS 0xfffff000u

#define VM_KERNEL_OWNER 1u
#define VM_SDRAM_BASE 0x02000000u
/*
 * One entry per physical frame: the alias count above, the mapping class
 * below. It was a byte split four and four, which capped a shared frame at
 * fifteen aliases and therefore capped the whole machine at fifteen processes.
 * Sixteen bits split eight and eight costs one more byte per frame -- 32 KiB
 * across 128 MiB -- and takes the ceiling off.
 */
#define VM_MAPPING_COUNT_SHIFT 8u
#define VM_MAPPING_CLASS_MASK 0x00ffu
#define VM_MAPPING_PRIVATE_CLASS 0x0fu
#define VM_MAPPING_COW_CLASS 0x0eu
#define VM_MAPPING_AREA_CLASS 0xffu

_Static_assert(KERNEL_VM_PRIVATE_END == KERNEL_THREAD_STACK_BASE,
               "private VM window must end where user stacks begin");
_Static_assert(KERNEL_VM_PRIVATE_END - KERNEL_VM_PRIVATE_BASE ==
                   ASTRA_VM_PRIVATE_ADDRESS_SPACE_MAX,
               "public private-VM extent differs from the address map");
_Static_assert((KERNEL_VM_PRIVATE_BASE & (KERNEL_VM_PRIVATE_SLOT_SIZE - 1u)) ==
                   0u &&
                   (KERNEL_VM_PRIVATE_END &
                    (KERNEL_VM_PRIVATE_SLOT_SIZE - 1u)) == 0u,
               "private VM window must follow PMMU root slots");

#if defined(KERNEL_VM_HOST_TEST)
#define VM_KERNEL_THREAD_STACKS_START \
    KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE
#define VM_KERNEL_THREAD_STACKS_END \
    (VM_KERNEL_THREAD_STACKS_START + \
     KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE)
/*
 * Above the arena, and derived from its end rather than written out. They were
 * fixed addresses that happened to sit just past sixteen slots; raising the
 * thread count moved the arena over them, and vm_init answered CORRUPT because
 * a guard that is inside the thread arena is a guard for one of its slots.
 */
#define VM_KERNEL_STACK_GUARD VM_KERNEL_THREAD_STACKS_END
#define VM_KERNEL_WORKER_STACK_GUARD \
    (VM_KERNEL_THREAD_STACKS_END + KERNEL_PAGE_SIZE)
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
static uint32_t empty_root_physical;
static uint32_t current_user_root;
/*
 * One alias count per frame of RAM, sized from the machine rather than from a
 * constant. It was a static array of KERNEL_MAX_FRAMES entries in `.tables`,
 * which made it another place the size of the board was compiled in.
 */
/*
 * Volatile because it is reached through the same physical-address
 * translation as the page tables, and casting that qualifier away to keep a
 * plain pointer would be lying about where the storage is.
 */
static volatile uint16_t *mapped_user_frames;
static uint32_t mapped_user_frame_count;
static KernelAddressSpace *address_space_head;
static KernelVmStats vm_stats;
static bool initialized;
static bool enabled;

static bool register_address_space(KernelAddressSpace *space)
{
    KernelAddressSpace *candidate = address_space_head;

    while (candidate != NULL) {
        if (candidate == space)
            return false;
        candidate = candidate->registry_next;
    }
    space->registry_next = address_space_head;
    address_space_head = space;
    return true;
}

static bool unregister_address_space(KernelAddressSpace *space)
{
    KernelAddressSpace **link = &address_space_head;

    while (*link != NULL) {
        if (*link == space) {
            *link = space->registry_next;
            space->registry_next = NULL;
            return true;
        }
        link = &(*link)->registry_next;
    }
    return false;
}

static uint32_t supervisor_root;
static uint32_t current_process_root;

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

static KernelVmMapping probe_root(uint32_t root_physical,
                                  uint32_t virtual_address,
                                  uint32_t *physical_address)
{
    volatile uint32_t *root;
    volatile uint32_t *pointer;
    volatile uint32_t *page_table;
    uint32_t root_descriptor;
    uint32_t pointer_descriptor;
    uint32_t page_descriptor;
    uint32_t ignored_physical;

    if (physical_address == NULL)
        physical_address = &ignored_physical;

    if (root_physical == 0u)
        return KERNEL_VM_MAPPING_UNKNOWN;
    root = physical_words(root_physical);
    if (root == NULL)
        return KERNEL_VM_MAPPING_UNKNOWN;
    root_descriptor = root[VM_ROOT_INDEX(virtual_address)];
    if (root_descriptor == VM_DESC_INVALID)
        return KERNEL_VM_MAPPING_UNMAPPED;
    if ((root_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_MAPPING_UNKNOWN;
    pointer = physical_words(root_descriptor & VM_DESC_TABLE_ADDRESS);
    if (pointer == NULL)
        return KERNEL_VM_MAPPING_UNKNOWN;
    pointer_descriptor = pointer[VM_POINTER_INDEX(virtual_address)];
    if (pointer_descriptor == VM_DESC_INVALID)
        return KERNEL_VM_MAPPING_UNMAPPED;
    if ((pointer_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_MAPPING_UNKNOWN;
    page_table = physical_words(pointer_descriptor & VM_DESC_TABLE_ADDRESS);
    if (page_table == NULL)
        return KERNEL_VM_MAPPING_UNKNOWN;
    page_descriptor = page_table[VM_PAGE_INDEX(virtual_address)];
    if (page_descriptor == VM_DESC_INVALID)
        return KERNEL_VM_MAPPING_UNMAPPED;
    if ((page_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_MAPPING_UNKNOWN;
    *physical_address = (page_descriptor & VM_DESC_PAGE_ADDRESS) |
                        (virtual_address & (KERNEL_PAGE_SIZE - 1u));
    return (page_descriptor & VM_DESC_WRITE_PROTECT) != 0u ?
        KERNEL_VM_MAPPING_READ_ONLY : KERNEL_VM_MAPPING_READ_WRITE;
}

KernelVmMapping kernel_vm_probe_current(uint32_t virtual_address,
                                        bool supervisor,
                                        uint32_t *physical_address)
{
    uint32_t ignored_physical;
    uint32_t root_physical;

    if (!initialized)
        return KERNEL_VM_MAPPING_UNKNOWN;
    if (physical_address == NULL)
        physical_address = &ignored_physical;
    if (!supervisor && (virtual_address < KERNEL_VM_USER_MIN ||
                        virtual_address > KERNEL_VM_USER_MAX))
        return KERNEL_VM_MAPPING_UNMAPPED;
    root_physical = supervisor ? kernel_root_physical : current_user_root;
    return probe_root(root_physical, virtual_address, physical_address);
}

static KernelVmStatus copy_address_space(const KernelAddressSpace *space,
                                         uint32_t virtual_address,
                                         void *destination,
                                         const void *source,
                                         uint32_t byte_size, bool write)
{
    uint8_t *output = destination;
    const uint8_t *input = source;
    uint64_t end = (uint64_t)virtual_address + byte_size;

    if (!initialized || space == NULL || space->initialized == 0u ||
        (byte_size != 0u && (write ? source == NULL : destination == NULL)) ||
        virtual_address < KERNEL_VM_USER_MIN || end > KERNEL_VM_USER_MAX + 1u)
        return KERNEL_VM_INVALID_ARGUMENT;
    while (byte_size != 0u) {
        uint32_t physical;
        uint32_t offset = virtual_address & (KERNEL_PAGE_SIZE - 1u);
        uint32_t chunk = KERNEL_PAGE_SIZE - offset;
        KernelVmMapping mapping = probe_root(
            space->root_physical, virtual_address, &physical);
        volatile uint8_t *page;

        if (mapping == KERNEL_VM_MAPPING_UNMAPPED)
            return KERNEL_VM_NOT_MAPPED;
        if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
            return KERNEL_VM_CORRUPT;
        if (write && mapping != KERNEL_VM_MAPPING_READ_WRITE)
            return KERNEL_VM_NOT_OWNED;
        if (chunk > byte_size)
            chunk = byte_size;
        page = (volatile uint8_t *)physical_words(
            physical & ~(KERNEL_PAGE_SIZE - 1u));
        if (page == NULL)
            return KERNEL_VM_CORRUPT;
        for (uint32_t index = 0u; index < chunk; ++index) {
            if (write)
                page[offset + index] = input[index];
            else
                output[index] = page[offset + index];
        }
        if (write)
            input += chunk;
        else
            output += chunk;
        virtual_address += chunk;
        byte_size -= chunk;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_read(const KernelAddressSpace *space,
                              uint32_t virtual_address,
                              void *destination, uint32_t byte_size)
{
    return copy_address_space(space, virtual_address, destination, NULL,
                              byte_size, false);
}

KernelVmStatus kernel_vm_write(KernelAddressSpace *space,
                               uint32_t virtual_address,
                               const void *source, uint32_t byte_size)
{
    return copy_address_space(space, virtual_address, NULL, source, byte_size,
                              true);
}

#if defined(KERNEL_VM_HOST_TEST)
bool kernel_vm_test_translate_current(uint32_t virtual_address, bool write,
                                      uint32_t *physical_address)
{
    KernelVmMapping mapping = kernel_vm_probe_current(
        virtual_address, false, physical_address);

    if (mapping != KERNEL_VM_MAPPING_READ_WRITE &&
        !(mapping == KERNEL_VM_MAPPING_READ_ONLY && !write))
        return false;
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
            (uint64_t)mapped_user_frame_count * KERNEL_PAGE_SIZE)
        return false;
    return (mapped_user_frames[frame_index(physical_address)] >>
            VM_MAPPING_COUNT_SHIFT) != 0u;
}

/*
 * Transfer memory is mapped once, into one address space, cache-inhibited. It
 * carries none of the aliasing a shared area does, so it is accounted exactly
 * like a private process page rather than gaining a second alias class.
 */
static bool private_mapping_state(KernelFrameState state)
{
    return state == KERNEL_FRAME_PROCESS || state == KERNEL_FRAME_DMA;
}

static bool cow_mapping_state(KernelFrameState state)
{
    return state == KERNEL_FRAME_COW_READ_ONLY ||
           state == KERNEL_FRAME_COW_WRITE;
}

static bool shared_mapping_class(uint32_t virtual_address,
                                 uint8_t *mapping_class)
{
    uint32_t offset;

    if (mapping_class == NULL)
        return false;
    if (virtual_address >= ASTRA_LIBRARY_BASE &&
        virtual_address < ASTRA_LIBRARY_BASE +
                              (ASTRA_LIBRARY_SLOT_COUNT *
                               ASTRA_LIBRARY_SLOT_SIZE)) {
        /* Library frames always keep one globally assigned virtual slot. */
        *mapping_class = (uint8_t)((virtual_address - ASTRA_LIBRARY_BASE) /
                                  ASTRA_LIBRARY_SLOT_SIZE);
        return true;
    }
    if (virtual_address < KERNEL_VM_AREA_BASE)
        return false;
    offset = virtual_address - KERNEL_VM_AREA_BASE;
    if (offset >= KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE)
        return false;
    /* MC68040 caches are physically addressed; area aliases need not share VA. */
    *mapping_class = VM_MAPPING_AREA_CLASS;
    return true;
}

static bool library_mapping_address(uint32_t virtual_address)
{
    return virtual_address >= ASTRA_LIBRARY_BASE &&
           virtual_address < ASTRA_LIBRARY_BASE +
                                 (ASTRA_LIBRARY_SLOT_COUNT *
                                  ASTRA_LIBRARY_SLOT_SIZE);
}

static bool frame_mapping_can_add(uint32_t physical_address,
                                  KernelFrameState state,
                                  uint32_t virtual_address)
{
    uint16_t encoded = mapped_user_frames[frame_index(physical_address)];
    uint16_t count = (uint16_t)(encoded >> VM_MAPPING_COUNT_SHIFT);
    uint8_t mapping_class;

    if (private_mapping_state(state))
        return count == 0u;
    if (cow_mapping_state(state))
        return count < KERNEL_VM_SHARED_ALIAS_MAX &&
               (count == 0u ||
                (encoded & VM_MAPPING_CLASS_MASK) == VM_MAPPING_COW_CLASS);
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
    uint16_t encoded = mapped_user_frames[index];
    uint16_t count = (uint16_t)(encoded >> VM_MAPPING_COUNT_SHIFT);
    uint8_t mapping_class;

    if (!frame_mapping_can_add(physical_address, state, virtual_address))
        return false;
    if (private_mapping_state(state)) {
        mapped_user_frames[index] =
            (uint16_t)((1u << VM_MAPPING_COUNT_SHIFT) |
                       VM_MAPPING_PRIVATE_CLASS);
        return true;
    }
    if (cow_mapping_state(state)) {
        mapped_user_frames[index] =
            (uint16_t)(((count + 1u) << VM_MAPPING_COUNT_SHIFT) |
                       VM_MAPPING_COW_CLASS);
        return true;
    }
    if (!shared_mapping_class(virtual_address, &mapping_class))
        return false;
    mapped_user_frames[index] =
        (uint16_t)(((count + 1u) << VM_MAPPING_COUNT_SHIFT) | mapping_class);
    return true;
}

static bool frame_mapping_remove(uint32_t physical_address,
                                 KernelFrameState state,
                                 uint32_t virtual_address)
{
    uint32_t index = frame_index(physical_address);
    uint16_t encoded = mapped_user_frames[index];
    uint16_t count = (uint16_t)(encoded >> VM_MAPPING_COUNT_SHIFT);
    uint8_t mapping_class;

    if (private_mapping_state(state)) {
        if (count != 1u ||
            (encoded & VM_MAPPING_CLASS_MASK) != VM_MAPPING_PRIVATE_CLASS)
            return false;
        mapped_user_frames[index] = 0u;
        return true;
    }
    if (cow_mapping_state(state)) {
        if (count == 0u ||
            (encoded & VM_MAPPING_CLASS_MASK) != VM_MAPPING_COW_CLASS)
            return false;
        --count;
        mapped_user_frames[index] = count == 0u ? 0u :
            (uint16_t)((count << VM_MAPPING_COUNT_SHIFT) |
                       VM_MAPPING_COW_CLASS);
        return true;
    }
    if (state != KERNEL_FRAME_SHARED || count == 0u ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        (encoded & VM_MAPPING_CLASS_MASK) != mapping_class)
        return false;
    --count;
    mapped_user_frames[index] = count == 0u ? 0u :
        (uint16_t)((count << VM_MAPPING_COUNT_SHIFT) | mapping_class);
    return true;
}

static void clear_space(KernelAddressSpace *space)
{
    space->registry_next = NULL;
    space->owner = 0u;
    space->root_physical = 0u;
    space->mapped_pages = 0u;
    space->table_pages = 0u;
    for (uint32_t index = 0u; index < KERNEL_VM_PRIVATE_BITMAP_WORDS;
         ++index) {
        space->private_reserved[index] = 0u;
        space->private_writable[index] = 0u;
    }
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

/*
 * The flush for a change that touched one page. Everything else still goes
 * through flush_all: a range change of many pages is cheaper as one flush than
 * as many, and dropping a table descriptor invalidates a whole region.
 */
static void flush_page(uint32_t virtual_address)
{
    if (enabled) {
        kernel_pmmu_flush_page(virtual_address);
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
    KernelMemoryStatus status = kernel_memory_alloc_tagged(
        KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, 1u, 1u,
        KERNEL_FRAME_PAGE_TABLE, owner, physical);

    if (status != KERNEL_MEMORY_OK)
        return status == KERNEL_MEMORY_OUT_OF_MEMORY ?
            KERNEL_VM_OUT_OF_MEMORY : KERNEL_VM_CORRUPT;
    words = physical_words(*physical);
    if (words == NULL) {
        (void)kernel_memory_release(*physical, 1u, owner);
        return KERNEL_VM_CORRUPT;
    }
    for (uint32_t index = 0u; index < VM_ALLOCATED_TABLE_WORDS; ++index)
        words[index] = VM_DESC_INVALID;
    return KERNEL_VM_OK;
}

typedef struct VmPagePath {
    volatile uint32_t *root;
    volatile uint32_t *pointer;
    volatile uint32_t *page_table;
    uint32_t pointer_physical;
    uint32_t page_table_physical;
} VmPagePath;

static bool table_empty(const volatile uint32_t *table, uint32_t entries)
{
    for (uint32_t index = 0u; index < entries; ++index) {
        if ((table[index] & VM_DESC_TYPE_MASK) != VM_DESC_INVALID)
            return false;
    }
    return true;
}

static KernelVmStatus page_path(uint32_t root_physical,
                                uint32_t virtual_address,
                                VmPagePath *path)
{
    uint32_t descriptor;

    path->root = physical_words(root_physical);
    path->pointer = NULL;
    path->page_table = NULL;
    path->pointer_physical = 0u;
    path->page_table_physical = 0u;
    if (path->root == NULL)
        return KERNEL_VM_CORRUPT;
    descriptor = path->root[VM_ROOT_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_CORRUPT;
    path->pointer_physical = descriptor & VM_DESC_TABLE_ADDRESS;
    path->pointer = physical_words(path->pointer_physical);
    if (path->pointer == NULL)
        return KERNEL_VM_CORRUPT;
    descriptor = path->pointer[VM_POINTER_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
        return KERNEL_VM_CORRUPT;
    path->page_table_physical = descriptor & VM_DESC_TABLE_ADDRESS;
    path->page_table = physical_words(path->page_table_physical);
    return path->page_table == NULL ? KERNEL_VM_CORRUPT : KERNEL_VM_OK;
}

static KernelVmStatus ensure_page_path(uint32_t root_physical,
                                       uint32_t virtual_address,
                                       uint32_t owner,
                                       VmPagePath *path,
                                       uint32_t *allocated_tables)
{
    uint32_t descriptor;
    uint32_t new_pointer = 0u;
    uint32_t new_page_table = 0u;
    KernelVmStatus status;

    *allocated_tables = 0u;
    path->root = physical_words(root_physical);
    if (path->root == NULL)
        return KERNEL_VM_CORRUPT;
    descriptor = path->root[VM_ROOT_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID) {
        status = allocate_table(owner, &new_pointer);
        if (status != KERNEL_VM_OK)
            return status;
        path->pointer_physical = new_pointer;
        path->pointer = physical_words(new_pointer);
    } else {
        if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
            return KERNEL_VM_CORRUPT;
        path->pointer_physical = descriptor & VM_DESC_TABLE_ADDRESS;
        path->pointer = physical_words(path->pointer_physical);
    }
    if (path->pointer == NULL)
        return KERNEL_VM_CORRUPT;
    descriptor = path->pointer[VM_POINTER_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID) {
        status = allocate_table(owner, &new_page_table);
        if (status != KERNEL_VM_OK) {
            if (new_pointer != 0u)
                (void)kernel_memory_release(new_pointer, 1u, owner);
            return status;
        }
        path->page_table_physical = new_page_table;
        path->page_table = physical_words(new_page_table);
        path->pointer[VM_POINTER_INDEX(virtual_address)] =
            new_page_table | VM_DESC_CACHE_INHIBIT | VM_DESC_TABLE;
        if (new_pointer != 0u)
            path->root[VM_ROOT_INDEX(virtual_address)] =
                new_pointer | VM_DESC_CACHE_INHIBIT | VM_DESC_TABLE;
        *allocated_tables = new_pointer != 0u ? 2u : 1u;
    } else {
        if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE) {
            if (new_pointer != 0u)
                (void)kernel_memory_release(new_pointer, 1u, owner);
            return KERNEL_VM_CORRUPT;
        }
        path->page_table_physical = descriptor & VM_DESC_TABLE_ADDRESS;
        path->page_table = physical_words(path->page_table_physical);
    }
    return path->page_table == NULL ? KERNEL_VM_CORRUPT : KERNEL_VM_OK;
}

static KernelVmStatus release_empty_path(uint32_t root_physical,
                                         uint32_t virtual_address,
                                         uint32_t owner,
                                         uint32_t *released_tables)
{
    VmPagePath path;
    KernelVmStatus status = page_path(root_physical, virtual_address, &path);

    *released_tables = 0u;
    if (status != KERNEL_VM_OK)
        return status;
    if (!table_empty(path.page_table, VM_PAGE_ENTRIES))
        return KERNEL_VM_OK;
    path.pointer[VM_POINTER_INDEX(virtual_address)] = VM_DESC_INVALID;
    if (kernel_memory_release(path.page_table_physical, 1u, owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    *released_tables = 1u;
    if (!table_empty(path.pointer, VM_POINTER_ENTRIES))
        return KERNEL_VM_OK;
    path.root[VM_ROOT_INDEX(virtual_address)] = VM_DESC_INVALID;
    if (kernel_memory_release(path.pointer_physical, 1u, owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    *released_tables = 2u;
    return KERNEL_VM_OK;
}

static KernelVmStatus release_table_tree(uint32_t root_physical,
                                         uint32_t owner)
{
    volatile uint32_t *root = physical_words(root_physical);

    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    for (uint32_t root_index = 0u; root_index < VM_ROOT_ENTRIES;
         ++root_index) {
        uint32_t root_descriptor = root[root_index];
        volatile uint32_t *pointer;

        if (root_descriptor == VM_DESC_INVALID)
            continue;
        if ((root_descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
            return KERNEL_VM_CORRUPT;
        pointer = physical_words(root_descriptor & VM_DESC_TABLE_ADDRESS);
        if (pointer == NULL)
            return KERNEL_VM_CORRUPT;
        for (uint32_t pointer_index = 0u;
             pointer_index < VM_POINTER_ENTRIES; ++pointer_index) {
            uint32_t descriptor = pointer[pointer_index];

            if (descriptor == VM_DESC_INVALID)
                continue;
            if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_TABLE ||
                kernel_memory_release(descriptor & VM_DESC_TABLE_ADDRESS,
                                      1u, owner) != KERNEL_MEMORY_OK)
                return KERNEL_VM_CORRUPT;
        }
        if (kernel_memory_release(root_descriptor & VM_DESC_TABLE_ADDRESS,
                                  1u, owner) != KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
    }
    return kernel_memory_release(root_physical, 1u, owner) ==
                   KERNEL_MEMORY_OK ?
               KERNEL_VM_OK : KERNEL_VM_CORRUPT;
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

static KernelVmStatus map_supervisor_identity_page(uint32_t address,
                                                   bool cache_inhibit)
{
    VmPagePath path;
    uint32_t allocated;
    KernelVmStatus status = ensure_page_path(
        kernel_root_physical, address, VM_KERNEL_OWNER, &path, &allocated);

    if (status != KERNEL_VM_OK)
        return status;
    if (path.page_table[VM_PAGE_INDEX(address)] != VM_DESC_INVALID)
        return KERNEL_VM_ALREADY_MAPPED;
    path.page_table[VM_PAGE_INDEX(address)] =
        (address & VM_DESC_PAGE_ADDRESS) | VM_DESC_PAGE |
        VM_DESC_SUPERVISOR_ONLY | VM_DESC_GLOBAL |
        (cache_inhibit ? VM_DESC_CACHE_INHIBIT : 0u);
    vm_stats.supervisor_table_pages += allocated;
    return KERNEL_VM_OK;
}

static KernelVmStatus map_supervisor_mmio_range(uint32_t base,
                                                uint32_t page_count)
{
    for (uint32_t page = 0u; page < page_count; ++page) {
        KernelVmStatus status = map_supervisor_identity_page(
            base + page * KERNEL_PAGE_SIZE, true);

        if (status != KERNEL_VM_OK)
            return status;
    }
    return KERNEL_VM_OK;
}

static KernelVmStatus build_supervisor_root(void)
{
    KernelMemoryStats memory;
    uint64_t ram_end;
    KernelVmStatus status;

    if ((VM_KERNEL_STACK_GUARD & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_WORKER_STACK_GUARD & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_THREAD_STACKS_START & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        (VM_KERNEL_THREAD_STACKS_END & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
        VM_KERNEL_THREAD_STACKS_END - VM_KERNEL_THREAD_STACKS_START !=
            KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE ||
        VM_KERNEL_STACK_GUARD == VM_KERNEL_WORKER_STACK_GUARD ||
        is_thread_stack_guard(VM_KERNEL_STACK_GUARD) ||
        is_thread_stack_guard(VM_KERNEL_WORKER_STACK_GUARD))
        return KERNEL_VM_CORRUPT;
    status = allocate_table(VM_KERNEL_OWNER, &kernel_root_physical);
    if (status != KERNEL_VM_OK)
        return status;
    vm_stats.supervisor_table_pages = 1u;
    if (!kernel_memory_stats(&memory)) {
        status = KERNEL_VM_CORRUPT;
        goto fail;
    }
    ram_end = (uint64_t)memory.ram_base +
              (uint64_t)memory.total_frames * KERNEL_PAGE_SIZE;
    for (uint32_t address = memory.ram_base;
         (uint64_t)address < ram_end; address += KERNEL_PAGE_SIZE) {
        KernelFrameInfo frame;

        if (!kernel_memory_frame_info(address, &frame)) {
            status = KERNEL_VM_CORRUPT;
            goto fail;
        }
        if (!is_supervisor_guard(address)) {
            status = map_supervisor_identity_page(
                address, frame.state == KERNEL_FRAME_DEVICE);
            if (status != KERNEL_VM_OK)
                goto fail;
        }
    }
    status = map_supervisor_mmio_range(0xfff00000u, 16u);
    if (status != KERNEL_VM_OK)
        goto fail;
    status = map_supervisor_mmio_range(0xfff10000u, 16u);
    if (status != KERNEL_VM_OK)
        goto fail;
    status = map_supervisor_mmio_range(0xfff20000u, 16u);
    if (status != KERNEL_VM_OK)
        goto fail;
    status = map_supervisor_mmio_range(0xfff40000u, 1u);
    if (status != KERNEL_VM_OK)
        goto fail;
    status = map_supervisor_mmio_range(
        KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE,
        KERNEL_VM_HOST_CHANNEL_PAGE_COUNT);
    if (status != KERNEL_VM_OK)
        goto fail;
    return KERNEL_VM_OK;

fail:
    (void)release_table_tree(kernel_root_physical, VM_KERNEL_OWNER);
    kernel_root_physical = 0u;
    vm_stats.supervisor_table_pages = 0u;
    return status;
}

static bool valid_user_page(uint32_t virtual_address)
{
    return (virtual_address & (KERNEL_PAGE_SIZE - 1u)) == 0u &&
           virtual_address >= KERNEL_VM_USER_MIN &&
           virtual_address <= KERNEL_VM_USER_MAX - (KERNEL_PAGE_SIZE - 1u);
}

static bool host_channel_virtual_page(uint32_t virtual_address)
{
    return (virtual_address & (KERNEL_PAGE_SIZE - 1u)) == 0u &&
           virtual_address >= KERNEL_VM_HOST_CHANNEL_BASE &&
           virtual_address < KERNEL_VM_HOST_CHANNEL_BASE +
                                 KERNEL_VM_HOST_CHANNEL_PAGE_COUNT *
                                     KERNEL_PAGE_SIZE;
}

static bool host_channel_physical_page(uint32_t physical_address)
{
    return (physical_address & (KERNEL_PAGE_SIZE - 1u)) == 0u &&
           physical_address >= KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE &&
           physical_address < KERNEL_VM_HOST_CHANNEL_PHYSICAL_BASE +
                                KERNEL_VM_HOST_CHANNEL_PAGE_COUNT *
                                    KERNEL_PAGE_SIZE;
}

static bool host_channel_mapping(uint32_t virtual_address,
                                 uint32_t descriptor)
{
    return host_channel_virtual_page(virtual_address) &&
           host_channel_physical_page(descriptor & VM_DESC_PAGE_ADDRESS) &&
           (descriptor & VM_DESC_CACHE_INHIBIT) != 0u;
}

static uint32_t private_slot(uint32_t virtual_address)
{
    return (virtual_address - KERNEL_VM_PRIVATE_BASE) /
           KERNEL_VM_PRIVATE_SLOT_SIZE;
}

static bool private_slot_marked(const uint32_t *bitmap, uint32_t slot)
{
    return (bitmap[slot / 32u] & (1u << (slot % 32u))) != 0u;
}

static void private_slot_set(uint32_t *bitmap, uint32_t slot)
{
    bitmap[slot / 32u] |= 1u << (slot % 32u);
}

static bool private_address_reserved(const KernelAddressSpace *space,
                                     uint32_t virtual_address)
{
    return virtual_address >= KERNEL_VM_PRIVATE_BASE &&
           virtual_address < KERNEL_VM_PRIVATE_END &&
           private_slot_marked(space->private_reserved,
                               private_slot(virtual_address));
}

KernelVmStatus kernel_vm_private_reserve(KernelAddressSpace *space,
                                         uint32_t byte_size,
                                         uint32_t permissions,
                                         uint32_t *virtual_base,
                                         uint32_t *mapped_span)
{
    uint32_t slots;
    uint32_t run = 0u;
    uint32_t first = 0u;

    if (!initialized || space == NULL || space->initialized == 0u ||
        byte_size == 0u || virtual_base == NULL || mapped_span == NULL ||
        (permissions & KERNEL_VM_READ) == 0u ||
        (permissions & KERNEL_VM_EXEC) != 0u ||
        (permissions & ~(KERNEL_VM_READ | KERNEL_VM_WRITE)) != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    *virtual_base = 0u;
    *mapped_span = 0u;
    slots = byte_size / KERNEL_VM_PRIVATE_SLOT_SIZE +
            (byte_size % KERNEL_VM_PRIVATE_SLOT_SIZE != 0u ? 1u : 0u);
    if (slots == 0u || slots > KERNEL_VM_PRIVATE_SLOT_COUNT)
        return KERNEL_VM_OUT_OF_MEMORY;
    for (uint32_t slot = 0u; slot < KERNEL_VM_PRIVATE_SLOT_COUNT; ++slot) {
        uint32_t base = KERNEL_VM_PRIVATE_BASE +
                        slot * KERNEL_VM_PRIVATE_SLOT_SIZE;
        bool unmapped = true;

        for (uint32_t offset = 0u; offset < KERNEL_VM_PRIVATE_SLOT_SIZE;
             offset += KERNEL_PAGE_SIZE) {
            KernelVmMapping mapping = probe_root(
                space->root_physical, base + offset, NULL);

            if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
                return KERNEL_VM_CORRUPT;
            if (mapping != KERNEL_VM_MAPPING_UNMAPPED) {
                unmapped = false;
                break;
            }
        }
        if (unmapped &&
            !private_slot_marked(space->private_reserved, slot)) {
            if (run == 0u)
                first = slot;
            if (++run == slots)
                break;
        } else {
            run = 0u;
        }
    }
    if (run != slots)
        return KERNEL_VM_OUT_OF_MEMORY;
    for (uint32_t slot = first; slot < first + slots; ++slot) {
        private_slot_set(space->private_reserved, slot);
        if ((permissions & KERNEL_VM_WRITE) != 0u)
            private_slot_set(space->private_writable, slot);
    }
    *virtual_base = KERNEL_VM_PRIVATE_BASE +
                    first * KERNEL_VM_PRIVATE_SLOT_SIZE;
    *mapped_span = slots * KERNEL_VM_PRIVATE_SLOT_SIZE;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_private_fault(KernelAddressSpace *space,
                                       uint32_t virtual_address,
                                       bool write)
{
    uint32_t physical = 0u;
    uint32_t page;
    uint32_t slot;
    KernelMemoryStatus memory_status;
    KernelVmStatus status;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !private_address_reserved(space, virtual_address))
        return KERNEL_VM_NOT_MAPPED;
    slot = private_slot(virtual_address);
    if (write && !private_slot_marked(space->private_writable, slot))
        return KERNEL_VM_NOT_OWNED;
    page = virtual_address & ~(KERNEL_PAGE_SIZE - 1u);
    {
        KernelVmMapping mapping = probe_root(
            space->root_physical, page, NULL);

        if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
            return KERNEL_VM_CORRUPT;
        if (mapping != KERNEL_VM_MAPPING_UNMAPPED)
            return KERNEL_VM_ALREADY_MAPPED;
    }
    memory_status = kernel_memory_alloc_zeroed_tagged(
        KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, 1u, 1u,
        KERNEL_FRAME_PROCESS, space->owner, &physical);
    if (memory_status != KERNEL_MEMORY_OK)
        return memory_status == KERNEL_MEMORY_OUT_OF_MEMORY ?
            KERNEL_VM_OUT_OF_MEMORY : KERNEL_VM_CORRUPT;
    status = kernel_vm_map_page(
        space, page, physical,
        KERNEL_VM_READ |
            (private_slot_marked(space->private_writable, slot) ?
                 KERNEL_VM_WRITE : 0u));
    if (status == KERNEL_VM_OK) {
        if (kernel_memory_release(physical, 1u, space->owner) !=
            KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
        return KERNEL_VM_OK;
    }
    if (kernel_memory_release(physical, 1u, space->owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    return status;
}

KernelVmStatus kernel_vm_private_commit_range(KernelAddressSpace *space,
                                              uint32_t virtual_address,
                                              uint32_t byte_size,
                                              bool write)
{
    uint64_t end;
    uint32_t first;
    uint32_t last;

    if (!initialized || space == NULL || space->initialized == 0u ||
        byte_size == 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    end = (uint64_t)virtual_address + byte_size;
    if (virtual_address < KERNEL_VM_PRIVATE_BASE ||
        end > KERNEL_VM_PRIVATE_END)
        return KERNEL_VM_NOT_MAPPED;
    for (uint32_t address = virtual_address &
                                ~(KERNEL_VM_PRIVATE_SLOT_SIZE - 1u);
         (uint64_t)address < end; address += KERNEL_VM_PRIVATE_SLOT_SIZE) {
        uint32_t slot = private_slot(address);

        if (!private_slot_marked(space->private_reserved, slot))
            return KERNEL_VM_NOT_MAPPED;
        if (write && !private_slot_marked(space->private_writable, slot))
            return KERNEL_VM_NOT_OWNED;
    }
    first = virtual_address & ~(KERNEL_PAGE_SIZE - 1u);
    last = ((uint32_t)end - 1u) & ~(KERNEL_PAGE_SIZE - 1u);
    for (uint32_t address = first;; address += KERNEL_PAGE_SIZE) {
        KernelVmMapping mapping = probe_root(
            space->root_physical, address, NULL);

        if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
            return KERNEL_VM_CORRUPT;
        if (mapping == KERNEL_VM_MAPPING_UNMAPPED) {
            KernelVmStatus status = kernel_vm_private_fault(
                space, address, write);

            if (status != KERNEL_VM_OK)
                return status;
        } else if (write && mapping != KERNEL_VM_MAPPING_READ_WRITE)
            return KERNEL_VM_NOT_OWNED;
        if (address == last)
            break;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_private_decommit(KernelAddressSpace *space,
                                          uint32_t virtual_address,
                                          uint32_t byte_size,
                                          uint32_t *released_pages)
{
    uint64_t end;
    uint32_t first;
    uint32_t last;
    uint32_t released = 0u;

    if (!initialized || space == NULL || space->initialized == 0u ||
        byte_size == 0u || released_pages == NULL)
        return KERNEL_VM_INVALID_ARGUMENT;
    *released_pages = 0u;
    end = (uint64_t)virtual_address + byte_size;
    if (virtual_address < KERNEL_VM_PRIVATE_BASE ||
        end > KERNEL_VM_PRIVATE_END)
        return KERNEL_VM_INVALID_ARGUMENT;
    first = (virtual_address + KERNEL_PAGE_SIZE - 1u) &
            ~(KERNEL_PAGE_SIZE - 1u);
    last = (uint32_t)end & ~(KERNEL_PAGE_SIZE - 1u);
    for (uint32_t address = virtual_address &
                                ~(KERNEL_VM_PRIVATE_SLOT_SIZE - 1u);
         (uint64_t)address < end; address += KERNEL_VM_PRIVATE_SLOT_SIZE) {
        if (!private_address_reserved(space, address))
            return KERNEL_VM_NOT_MAPPED;
    }
    for (uint32_t address = first; address < last;
         address += KERNEL_PAGE_SIZE) {
        KernelVmMapping mapping = probe_root(
            space->root_physical, address, NULL);

        if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
            return KERNEL_VM_CORRUPT;
        if (mapping == KERNEL_VM_MAPPING_UNMAPPED)
            continue;
        if (kernel_vm_unmap_page(space, address) != KERNEL_VM_OK)
            return KERNEL_VM_CORRUPT;
        ++released;
    }
    *released_pages = released;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_init(void)
{
    KernelVmStatus status;

    initialized = false;
    enabled = false;
    kernel_root_physical = 0u;
    empty_root_physical = 0u;
    current_user_root = 0u;
    address_space_head = NULL;
#if defined(KERNEL_VM_HOST_TEST)
    next_shared_map_fault = KERNEL_VM_SHARED_MAP_FAULT_NONE;
#endif
    /*
     * The alias table comes from the frame allocator, which is running by now,
     * rather than from a static array sized for a board this may not be.
     */
    {
        KernelMemoryStats memory;
        uint32_t bytes;
        uint32_t table_frames;
        uint32_t physical = 0u;

        if (!kernel_memory_stats(&memory))
            return KERNEL_VM_CORRUPT;
        mapped_user_frame_count = memory.total_frames;
        bytes = mapped_user_frame_count * (uint32_t)sizeof(uint16_t);
        table_frames = (bytes + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
        if (kernel_memory_alloc_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE, table_frames, 1u,
                KERNEL_FRAME_PAGE_TABLE, VM_KERNEL_OWNER, &physical) !=
            KERNEL_MEMORY_OK)
            return KERNEL_VM_OUT_OF_MEMORY;
        /*
         * Through the same translation every other physical access here uses,
         * so a host test reaches its bound backing store rather than the raw
         * address, which on a host is somebody else's memory.
         */
        mapped_user_frames =
            (volatile uint16_t *)(volatile void *)physical_words(physical);
        if (mapped_user_frames == NULL) {
            (void)kernel_memory_release(physical, table_frames,
                                        VM_KERNEL_OWNER);
            return KERNEL_VM_CORRUPT;
        }
        for (uint32_t index = 0u; index < mapped_user_frame_count; ++index)
            mapped_user_frames[index] = 0u;
    }
    reset_stats();

    status = build_supervisor_root();
    if (status != KERNEL_VM_OK)
        return status;
    status = allocate_table(VM_KERNEL_OWNER, &empty_root_physical);
    if (status != KERNEL_VM_OK) {
        (void)release_table_tree(kernel_root_physical, VM_KERNEL_OWNER);
        kernel_root_physical = 0u;
        return status;
    }

    supervisor_root = kernel_root_physical;
    current_process_root = empty_root_physical;
    current_user_root = empty_root_physical;
    vm_stats.kernel_root_physical = kernel_root_physical;
    vm_stats.empty_root_physical = empty_root_physical;
    vm_stats.kernel_stack_guard = VM_KERNEL_STACK_GUARD;
    vm_stats.kernel_worker_stack_guard = VM_KERNEL_WORKER_STACK_GUARD;
    vm_stats.kernel_thread_stack_arena = VM_KERNEL_THREAD_STACKS_START;
    vm_stats.kernel_thread_stack_arena_end = VM_KERNEL_THREAD_STACKS_END;
    vm_stats.kernel_thread_stack_guards = KERNEL_THREAD_MAX;
    initialized = true;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_enable(void)
{
    if (!initialized)
        return KERNEL_VM_INVALID_ARGUMENT;
    if (enabled)
        return KERNEL_VM_OK;

    kernel_pmmu_load_tc(0u);
    kernel_pmmu_disable_transparent_translation();
    kernel_pmmu_load_srp(supervisor_root);
    kernel_pmmu_load_urp(current_process_root);
    kernel_pmmu_set_user_function_codes();
    kernel_pmmu_flush_all();
    kernel_cache_invalidate_all();
    kernel_pmmu_load_tc(KERNEL_PMMU_TC_4K);
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
    if (!register_address_space(space)) {
        (void)kernel_memory_release(space->root_physical, 1u, owner);
        clear_space(space);
        return KERNEL_VM_OUT_OF_MEMORY;
    }
    ++vm_stats.address_spaces;
    ++vm_stats.user_table_pages;
    return KERNEL_VM_OK;
}

static bool address_space_maps_page(const KernelAddressSpace *space,
                                    uint32_t virtual_address,
                                    uint32_t physical_address)
{
    uint32_t translated;
    KernelVmMapping mapping;

    if (space == NULL || space->initialized == 0u)
        return false;
    mapping = probe_root(space->root_physical, virtual_address, &translated);
    return (mapping == KERNEL_VM_MAPPING_READ_ONLY ||
            mapping == KERNEL_VM_MAPPING_READ_WRITE) &&
           (translated & VM_DESC_PAGE_ADDRESS) == physical_address;
}

static KernelAddressSpace *cow_surviving_space(
    const KernelAddressSpace *excluded, uint32_t virtual_address,
    uint32_t physical_address)
{
    KernelAddressSpace *candidate = address_space_head;

    while (candidate != NULL) {
        if (candidate != excluded &&
            address_space_maps_page(candidate, virtual_address,
                                    physical_address))
            return candidate;
        candidate = candidate->registry_next;
    }
    return NULL;
}

static KernelVmStatus cow_rehome_before_unmap(KernelAddressSpace *space,
                                               uint32_t virtual_address,
                                               uint32_t physical_address,
                                               KernelFrameInfo *frame)
{
    uint16_t encoded;
    KernelAddressSpace *survivor;

    if (!cow_mapping_state((KernelFrameState)frame->state) ||
        frame->owner != space->owner)
        return KERNEL_VM_OK;
    encoded = mapped_user_frames[frame_index(physical_address)];
    if ((encoded >> VM_MAPPING_COUNT_SHIFT) <= 1u)
        return KERNEL_VM_OK;
    survivor = cow_surviving_space(space, virtual_address, physical_address);
    if (survivor == NULL ||
        kernel_memory_transfer_owner(physical_address, frame->owner,
                                     survivor->owner) != KERNEL_MEMORY_OK ||
        !kernel_memory_frame_info(physical_address, frame))
        return KERNEL_VM_CORRUPT;
    return KERNEL_VM_OK;
}

/*
 * Transfer memory is mapped cache-inhibited. The 68040 data cache does not
 * snoop the device writing into these frames, so a cached mapping would hand
 * the service whichever stale line it read last. Ordinary process pages stay
 * cacheable.
 */
static KernelVmStatus map_owned_page(KernelAddressSpace *space,
                                     uint32_t virtual_address,
                                     uint32_t physical_address,
                                     uint32_t permissions,
                                     KernelFrameState required_state,
                                     bool cache_inhibit,
                                     uint32_t frame_owner)
{
    VmPagePath path;
    KernelFrameInfo frame;
    uint32_t allocated_tables;
    uint32_t released_tables;
    KernelVmStatus status;

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
        frame.owner != frame_owner || frame.state != required_state ||
        frame.references == 0u)
        return KERNEL_VM_NOT_OWNED;

    status = ensure_page_path(space->root_physical, virtual_address,
                              space->owner, &path, &allocated_tables);
    if (status != KERNEL_VM_OK)
        return status;
    if (path.page_table[VM_PAGE_INDEX(virtual_address)] != VM_DESC_INVALID) {
        status = KERNEL_VM_ALREADY_MAPPED;
        goto rollback_tables;
    }
    if (!frame_mapping_can_add(physical_address, required_state,
                               virtual_address)) {
        status = KERNEL_VM_CACHE_ALIAS;
        goto rollback_tables;
    }
    if (kernel_memory_retain(physical_address, 1u, frame_owner) !=
        KERNEL_MEMORY_OK) {
        status = KERNEL_VM_CORRUPT;
        goto rollback_tables;
    }

    if (!frame_mapping_add(physical_address, required_state,
                           virtual_address)) {
        (void)kernel_memory_release(physical_address, 1u, frame_owner);
        status = KERNEL_VM_CORRUPT;
        goto rollback_tables;
    }
    path.page_table[VM_PAGE_INDEX(virtual_address)] =
        physical_address | VM_DESC_PAGE |
        ((permissions & KERNEL_VM_WRITE) != 0u ? 0u :
         VM_DESC_WRITE_PROTECT) |
        (cache_inhibit ? VM_DESC_CACHE_INHIBIT : 0u);
    invalidate_caches();
    flush_page(virtual_address);
    ++space->mapped_pages;
    ++vm_stats.user_mappings;
    space->table_pages += allocated_tables;
    vm_stats.user_table_pages += allocated_tables;
    return KERNEL_VM_OK;

rollback_tables:
    if (allocated_tables != 0u &&
        (release_empty_path(space->root_physical, virtual_address,
                            space->owner, &released_tables) != KERNEL_VM_OK ||
         released_tables != allocated_tables))
        return KERNEL_VM_CORRUPT;
    return status;
}

KernelVmStatus kernel_vm_map_host_channel_page(
    KernelAddressSpace *space, uint32_t virtual_address,
    uint32_t physical_address)
{
    VmPagePath path;
    uint32_t allocated_tables;
    uint32_t released_tables;
    KernelVmStatus status;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !host_channel_virtual_page(virtual_address) ||
        !host_channel_physical_page(physical_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    status = ensure_page_path(space->root_physical, virtual_address,
                              space->owner, &path, &allocated_tables);
    if (status != KERNEL_VM_OK)
        return status;
    if (path.page_table[VM_PAGE_INDEX(virtual_address)] != VM_DESC_INVALID) {
        if (allocated_tables != 0u &&
            (release_empty_path(space->root_physical, virtual_address,
                                space->owner, &released_tables) !=
                 KERNEL_VM_OK ||
             released_tables != allocated_tables))
            return KERNEL_VM_CORRUPT;
        return KERNEL_VM_ALREADY_MAPPED;
    }
    path.page_table[VM_PAGE_INDEX(virtual_address)] =
        physical_address | VM_DESC_CACHE_INHIBIT | VM_DESC_PAGE;
    invalidate_caches();
    flush_page(virtual_address);
    ++space->mapped_pages;
    ++vm_stats.user_mappings;
    space->table_pages += allocated_tables;
    vm_stats.user_table_pages += allocated_tables;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_map_page(KernelAddressSpace *space,
                                  uint32_t virtual_address,
                                  uint32_t physical_address,
                                  uint32_t permissions)
{
    return map_owned_page(space, virtual_address, physical_address,
                          permissions, KERNEL_FRAME_PROCESS, false,
                          space->owner);
}

KernelVmStatus kernel_vm_map_transfer_page(KernelAddressSpace *space,
                                           uint32_t virtual_address,
                                           uint32_t physical_address,
                                           uint32_t permissions)
{
    if ((permissions & KERNEL_VM_EXEC) != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    return map_owned_page(space, virtual_address, physical_address,
                          permissions, KERNEL_FRAME_DMA, true, space->owner);
}

KernelVmStatus kernel_vm_map_shared_page(KernelAddressSpace *space,
                                         uint32_t virtual_address,
                                         uint32_t physical_address,
                                         uint32_t frame_owner,
                                         uint32_t permissions)
{
    if (frame_owner == KERNEL_OWNER_NONE)
        return KERNEL_VM_INVALID_ARGUMENT;
    return map_owned_page(space, virtual_address, physical_address,
                          permissions, KERNEL_FRAME_SHARED, false,
                          frame_owner);
}

KernelVmStatus kernel_vm_map_cow_page(KernelAddressSpace *space,
                                      uint32_t virtual_address,
                                      uint32_t physical_address,
                                      uint32_t frame_owner,
                                      bool writable)
{
    if (frame_owner == KERNEL_OWNER_NONE)
        return KERNEL_VM_INVALID_ARGUMENT;
    return map_owned_page(space, virtual_address, physical_address,
                          KERNEL_VM_READ,
                          writable ? KERNEL_FRAME_COW_WRITE :
                                     KERNEL_FRAME_COW_READ_ONLY,
                          false, frame_owner);
}

KernelVmStatus kernel_vm_promote_page_to_cow(KernelAddressSpace *space,
                                             uint32_t virtual_address)
{
    VmPagePath path;
    uint32_t descriptor;
    uint32_t physical;
    uint32_t index;
    KernelFrameInfo frame;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    {
        KernelVmStatus status = page_path(
            space->root_physical, virtual_address, &path);

        if (status != KERNEL_VM_OK)
            return status;
    }
    descriptor = path.page_table[VM_PAGE_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_CORRUPT;
    physical = descriptor & VM_DESC_PAGE_ADDRESS;
    index = frame_index(physical);
    if (!kernel_memory_frame_info(physical, &frame) ||
        frame.owner != space->owner || frame.state != KERNEL_FRAME_PROCESS ||
        mapped_user_frames[index] !=
            ((1u << VM_MAPPING_COUNT_SHIFT) | VM_MAPPING_PRIVATE_CLASS))
        return KERNEL_VM_NOT_OWNED;
    if (kernel_memory_reclassify(
            physical, space->owner, KERNEL_FRAME_PROCESS,
            (descriptor & VM_DESC_WRITE_PROTECT) != 0u ?
                KERNEL_FRAME_COW_READ_ONLY : KERNEL_FRAME_COW_WRITE) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    mapped_user_frames[index] =
        (1u << VM_MAPPING_COUNT_SHIFT) | VM_MAPPING_COW_CLASS;
    path.page_table[VM_PAGE_INDEX(virtual_address)] =
        descriptor | VM_DESC_WRITE_PROTECT;
    flush_page(virtual_address);
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_cow_make_private(KernelAddressSpace *space,
                                          uint32_t virtual_address)
{
    VmPagePath path;
    uint32_t descriptor;
    uint32_t physical;
    uint32_t index;
    KernelFrameInfo frame;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    {
        KernelVmStatus status = page_path(
            space->root_physical, virtual_address, &path);

        if (status != KERNEL_VM_OK)
            return status;
    }
    descriptor = path.page_table[VM_PAGE_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_CORRUPT;
    physical = descriptor & VM_DESC_PAGE_ADDRESS;
    index = frame_index(physical);
    if (!kernel_memory_frame_info(physical, &frame) ||
        frame.owner != space->owner ||
        !cow_mapping_state((KernelFrameState)frame.state) ||
        mapped_user_frames[index] !=
            ((1u << VM_MAPPING_COUNT_SHIFT) | VM_MAPPING_COW_CLASS))
        return KERNEL_VM_BUSY;
    if (kernel_memory_reclassify(
            physical, space->owner, (KernelFrameState)frame.state,
            KERNEL_FRAME_PROCESS) != KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    mapped_user_frames[index] =
        (1u << VM_MAPPING_COUNT_SHIFT) | VM_MAPPING_PRIVATE_CLASS;
    path.page_table[VM_PAGE_INDEX(virtual_address)] =
        frame.state == KERNEL_FRAME_COW_WRITE ?
            descriptor & ~VM_DESC_WRITE_PROTECT :
            descriptor | VM_DESC_WRITE_PROTECT;
    flush_page(virtual_address);
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_cow_fault(KernelAddressSpace *space,
                                   uint32_t virtual_address)
{
    VmPagePath path;
    volatile uint32_t *source;
    volatile uint32_t *destination;
    uint32_t descriptor;
    uint32_t physical;
    uint32_t replacement = 0u;
    uint16_t encoded;
    KernelFrameInfo frame;

    virtual_address &= ~(KERNEL_PAGE_SIZE - 1u);
    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    {
        KernelVmStatus status = page_path(
            space->root_physical, virtual_address, &path);

        if (status != KERNEL_VM_OK)
            return status;
    }
    descriptor = path.page_table[VM_PAGE_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_CORRUPT;
    physical = descriptor & VM_DESC_PAGE_ADDRESS;
    encoded = mapped_user_frames[frame_index(physical)];
    if (!kernel_memory_frame_info(physical, &frame) ||
        frame.state != KERNEL_FRAME_COW_WRITE ||
        (encoded & VM_MAPPING_CLASS_MASK) != VM_MAPPING_COW_CLASS)
        return KERNEL_VM_NOT_OWNED;

    if ((encoded >> VM_MAPPING_COUNT_SHIFT) == 1u) {
        if (frame.owner != space->owner &&
            kernel_memory_transfer_owner(physical, frame.owner,
                                         space->owner) != KERNEL_MEMORY_OK)
            return KERNEL_VM_CORRUPT;
        return kernel_vm_cow_make_private(space, virtual_address);
    }

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_PROCESS_PRIVATE_PAGE, 1u, 1u,
            KERNEL_FRAME_PROCESS, space->owner, &replacement) !=
        KERNEL_MEMORY_OK)
        return KERNEL_VM_OUT_OF_MEMORY;
    source = physical_words(physical);
    destination = physical_words(replacement);
    if (source == NULL || destination == NULL) {
        (void)kernel_memory_release(replacement, 1u, space->owner);
        return KERNEL_VM_CORRUPT;
    }
    for (uint32_t word = 0u; word < KERNEL_PAGE_SIZE / sizeof(uint32_t);
         ++word)
        destination[word] = source[word];
    if (!frame_mapping_add(replacement, KERNEL_FRAME_PROCESS,
                           virtual_address)) {
        (void)kernel_memory_release(replacement, 1u, space->owner);
        return KERNEL_VM_CORRUPT;
    }
    if (!frame_mapping_remove(physical, KERNEL_FRAME_COW_WRITE,
                              virtual_address)) {
        (void)frame_mapping_remove(replacement, KERNEL_FRAME_PROCESS,
                                   virtual_address);
        (void)kernel_memory_release(replacement, 1u, space->owner);
        return KERNEL_VM_CORRUPT;
    }
    path.page_table[VM_PAGE_INDEX(virtual_address)] =
        replacement | VM_DESC_PAGE;
    invalidate_caches();
    flush_page(virtual_address);
    if (kernel_memory_release(physical, 1u, frame.owner) != KERNEL_MEMORY_OK)
        return KERNEL_VM_CORRUPT;
    return KERNEL_VM_OK;
}

static KernelVmStatus restore_single_cow_pages(KernelAddressSpace *space)
{
    volatile uint32_t *root = physical_words(space->root_physical);

    if (root == NULL)
        return KERNEL_VM_CORRUPT;
    for (uint32_t root_index = 0u; root_index < VM_ROOT_ENTRIES;
         ++root_index) {
        volatile uint32_t *pointer;

        if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
            continue;
        if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
            return KERNEL_VM_CORRUPT;
        pointer = physical_words(root[root_index] & VM_DESC_TABLE_ADDRESS);
        if (pointer == NULL)
            return KERNEL_VM_CORRUPT;
        for (uint32_t pointer_index = 0u;
             pointer_index < VM_POINTER_ENTRIES; ++pointer_index) {
            volatile uint32_t *page_table;

            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) ==
                VM_DESC_INVALID)
                continue;
            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) !=
                VM_DESC_TABLE)
                return KERNEL_VM_CORRUPT;
            page_table = physical_words(
                pointer[pointer_index] & VM_DESC_TABLE_ADDRESS);
            if (page_table == NULL)
                return KERNEL_VM_CORRUPT;
            for (uint32_t page_index = 0u; page_index < VM_PAGE_ENTRIES;
                 ++page_index) {
                uint32_t virtual_address;
                uint32_t physical;
                KernelFrameInfo frame;
                KernelVmStatus status;

                if ((page_table[page_index] & VM_DESC_TYPE_MASK) !=
                    VM_DESC_PAGE)
                    continue;
                physical = page_table[page_index] & VM_DESC_PAGE_ADDRESS;
                virtual_address = (root_index << 25) |
                                  (pointer_index << 18) |
                                  (page_index << KERNEL_PAGE_SHIFT);
                if (host_channel_mapping(virtual_address,
                                         page_table[page_index]))
                    continue;
                if (!kernel_memory_frame_info(physical, &frame))
                    return KERNEL_VM_CORRUPT;
                if (!cow_mapping_state((KernelFrameState)frame.state))
                    continue;
                status = kernel_vm_cow_make_private(space, virtual_address);
                if (status != KERNEL_VM_OK && status != KERNEL_VM_BUSY)
                    return KERNEL_VM_CORRUPT;
            }
        }
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_clone_address_space(
    KernelAddressSpace *source, uint32_t owner,
    KernelAddressSpace *destination)
{
    volatile uint32_t *root;
    KernelVmStatus result;

    if (!initialized || source == NULL || source->initialized == 0u ||
        destination == NULL || destination->initialized != 0u ||
        source == destination || owner == source->owner)
        return KERNEL_VM_INVALID_ARGUMENT;
    result = kernel_vm_create_address_space(owner, destination);
    if (result != KERNEL_VM_OK)
        return result;
    root = physical_words(source->root_physical);
    if (root == NULL) {
        result = KERNEL_VM_CORRUPT;
        goto failed;
    }
    for (uint32_t root_index = 0u; root_index < VM_ROOT_ENTRIES;
         ++root_index) {
        volatile uint32_t *pointer;

        if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
            continue;
        if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE) {
            result = KERNEL_VM_CORRUPT;
            goto failed;
        }
        pointer = physical_words(root[root_index] & VM_DESC_TABLE_ADDRESS);
        if (pointer == NULL) {
            result = KERNEL_VM_CORRUPT;
            goto failed;
        }
        for (uint32_t pointer_index = 0u;
             pointer_index < VM_POINTER_ENTRIES; ++pointer_index) {
            volatile uint32_t *page_table;

            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) ==
                VM_DESC_INVALID)
                continue;
            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) !=
                VM_DESC_TABLE) {
                result = KERNEL_VM_CORRUPT;
                goto failed;
            }
            page_table = physical_words(
                pointer[pointer_index] & VM_DESC_TABLE_ADDRESS);
            if (page_table == NULL) {
                result = KERNEL_VM_CORRUPT;
                goto failed;
            }
            for (uint32_t page_index = 0u;
                 page_index < VM_PAGE_ENTRIES; ++page_index) {
                uint32_t descriptor = page_table[page_index];
                uint32_t virtual_address;
                uint32_t physical;
                KernelFrameInfo frame;

                if ((descriptor & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
                    continue;
                if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE) {
                    result = KERNEL_VM_CORRUPT;
                    goto failed;
                }
                physical = descriptor & VM_DESC_PAGE_ADDRESS;
                virtual_address = (root_index << 25) |
                                  (pointer_index << 18) |
                                  (page_index << KERNEL_PAGE_SHIFT);
                if (host_channel_mapping(virtual_address, descriptor))
                    continue;
                if (!kernel_memory_frame_info(physical, &frame)) {
                    result = KERNEL_VM_CORRUPT;
                    goto failed;
                }
                if (frame.state == KERNEL_FRAME_DMA)
                    continue;
                if (frame.state == KERNEL_FRAME_PROCESS) {
                    result = kernel_vm_promote_page_to_cow(
                        source, virtual_address);
                    if (result != KERNEL_VM_OK ||
                        !kernel_memory_frame_info(physical, &frame))
                        goto failed;
                }
                if (cow_mapping_state((KernelFrameState)frame.state)) {
                    result = kernel_vm_map_cow_page(
                        destination, virtual_address, physical, frame.owner,
                        frame.state == KERNEL_FRAME_COW_WRITE);
                } else if (frame.state == KERNEL_FRAME_SHARED) {
                    uint32_t permissions = KERNEL_VM_READ;

                    if ((descriptor & VM_DESC_WRITE_PROTECT) == 0u)
                        permissions |= KERNEL_VM_WRITE;
                    else if (library_mapping_address(virtual_address))
                        permissions |= KERNEL_VM_EXEC;
                    result = kernel_vm_map_shared_page(
                        destination, virtual_address, physical, frame.owner,
                        permissions);
                } else {
                    result = KERNEL_VM_CORRUPT;
                }
                if (result != KERNEL_VM_OK)
                    goto failed;
            }
        }
    }
    for (uint32_t index = 0u; index < KERNEL_VM_PRIVATE_BITMAP_WORDS;
         ++index) {
        destination->private_reserved[index] =
            source->private_reserved[index];
        destination->private_writable[index] =
            source->private_writable[index];
    }
    return KERNEL_VM_OK;

failed:
    if (kernel_vm_destroy_address_space(destination) != KERNEL_VM_OK ||
        restore_single_cow_pages(source) != KERNEL_VM_OK)
        return KERNEL_VM_CORRUPT;
    return result;
}

KernelVmStatus kernel_vm_exchange_address_spaces(KernelAddressSpace *left,
                                                  KernelAddressSpace *right)
{
    uint32_t value;

    if (left == NULL || right == NULL || left == right ||
        left->initialized == 0u || right->initialized == 0u ||
        left->owner == 0u || left->owner != right->owner)
        return KERNEL_VM_INVALID_ARGUMENT;
#define EXCHANGE_WORD(field) do {                                           \
        value = left->field;                                                \
        left->field = right->field;                                         \
        right->field = value;                                               \
    } while (0)
    EXCHANGE_WORD(root_physical);
    EXCHANGE_WORD(mapped_pages);
    EXCHANGE_WORD(table_pages);
    for (uint32_t index = 0u; index < KERNEL_VM_PRIVATE_BITMAP_WORDS;
         ++index) {
        EXCHANGE_WORD(private_reserved[index]);
        EXCHANGE_WORD(private_writable[index]);
    }
#undef EXCHANGE_WORD
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_unmap_page(KernelAddressSpace *space,
                                    uint32_t virtual_address)
{
    VmPagePath path;
    uint32_t descriptor;
    uint32_t page_physical;
    uint32_t frame_owner;
    uint32_t released_tables;
    KernelFrameInfo frame;
    bool host_channel;

    if (!initialized || space == NULL || space->initialized == 0u ||
        !valid_user_page(virtual_address))
        return KERNEL_VM_INVALID_ARGUMENT;
    {
        KernelVmStatus status = page_path(
            space->root_physical, virtual_address, &path);

        if (status != KERNEL_VM_OK)
            return status;
    }
    descriptor = path.page_table[VM_PAGE_INDEX(virtual_address)];
    if (descriptor == VM_DESC_INVALID)
        return KERNEL_VM_NOT_MAPPED;
    if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
        return KERNEL_VM_CORRUPT;
    page_physical = descriptor & VM_DESC_PAGE_ADDRESS;
    host_channel = host_channel_mapping(virtual_address, descriptor);
    if (!host_channel &&
        (!kernel_memory_frame_info(page_physical, &frame) ||
        (frame.state != KERNEL_FRAME_PROCESS &&
         frame.state != KERNEL_FRAME_COW_READ_ONLY &&
         frame.state != KERNEL_FRAME_COW_WRITE &&
         frame.state != KERNEL_FRAME_SHARED &&
         frame.state != KERNEL_FRAME_DMA) ||
         !frame_is_user_mapped(page_physical)))
        return KERNEL_VM_CORRUPT;
    if (!host_channel &&
        cow_rehome_before_unmap(space, virtual_address, page_physical,
                                &frame) != KERNEL_VM_OK)
        return KERNEL_VM_CORRUPT;
    frame_owner = host_channel ? 0u : frame.owner;
    path.page_table[VM_PAGE_INDEX(virtual_address)] = VM_DESC_INVALID;
    invalidate_caches();
    flush_page(virtual_address);
    if (!host_channel &&
        (!frame_mapping_remove(page_physical,
                               (KernelFrameState)frame.state,
                               virtual_address) ||
         kernel_memory_release(page_physical, 1u, frame_owner) !=
             KERNEL_MEMORY_OK))
        return KERNEL_VM_CORRUPT;
    --space->mapped_pages;
    --vm_stats.user_mappings;

    if (release_empty_path(space->root_physical, virtual_address,
                           space->owner, &released_tables) != KERNEL_VM_OK)
        return KERNEL_VM_CORRUPT;
    if (released_tables != 0u) {
        flush_all();
        space->table_pages -= released_tables;
        vm_stats.user_table_pages -= released_tables;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_map_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner, uint32_t permissions)
{
    uint32_t mapped = 0u;
    KernelVmStatus result = KERNEL_VM_CORRUPT;
    uint8_t mapping_class;

    if (!initialized || space == NULL || space->initialized == 0u ||
        physical_pages == NULL || page_count == 0u ||
        page_count > KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE ||
        frame_owner == KERNEL_OWNER_NONE ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        !valid_user_page(virtual_address) ||
        page_count - 1u >
            (KERNEL_VM_USER_MAX - virtual_address) / KERNEL_PAGE_SIZE ||
        (permissions & KERNEL_VM_READ) == 0u ||
        ((permissions & KERNEL_VM_WRITE) != 0u &&
         (permissions & KERNEL_VM_EXEC) != 0u) ||
        ((permissions & KERNEL_VM_EXEC) != 0u &&
         !library_mapping_address(virtual_address)) ||
        (permissions &
         ~(KERNEL_VM_READ | KERNEL_VM_WRITE | KERNEL_VM_EXEC)) != 0u)
        return KERNEL_VM_INVALID_ARGUMENT;

    for (uint32_t page = 0u; page < page_count; ++page) {
        KernelFrameInfo frame;
        uint32_t physical = physical_pages[page];
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;
        uint8_t address_class;
        KernelVmMapping mapping;

        if ((physical & (KERNEL_PAGE_SIZE - 1u)) != 0u ||
            !kernel_memory_frame_info(physical, &frame) ||
            !shared_mapping_class(address, &address_class) ||
            address_class != mapping_class)
            return KERNEL_VM_INVALID_ARGUMENT;
        mapping = probe_root(space->root_physical, address, NULL);
        if (mapping == KERNEL_VM_MAPPING_UNKNOWN)
            return KERNEL_VM_CORRUPT;
        if (mapping != KERNEL_VM_MAPPING_UNMAPPED)
            return KERNEL_VM_ALREADY_MAPPED;
        if (frame.owner != frame_owner ||
            frame.state != KERNEL_FRAME_SHARED || frame.references == 0u)
            return KERNEL_VM_NOT_OWNED;
        if (!frame_mapping_can_add(physical, KERNEL_FRAME_SHARED, address))
            return KERNEL_VM_CACHE_ALIAS;
        for (uint32_t prior = 0u; prior < page; ++prior) {
            if (physical_pages[prior] == physical)
                return KERNEL_VM_INVALID_ARGUMENT;
        }
    }

    for (; mapped < page_count; ++mapped) {
        result = map_owned_page(
            space, virtual_address + mapped * KERNEL_PAGE_SIZE,
            physical_pages[mapped], permissions, KERNEL_FRAME_SHARED,
            false, frame_owner);
        if (result != KERNEL_VM_OK)
            goto rollback;
#if defined(KERNEL_VM_HOST_TEST)
        if (consume_shared_map_fault(
                KERNEL_VM_SHARED_MAP_FAULT_AFTER_TABLE_ALLOCATE) ||
            consume_shared_map_fault(
                KERNEL_VM_SHARED_MAP_FAULT_AFTER_FRAME_RETAIN) ||
            consume_shared_map_fault(
                KERNEL_VM_SHARED_MAP_FAULT_AFTER_MAPPING_METADATA) ||
            consume_shared_map_fault(
                KERNEL_VM_SHARED_MAP_FAULT_AFTER_DESCRIPTOR_PUBLISH) ||
            consume_shared_map_fault(
                KERNEL_VM_SHARED_MAP_FAULT_AFTER_ROOT_PUBLISH)) {
            result = KERNEL_VM_OUT_OF_MEMORY;
            ++mapped;
            goto rollback;
        }
#endif
    }
    return KERNEL_VM_OK;

rollback:
    while (mapped != 0u) {
        --mapped;
        if (kernel_vm_unmap_page(
                space, virtual_address + mapped * KERNEL_PAGE_SIZE) !=
            KERNEL_VM_OK)
            return KERNEL_VM_CORRUPT;
    }
    return result;
}

KernelVmStatus kernel_vm_unmap_shared_range(
    KernelAddressSpace *space, uint32_t virtual_address,
    const uint32_t *physical_pages, uint32_t page_count,
    uint32_t frame_owner)
{
    uint8_t mapping_class;

    if (!initialized || space == NULL || space->initialized == 0u ||
        physical_pages == NULL || page_count == 0u ||
        page_count > KERNEL_VM_AREA_SLOT_SIZE / KERNEL_PAGE_SIZE ||
        frame_owner == KERNEL_OWNER_NONE ||
        !shared_mapping_class(virtual_address, &mapping_class) ||
        !valid_user_page(virtual_address) ||
        page_count - 1u >
            (KERNEL_VM_USER_MAX - virtual_address) / KERNEL_PAGE_SIZE)
        return KERNEL_VM_INVALID_ARGUMENT;

    for (uint32_t page = 0u; page < page_count; ++page) {
        VmPagePath path;
        KernelFrameInfo frame;
        uint32_t address = virtual_address + page * KERNEL_PAGE_SIZE;
        uint32_t descriptor;
        uint8_t address_class;
        KernelVmStatus status = page_path(
            space->root_physical, address, &path);

        if (status != KERNEL_VM_OK)
            return status;
        if (!shared_mapping_class(address, &address_class) ||
            address_class != mapping_class)
            return KERNEL_VM_INVALID_ARGUMENT;
        descriptor = path.page_table[VM_PAGE_INDEX(address)];
        if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE ||
            (descriptor & VM_DESC_PAGE_ADDRESS) != physical_pages[page])
            return KERNEL_VM_NOT_MAPPED;
        if (!kernel_memory_frame_info(physical_pages[page], &frame) ||
            frame.owner != frame_owner || frame.state != KERNEL_FRAME_SHARED ||
            !frame_is_user_mapped(physical_pages[page]))
            return KERNEL_VM_CORRUPT;
    }

    for (uint32_t page = 0u; page < page_count; ++page) {
        if (kernel_vm_unmap_page(
                space, virtual_address + page * KERNEL_PAGE_SIZE) !=
            KERNEL_VM_OK)
            return KERNEL_VM_CORRUPT;
    }
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_destroy_address_space(KernelAddressSpace *space)
{
    volatile uint32_t *root;
    uint32_t table_pages;

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
        volatile uint32_t *pointer;

        if ((root[root_index] & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
            continue;
        if ((root[root_index] & VM_DESC_TYPE_MASK) != VM_DESC_TABLE)
            return KERNEL_VM_CORRUPT;
        pointer = physical_words(root[root_index] & VM_DESC_TABLE_ADDRESS);
        if (pointer == NULL)
            return KERNEL_VM_CORRUPT;
        for (uint32_t pointer_index = 0u;
             pointer_index < VM_POINTER_ENTRIES; ++pointer_index) {
            volatile uint32_t *page_table;

            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) ==
                VM_DESC_INVALID)
                continue;
            if ((pointer[pointer_index] & VM_DESC_TYPE_MASK) !=
                VM_DESC_TABLE)
                return KERNEL_VM_CORRUPT;
            page_table = physical_words(
                pointer[pointer_index] & VM_DESC_TABLE_ADDRESS);
            if (page_table == NULL)
                return KERNEL_VM_CORRUPT;
            for (uint32_t page_index = 0u; page_index < VM_PAGE_ENTRIES;
                 ++page_index) {
                uint32_t descriptor = page_table[page_index];
                uint32_t page_physical;
                uint32_t virtual_address;
                KernelFrameInfo frame;

                if ((descriptor & VM_DESC_TYPE_MASK) == VM_DESC_INVALID)
                    continue;
                if ((descriptor & VM_DESC_TYPE_MASK) != VM_DESC_PAGE)
                    return KERNEL_VM_CORRUPT;
                page_physical = descriptor & VM_DESC_PAGE_ADDRESS;
                virtual_address = (root_index << 25) |
                                  (pointer_index << 18) |
                                  (page_index << KERNEL_PAGE_SHIFT);
                if (host_channel_mapping(virtual_address, descriptor)) {
                    page_table[page_index] = VM_DESC_INVALID;
                    --space->mapped_pages;
                    --vm_stats.user_mappings;
                    continue;
                }
                if (!kernel_memory_frame_info(page_physical, &frame) ||
                    (frame.state != KERNEL_FRAME_PROCESS &&
                     frame.state != KERNEL_FRAME_COW_READ_ONLY &&
                     frame.state != KERNEL_FRAME_COW_WRITE &&
                     frame.state != KERNEL_FRAME_SHARED) ||
                    !frame_is_user_mapped(page_physical))
                    return KERNEL_VM_CORRUPT;
                if (cow_rehome_before_unmap(space, virtual_address,
                                            page_physical, &frame) !=
                    KERNEL_VM_OK)
                    return KERNEL_VM_CORRUPT;
                page_table[page_index] = VM_DESC_INVALID;
                if (!frame_mapping_remove(
                        page_physical, (KernelFrameState)frame.state,
                        virtual_address) ||
                    kernel_memory_release(page_physical, 1u, frame.owner) !=
                        KERNEL_MEMORY_OK)
                    return KERNEL_VM_CORRUPT;
                --space->mapped_pages;
                --vm_stats.user_mappings;
            }
        }
    }
    flush_all();
    table_pages = space->table_pages;
    if (space->mapped_pages != 0u ||
        release_table_tree(space->root_physical, space->owner) !=
            KERNEL_VM_OK)
        return KERNEL_VM_CORRUPT;
    vm_stats.user_table_pages -= table_pages;
    --vm_stats.address_spaces;
    if (!unregister_address_space(space))
        return KERNEL_VM_CORRUPT;
    clear_space(space);
    return KERNEL_VM_OK;
}

static KernelVmStatus switch_user_root(uint32_t root_physical)
{
    if (current_user_root == root_physical)
        return KERNEL_VM_OK;
    invalidate_caches();
    current_process_root = root_physical;
    kernel_pmmu_load_urp(current_process_root);
    kernel_pmmu_flush_non_global();
    current_user_root = root_physical;
    ++vm_stats.flushes;
    ++vm_stats.switches;
    return KERNEL_VM_OK;
}

KernelVmStatus kernel_vm_switch(const KernelAddressSpace *space)
{
    if (!initialized || !enabled || space == NULL ||
        space->initialized == 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    return switch_user_root(space->root_physical);
}

KernelVmStatus kernel_vm_switch_to_empty(void)
{
    if (!initialized || !enabled)
        return KERNEL_VM_INVALID_ARGUMENT;
    return switch_user_root(empty_root_physical);
}

KernelVmStatus kernel_vm_deactivate(const KernelAddressSpace *space)
{
    if (!initialized || !enabled || space == NULL ||
        space->initialized == 0u)
        return KERNEL_VM_INVALID_ARGUMENT;
    return space->root_physical == current_user_root ?
        switch_user_root(empty_root_physical) : KERNEL_VM_OK;
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

bool kernel_vm_control_state(KernelVmControlState *state)
{
    uint32_t srp;
    uint32_t urp;
    uint32_t tc;

    if (!initialized || state == NULL)
        return false;
    kernel_pmmu_read_srp(&srp);
    kernel_pmmu_read_urp(&urp);
    kernel_pmmu_read_tc(&tc);
    state->srp_table_address = srp;
    state->urp_table_address = urp;
    state->translation_control = tc;
    state->cache_control = kernel_cache_read_control();
    state->translation_enabled = enabled ? 1u : 0u;
    state->reserved[0] = 0u;
    state->reserved[1] = 0u;
    state->reserved[2] = 0u;
    return true;
}
