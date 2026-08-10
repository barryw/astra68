#ifndef ASTRA_EXT4_ALLOC_H
#define ASTRA_EXT4_ALLOC_H

#include <stdint.h>

#include <astra/alloc.h>

/*
 * lwext4's allocation hooks bound to the bounded Astra allocator.
 *
 * lwext4 calls ext4_user_malloc/calloc/free with no context argument, so the
 * allocator has to be reachable from file scope. The arena itself stays with
 * the service: this module decides nothing about how much memory the
 * filesystem may have, it only routes the requests.
 *
 * ext4_user_realloc is not implemented. No file in the vendored source set
 * calls ext4_realloc, and a bounded class allocator has no resize operation to
 * bind it to; if a future upstream bump introduces a caller it must fail to
 * link rather than silently acquire an unbounded one.
 */

#define ASTRA_EXT4_ALLOC_CLASS_COUNT 4u
#define ASTRA_EXT4_ARENA_BYTES (5u * 1024u * 1024u)

/*
 * The measured shape of lwext4's demand, not a guess. The evaluation workload
 * in sw/userspace/storage/lwext4-eval holds 855 live descriptors of 33..64
 * bytes and exactly CONFIG_BLOCK_DEV_CACHE_SIZE + 1 block buffers. Headroom
 * is deliberately small,
 * because the point of a bounded allocator is that exhaustion is reported
 * rather than absorbed.
 *
 * Descriptor demand was measured through 1 TiB and plateaus at the configured
 * bound below; the block-buffer class follows the configured cache size.
 */
extern const AstraAllocClass astra_ext4_alloc_classes[ASTRA_EXT4_ALLOC_CLASS_COUNT];

typedef struct AstraExt4AllocStats {
    /* Requests that arrived before any allocator was bound. */
    uint32_t unbound_requests;
    /* Frees the allocator refused: foreign, misaligned, or double. */
    uint32_t rejected_frees;
    /* Last refusal reason, for diagnosis. */
    AstraAllocStatus last_reject;
} AstraExt4AllocStats;

/* Passing NULL unbinds, which makes every later request a counted failure. */
void astra_ext4_alloc_bind(AstraAllocator *allocator);
const AstraExt4AllocStats *astra_ext4_alloc_stats(void);

#endif
