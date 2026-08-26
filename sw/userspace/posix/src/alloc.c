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
 * HANDOVER-memory-and-modernity.md section 4 said that was a claim to be
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
 * What it deliberately is not: any of jemalloc's threading machinery. There
 * is one core. No per-thread caches, no lock-free fast paths, no atomics, no
 * arenas to spread contention that cannot occur. Taking the idea and dropping
 * what does not apply is most of what makes this the right size -- the whole
 * file is smaller than the part of jemalloc that decides which arena to use.
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

#include "heap_internal.h"

extern void *sbrk(intptr_t increment);

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

static uint8_t *heap_base;
static uint32_t heap_pages_taken;
static uint32_t heap_page_count;
/*
 * page_run[p] is one more than the index of the first page of the run that
 * page p belongs to; zero means the page belongs to no run. The bias is what
 * lets zero mean absent without a second array.
 */
static uint32_t *page_run;
static AstraRun *class_runs[CLASS_COUNT];
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
static uint32_t *extent_next;
static uint32_t *extent_pages;
static uint32_t free_extent_head;

static AstraRun *
run_at_page(uint32_t page)
{
    return (AstraRun *)(void *)(heap_base + ((size_t)page << PAGE_SHIFT));
}

static int
heap_ready(void)
{
    AstraPosixHeapLayout layout;

    if (heap_base != NULL)
        return 1;
    if (!astra_posix_heap_layout(&layout))
        return 0;
    /*
     * The VM reservation is root-slot aligned; every request below is a whole
     * number of pages, which keeps it page aligned.
     */
    if (((uintptr_t)layout.base & (PAGE_BYTES - 1u)) != 0u)
        return 0;
    heap_base = layout.base;
    heap_page_count = layout.page_count;
    page_run = layout.page_run;
    extent_next = layout.extent_next;
    extent_pages = layout.extent_pages;
    free_extent_head = 0u;
    return 1;
}

/* Takes `pages` contiguous pages: a free extent if one fits, else the break. */
static AstraRun *
take_pages(uint32_t pages)
{
    uint32_t previous = 0u;
    uint32_t current = free_extent_head;
    void *fresh;

    while (current != 0u) {
        uint32_t page = (uint32_t)current - 1u;

        if (extent_pages[page] >= pages) {
            uint32_t remainder = extent_pages[page] - pages;
            uint32_t next = extent_next[page];

            /* Split, and leave the tail free rather than handing it over. */
            if (remainder != 0u) {
                uint32_t rest = page + pages;

                extent_pages[rest] = remainder;
                extent_next[rest] = next;
                next = rest + 1u;
            }
            if (previous == 0u)
                free_extent_head = next;
            else
                extent_next[(uint32_t)previous - 1u] = next;
            return run_at_page(page);
        }
        previous = current;
        current = extent_next[page];
    }
    if (pages > heap_page_count - heap_pages_taken)
        return NULL;
    fresh = sbrk((intptr_t)(pages * PAGE_BYTES));
    if (fresh == (void *)-1)
        return NULL;
    heap_pages_taken += pages;
    return fresh;
}

static void
give_pages(AstraRun *run)
{
    uint32_t page = (uint32_t)(((uint8_t *)run - heap_base) >> PAGE_SHIFT);
    /*
     * Read out of the header before anything is dropped. The header lives on
     * the run's own first page, so once that page is decommitted every field
     * reads back as the zero of a freshly faulted page.
     */
    uint32_t pages = run->pages;
    uint32_t released = 0u;
    uint32_t previous = 0u;
    uint32_t current = free_extent_head;

    for (uint32_t index = 0u; index < pages; ++index)
        page_run[page + index] = 0u;
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
        current = extent_next[(uint32_t)current - 1u];
    }
    extent_pages[page] = pages;
    extent_next[page] = current;
    if (previous == 0u)
        free_extent_head = page + 1u;
    else
        extent_next[(uint32_t)previous - 1u] = page + 1u;
    if (current != 0u && page + pages == (uint32_t)current - 1u) {
        extent_pages[page] += extent_pages[(uint32_t)current - 1u];
        extent_next[page] = extent_next[(uint32_t)current - 1u];
    }
    if (previous != 0u) {
        uint32_t before = (uint32_t)previous - 1u;

        if (before + extent_pages[before] == page) {
            extent_pages[before] += extent_pages[page];
            extent_next[before] = extent_next[page];
        }
    }
}

