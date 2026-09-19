#include <astra/alloc.h>
#include <astra/ext4_alloc.h>

#include <assert.h>
#include <stdio.h>

#include <generated/ext4_config.h>

#define PROFILE_CACHE_BLOCKS 16u
#define PROFILE_DESCRIPTOR_SLOTS 1900u
#define REQUIRED_DESCRIPTOR_SLOTS \
    (PROFILE_DESCRIPTOR_SLOTS + CONFIG_BLOCK_DEV_CACHE_SIZE - \
     PROFILE_CACHE_BLOCKS)

static AstraAllocScalar arena[ASTRA_EXT4_ARENA_BYTES /
                              sizeof(AstraAllocScalar)];
static void *descriptors[REQUIRED_DESCRIPTOR_SLOTS];
static void *blocks[CONFIG_BLOCK_DEV_CACHE_SIZE + ASTRA_EXT4_TRANSIENT_BLOCKS];

int
main(void)
{
    AstraAllocator allocator;
    uint32_t index;

    assert(CONFIG_BLOCK_DEV_CACHE_SIZE >= PROFILE_CACHE_BLOCKS);
    assert(astra_ext4_alloc_classes[0].count ==
           REQUIRED_DESCRIPTOR_SLOTS);
    assert(astra_ext4_alloc_classes[3].count ==
           CONFIG_BLOCK_DEV_CACHE_SIZE + ASTRA_EXT4_TRANSIENT_BLOCKS);
    assert(astra_alloc_arena_bytes(astra_ext4_alloc_classes,
                                   ASTRA_EXT4_ALLOC_CLASS_COUNT) <=
           ASTRA_EXT4_ARENA_BYTES);
    assert(astra_alloc_init(&allocator, astra_ext4_alloc_classes,
                            ASTRA_EXT4_ALLOC_CLASS_COUNT, arena,
                            sizeof(arena)) == ASTRA_ALLOC_OK);

    for (index = 0u; index < REQUIRED_DESCRIPTOR_SLOTS; ++index) {
        descriptors[index] = astra_alloc(&allocator, 64u);
        assert(descriptors[index] != NULL);
    }
    assert(astra_alloc(&allocator, 64u) == NULL);

    while (index != 0u)
        assert(astra_alloc_free(&allocator, descriptors[--index]) ==
               ASTRA_ALLOC_OK);

    for (index = 0u; index < sizeof(blocks) / sizeof(blocks[0]); ++index) {
        blocks[index] = astra_alloc(&allocator, 4096u);
        assert(blocks[index] != NULL);
    }
    assert(astra_alloc(&allocator, 4096u) == NULL);
    while (index != 0u)
        assert(astra_alloc_free(&allocator, blocks[--index]) == ASTRA_ALLOC_OK);
    assert(astra_alloc_valid(&allocator));
    puts("ext4 allocator production profile: PASS");
    return 0;
}
