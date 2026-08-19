/*
 * The heap, and why it is a reserved area.
 *
 * `malloc` needs somewhere to allocate from, and on a machine with no root and
 * no mmap the only question is what that somewhere *is*. Three answers were
 * available and two of them are wrong:
 *
 *   - a region reserved in the linker script, between `__heap_start` and
 *     `__heap_end`, which is what picolibc's fallback `sbrk` uses. It costs
 *     nothing to write and it is part of the image's writable segment, so the
 *     loader maps every page of it at launch. A megabyte of heap is then a
 *     megabyte of frames committed by `status`, which allocates nothing, on
 *     every launch. The machine pays for the largest program's appetite in
 *     every program.
 *   - an area of a **fixed size**, created on first allocation. That is what
 *     stood here, and it was a fixed partition with a ceiling compiled into
 *     the image: a program needing 300 KiB failed at 256, a long-running one
 *     ratcheted upward and never gave a page back, and the limit was a
 *     constant chosen by whoever wrote this file rather than a budget. It
 *     worked, and it was the wrong shape.
 *   - a **reserved** area, which is this one. Creation takes the address
 *     range and commits nothing; a page arrives when it is first touched, a
 *     cluster at a time, and is charged to this process then. What the
 *     program never allocates, the machine never spends a frame on.
 *
 * Reserving is free, so the reservation is the whole of what the VM will give
 * one area -- ASTRA_AREA_SIZE_MAX, 2 MiB -- rather than a guess at what this
 * program will want. There is 2 GB of user address space and 128 MB of RAM;
 * naming memory and owning it are different operations, and only the second
 * one is scarce. The ceiling that finally refuses is the owner's frame quota,
 * which the kernel already tracks, rather than a second and smaller invisible
 * one declared here.
 *
 * `astra_heap_bytes` is gone with it. A knob whose only correct setting is
 * "as much as I turn out to need" is not a knob, and every program that had
 * to define one was working around this file.
 *
 * A heap larger than one area slot would need a second area and an `sbrk`
 * handing out a discontiguous chunk -- and a `malloc` checked against one.
 * picolibc's has not been, so that stays undone rather than half done.
 *
 * `free` is picolibc's. This is only where the memory comes from, and where
 * it goes back to: shrinking the break decommits the pages it passes, which
 * is what makes releasing memory a behaviour rather than a phrase.
 *
 * It lives in the POSIX library rather than the runtime because that is whose
 * problem it is: `sbrk` exists for a C library's allocator, and a program
 * written against the NDK that never links picolibc has no use for one. The
 * runtime stays free of libc headers, which is also what lets it be built for
 * the host tests.
 */

#include <astra/runtime.h>
#include <astra/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define ASTRA_HEAP_ALIGNMENT 8u
#define ASTRA_HEAP_PAGE_SIZE 4096u

static uint8_t *heap_base;
static uint32_t heap_span;
static uint32_t heap_used;
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
    uint32_t handle = 0u;
    void *address = NULL;
    uint32_t span = 0u;

    if (heap_base != NULL)
        return 1;
    /*
     * READ and WRITE and nothing else. A heap is not transferred, not mapped
     * by anybody else and not administered, and a capability that carried
     * those rights would be handing them to whatever the program is running.
     */
    if (astra_rt_area_create_flagged(ASTRA_AREA_SIZE_MAX,
                                     ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                         ASTRA_RIGHT_MAP,
                                     ASTRA_AREA_CREATE_RESERVED,
                                     &handle) != ASTRA_SYSCALL_OK)
        return 0;
    if (astra_rt_area_map(handle, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &span) != ASTRA_SYSCALL_OK ||
        address == NULL || span == 0u) {
        (void)astra_close(handle);
        return 0;
    }
    /*
     * The handle is deliberately not kept. Nothing may unmap the heap while
     * the program is running, and a handle nobody holds is one nobody can
     * close by mistake; the process exiting is what releases it, which is the
     * only moment it is safe to.
     */
    heap_base = address;
    heap_span = span;
    heap_used = 0u;
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
            (void)astra_rt_area_decommit(heap_base + first,
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