static AstraRun *
new_run(uint32_t class_index)
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
    run = take_pages(pages);
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
    page = (uint32_t)(((uint8_t *)run - heap_base) >> PAGE_SHIFT);
    for (uint32_t index = 0u; index < pages; ++index)
        page_run[page + index] = page + 1u;
    run->next = class_runs[class_index];
    run->previous = NULL;
    if (run->next != NULL)
        run->next->previous = run;
    class_runs[class_index] = run;
    return run;
}

static void
unlink_run(AstraRun *run)
{
    if (run->previous != NULL)
        run->previous->next = run->next;
    else
        class_runs[run->class_index] = run->next;
    if (run->next != NULL)
        run->next->previous = run->previous;
    run->next = NULL;
    run->previous = NULL;
}

static AstraRun *
run_for(const void *pointer)
{
    uint32_t page;
    uint32_t first;

    if (heap_base == NULL || (const uint8_t *)pointer < heap_base)
        return NULL;
    page = (uint32_t)(((const uint8_t *)pointer - heap_base) >> PAGE_SHIFT);
    if (page >= heap_page_count)
        return NULL;
    first = page_run[page];
    if (first == 0u)
        return NULL;
    return run_at_page((uint32_t)first - 1u);
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

void *
malloc(size_t size)
{
    uint32_t class_index;
    AstraRun *run;
    void *block;

    if (!heap_ready())
        return NULL;
    if (size == 0u)
        size = 1u;
    if (size > LARGE_THRESHOLD) {
        /* Whole pages of its own, so it cannot hole the small-object heap. */
        uint64_t wanted = (uint64_t)size + RUN_HEADER_BYTES;
        uint32_t pages;
        uint32_t page;

        if (wanted > (uint64_t)heap_page_count * PAGE_BYTES)
            return NULL;
        pages = (uint32_t)((wanted + PAGE_BYTES - 1u) >> PAGE_SHIFT);
        run = take_pages(pages);
        if (run == NULL)
            return NULL;
        run->class_index = CLASS_LARGE;
        run->capacity = 0u;
        run->free_count = 0u;
        run->pages = pages;
        run->free_head = NULL;
        run->next = NULL;
        run->previous = NULL;
        page = (uint32_t)(((uint8_t *)run - heap_base) >> PAGE_SHIFT);
        for (uint32_t index = 0u; index < pages; ++index)
            page_run[page + index] = page + 1u;
        return (uint8_t *)run + RUN_HEADER_BYTES;
    }
    class_index = class_for(size);
    run = class_runs[class_index];
    if (run == NULL) {
        run = new_run(class_index);
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
        unlink_run(run);
    return block;
}

void
free(void *pointer)
{
    AstraRun *run;

    if (pointer == NULL)
        return;
    run = run_for(pointer);
    if (run == NULL)
        return;
    if (run->class_index == CLASS_LARGE) {
        give_pages(run);
        return;
    }
    if (run->free_count == 0u) {
        /* It was full, so it is not on its class's list; put it back. */
        run->next = class_runs[run->class_index];
        run->previous = NULL;
        if (run->next != NULL)
            run->next->previous = run;
        class_runs[run->class_index] = run;
    }
    *(void **)pointer = run->free_head;
    run->free_head = pointer;
    ++run->free_count;
    if (run->free_count == run->capacity) {
        /* Entirely free: the frames go back rather than being held. */
        unlink_run(run);
        give_pages(run);
    }
}

size_t
malloc_usable_size(void *pointer)
{
    AstraRun *run = run_for(pointer);

    if (run == NULL)
        return 0u;
    if (run->class_index == CLASS_LARGE)
        return ((size_t)run->pages * PAGE_BYTES) - RUN_HEADER_BYTES;
    return class_bytes[run->class_index];
}

void *
calloc(size_t count, size_t size)
{
    size_t total;
    void *block;

    if (count != 0u && size > (size_t)-1 / count)
        return NULL;
    total = count * size;
    block = malloc(total);
    if (block != NULL)
        (void)memset(block, 0, total);
    return block;
}

void *
realloc(void *pointer, size_t size)
{
    size_t usable;
    void *block;

    if (pointer == NULL)
        return malloc(size);
    if (size == 0u) {
        free(pointer);
        return NULL;
    }
    usable = malloc_usable_size(pointer);
    /*
     * Staying put when the request still fits the class is most of what makes
     * a growing buffer cheap: a program appending a byte at a time moves once
     * per class rather than once per byte.
     */
    if (size <= usable && (size > usable / 2u || usable <= class_bytes[0]))
        return pointer;
    block = malloc(size);
    if (block == NULL)
        return NULL;
    (void)memcpy(block, pointer, size < usable ? size : usable);
    free(pointer);
    return block;
}
