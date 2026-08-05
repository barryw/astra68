/*
 * Binds lwext4's allocation hooks to the bounded Astra allocator.
 *
 * Class counts come from the measured profile in README.md: ~855 live
 * descriptors of 33..64 bytes, short-lived objects up to 128 bytes, and
 * cache_size + 1 block buffers. Headroom is deliberate and small; the point of
 * a bounded allocator is that exhaustion is reported, not absorbed.
 */

#include <stddef.h>
#include <stdio.h>

#include <astra/alloc.h>

#define PORT_BLOCK_BUFFERS 20u

static const AstraAllocClass port_classes[] = {
    {64u, 900u},
    {128u, 32u},
    {2048u, 4u},
    {4096u, PORT_BLOCK_BUFFERS},
};

#define PORT_CLASS_COUNT \
    ((uint32_t)(sizeof(port_classes) / sizeof(port_classes[0])))

/* 900*64 + 32*128 + 4*2048 + 20*4096, plus bitmaps and alignment padding. */
static AstraAllocScalar port_arena[19200];

static AstraAllocator port_allocator;
static int port_ready;

static void
port_init(void)
{
    AstraAllocStatus status;

    if (port_ready) {
        return;
    }
    status = astra_alloc_init(&port_allocator, port_classes, PORT_CLASS_COUNT,
                              port_arena, sizeof(port_arena));
    if (status != ASTRA_ALLOC_OK) {
        printf("port_alloc: init failed status=%d need=%lu have=%lu\n",
               (int)status,
               (unsigned long)astra_alloc_arena_bytes(port_classes,
                                                      PORT_CLASS_COUNT),
               (unsigned long)sizeof(port_arena));
        return;
    }
    port_ready = 1;
}

void *
ext4_user_malloc(size_t size)
{
    port_init();
    return astra_alloc(&port_allocator, size);
}

void *
ext4_user_calloc(size_t count, size_t size)
{
    size_t bytes = count * size;
    unsigned char *block;

    if (count != 0u && bytes / count != size) {
        return NULL;
    }
    block = ext4_user_malloc(bytes);
    if (block != NULL) {
        size_t index;

        for (index = 0u; index < bytes; ++index) {
            block[index] = 0u;
        }
    }
    return block;
}

void
ext4_user_free(void *pointer)
{
    AstraAllocStatus status = astra_alloc_free(&port_allocator, pointer);

    if (status != ASTRA_ALLOC_OK) {
        printf("port_alloc: rejected free %p status=%d\n", pointer,
               (int)status);
    }
}

void
astra_alloc_report(const char *tag)
{
    const AstraAllocMetrics *metrics = astra_alloc_metrics(&port_allocator);
    uint32_t index;

    printf("astra_alloc[%s]: arena=%lu bytes\n", tag,
           (unsigned long)astra_alloc_arena_bytes(port_classes,
                                                  PORT_CLASS_COUNT));
    printf("astra_alloc[%s]: allocations=%lu frees=%lu failures=%lu "
           "rejections=%lu live=%lu peak_live=%lu peak_bytes=%lu valid=%d\n",
           tag, (unsigned long)metrics->allocations,
           (unsigned long)metrics->frees, (unsigned long)metrics->failures,
           (unsigned long)metrics->rejections,
           (unsigned long)metrics->live_blocks,
           (unsigned long)metrics->peak_live_blocks,
           (unsigned long)metrics->peak_charged_bytes,
           astra_alloc_valid(&port_allocator));
    for (index = 0u; index < PORT_CLASS_COUNT; ++index) {
        printf("  class %-5lu count=%-5lu peak_live=%lu failures=%lu\n",
               (unsigned long)port_classes[index].size,
               (unsigned long)port_classes[index].count,
               (unsigned long)metrics->per_class[index].peak_live,
               (unsigned long)metrics->per_class[index].failures);
    }
}
