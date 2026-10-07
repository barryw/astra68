/*
 * Time for callers that cannot afford a trap: the kernel time page, or the
 * clock syscalls when the page is absent (a kernel older than the page).
 */

#include <astra/runtime.h>
#include <astra/time_page.h>

/*
 * The inverse of astra_civil_zone_unpack(): four ASCII characters packed one
 * per byte, most significant byte first. A short zone's unused trailing bytes
 * are already NUL in AstraTimeZone.name, so they pack as zero.
 */
static uint32_t
pack_zone_name(const char name[5])
{
    uint32_t packed = 0u;
    uint32_t index;

    for (index = 0u; index < 4u; ++index)
        packed |= (uint32_t)(uint8_t)name[index] << (24u - 8u * index);
    return packed;
}

void
astra_time_now(AstraTimeSample *sample)
{
    uint64_t wall = 0u;
    AstraTimeZone zone;
    uint32_t status;

    if (sample == NULL)
        return;
    if (astra_time_page_read(
            (const volatile AstraTimePage *)(uintptr_t)ASTRA_TIME_PAGE_ADDRESS,
            sample))
        return;
    sample->monotonic_ns = astra_clock_monotonic();
    status = astra_clock_realtime_zone(&wall, &zone);
    sample->wall_valid = status == ASTRA_SYSCALL_OK;
    sample->wall_ns = sample->wall_valid ? wall : 0u;
    sample->utc_offset = sample->wall_valid ? zone.utc_offset : 0;
    sample->zone = sample->wall_valid ? pack_zone_name(zone.name) : 0u;
}
