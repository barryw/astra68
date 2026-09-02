#include <astra/ext4_alloc.h>

#include <stddef.h>
#include <string.h>
#include <generated/ext4_config.h>

/*
 * Measured on big-endian MC68030 under qemu-m68k, running the storage suite's
 * own mount test against mke2fs volumes.
 *
 * The demand is set by the **journal size**, not the volume size. Measured
 * across 16 MiB to 1 TiB:
 *
 *   journal 4 MiB or smaller   class 64 peak_live=855
 *   journal 16 MiB or larger   class 64 peak_live=1767, flat to 1 TiB
 *
 * A 200 GB volume with a 4 MiB journal needs 855; the same volume with the
 * default journal needs 1767. Volume size itself changes nothing once past
 * that step, because what is live is bounded by the size of a transaction
 * rather than by the journal file. The other three classes do not move at all:
 * 16, 1 and 17 respectively, at every size tested.
 *
 * Sized for the plateau so that any volume up to 1 TiB mounts whatever journal
 * it was formatted with. That costs about 64 KiB of arena over sizing for a
 * pinned 4 MiB journal — a lever worth remembering if RAM ever gets tight,
 * since it halves this class.
 *
 * These are LP32 numbers and they are the ones that matter, because LP32 is
 * what ships. The same workload on an LP64 host produces a different shape
 * entirely — the 33..64-byte descriptors grow past 64 bytes and land in the
 * next class up — so a host measurement must not be used to size this.
 */
const AstraAllocClass astra_ext4_alloc_classes[ASTRA_EXT4_ALLOC_CLASS_COUNT] = {
    {64u, 1900u},  /* inode refs, directory contexts, journal descriptors */
    {128u, CONFIG_BLOCK_DEV_CACHE_SIZE + 32u}, /* buffers plus workers */
    {2048u, 4u},   /* the superblock copy */
    {4096u, CONFIG_BLOCK_DEV_CACHE_SIZE + 4u},
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
    void *block;

    if (count != 0u && size > (size_t)-1 / count) {
        return NULL;
    }
    bytes = count * size;
    block = ext4_user_malloc(bytes);
    if (block == NULL) {
        return NULL;
    }
    memset(block, 0, bytes);
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
