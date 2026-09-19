/* Process-wide, clone-private, demand-paged allocator storage. */

#include <astra/runtime.h>
#include <astra/syscall.h>
#include <astra/address_space.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "allocator_internal.h"

#define ASTRA_HEAP_ALIGNMENT 8u
#define ASTRA_HEAP_PAGE_SIZE 4096u

#if !defined(__m68k__)
static AstraAllocatorControl host_control;
#endif

_Static_assert(sizeof(AstraAllocatorControl) <= ASTRA_HEAP_PAGE_SIZE,
               "allocator control must fit in its first demand-paged page");
/*
 * The high-water mark of committed pages, which is not the same as the break:
 * shrinking hands pages back, so the next growth has to be able to tell the
 * difference between space it still holds and space it must fault in again.
 * Nothing here tracks that -- the kernel does -- but it is why decommit takes
 * the range it does rather than everything above the break.
 */

static int
heap_start(AstraAllocatorControl *control)
{
    void *address = NULL;
    uint32_t span = 0u;
    uint32_t total_pages;
    uint32_t heap_pages;
    uint32_t metadata_pages;

    if (control->magic != 0u)
        return control->magic == ASTRA_ALLOCATOR_CONTROL_MAGIC &&
               control->version == ASTRA_ALLOCATOR_CONTROL_VERSION &&
               control->size == sizeof(*control) &&
               control->layout.base != NULL &&
               control->layout.page_count != 0u;
    if (astra_rt_private_reserve_largest(
            1u,
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
    (void)memset(&control->magic, 0,
                 sizeof(*control) - offsetof(AstraAllocatorControl, magic));
    control->version = ASTRA_ALLOCATOR_CONTROL_VERSION;
    control->size = sizeof(*control);
    control->layout.page_count = heap_pages;
    control->layout.page_run = address;
    control->layout.extent_next = control->layout.page_run + heap_pages;
    control->layout.extent_pages = control->layout.extent_next + heap_pages;
    control->layout.base = (uint8_t *)address +
                           metadata_pages * ASTRA_HEAP_PAGE_SIZE;
    control->heap_span = heap_pages * ASTRA_HEAP_PAGE_SIZE;
    /* Publish last: a second runtime image never accepts partial state. */
    control->magic = ASTRA_ALLOCATOR_CONTROL_MAGIC;
    return 1;
}

AstraAllocatorControl *
astra_allocator_control(void)
{
#if defined(__m68k__)
    return (AstraAllocatorControl *)(uintptr_t)
        ASTRA_PROCESS_HEAP_CONTROL_START;
#else
    return &host_control;
#endif
}

int
astra_allocator_control_acquire(AstraAllocatorControl **control)
{
    AstraAllocatorControl *shared;

    if (control == NULL)
        return 0;
    shared = astra_allocator_control();
    if (astra_mutex_lock(&shared->lock) != ASTRA_SYSCALL_OK)
        return 0;
    if (!heap_start(shared)) {
        (void)astra_mutex_unlock(&shared->lock);
        return 0;
    }
    *control = shared;
    return 1;
}

void
astra_allocator_control_release(AstraAllocatorControl *control)
{
    (void)astra_mutex_unlock(&control->lock);
}

/*
 * The break used by the runtime allocator and exposed to compatibility
 * layers: the *previous* end on growth, the new end on shrink, and NULL when
 * the adjustment cannot be made.
 *
 * Growing does not touch the kernel at all. The pages are already named, so
 * moving the break is arithmetic, and the first write to each cluster is what
 * asks for the frames -- which means a program that asks for a megabyte and
 * writes a page pays for a page.
 */
void
astra_runtime_set_growth_policy(
    AstraRuntimeGrowthPolicy policy,
    void *context)
{
    AstraAllocatorControl *control;

    if (!astra_allocator_control_acquire(&control))
        return;
    control->growth_policy = policy;
    control->growth_policy_context = context;
    astra_allocator_control_release(control);
}

uint32_t
astra_runtime_allocation_span(void)
{
    AstraAllocatorControl *control;
    uint32_t span;

    if (!astra_allocator_control_acquire(&control))
        return 0u;
    span = control->heap_used;
    astra_allocator_control_release(control);
    return span;
}

void *
astra_runtime_sbrk_locked(AstraAllocatorControl *control, intptr_t increment)
{
    uint32_t wanted;
    uint8_t *previous;

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

        if (amount > control->heap_used) {
            return NULL;
        }
        control->heap_used -= amount;
        first = (control->heap_used + ASTRA_HEAP_PAGE_SIZE - 1u) &
                ~(uint32_t)(ASTRA_HEAP_PAGE_SIZE - 1u);
        if (first < control->heap_used + amount)
            (void)astra_rt_private_decommit(control->layout.base + first,
                                            (control->heap_used + amount) - first,
                                            &released);
        return control->layout.base + control->heap_used;
    }
    wanted = (uint32_t)increment;
    /* Aligned, because what comes back is about to hold anything at all. */
    if (wanted > UINT32_MAX - (ASTRA_HEAP_ALIGNMENT - 1u)) {
        return NULL;
    }
    wanted = (wanted + ASTRA_HEAP_ALIGNMENT - 1u) &
             ~(uint32_t)(ASTRA_HEAP_ALIGNMENT - 1u);
    if (wanted > control->heap_span - control->heap_used ||
        (control->growth_policy != NULL &&
         !control->growth_policy(control->heap_used, wanted,
                                 control->growth_policy_context)))
        return NULL;
    previous = control->layout.base + control->heap_used;
    control->heap_used += wanted;
    return previous;
}

void *
astra_runtime_sbrk(intptr_t increment)
{
    AstraAllocatorControl *control;
    void *result;

    if (!astra_allocator_control_acquire(&control))
        return NULL;
    result = astra_runtime_sbrk_locked(control, increment);
    astra_allocator_control_release(control);
    return result;
}
