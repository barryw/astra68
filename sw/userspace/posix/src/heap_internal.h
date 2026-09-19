#ifndef ASTRA_POSIX_HEAP_INTERNAL_H
#define ASTRA_POSIX_HEAP_INTERNAL_H

#include <stdint.h>

/* Bind POSIX resource policy to the runtime-owned process allocator. */
void astra_posix_heap_prepare(uint32_t program_writable_bytes);

#endif
