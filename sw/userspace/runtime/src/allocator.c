/*
 * The allocator, and the number that replaced picolibc's.
 *
 * picolibc's `malloc` is a first-fit free list. It is not a bad one; it is a
 * 1970s one, and the shape it has trouble with is exactly the shape a program
 * a person uses has: many small objects of several sizes, freed in an order
 * unrelated to the order they were taken, with the occasional large buffer
 * cutting through the middle of it. A freed 40-byte hole cannot answer a
 * 48-byte request, and after enough churn the heap is mostly holes.
 *
 * Earlier allocator work said that was a claim to be
 * settled against a number rather than taste, and set the bar: peak footprint
 * over peak live bytes, above roughly 1.5 means first-fit is costing real
 * memory on a 128 MB machine. `heapbench` runs the editor-shaped trace and
 * measured **1.70** -- 688 KiB of footprint carrying 403 KiB of live data.
 * So this exists.
 *
 * What it is: **segregated fit**, the idea every modern C allocator is built
 * on -- jemalloc, tcmalloc, mimalloc. A request is rounded up to one of a
 * fixed set of size classes, and each class is served from runs of pages that
 * hold nothing else. A freed 32-byte block can therefore only ever be reused
 * by another 32-byte request, and external fragmentation stops being a thing
 * that accumulates: it is prevented by construction rather than repaired.
 * The cost is internal -- rounding 40 bytes up to 48 -- and it is bounded and
 * predictable, which is the trade worth making.
 *
 * One process-wide mutex protects the metadata. There is one core, so
 * per-thread caches and multiple arenas would add machinery without adding
 * parallel allocator throughput, but preemption still makes synchronization
 * mandatory.
 *
 * Large requests bypass the classes and take whole pages, so they never punch
 * a hole in the small-object heap, and they are returned exactly on free.
 *
 * Runs that become entirely free are **decommitted** -- the frames go back to
 * the kernel and the address range stays reserved, so touching it again
 * re-faults. That is the third of section 4's points, and it is what makes a
 * long-running program's footprint follow its live set back down instead of
 * ratcheting.
 *
 * Finding the run from a pointer uses page-index metadata stored at the front
 * of the private reservation. Its pages commit on demand with the heap, so
 * the table follows the address space rather than bloating every executable.
 */

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "allocator_internal.h"

#define PAGE_BYTES 4096u
#define PAGE_SHIFT 12u
/* Above this a request takes whole pages of its own. */
#define LARGE_THRESHOLD 2048u
#define RUN_PAGES_MAX 8u
/* Blocks a run should hold before it is worth dedicating pages to the class. */
#define RUN_MIN_CAPACITY 6u

/*
 * The classes. Eight-byte steps while the objects are small enough that the
 * rounding would otherwise dominate, widening as they grow -- the standard
 * shape, because the waste that matters is proportional.
 */
static const uint16_t class_bytes[] = {
    16u, 24u, 32u, 48u, 64u, 80u, 96u, 128u,
    160u, 192u, 256u, 320u, 384u, 512u,
    640u, 768u, 1024u, 1280u, 1536u, 2048u
};

#define CLASS_COUNT (sizeof(class_bytes) / sizeof(class_bytes[0]))
#define CLASS_LARGE 0xffffu

_Static_assert(CLASS_COUNT == ASTRA_ALLOCATOR_CLASS_COUNT,
               "allocator control class count differs from class table");

typedef struct AstraRun {
    struct AstraRun *next;
    struct AstraRun *previous;
    void *free_head;
    uint16_t class_index;
    uint16_t capacity;
    uint16_t free_count;
    uint32_t pages;
} AstraRun;

#define RUN_HEADER_BYTES \
    ((uint32_t)((sizeof(AstraRun) + 7u) & ~(size_t)7u))

/*
 * The free extents, which are how released runs come back.
 *
 * Rounding a run up to a power of two pages was the first shape here and it
 * was wrong twice: it refused anything over eight pages outright, and it
 * would have spent 128 KiB of a 512-page reservation on a 64 KiB buffer.
 * Address space is the scarce thing inside the reservation -- frames are
 * already handled, because an untouched page costs none -- so
 * extents are exact, and adjacent ones coalesce on release so a long-running
 * program does not saw its own reservation into unusable pieces.
 *
 * The bookkeeping lives in side arrays rather than in the extents themselves,
 * because a free extent has been decommitted: reading a link out of it would
 * fault a frame back in to find out where the next free one is.
 */
