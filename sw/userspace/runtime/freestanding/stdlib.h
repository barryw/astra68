/*
 * Freestanding <stdlib.h> for Astra userspace.
 *
 * Deliberately does not declare malloc, free, calloc or realloc. Astra
 * services allocate from a bounded arena through <astra/alloc.h>, and vendored
 * code is pointed at that arena by its own porting hooks. Leaving the libc
 * names undeclared means an accidental call to the unbounded heap is a
 * compile error rather than an undefined symbol discovered at link time.
 */
#ifndef ASTRA_FREESTANDING_STDLIB_H
#define ASTRA_FREESTANDING_STDLIB_H

#include <stddef.h>

void qsort(void *array, size_t count, size_t size,
           int (*compare)(const void *, const void *));

#endif
