#ifndef ASTRA_BOOT_PROGRESS_H
#define ASTRA_BOOT_PROGRESS_H

/*
 * The boot progress record: how far the machine has come up, counted, in
 * the unused tail of the shared text page (display.h). The DE25 display
 * helper draws the splash bar from it and shows the text page instead when
 * it says FAILED.
 *
 * Haiku lights a fixed slice of its splash per stage of a fixed enum
 * (src/system/kernel/boot_splash.cpp:67-94). The bar here has two phases:
 * the fixed steps every boot takes before services -- ROM, kernel,
 * supervisor stages 1-7 -- fill the first 30% in equal slices, and the
 * services fill the remaining 70% in N equal slices once the supervisor
 * announces N (Astra's service count is data, not a constant). Stage 8 is
 * 100%.
 *
 * Writers: the ROM from screen_init until handoff, then the kernel alone.
 * Each word is one aligned big-endian 32-bit store and one aligned 32-bit
 * load: no reader sees half of one. The header is written once per boot,
 * after the value is zeroed, and never changes.
 *
 * Value: state 31..28, announced 27, fixed step 26..21, zero 20, service
 * count 19..10, services done 9..0. Every legal transition leaves it no
 * smaller than it was; a reader that sees it decrease has found a bug.
 */

#include <stdint.h>

#include <astra/display.h>
#include <astra/supervisor.h>

#define ASTRA_BOOT_PROGRESS_HEADER_OFFSET 0xFE0u
#define ASTRA_BOOT_PROGRESS_VALUE_OFFSET  0xFE4u
#define ASTRA_BOOT_PROGRESS_MAGIC         0x4250u /* "BP" */
#define ASTRA_BOOT_PROGRESS_VERSION       1u
#define ASTRA_BOOT_PROGRESS_HEADER \
    ((ASTRA_BOOT_PROGRESS_MAGIC << 16) | ASTRA_BOOT_PROGRESS_VERSION)

#define ASTRA_BOOT_PROGRESS_STATE_BOOTING 0u
#define ASTRA_BOOT_PROGRESS_STATE_DONE    1u
#define ASTRA_BOOT_PROGRESS_STATE_FAILED  2u

/* The fixed phase: every step a successful boot takes before services. */
#define ASTRA_BOOT_PROGRESS_ROM_STEPS    10u /* 9 POST checks, kernel image */
#define ASTRA_BOOT_PROGRESS_KERNEL_STEPS 4u
#define ASTRA_BOOT_PROGRESS_FIXED_STEPS \
    (ASTRA_BOOT_PROGRESS_ROM_STEPS + ASTRA_BOOT_PROGRESS_KERNEL_STEPS + \
     ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED)
#define ASTRA_BOOT_PROGRESS_SERVICES_MAX 1023u
/* Bar shares in thousandths. */
#define ASTRA_BOOT_PROGRESS_FIXED_SHARE  300u
#define ASTRA_BOOT_PROGRESS_SHARE_TOTAL  1000u

#define ASTRA_BOOT_PROGRESS_ANNOUNCED   (UINT32_C(1) << 27)
#define ASTRA_BOOT_PROGRESS_FIXED_SHIFT 21
#define ASTRA_BOOT_PROGRESS_FIXED_MASK  0x3Fu
#define ASTRA_BOOT_PROGRESS_COUNT_SHIFT 10
#define ASTRA_BOOT_PROGRESS_COUNT_MASK  0x3FFu

_Static_assert(ASTRA_BOOT_PROGRESS_HEADER_OFFSET >=
                   ASTRA_TEXT_COLUMNS * ASTRA_TEXT_ROWS,
               "the boot progress record overlaps the text cells");
_Static_assert(ASTRA_BOOT_PROGRESS_VALUE_OFFSET + 4u <=
                   ASTRA_TEXT_CURSOR_OFFSET,
               "the boot progress record overlaps the cursor block");
_Static_assert((ASTRA_BOOT_PROGRESS_HEADER_OFFSET & 3u) == 0u &&
                   (ASTRA_BOOT_PROGRESS_VALUE_OFFSET & 3u) == 0u,
               "boot progress words must be naturally aligned");
_Static_assert(ASTRA_BOOT_PROGRESS_FIXED_STEPS <
                   ASTRA_BOOT_PROGRESS_FIXED_MASK,
               "the fixed phase must fit its field");

static inline uint32_t astra_boot_progress_pack(uint32_t state,
                                                uint32_t announced,
                                                uint32_t fixed,
                                                uint32_t count,
                                                uint32_t done)
{
    return (state << 28) |
           (announced != 0u ? ASTRA_BOOT_PROGRESS_ANNOUNCED : 0u) |
           ((fixed & ASTRA_BOOT_PROGRESS_FIXED_MASK)
            << ASTRA_BOOT_PROGRESS_FIXED_SHIFT) |
           ((count & ASTRA_BOOT_PROGRESS_COUNT_MASK)
            << ASTRA_BOOT_PROGRESS_COUNT_SHIFT) |
           (done & ASTRA_BOOT_PROGRESS_COUNT_MASK);
}

