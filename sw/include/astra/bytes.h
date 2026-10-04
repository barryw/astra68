#ifndef ASTRA_BYTES_H
#define ASTRA_BYTES_H

/**
 * @file bytes.h
 * @brief Freestanding byte and string primitives provided by libastrart.
 *
 * Userspace modules include this instead of <%string.h>. A freestanding
 * target toolchain need not ship <%string.h> at all, and m68k-elf on the Mac
 * does not, so pulling the hosted header breaks the cross build even though
 * the symbols resolve. The declarations match the standard ones, so host
 * builds link the C library's versions unchanged.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
/** `restrict`-equivalent qualifier for a non-overlapping pointer parameter. */
#define ASTRA_BYTES_RESTRICT __restrict
extern "C" {
#else
/** `restrict`-equivalent qualifier for a non-overlapping pointer parameter. */
#define ASTRA_BYTES_RESTRICT restrict
#endif

/**
 * Copy count bytes from source to destination.
 *
 * destination and source must not overlap; use ::memmove if they might.
 *
 * @param destination Buffer to copy into.
 * @param source Buffer to copy from.
 * @param count Number of bytes to copy.
 * @return destination.
 */
void *memcpy(void *ASTRA_BYTES_RESTRICT destination,
             const void *ASTRA_BYTES_RESTRICT source, size_t count);

/**
 * Copy count bytes from source to destination, correctly even when the two
 * regions overlap.
 *
 * @param destination Buffer to copy into.
 * @param source Buffer to copy from.
 * @param count Number of bytes to copy.
 * @return destination.
 */
void *memmove(void *destination, const void *source, size_t count);

/**
 * Fill the first count bytes of destination with a single byte value.
 *
 * @param destination Buffer to fill.
 * @param value Byte value to store, taken as (unsigned char)value.
 * @param count Number of bytes to set.
 * @return destination.
 */
void *memset(void *destination, int value, size_t count);

/**
 * Compare the first count bytes of two buffers, treating each byte as
 * unsigned char.
 *
 * @param left First buffer.
 * @param right Second buffer.
 * @param count Number of bytes to compare.
 * @return 0 if the two regions are equal over count bytes; negative if the
 *     first differing byte in left is smaller than in right; positive if
 *     larger. Always 0 if count is 0.
 */
int memcmp(const void *left, const void *right, size_t count);

/**
 * Compute the length of a NUL-terminated string.
 *
 * @param text NUL-terminated string.
 * @return Number of characters before the terminating NUL.
 */
size_t strlen(const char *text);

/**
 * Compare two NUL-terminated strings lexicographically, byte by byte.
 *
 * @param left First string.
 * @param right Second string.
 * @return 0 if the strings are equal; negative if left sorts before right;
 *     positive if left sorts after right.
 */
int strcmp(const char *left, const char *right);

/**
 * Compare at most count characters of two NUL-terminated strings
 * lexicographically, stopping at the first NUL encountered in either one.
 *
 * @param left First string.
 * @param right Second string.
 * @param count Maximum number of characters to compare.
 * @return 0 if equal over that span; negative if left sorts before right;
 *     positive if left sorts after right.
 */
int strncmp(const char *left, const char *right, size_t count);

/**
 * Copy the NUL-terminated string at source, including its terminator, into
 * destination.
 *
 * destination must be large enough to hold it, and the strings must not
 * overlap.
 *
 * @param destination Buffer to copy into.
 * @param source NUL-terminated string to copy from.
 * @return destination.
 */
char *strcpy(char *ASTRA_BYTES_RESTRICT destination,
             const char *ASTRA_BYTES_RESTRICT source);

/**
 * Copy at most count characters from source into destination.
 *
 * Pads with NUL bytes if source is shorter than count; does not
 * NUL-terminate destination if source is not shorter than count.
 *
 * @param destination Buffer to copy into.
 * @param source Source string.
 * @param count Number of characters to write into destination.
 * @return destination.
 */
char *strncpy(char *destination, const char *source, size_t count);

#ifdef __cplusplus
}
#endif

/**
 * Report whether every element of a uint32_t array is zero.
 *
 * Used to check that a reserved/padding field in a wire or ABI structure was
 * left zeroed by the caller.
 *
 * @param words Array of count 32-bit words.
 * @param count Number of words to check; 0 always reports true.
 * @return 1 if every word is zero (or count is 0); 0 if any word is
 *     nonzero.
 */
static inline int astra_words_zero(const uint32_t *words, size_t count)
{
    for (size_t index = 0u; index < count; ++index)
        if (words[index] != 0u)
            return 0;
    return 1;
}

#endif
