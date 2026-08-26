#ifndef ASTRA_KERNEL_BYTES_H
#define ASTRA_KERNEL_BYTES_H

#include <stdbool.h>
#include <stdint.h>

void kernel_bytes_clear(void *destination, uint32_t size);

void kernel_words_fill(volatile uint32_t *destination,
                       uint32_t word_count, uint32_t value);

/* Source and destination must not overlap. */
void kernel_bytes_copy(void *destination, const void *source, uint32_t size);

bool kernel_bytes_equal(const void *left, const void *right, uint32_t size);

#endif
