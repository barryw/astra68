#include <astra/ext4_time.h>

#include <stddef.h>

static AstraExt4Clock bound_clock;

void astra_ext4_clock_bind(AstraExt4Clock clock)
{
    bound_clock = clock;
}

uint32_t ext4_user_now(void)
{
    return bound_clock != NULL ? bound_clock() : 0u;
}
