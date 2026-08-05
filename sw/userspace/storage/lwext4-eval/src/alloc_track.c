/*
 * Accounting allocator for the Astra lwext4 probe.
 *
 * Built with CONFIG_USE_USER_MALLOC=1 so every lwext4 allocation is routed
 * here. Astra has no userspace heap yet, so the numbers this reports are the
 * budget a bounded static pool would have to cover.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct header {
    size_t size;
    size_t guard;
};

#define GUARD_VALUE ((size_t)0xA57A1234u)

static size_t live_bytes;
static size_t live_count;
static size_t peak_bytes;
static size_t peak_count;
static size_t largest;
static size_t total_calls;

static void account_alloc(size_t size)
{
    live_bytes += size;
    ++live_count;
    ++total_calls;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    if (live_count > peak_count)
        peak_count = live_count;
    if (size > largest)
        largest = size;
}

void *ext4_user_malloc(size_t size)
{
    struct header *h = malloc(sizeof(*h) + size);
    if (h == NULL)
        return NULL;
    h->size = size;
    h->guard = GUARD_VALUE;
    account_alloc(size);
    return h + 1;
}

void *ext4_user_calloc(size_t count, size_t size)
{
    void *p = ext4_user_malloc(count * size);
    if (p != NULL)
        memset(p, 0, count * size);
    return p;
}

void ext4_user_free(void *ptr)
{
    struct header *h;

    if (ptr == NULL)
        return;
    h = (struct header *)ptr - 1;
    if (h->guard != GUARD_VALUE) {
        printf("ALLOC: corrupt header on free\n");
        abort();
    }
    live_bytes -= h->size;
    --live_count;
    h->guard = 0;
    free(h);
}

void *ext4_user_realloc(void *ptr, size_t size)
{
    struct header *h;
    void *out;

    if (ptr == NULL)
        return ext4_user_malloc(size);

    h = (struct header *)ptr - 1;
    if (h->guard != GUARD_VALUE) {
        printf("ALLOC: corrupt header on realloc\n");
        abort();
    }

    out = ext4_user_malloc(size);
    if (out == NULL)
        return NULL;
    memcpy(out, ptr, h->size < size ? h->size : size);
    ext4_user_free(ptr);
    return out;
}

void astra_alloc_report(const char *tag)
{
    printf("alloc[%s]: calls=%lu peak_bytes=%lu peak_live=%lu largest=%lu "
           "leaked_bytes=%lu leaked_blocks=%lu\n",
           tag, (unsigned long)total_calls, (unsigned long)peak_bytes,
           (unsigned long)peak_count, (unsigned long)largest,
           (unsigned long)live_bytes, (unsigned long)live_count);
}