static inline uint32_t astra_boot_progress_state(uint32_t value)
{
    return value >> 28;
}

static inline uint32_t astra_boot_progress_announced(uint32_t value)
{
    return (value & ASTRA_BOOT_PROGRESS_ANNOUNCED) != 0u;
}

static inline uint32_t astra_boot_progress_fixed(uint32_t value)
{
    return (value >> ASTRA_BOOT_PROGRESS_FIXED_SHIFT) &
           ASTRA_BOOT_PROGRESS_FIXED_MASK;
}

static inline uint32_t astra_boot_progress_count(uint32_t value)
{
    return (value >> ASTRA_BOOT_PROGRESS_COUNT_SHIFT) &
           ASTRA_BOOT_PROGRESS_COUNT_MASK;
}

static inline uint32_t astra_boot_progress_done(uint32_t value)
{
    return value & ASTRA_BOOT_PROGRESS_COUNT_MASK;
}

/* FAILED from any state: the state nibble only ever rises. */
static inline uint32_t astra_boot_progress_fail(uint32_t value)
{
    return (value & 0x0FFFFFFFu) |
           (ASTRA_BOOT_PROGRESS_STATE_FAILED << 28);
}

/* One fixed-phase step. A step after the announcement or past the fixed
   count is a miscount. DONE takes none. */
static inline uint32_t astra_boot_progress_advance(uint32_t value)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return value;
    if (astra_boot_progress_announced(value) ||
        astra_boot_progress_fixed(value) >= ASTRA_BOOT_PROGRESS_FIXED_STEPS)
        return astra_boot_progress_fail(value);
    return value + (UINT32_C(1) << ASTRA_BOOT_PROGRESS_FIXED_SHIFT);
}

/* The supervisor's service count. Announced before the fixed phase is
   complete, it is recorded and FAILED. */
static inline uint32_t astra_boot_progress_announce(uint32_t value,
                                                    uint32_t services)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE ||
        astra_boot_progress_announced(value) ||
        services > ASTRA_BOOT_PROGRESS_SERVICES_MAX)
        return astra_boot_progress_fail(value);
    value |= ASTRA_BOOT_PROGRESS_ANNOUNCED |
             (services << ASTRA_BOOT_PROGRESS_COUNT_SHIFT);
    return astra_boot_progress_fixed(value) ==
                   ASTRA_BOOT_PROGRESS_FIXED_STEPS ?
               value : astra_boot_progress_fail(value);
}

/* One announced service finished launching; @p failed marks the boot. */
static inline uint32_t astra_boot_progress_service(uint32_t value,
                                                   uint32_t failed)
{
    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return value;
    if (!astra_boot_progress_announced(value) ||
        astra_boot_progress_done(value) >= astra_boot_progress_count(value))
        return astra_boot_progress_fail(value);
    value += 1u;
    return failed != 0u ? astra_boot_progress_fail(value) : value;
}

/* Stage 8. DONE only if every fixed step and every announced service
   arrived. */
static inline uint32_t astra_boot_progress_finish(uint32_t value)
{
    if (astra_boot_progress_state(value) != ASTRA_BOOT_PROGRESS_STATE_BOOTING)
        return value;
    return astra_boot_progress_announced(value) &&
                   astra_boot_progress_fixed(value) ==
                       ASTRA_BOOT_PROGRESS_FIXED_STEPS &&
                   astra_boot_progress_done(value) ==
                       astra_boot_progress_count(value) ?
               value | (ASTRA_BOOT_PROGRESS_STATE_DONE << 28) :
               astra_boot_progress_fail(value);
}

/* The bar's fill in pixels of @p width (width <= 4096). Never smaller for a
   larger value of the same boot: each phase only grows, DONE is all. */
static inline uint32_t astra_boot_progress_fill(uint32_t value,
                                                uint32_t width)
{
    uint32_t fixed = astra_boot_progress_fixed(value);
    uint32_t count = astra_boot_progress_count(value);
    uint32_t done = astra_boot_progress_done(value);
    uint32_t fill;

    if (astra_boot_progress_state(value) == ASTRA_BOOT_PROGRESS_STATE_DONE)
        return width;
    if (fixed > ASTRA_BOOT_PROGRESS_FIXED_STEPS)
        fixed = ASTRA_BOOT_PROGRESS_FIXED_STEPS;
    fill = width * ASTRA_BOOT_PROGRESS_FIXED_SHARE * fixed /
           (ASTRA_BOOT_PROGRESS_SHARE_TOTAL * ASTRA_BOOT_PROGRESS_FIXED_STEPS);
    if (astra_boot_progress_announced(value) && count != 0u)
        fill += width *
                (ASTRA_BOOT_PROGRESS_SHARE_TOTAL -
                 ASTRA_BOOT_PROGRESS_FIXED_SHARE) *
                (done < count ? done : count) /
                (ASTRA_BOOT_PROGRESS_SHARE_TOTAL * count);
    return fill < width ? fill : width;
}

#endif
