#include "../date/date_support.h"

#include <astra/runtime.h>
#include <astra/status.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation;

void *astra_runtime_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return fail_allocation ? NULL : realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

int main(void)
{
    AstraCivilTime civil = {0};
    struct tm rendered = {0};
    char format[601];
    char *output = NULL;
    size_t length = 0u;

    memset(format, 'x', sizeof(format) - 1u);
    format[sizeof(format) - 1u] = '\0';
    civil.year = 2026;
    civil.month = 9u;
    civil.day = 19u;
    civil.zone[0] = 'U';
    civil.zone[1] = 'T';
    civil.zone[2] = 'C';
    rendered.tm_year = 126;
    rendered.tm_mon = 8;
    rendered.tm_mday = 19;
    assert(date_format_alloc(&civil, &rendered, format, &output, &length) ==
           ASTRA_STATUS_OK);
    assert(length == 600u && memcmp(output, format, length) == 0);
    astra_runtime_deallocate(output);
    output = NULL;
    fail_allocation = 1;
    assert(date_format_alloc(&civil, &rendered, format, &output, &length) ==
           ASTRA_STATUS_LIMIT);
    assert(output == NULL);
    return 0;
}
