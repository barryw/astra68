#include <astra/datetime.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation;

void *astra_runtime_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

uint32_t astra_clock_realtime(uint64_t *nanoseconds)
{
    (void)nanoseconds;
    return 1u;
}

uint32_t astra_clock_realtime_zone(uint64_t *nanoseconds,
                                   AstraTimeZone *zone)
{
    (void)nanoseconds;
    (void)zone;
    return 1u;
}

int main(void)
{
    AstraCivilTime civil = {
        .year = 2026, .month = 9, .day = 19, .weekday = 6,
        .year_day = 262, .hour = 12, .minute = 34, .second = 56,
        .utc_offset = -14400, .zone = "EDT"
    };
    char format[201];
    char output[256];

    memset(format, 'x', sizeof(format) - 1u);
    format[sizeof(format) - 1u] = '\0';
    assert(astra_datetime_format(&civil, format, output, sizeof(output)) ==
           sizeof(format) - 1u);
    assert(strcmp(output, format) == 0);
    assert(astra_datetime_format(&civil, format, output, 16u) == 0u);
    fail_allocation = 1;
    assert(astra_datetime_format(&civil, "%z", output, sizeof(output)) == 0u);
    return 0;
}