static AstraRun *
run_at_page(const AstraAllocatorControl *control, uint32_t page)
{
    return (AstraRun *)(void *)(control->layout.base +
                                ((size_t)page << PAGE_SHIFT));
}

static AstraAllocatorControl *
heap_ready(void)
{
    AstraAllocatorControl *control;

    if (!astra_allocator_control_acquire(&control))
        return NULL;
    /*
     * The VM reservation is root-slot aligned; every request below is a whole
     * number of pages, which keeps it page aligned.
     */
    if (((uintptr_t)control->layout.base & (PAGE_BYTES - 1u)) != 0u) {
        astra_allocator_control_release(control);
        return NULL;
    }
    return control;
}

/* Takes `pages` contiguous pages: a free extent if one fits, else the break. */
static AstraRun *
take_pages(AstraAllocatorControl *control, uint32_t pages)
{
    uint32_t previous = 0u;
    uint32_t current = control->free_extent_head;
    void *fresh;

    while (current != 0u) {
        uint32_t page = (uint32_t)current - 1u;

        if (control->layout.extent_pages[page] >= pages) {
            uint32_t remainder = control->layout.extent_pages[page] - pages;
            uint32_t next = control->layout.extent_next[page];

            /* Split, and leave the tail free rather than handing it over. */
            if (remainder != 0u) {
                uint32_t rest = page + pages;

                control->layout.extent_pages[rest] = remainder;
                control->layout.extent_next[rest] = next;
                next = rest + 1u;
            }
            if (previous == 0u)
                control->free_extent_head = next;
            else
                control->layout.extent_next[(uint32_t)previous - 1u] = next;
            return run_at_page(control, page);
        }
        previous = current;
        current = control->layout.extent_next[page];
    }
    if (pages > control->layout.page_count - control->heap_pages_taken)
        return NULL;
    fresh = astra_runtime_sbrk_locked(
        control, (intptr_t)(pages * PAGE_BYTES));
    if (fresh == NULL)
        return NULL;
    control->heap_pages_taken += pages;
    return fresh;
}

static void
give_pages(AstraAllocatorControl *control, AstraRun *run)
{
    uint32_t page = (uint32_t)(((uint8_t *)run - control->layout.base) >>
                               PAGE_SHIFT);
    /*
     * Read out of the header before anything is dropped. The header lives on
     * the run's own first page, so once that page is decommitted every field
     * reads back as the zero of a freshly faulted page.
     */
    uint32_t pages = run->pages;
    uint32_t released = 0u;
    uint32_t previous = 0u;
    uint32_t current = control->free_extent_head;

    for (uint32_t index = 0u; index < pages; ++index)
        control->layout.page_run[page + index] = 0u;
    /*
     * The frames go back; the address range stays ours. Touching it again
     * faults a fresh zeroed page in, which is exactly what a reused run
     * wants. A failure here is not fatal -- the pages simply stay committed
     * and the extent is still reusable -- so it is not worth a branch.
     */
    (void)astra_rt_private_decommit(run, pages * PAGE_BYTES, &released);

    /* Sorted by page, so the two neighbours are the two this can join. */
    while (current != 0u && (uint32_t)current - 1u < page) {
        previous = current;
        current = control->layout.extent_next[(uint32_t)current - 1u];
    }
    control->layout.extent_pages[page] = pages;
    control->layout.extent_next[page] = current;
    if (previous == 0u)
        control->free_extent_head = page + 1u;
    else
        control->layout.extent_next[(uint32_t)previous - 1u] = page + 1u;
    if (current != 0u && page + pages == (uint32_t)current - 1u) {
        control->layout.extent_pages[page] +=
            control->layout.extent_pages[(uint32_t)current - 1u];
        control->layout.extent_next[page] =
            control->layout.extent_next[(uint32_t)current - 1u];
    }
    if (previous != 0u) {
        uint32_t before = (uint32_t)previous - 1u;

        if (before + control->layout.extent_pages[before] == page) {
            control->layout.extent_pages[before] +=
                control->layout.extent_pages[page];
            control->layout.extent_next[before] =
                control->layout.extent_next[page];
        }
    }
}

