/* Clone-private, demand-paged storage behind picolibc's contiguous sbrk. */

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "heap_internal.h"

#define ASTRA_HEAP_ALIGNMENT 8u
#define ASTRA_HEAP_PAGE_SIZE 4096u

static uint8_t *heap_base;
static uint32_t heap_span;
static uint32_t heap_used;
static AstraPosixHeapLayout heap_layout;
/*
 * The high-water mark of committed pages, which is not the same as the break:
 * shrinking hands pages back, so the next growth has to be able to tell the
 * difference between space it still holds and space it must fault in again.
 * Nothing here tracks that -- the kernel does -- but it is why decommit takes
 * the range it does rather than everything above the break.
 */

static int
heap_start(void)
{
    void *address = NULL;
    uint32_t span = 0u;
    uint32_t total_pages;
    uint32_t heap_pages;
    uint32_t metadata_pages;

    if (heap_base != NULL)
        return 1;
    if (astra_rt_private_reserve(
            ASTRA_VM_PRIVATE_ADDRESS_SPACE_MAX,
            ASTRA_VM_PRIVATE_READ | ASTRA_VM_PRIVATE_WRITE,
            &address, &span) != ASTRA_SYSCALL_OK || address == NULL)
        return 0;
    total_pages = span / ASTRA_HEAP_PAGE_SIZE;
    heap_pages = total_pages;
    for (;;) {
        uint64_t metadata_bytes =
            (uint64_t)heap_pages * 3u * sizeof(uint32_t);
        uint32_t next;

        metadata_pages = (uint32_t)((metadata_bytes +
                                     ASTRA_HEAP_PAGE_SIZE - 1u) /
                                    ASTRA_HEAP_PAGE_SIZE);
        if (metadata_pages >= total_pages)
            return 0;
        next = total_pages - metadata_pages;
        if (next == heap_pages)
            break;
        heap_pages = next;
    }
    if (heap_pages == 0u)
        return 0;
    heap_layout.page_count = heap_pages;
    heap_layout.page_run = address;
    heap_layout.extent_next = heap_layout.page_run + heap_pages;
    heap_layout.extent_pages = heap_layout.extent_next + heap_pages;
    heap_layout.base = (uint8_t *)address +
                       metadata_pages * ASTRA_HEAP_PAGE_SIZE;
    heap_base = heap_layout.base;
    heap_span = heap_pages * ASTRA_HEAP_PAGE_SIZE;
    heap_used = 0u;
    return 1;
}

int
astra_posix_heap_layout(AstraPosixHeapLayout *layout)
{
    if (layout == NULL || !heap_start())
        return 0;
    *layout = heap_layout;
    return 1;
}

/*
 * The break, as picolibc's allocator expects it: the *previous* end on
 * success, and (void *)-1 with ENOMEM when there is no more.
 *
 * Growing does not touch the kernel at all. The pages are already named, so
 * moving the break is arithmetic, and the first write to each cluster is what
 * asks for the frames -- which means a program that asks for a megabyte and
 * writes a page pays for a page.
 */
void *
sbrk(intptr_t increment)
{
    uint32_t wanted;
    uint8_t *previous;

    if (!heap_start()) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (increment < 0) {
        /*
         * Giving memory back, and now it means it. The pages that fall wholly
         * above the new break are handed to the kernel; the reservation stays,
         * so the address space is still ours and touching it again re-faults.
         * Rounding up to a page boundary is what keeps the page containing the
         * new break -- part of it is still live.
         */
        uint32_t amount = (uint32_t)(-(increment + 1)) + 1u;
        uint32_t released;
        uint32_t first;

        if (amount > heap_used) {
            errno = EINVAL;
            return (void *)-1;
        }
        heap_used -= amount;
        first = (heap_used + ASTRA_HEAP_PAGE_SIZE - 1u) &
                ~(uint32_t)(ASTRA_HEAP_PAGE_SIZE - 1u);
        if (first < heap_used + amount)
            (void)astra_rt_private_decommit(heap_base + first,
                                            (heap_used + amount) - first,
                                            &released);
        return heap_base + heap_used;
    }
    wanted = (uint32_t)increment;
    /* Aligned, because what comes back is about to hold anything at all. */
    if (wanted > UINT32_MAX - (ASTRA_HEAP_ALIGNMENT - 1u)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    wanted = (wanted + ASTRA_HEAP_ALIGNMENT - 1u) &
             ~(uint32_t)(ASTRA_HEAP_ALIGNMENT - 1u);
    if (wanted > heap_span - heap_used) {
        errno = ENOMEM;
        return (void *)-1;
    }
    previous = heap_base + heap_used;
    heap_used += wanted;
    return previous;
}
