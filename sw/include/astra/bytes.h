#ifndef ASTRA_BYTES_H
#define ASTRA_BYTES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Freestanding byte and string primitives provided by libastrart.
 *
 * Userspace modules include this instead of <string.h>. A freestanding target
 * toolchain need not ship <string.h> at all, and m68k-elf on the Mac does not,
 * so pulling the hosted header breaks the cross build even though the symbols
 * resolve. The declarations match the standard ones, so host builds link the
 * C library's versions unchanged.
 */

void *memcpy(void *restrict destination, const void *restrict source,
             size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strncpy(char *destination, const char *source, size_t count);

static inline int astra_words_zero(const uint32_t *words, size_t count)
{
    for (size_t index = 0u; index < count; ++index)
        if (words[index] != 0u)
            return 0;
    return 1;
}

#endif
