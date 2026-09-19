#include <astra/runtime.h>
#include <astra/syscall.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/heap_internal.h"

static AstraRuntimeGrowthPolicy installed_policy;
static void *installed_context;
static intptr_t last_increment;
static uint8_t break_bytes[32];
static uint64_t policy_static;
static uint32_t policy_current;
static uint32_t policy_growth;

extern void *sbrk(intptr_t increment);

int
astra_posix_resource_heap_allows(uint64_t static_bytes, uint32_t current_bytes,
                                 uint32_t growth_bytes)
{
    policy_static = static_bytes;
    policy_current = current_bytes;
    policy_growth = growth_bytes;
    return current_bytes == 17u && growth_bytes == 9u;
}

void
astra_runtime_set_growth_policy(AstraRuntimeGrowthPolicy policy,
                                void *context)
{
    installed_policy = policy;
    installed_context = context;
}

void *
astra_runtime_sbrk(intptr_t increment)
{
    last_increment = increment;
    return increment == 23 ? break_bytes : NULL;
}

int
main(void)
{
    astra_posix_heap_prepare(123u);
    assert(installed_policy != NULL);
    assert(installed_context == NULL);
    assert(installed_policy(17u, 9u, installed_context));
    assert(policy_static == 123u);
    assert(policy_current == 17u);
    assert(policy_growth == 9u);
    assert(!installed_policy(18u, 9u, installed_context));
    assert(sbrk(23) == break_bytes);
    assert(last_increment == 23);
    errno = 0;
    assert(sbrk(24) == (void *)-1);
    assert(errno == ENOMEM);

    puts("ASTRA POSIX HEAP PASS");
    return 0;
}
