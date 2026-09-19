#include <astra/runtime.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "heap_internal.h"
#include "resource_internal.h"

static uint32_t executable_writable_bytes;

static int
posix_heap_growth(uint32_t current_bytes, uint32_t growth_bytes,
                  void *context)
{
    (void)context;
    return astra_posix_resource_heap_allows(executable_writable_bytes,
                                            current_bytes, growth_bytes);
}

void
astra_posix_heap_prepare(uint32_t program_writable_bytes)
{
    executable_writable_bytes = program_writable_bytes;
    astra_runtime_set_growth_policy(posix_heap_growth, NULL);
}

void *
sbrk(intptr_t increment)
{
    void *result = astra_runtime_sbrk(increment);

    if (result == NULL) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return result;
}
