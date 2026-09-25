#include <console_shutdown.h>

#include <assert.h>

int main(void)
{
    assert(console_shutdown_safe(1u, 0u, 0u, 2u, 17, 17));
    assert(!console_shutdown_safe(0u, 0u, 0u, 2u, 17, 17));
    assert(!console_shutdown_safe(1u, 1u, 0u, 2u, 17, 17));
    assert(!console_shutdown_safe(1u, 0u, 1u, 2u, 17, 17));
    assert(!console_shutdown_safe(1u, 0u, 0u, 3u, 17, 17));
    assert(!console_shutdown_safe(1u, 0u, 0u, 1u, 17, 17));
    assert(!console_shutdown_safe(1u, 0u, 0u, 2u, 17, 18));
    assert(!console_shutdown_safe(1u, 0u, 0u, 2u, 0, 0));
    return 0;
}
