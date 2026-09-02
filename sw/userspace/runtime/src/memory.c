#include <stddef.h>
#include <stdint.h>

/*
 * Block primitives, a word at a time.
 *
 * These were byte loops, and every byte the system moves goes through them:
 * a file read copies out of the transfer buffer, a message copies its payload,
 * a program image is copied into the process that will run it. A byte loop
 * costs one load, one store, one increment and one branch per byte, so moving
 * 16 KiB was over sixty thousand iterations -- and it was most of the cost of
 * reading a file, measured.
 *
 * `may_alias` because the words are read and written through a type the caller
 * did not use; without it the compiler is free to assume no overlap with the
 * caller's own object and reorder around it.
 *
 * The unaligned case still goes a byte at a time. Aligning two pointers that
 * disagree about phase needs a shift-and-merge loop, and every caller here --
 * page copies, sector copies, message payloads -- shares alignment already.
 */
typedef uint32_t AstraWord __attribute__((may_alias));

#define ASTRA_WORD_SIZE ((size_t)sizeof(AstraWord))
#define ASTRA_WORD_MASK (ASTRA_WORD_SIZE - 1u)

#if !defined(__m68k__)
void *
memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    if (count >= ASTRA_WORD_SIZE &&
        (((uintptr_t)to ^ (uintptr_t)from) & ASTRA_WORD_MASK) == 0u) {
        AstraWord *out;
        const AstraWord *in;

        while (((uintptr_t)to & ASTRA_WORD_MASK) != 0u) {
            *to++ = *from++;
            --count;
        }
        out = (AstraWord *)(void *)to;
        in = (const AstraWord *)(const void *)from;
        while (count >= 8u * ASTRA_WORD_SIZE) {
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = in[3];
            out[4] = in[4];
            out[5] = in[5];
            out[6] = in[6];
            out[7] = in[7];
            out += 8;
            in += 8;
            count -= 8u * ASTRA_WORD_SIZE;
        }
        while (count >= ASTRA_WORD_SIZE) {
            *out++ = *in++;
            count -= ASTRA_WORD_SIZE;
        }
        to = (unsigned char *)(void *)out;
        from = (const unsigned char *)(const void *)in;
    }
    while (count-- != 0u) {
        *to++ = *from++;
    }
    return destination;
}

void *
memmove(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    if ((uintptr_t)to < (uintptr_t)from) {
        /*
         * Forward and non-overlapping in the direction that matters, so the
         * word path is safe: a word written here is never a word read later.
         */
        return memcpy(destination, source, count);
    }
    if ((uintptr_t)to > (uintptr_t)from) {
        to += count;
        from += count;
        if (count >= ASTRA_WORD_SIZE &&
            (((uintptr_t)to ^ (uintptr_t)from) & ASTRA_WORD_MASK) == 0u) {
            AstraWord *out;
            const AstraWord *in;

            while (((uintptr_t)to & ASTRA_WORD_MASK) != 0u) {
                *--to = *--from;
                --count;
            }
            out = (AstraWord *)(void *)to;
            in = (const AstraWord *)(const void *)from;
            while (count >= ASTRA_WORD_SIZE) {
                *--out = *--in;
                count -= ASTRA_WORD_SIZE;
            }
            to = (unsigned char *)(void *)out;
            from = (const unsigned char *)(const void *)in;
        }
        while (count-- != 0u) {
            *--to = *--from;
        }
    }
    return destination;
}

void *
memset(void *destination, int value, size_t count)
{
    unsigned char *to = destination;
    unsigned char byte = (unsigned char)value;

    if (count >= ASTRA_WORD_SIZE) {
        AstraWord pattern = (AstraWord)byte;
        AstraWord *out;

        pattern |= pattern << 8;
        pattern |= pattern << 16;
        while (((uintptr_t)to & ASTRA_WORD_MASK) != 0u) {
            *to++ = byte;
            --count;
        }
        out = (AstraWord *)(void *)to;
        while (count >= 8u * ASTRA_WORD_SIZE) {
            out[0] = pattern;
            out[1] = pattern;
            out[2] = pattern;
            out[3] = pattern;
            out[4] = pattern;
            out[5] = pattern;
            out[6] = pattern;
            out[7] = pattern;
            out += 8;
            count -= 8u * ASTRA_WORD_SIZE;
        }
        while (count >= ASTRA_WORD_SIZE) {
            *out++ = pattern;
            count -= ASTRA_WORD_SIZE;
        }
        to = (unsigned char *)(void *)out;
    }
    while (count-- != 0u) {
        *to++ = byte;
    }
    return destination;
}

int
memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;

    while (count-- != 0u) {
        if (*a != *b) {
            return *a < *b ? -1 : 1;
        }
        ++a;
        ++b;
    }
    return 0;
}
#endif

size_t
strlen(const char *text)
{
    const char *end = text;

    while (*end != '\0') {
        ++end;
    }
    return (size_t)(end - text);
}

int
strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int
strncmp(const char *left, const char *right, size_t count)
{
    while (count != 0u && *left != '\0' && *left == *right) {
        ++left;
        ++right;
        --count;
    }
    if (count == 0u) {
        return 0;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

char *
strcpy(char *destination, const char *source)
{
    char *out = destination;

    while ((*out++ = *source++) != '\0') {
    }
    return destination;
}

char *
strncpy(char *destination, const char *source, size_t count)
{
    char *out = destination;

    while (count != 0u && *source != '\0') {
        *out++ = *source++;
        --count;
    }
    while (count-- != 0u) {
        *out++ = '\0';
    }
    return destination;
}
