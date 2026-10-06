/*
 * One frame the kernel owns and every process maps read-only at
 * ASTRA_TIME_PAGE_ADDRESS (Haiku's real_time_data commpage idea). Every RAM
 * mapping is write-through and the MC68040 caches are physically tagged
 * (docs/MEMORY_MAP_AND_PMMU.md, "Cache and alias policy"), so a store through
 * the supervisor map is what the next user read sees: no cache push.
 */

#include "time_page.h"

#include <astra/time_page.h>

#include "allocation.h"
#include "bytes.h"
#include "memory.h"
#include "platform.h"

static uint32_t time_physical;
static volatile AstraTimePage *time_page;
/*
 * A timer interrupt can land inside an update made from a context switch.
 * The nested one would write the copy the outer one is half way through,
 * and re-latch the clock registers between the outer one's LO and HI
 * reads. One CPU: the nested update runs to completion before the outer
 * resumes, so a flag is enough and the outer one publishes for both.
 */
static volatile uint8_t time_publishing;

bool kernel_time_page_init(void)
{
    void *bytes;

    /* No "already done" guard: host tests re-initialize memory under it. */
    time_page = NULL;
    time_physical = 0u;
    time_publishing = 0u;
    if (kernel_memory_alloc_zeroed_tagged(KERNEL_ALLOCATION_SITE_TIME_PAGE,
                                          1u, 1u, KERNEL_FRAME_SHARED,
                                          KERNEL_TIME_PAGE_OWNER,
                                          &time_physical) !=
        KERNEL_MEMORY_OK)
        return false;
    bytes = kernel_memory_access(time_physical, KERNEL_PAGE_SIZE);
    if (bytes == NULL) {
        (void)kernel_memory_release(time_physical, 1u,
                                    KERNEL_TIME_PAGE_OWNER);
        time_physical = 0u;
        return false;
    }
    kernel_bytes_clear(bytes, sizeof(AstraTimePage));
    time_page = bytes;
    time_page->version = ASTRA_TIME_PAGE_VERSION;
    kernel_time_page_update();
    /* Magic last: a reader never sees a page with magic and no time. */
    time_page->magic = ASTRA_TIME_PAGE_MAGIC;
    return true;
}

void kernel_time_page_update(void)
{
    AstraTimePageCopy copy;
    uint64_t monotonic;
    uint64_t wall = 0u;
    int32_t offset = 0;
    uint32_t zone = 0u;

    if (time_page == NULL || time_publishing != 0u)
        return;
    time_publishing = 1u;
    kernel_bytes_clear(&copy, sizeof(copy));
    monotonic = kernel_platform_monotonic_ns();
    copy.monotonic_high = (uint32_t)(monotonic >> 32);
    copy.monotonic_low = (uint32_t)monotonic;
    if (kernel_platform_wall_clock(&wall, &offset, &zone)) {
        copy.wall_high = (uint32_t)(wall >> 32);
        copy.wall_low = (uint32_t)wall;
        copy.utc_offset = offset;
        copy.zone = zone;
        copy.flags = ASTRA_TIME_PAGE_WALL_VALID;
    }
    astra_time_page_publish(time_page, &copy);
    time_publishing = 0u;
}

KernelProcessStatus kernel_time_page_map(KernelAddressSpace *space)
{
    KernelVmStatus status;

    if (time_page == NULL)
        return KERNEL_PROCESS_OK; /* no page: readers fall back */
    status = kernel_vm_map_shared_page(space, ASTRA_TIME_PAGE_ADDRESS,
                                       time_physical, KERNEL_TIME_PAGE_OWNER,
                                       KERNEL_VM_READ);
    if (status == KERNEL_VM_OK)
        return KERNEL_PROCESS_OK;
    return status == KERNEL_VM_OUT_OF_MEMORY ? KERNEL_PROCESS_OUT_OF_MEMORY :
                                               KERNEL_PROCESS_CORRUPT;
}
