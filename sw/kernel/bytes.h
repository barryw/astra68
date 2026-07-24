#ifndef ASTRA_KERNEL_BYTES_H
#define ASTRA_KERNEL_BYTES_H

#include <stdint.h>

void kernel_bytes_clear(void *destination, uint32_t size);

/* Source and destination must not overlap. */
void kernel_bytes_copy(void *destination, const void *source, uint32_t size);

#endif
