#include <astra/time_page.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static AstraTimePage page;

static AstraTimePageCopy
value(uint64_t monotonic, uint64_t wall, int32_t offset)
{
    AstraTimePageCopy copy;

    memset(&copy, 0, sizeof(copy));
    copy.monotonic_high = (uint32_t)(monotonic >> 32);
    copy.monotonic_low = (uint32_t)monotonic;
    copy.wall_high = (uint32_t)(wall >> 32);
    copy.wall_low = (uint32_t)wall;
    copy.utc_offset = offset;
    copy.zone = 0x45445400u; /* "EDT\0" */
    copy.flags = ASTRA_TIME_PAGE_WALL_VALID;
    return copy;
}

int
main(void)
{
    AstraTimeSample sample;
    AstraTimePageCopy first = value(5000000000ull, 1759680000000000000ull,
                                    -14400);
    AstraTimePageCopy second = value(5005000000ull, 1759680000005000000ull,
                                     -14400);

    /* Absent page: no magic. */
    assert(!astra_time_page_read(&page, &sample));
    page.magic = ASTRA_TIME_PAGE_MAGIC;
    page.version = ASTRA_TIME_PAGE_VERSION + 1u;
    assert(!astra_time_page_read(&page, &sample));
    page.version = ASTRA_TIME_PAGE_VERSION;

    astra_time_page_publish(&page, &first);
    assert(page.sequence == 1u);
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 5000000000ull);
    assert(sample.wall_ns == 1759680000000000000ull);
    assert(sample.utc_offset == -14400 && sample.wall_valid);

    /* The publish writes the other copy: the copy a reader holds is intact. */
    astra_time_page_publish(&page, &second);
    assert(page.sequence == 2u);
    assert(page.copy[1].monotonic_low == first.monotonic_low);
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 5005000000ull);

    /* A 64-bit value whose low word wraps. */
    astra_time_page_publish(&page, &(AstraTimePageCopy){
        .monotonic_high = 1u, .monotonic_low = 0u });
    assert(astra_time_page_read(&page, &sample));
    assert(sample.monotonic_ns == 0x100000000ull && !sample.wall_valid);

    puts("TIME PAGE PASS");
    return 0;
}
