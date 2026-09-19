#include <stdint.h>

/* Descriptor tests exercise console state, not allocator policy wiring. */
void
astra_posix_heap_prepare(uint32_t program_writable_bytes)
{
    (void)program_writable_bytes;
}
