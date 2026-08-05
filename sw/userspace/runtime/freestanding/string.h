/*
 * Freestanding <string.h> for Astra userspace.
 *
 * The MC68030 toolchain ships no C library and services link -nostdlib, so
 * vendored third-party code that includes <string.h> has nothing to include.
 * This declares exactly the primitives libastrart implements, so a caller
 * reaching for anything else fails at compile time instead of at link time in
 * a service that has already shipped.
 */
#ifndef ASTRA_FREESTANDING_STRING_H
#define ASTRA_FREESTANDING_STRING_H

#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source,
             size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);

#endif