static AstraRun *
new_run(AstraAllocatorControl *control, uint32_t class_index)
{
    uint32_t size = class_bytes[class_index];
    uint32_t pages = 1u;
    uint32_t capacity;
    AstraRun *run;
    uint32_t page;
    uint8_t *block;

    while (((pages * PAGE_BYTES) - RUN_HEADER_BYTES) / size <
               RUN_MIN_CAPACITY &&
           pages < RUN_PAGES_MAX)
        pages <<= 1;
    run = take_pages(control, pages);
    if (run == NULL)
        return NULL;
    capacity = ((pages * PAGE_BYTES) - RUN_HEADER_BYTES) / size;
    run->class_index = (uint16_t)class_index;
    run->capacity = (uint16_t)capacity;
    run->free_count = (uint16_t)capacity;
    run->pages = pages;
    run->free_head = NULL;
    /*
     * Threaded back to front so the list hands out ascending addresses, which
     * keeps a run's early allocations on its first page and lets a run that is
     * barely used stay barely committed.
     */
    block = (uint8_t *)run + RUN_HEADER_BYTES + ((capacity - 1u) * size);
    for (uint32_t index = 0u; index < capacity; ++index) {
        *(void **)(void *)block = run->free_head;
        run->free_head = block;
        block -= size;
    }
    page = (uint32_t)(((uint8_t *)run - control->layout.base) >> PAGE_SHIFT);
    for (uint32_t index = 0u; index < pages; ++index)
        control->layout.page_run[page + index] = page + 1u;
    run->next = control->class_runs[class_index];
    run->previous = NULL;
    if (run->next != NULL)
        run->next->previous = run;
    control->class_runs[class_index] = run;
    return run;
}

static void
unlink_run(AstraAllocatorControl *control, AstraRun *run)
{
    if (run->previous != NULL)
        run->previous->next = run->next;
    else
        control->class_runs[run->class_index] = run->next;
    if (run->next != NULL)
        run->next->previous = run->previous;
    run->next = NULL;
    run->previous = NULL;
}

static AstraRun *
run_for(const AstraAllocatorControl *control, const void *pointer)
{
    uint32_t page;
    uint32_t first;

    if ((const uint8_t *)pointer < control->layout.base)
        return NULL;
    page = (uint32_t)(((const uint8_t *)pointer - control->layout.base) >>
                      PAGE_SHIFT);
    if (page >= control->layout.page_count)
        return NULL;
    first = control->layout.page_run[page];
    if (first == 0u)
        return NULL;
    return run_at_page(control, (uint32_t)first - 1u);
}

static uint32_t
class_for(size_t size)
{
    for (uint32_t index = 0u; index < CLASS_COUNT; ++index) {
        if (size <= class_bytes[index])
            return index;
    }
    return CLASS_LARGE;
}

/*
 * Whole-page allocations are also the one correct place to satisfy extended
 * alignment.  The page metadata already maps every interior address back to
 * its run, so no second allocator or hidden prefix is needed.  free_head is
 * unused by large runs and records the one pointer that may release the run;
 * this also stops an interior pointer from freeing a live allocation.
 */
