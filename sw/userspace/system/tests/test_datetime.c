#include <astra/datetime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <string.h>

/* 2026-09-19 16:34:56 UTC, 12:34:56 EDT. */
#define INSTANT_NS UINT64_C(1789835696000000000)

static int clock_known;

uint32_t astra_clock_realtime(uint64_t *nanoseconds)
{
    if (!clock_known)
        return ASTRA_SYSCALL_UNSUPPORTED;
    *nanoseconds = INSTANT_NS;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_clock_realtime_zone(uint64_t *nanoseconds,
                                   AstraTimeZone *zone)
{
    if (!clock_known)
        return ASTRA_SYSCALL_UNSUPPORTED;
    *nanoseconds = INSTANT_NS;
    memset(zone, 0, sizeof(*zone));
    zone->utc_offset = -14400;
    memcpy(zone->name, "EDT", 4u);
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraDateTime now;
    uint64_t seconds = 7u;

    memset(&now, 0x5a, sizeof(now));
    assert(!astra_datetime_now(&now));
    assert(now.unix_nanoseconds == UINT64_C(0x5a5a5a5a5a5a5a5a));
    assert(!astra_datetime_unix_seconds(&seconds) && seconds == 7u);
    assert(!astra_datetime_now(NULL) && !astra_datetime_unix_seconds(NULL));

    clock_known = 1;
    assert(astra_datetime_now(&now));
    assert(now.unix_nanoseconds == INSTANT_NS);
    assert(now.utc.year == 2026 && now.utc.month == 9 && now.utc.day == 19);
    assert(now.utc.hour == 16 && now.local.hour == 12);
    assert(now.local.minute == 34 && now.local.second == 56);
    assert(strcmp(now.local.zone, "EDT") == 0);
    assert(astra_datetime_unix_seconds(&seconds));
    assert(seconds == INSTANT_NS / UINT64_C(1000000000));
    return 0;
}
