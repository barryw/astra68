#include <astra/ext4_alloc.h>

#include <stddef.h>

/*
 * Measured on big-endian MC68030 under qemu-m68k against a 16 MiB mke2fs
 * volume, running the storage suite's own mount test:
 *
 *   class 64    peak_live=855   class 2048  peak_live=1
 *   class 128   peak_live=16    class 4096  peak_live=17
 *
 * with 9,396 allocations, zero failures, zero rejections, nothing live at
 * unmount and a 126,400-byte peak charge. Counts carry only the headroom
 * above those peaks that the earlier evaluation already established; a
 * bounded allocator exists so exhaustion is reported, not absorbed.
 *
 * These are LP32 numbers and they are the ones that matter, because LP32 is
 * what ships. The same workload on an LP64 host produces a different shape
 * entirely — the 33..64-byte descriptors grow past 64 bytes and land in the
 * next class up — so a host measurement must not be used to size this.
 */
const AstraAllocClass astra_ext4_alloc_classes[ASTRA_EXT4_ALLOC_CLASS_COUNT] = {
    {64u, 900u},   /* inode refs, directory contexts, journal descriptors */
    {128u, 32u},   /* short-lived working structures */
    {2048u, 4u},   /* the superblock copy */
    {4096u, 20u},  /* CONFIG_BLOCK_DEV_CACHE_SIZE + 1, plus headroom */
};

static AstraAllocator *bound_allocator;
static AstraExt4AllocStats alloc_stats;

void
astra_ext4_alloc_bind(AstraAllocator *allocator)
{
    bound_allocator = allocator;
}

const AstraExt4AllocStats *
astra_ext4_alloc_stats(void)
{
    return &alloc_stats;
}

void *
ext4_user_malloc(size_t size)
{
    if (bound_allocator == NULL) {
        ++alloc_stats.unbound_requests;
        return NULL;
    }
    return astra_alloc(bound_allocator, size);
}

void *
ext4_user_calloc(size_t count, size_t size)
{
    size_t bytes;
    unsigned char *block;
    size_t index;

    if (count != 0u && size > (size_t)-1 / count) {
        return NULL;
    }
    bytes = count * size;
    block = ext4_user_malloc(bytes);
    if (block == NULL) {
        return NULL;
    }
    for (index = 0u; index < bytes; ++index) {
        block[index] = 0u;
    }
    return block;
}

void
ext4_user_free(void *pointer)
{
    AstraAllocStatus status;

    if (bound_allocator == NULL) {
        ++alloc_stats.unbound_requests;
        return;
    }
    status = astra_alloc_free(bound_allocator, pointer);
    if (status != ASTRA_ALLOC_OK) {
        /*
         * A refused free is a real defect somewhere: a foreign pointer, a
         * misaligned one, or a double free. It is counted rather than
         * asserted because unwinding a mount is more useful than halting the
         * service, and because the allocator has already declined to corrupt
         * its own free list.
         */
        ++alloc_stats.rejected_frees;
        alloc_stats.last_reject = status;
    }
}
