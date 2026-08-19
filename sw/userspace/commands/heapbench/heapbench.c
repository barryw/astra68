/*
 * `heapbench` -- the measurement HANDOVER-memory-and-modernity.md section 4
 * asks for, and the instrument for the two numbers left open after the
 * reserve/commit split.
 *
 * It answers three questions, and deliberately answers none of them with a
 * clock. QEMU's cycle counter is TCG bookkeeping rather than 68030 time, and
 * that is true on the Arty too because the m68k there is emulated on the ARM
 * cores. So every number printed here is a *count* -- bytes, pages, faults --
 * which is exactly as true on this machine as on hardware. Converting a fault
 * count into a time is a job for a real 68030 and is left undone rather than
 * done wrongly.
 *
 *   1. **Allocator fragmentation.** Editor-shaped churn -- many small objects,
 *      mixed lifetimes, occasional large buffers -- against picolibc's
 *      first-fit `malloc`. It reports peak footprint over peak live bytes.
 *      Section 4 sets the bar: above roughly 1.5 the first-fit allocator is
 *      costing real memory on a 128 MB machine and a segregated-fit
 *      replacement pays for itself.
 *
 *   2. **Commit clustering.** The heap's pages are touched in address order,
 *      because `sbrk` is a bump pointer, so for any cluster size the fault
 *      count and the over-commit follow from the peak footprint alone:
 *      ceil(pages / cluster) faults, and cluster * that - pages wasted. The
 *      table it prints is the whole tradeoff, and it does not need the kernel
 *      rebuilt at each size to produce it.
 *
 *   3. **Reserve versus commit.** What the old fixed 256 KiB heap would have
 *      committed against what this trace actually touches.
 *
 * The trace is deterministic -- a fixed seed and a xorshift -- so two runs are
 * comparable and a change in the allocator shows up as a change in the number
 * rather than as noise.
 */

#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *sbrk(intptr_t increment);

#define PAGE_BYTES 4096u
#define SLOTS 192u
#define OPERATIONS 20000u
/* What the heap used to be, for the comparison at the end. */
#define OLD_FIXED_HEAP (256u * 1024u)

static uint32_t random_state = 0x1a2b3c4du;

static uint32_t
next_random(void)
{
    uint32_t value = random_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

/*
 * Editor-shaped: overwhelmingly small objects, a tail of medium ones, and the
 * occasional large buffer -- a line, a token, a undo record, and now and then
 * a whole file read in. The proportions matter more than the exact bounds;
 * what makes it a fragmentation test is that the lifetimes are mixed, so
 * freed holes are of every size and are never freed in allocation order.
 */
static uint32_t
next_size(void)
{
    uint32_t roll = next_random() % 100u;

    if (roll < 85u)
        return 16u + (next_random() % 496u);
    if (roll < 97u)
        return 512u + (next_random() % 3584u);
    return 8192u + (next_random() % 57344u);
}

ASTRA_PROGRAM("heapbench", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    static uint8_t *slots[SLOTS];
    static uint32_t sizes[SLOTS];
    const uint8_t *break_start;
    const uint8_t *break_peak;
    uint32_t live_bytes = 0u;
    uint32_t peak_live = 0u;
    uint32_t footprint;
    uint32_t footprint_pages;
    uint32_t allocations = 0u;
    uint32_t failures = 0u;
    uint32_t frees = 0u;

    astra_posix_start(startup);
    memset(slots, 0, sizeof(slots));
    memset(sizes, 0, sizeof(sizes));
    /*
     * The break before anything is allocated. `malloc`'s own first request
     * creates the heap area, so this is read after a warm-up allocation to
     * keep the area's creation out of the footprint.
     */
    {
        void *warm = malloc(16u);

        if (warm == NULL) {
            printf("heapbench: no heap\n");
            return 1;
        }
        free(warm);
    }
    break_start = sbrk(0);
    break_peak = break_start;

    for (uint32_t step = 0u; step < OPERATIONS; ++step) {
        uint32_t slot = next_random() % SLOTS;
        const uint8_t *now;

        if (slots[slot] != NULL) {
            /*
             * Occupied slots are freed rather than skipped, which is what
             * mixes the lifetimes: a slot picked early in the run holds a
             * short-lived object and one picked late holds an old one.
             */
            free(slots[slot]);
            live_bytes -= sizes[slot];
            slots[slot] = NULL;
            sizes[slot] = 0u;
            ++frees;
            continue;
        }
        sizes[slot] = next_size();
        slots[slot] = malloc(sizes[slot]);
        if (slots[slot] == NULL) {
            ++failures;
            sizes[slot] = 0u;
            continue;
        }
        /*
         * Written through, because an allocator that hands back memory the
         * reservation never committed fails here rather than later somewhere
         * that looks like a different bug -- and because the write is what
         * makes the page real, which is the whole point of the reserved form.
         */
        (void)memset(slots[slot], (int)(slot & 0xffu), sizes[slot]);
        ++allocations;
        live_bytes += sizes[slot];
        if (live_bytes > peak_live)
            peak_live = live_bytes;
        now = sbrk(0);
        if (now > break_peak)
            break_peak = now;
    }

    footprint = (uint32_t)(break_peak - break_start);
    footprint_pages = (footprint + PAGE_BYTES - 1u) / PAGE_BYTES;

    printf("heapbench: %lu allocations, %lu frees, %lu failures\n",
           (unsigned long)allocations, (unsigned long)frees,
           (unsigned long)failures);
    printf("heapbench: peak live %lu bytes, peak footprint %lu bytes"
           " (%lu pages)\n",
           (unsigned long)peak_live, (unsigned long)footprint,
           (unsigned long)footprint_pages);
    /*
     * The ratio, in hundredths, because this machine has no float and a
     * printf that did would be a worse trade than reading 152 as 1.52.
     */
    if (peak_live != 0u)
        printf("heapbench: ratio %lu (hundredths; section 4's bar is 150)\n",
               (unsigned long)((uint64_t)footprint * 100u / peak_live));

    /*
     * The clustering table. Faults and waste both follow from the footprint
     * because sbrk hands out pages in address order, so this is arithmetic on
     * a measured number rather than a second experiment.
     */
    printf("heapbench: cluster faults waste_pages waste_bytes\n");
    for (uint32_t cluster = 1u; cluster <= 64u; cluster <<= 1) {
        uint32_t faults = (footprint_pages + cluster - 1u) / cluster;
        uint32_t waste = faults * cluster - footprint_pages;

        printf("heapbench:  %2lu     %4lu      %4lu     %6lu\n",
               (unsigned long)cluster, (unsigned long)faults,
               (unsigned long)waste, (unsigned long)(waste * PAGE_BYTES));
    }

    printf("heapbench: old fixed heap committed %lu bytes always;"
           " this trace touched %lu\n",
           (unsigned long)OLD_FIXED_HEAP, (unsigned long)footprint);
    if (footprint > OLD_FIXED_HEAP)
        printf("heapbench: the old ceiling would have refused this trace\n");
    (void)fflush(stdout);
    return 0;
}