static void *
allocate_large(AstraAllocatorControl *control, size_t size, size_t alignment)
{
    uint64_t wanted;
    uintptr_t first;
    uintptr_t aligned;
    uint32_t pages;
    uint32_t page;
    AstraRun *run;

    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        return NULL;
    if (size == 0u)
        size = 1u;
    wanted = (uint64_t)size + RUN_HEADER_BYTES + alignment - 1u;
    if (wanted > (uint64_t)control->layout.page_count * PAGE_BYTES)
        return NULL;
    pages = (uint32_t)((wanted + PAGE_BYTES - 1u) >> PAGE_SHIFT);
    run = take_pages(control, pages);
    if (run == NULL)
        return NULL;
    first = (uintptr_t)((uint8_t *)run + RUN_HEADER_BYTES);
    aligned = (first + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    run->class_index = CLASS_LARGE;
    run->capacity = 0u;
    run->free_count = 0u;
    run->pages = pages;
    run->free_head = (void *)aligned;
    run->next = NULL;
    run->previous = NULL;
    page = (uint32_t)(((uint8_t *)run - control->layout.base) >> PAGE_SHIFT);
    for (uint32_t index = 0u; index < pages; ++index)
        control->layout.page_run[page + index] = page + 1u;
    return (void *)aligned;
}

static void *
allocate(AstraAllocatorControl *control, size_t size)
{
    uint32_t class_index;
    AstraRun *run;
    void *block;

    if (size == 0u)
        size = 1u;
    if (size > LARGE_THRESHOLD)
        return allocate_large(control, size, 8u);
    class_index = class_for(size);
    run = control->class_runs[class_index];
    if (run == NULL) {
        run = new_run(control, class_index);
        if (run == NULL)
            return NULL;
    }
    block = run->free_head;
    if (block == NULL)
        return NULL;
    run->free_head = *(void **)block;
    --run->free_count;
    /* A run with nothing left is not a run worth walking to next time. */
    if (run->free_count == 0u)
        unlink_run(control, run);
    return block;
}

void *
astra_runtime_allocate(size_t size)
{
    AstraAllocatorControl *control = heap_ready();
    void *result;

    if (control == NULL)
        return NULL;
    result = allocate(control, size);
    astra_allocator_control_release(control);
    return result;
}

void *
astra_runtime_allocate_aligned(size_t alignment, size_t size)
{
    AstraAllocatorControl *control = heap_ready();
    void *result = NULL;

    if (control == NULL)
        return NULL;
    if (alignment != 0u && (alignment & (alignment - 1u)) == 0u)
        result = alignment <= 8u ? allocate(control, size) :
                                  allocate_large(control, size, alignment);
    astra_allocator_control_release(control);
    return result;
}

static void
deallocate(AstraAllocatorControl *control, void *pointer)
{
    AstraRun *run;

    if (pointer == NULL)
        return;
    run = run_for(control, pointer);
    if (run == NULL)
        return;
    if (run->class_index == CLASS_LARGE) {
        if (run->free_head != pointer)
            return;
        give_pages(control, run);
        return;
    }
    if (run->free_count == 0u) {
        /* It was full, so it is not on its class's list; put it back. */
        run->next = control->class_runs[run->class_index];
        run->previous = NULL;
        if (run->next != NULL)
            run->next->previous = run;
        control->class_runs[run->class_index] = run;
    }
    *(void **)pointer = run->free_head;
    run->free_head = pointer;
    ++run->free_count;
    if (run->free_count == run->capacity) {
        /* Entirely free: the frames go back rather than being held. */
        unlink_run(control, run);
        give_pages(control, run);
    }
}

void
astra_runtime_deallocate(void *pointer)
{
    AstraAllocatorControl *control = heap_ready();

    if (control == NULL)
        return;
    deallocate(control, pointer);
    astra_allocator_control_release(control);
}

static size_t
allocation_size(AstraAllocatorControl *control, void *pointer)
{
    AstraRun *run = run_for(control, pointer);

    if (run == NULL)
        return 0u;
    if (run->class_index == CLASS_LARGE) {
        if (run->free_head != pointer)
            return 0u;
        return (size_t)run->pages * PAGE_BYTES -
               (size_t)((uint8_t *)pointer - (uint8_t *)run);
    }
    return class_bytes[run->class_index];
}

size_t
astra_runtime_allocation_size(void *pointer)
{
    AstraAllocatorControl *control = heap_ready();
    size_t result;

    if (control == NULL)
        return 0u;
    result = allocation_size(control, pointer);
    astra_allocator_control_release(control);
    return result;
}

void *
astra_runtime_callocate(size_t count, size_t size)
{
    size_t total;
    void *block;

    if (count != 0u && size > (size_t)-1 / count)
        return NULL;
    total = count * size;
    block = astra_runtime_allocate(total);
    if (block != NULL)
        (void)memset(block, 0, total);
    return block;
}

void *
astra_runtime_reallocate(void *pointer, size_t size)
{
    AstraAllocatorControl *control;
    size_t usable;
    void *block;

    if (pointer == NULL)
        return astra_runtime_allocate(size);
    if (size == 0u) {
        astra_runtime_deallocate(pointer);
        return NULL;
    }
    control = heap_ready();
    if (control == NULL)
        return NULL;
    usable = allocation_size(control, pointer);
    /*
     * Staying put when the request still fits the class is most of what makes
     * a growing buffer cheap: a program appending a byte at a time moves once
     * per class rather than once per byte.
     */
    if (size <= usable && (size > usable / 2u || usable <= class_bytes[0]))
        block = pointer;
    else
        block = allocate(control, size);
    if (block == NULL) {
        astra_allocator_control_release(control);
        return NULL;
    }
    if (block != pointer) {
        (void)memcpy(block, pointer, size < usable ? size : usable);
        deallocate(control, pointer);
    }
    astra_allocator_control_release(control);
    return block;
}
