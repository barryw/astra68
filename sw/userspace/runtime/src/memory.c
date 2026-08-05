#include <stddef.h>
#include <stdint.h>

void *
memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

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
        while (count-- != 0u) {
            *to++ = *from++;
        }
    } else if ((uintptr_t)to > (uintptr_t)from) {
        to += count;
        from += count;
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

    while (count-- != 0u) {
        *to++ = (unsigned char)value;
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
