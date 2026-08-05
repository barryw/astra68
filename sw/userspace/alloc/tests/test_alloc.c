#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/alloc.h>

/*
 * Class table shaped by the measured lwext4 profile in
 * sw/userspace/storage/lwext4-eval: many small descriptors, a handful of
 * block-sized buffers.
 */
static const AstraAllocClass profile[] = {
    {32u, 64u},
    {64u, 32u},
    {256u, 8u},
    {1024u, 4u},
};

#define PROFILE_CLASSES ((uint32_t)(sizeof(profile) / sizeof(profile[0])))
#define PROFILE_BLOCKS (64u + 32u + 8u + 4u)

static AstraAllocScalar arena_storage[8192];
static void *live[PROFILE_BLOCKS];
static size_t live_size[PROFILE_BLOCKS];

static void
init_ok(AstraAllocator *allocator)
{
    size_t required = astra_alloc_arena_bytes(profile, PROFILE_CLASSES);

    assert(required != 0u);
    assert(required <= sizeof(arena_storage));
    assert(astra_alloc_init(allocator, profile, PROFILE_CLASSES,
                            arena_storage, sizeof(arena_storage)) ==
           ASTRA_ALLOC_OK);
    assert(astra_alloc_valid(allocator));
}

static void
test_init_contract(void)
{
    static const AstraAllocClass descending[] = {{64u, 4u}, {32u, 4u}};
    static const AstraAllocClass duplicate[] = {{64u, 4u}, {64u, 4u}};
    static const AstraAllocClass zero_size[] = {{0u, 4u}};
    static const AstraAllocClass zero_count[] = {{32u, 0u}};
    static const AstraAllocClass too_many[ASTRA_ALLOC_CLASS_MAX + 1u] = {
        {8u, 1u},  {16u, 1u},  {32u, 1u},  {64u, 1u}, {128u, 1u},
        {256u, 1u}, {512u, 1u}, {1024u, 1u}, {2048u, 1u}};
    AstraAllocator allocator;
    size_t required;

    assert(astra_alloc_arena_bytes(descending, 2u) == 0u);
    assert(astra_alloc_arena_bytes(duplicate, 2u) == 0u);
    assert(astra_alloc_arena_bytes(zero_size, 1u) == 0u);
    assert(astra_alloc_arena_bytes(zero_count, 1u) == 0u);
    assert(astra_alloc_arena_bytes(profile, 0u) == 0u);
    assert(astra_alloc_arena_bytes(too_many, ASTRA_ALLOC_CLASS_MAX + 1u) == 0u);
    assert(astra_alloc_arena_bytes(NULL, 1u) == 0u);

    assert(astra_alloc_init(NULL, profile, PROFILE_CLASSES, arena_storage,
                            sizeof(arena_storage)) ==
           ASTRA_ALLOC_INVALID_ARGUMENT);
    assert(astra_alloc_init(&allocator, profile, PROFILE_CLASSES, NULL,
                            sizeof(arena_storage)) ==
           ASTRA_ALLOC_INVALID_ARGUMENT);
    assert(astra_alloc_init(&allocator, descending, 2u, arena_storage,
                            sizeof(arena_storage)) == ASTRA_ALLOC_CLASS_INVALID);

    /* An arena one byte short of the computed requirement is refused. */
    required = astra_alloc_arena_bytes(profile, PROFILE_CLASSES);
    assert(astra_alloc_init(&allocator, profile, PROFILE_CLASSES,
                            arena_storage, required - 1u) ==
           ASTRA_ALLOC_ARENA_TOO_SMALL);
    assert(astra_alloc_init(&allocator, profile, PROFILE_CLASSES,
                            arena_storage, required) == ASTRA_ALLOC_OK);

    /* A misaligned arena is refused rather than silently realigned. */
    assert(astra_alloc_init(&allocator, profile, PROFILE_CLASSES,
                            (uint8_t *)arena_storage + 1,
                            sizeof(arena_storage) - 1u) ==
           ASTRA_ALLOC_INVALID_ARGUMENT);
}

static void
test_class_selection(void)
{
    AstraAllocator allocator;
    const AstraAllocMetrics *metrics;
    void *small;
    void *medium;

    init_ok(&allocator);
    metrics = astra_alloc_metrics(&allocator);

    small = astra_alloc(&allocator, 1u);
    assert(small != NULL);
    assert(metrics->per_class[0].live == 1u);

    small = astra_alloc(&allocator, 32u);
    assert(small != NULL);
    assert(metrics->per_class[0].live == 2u);

    medium = astra_alloc(&allocator, 33u);
    assert(medium != NULL);
    assert(metrics->per_class[1].live == 1u);

    medium = astra_alloc(&allocator, 257u);
    assert(medium != NULL);
    assert(metrics->per_class[3].live == 1u);

    /* Zero bytes still yields a distinct freeable block. */
    small = astra_alloc(&allocator, 0u);
    assert(small != NULL);
    assert(metrics->per_class[0].live == 3u);
    assert(astra_alloc_free(&allocator, small) == ASTRA_ALLOC_OK);

    assert(astra_alloc(&allocator, 1025u) == NULL);
    assert(astra_alloc_last_status(&allocator) == ASTRA_ALLOC_TOO_LARGE);
    assert(astra_alloc_valid(&allocator));
}

static void
test_exhaustion_does_not_spill(void)
{
    AstraAllocator allocator;
    const AstraAllocMetrics *metrics;
    void *blocks[64];
    uint32_t index;

    init_ok(&allocator);
    metrics = astra_alloc_metrics(&allocator);

    for (index = 0u; index < profile[0].count; ++index) {
        blocks[index] = astra_alloc(&allocator, 8u);
        assert(blocks[index] != NULL);
    }
    assert(metrics->per_class[0].live == profile[0].count);

    /* The 32-byte class is full; a 32-byte request must fail, not spill. */
    assert(astra_alloc(&allocator, 8u) == NULL);
    assert(astra_alloc_last_status(&allocator) == ASTRA_ALLOC_EXHAUSTED);
    assert(metrics->per_class[1].live == 0u);
    assert(metrics->per_class[0].failures == 1u);

    /* Larger classes remain available. */
    assert(astra_alloc(&allocator, 64u) != NULL);
    assert(metrics->per_class[1].live == 1u);
    assert(astra_alloc_valid(&allocator));

    for (index = 0u; index < profile[0].count; ++index) {
        assert(astra_alloc_free(&allocator, blocks[index]) == ASTRA_ALLOC_OK);
    }
    assert(metrics->per_class[0].live == 0u);
    assert(astra_alloc(&allocator, 8u) != NULL);
    assert(astra_alloc_valid(&allocator));
}

static void
test_free_rejections(void)
{
    static AstraAllocScalar foreign[4];
    AstraAllocator allocator;
    const AstraAllocMetrics *metrics;
    void *block;

    init_ok(&allocator);
    metrics = astra_alloc_metrics(&allocator);

    assert(astra_alloc_free(&allocator, NULL) == ASTRA_ALLOC_OK);
    assert(metrics->rejections == 0u);

    assert(astra_alloc_free(&allocator, foreign) ==
           ASTRA_ALLOC_FOREIGN_POINTER);
    assert(metrics->rejections == 1u);

    block = astra_alloc(&allocator, 8u);
    assert(block != NULL);
    assert(astra_alloc_free(&allocator, (uint8_t *)block + 1) ==
           ASTRA_ALLOC_MISALIGNED_POINTER);
    assert(metrics->rejections == 2u);

    assert(astra_alloc_free(&allocator, block) == ASTRA_ALLOC_OK);
    assert(astra_alloc_free(&allocator, block) == ASTRA_ALLOC_DOUBLE_FREE);
    assert(metrics->rejections == 3u);

    /* A rejected free leaves the pool untouched. */
    assert(metrics->live_blocks == 0u);
    assert(astra_alloc_valid(&allocator));
}

static void
test_injection(void)
{
    AstraAllocator allocator;
    const AstraAllocMetrics *metrics;
    uint32_t index;

    init_ok(&allocator);
    metrics = astra_alloc_metrics(&allocator);

    /* Global selector fires on exactly the nth allocation and disarms. */
    astra_alloc_inject(&allocator, 3u);
    assert(astra_alloc(&allocator, 8u) != NULL);
    assert(astra_alloc(&allocator, 8u) != NULL);
    assert(astra_alloc(&allocator, 8u) == NULL);
    assert(astra_alloc_last_status(&allocator) == ASTRA_ALLOC_INJECTED);
    assert(astra_alloc(&allocator, 8u) != NULL);
    assert(metrics->injected == 1u);

    /* Class selector counts only allocations routed to that class. */
    init_ok(&allocator);
    assert(astra_alloc_inject_class(&allocator, 2u, 2u) == ASTRA_ALLOC_OK);
    assert(astra_alloc_inject_class(&allocator, PROFILE_CLASSES, 1u) ==
           ASTRA_ALLOC_INVALID_ARGUMENT);
    for (index = 0u; index < 10u; ++index) {
        assert(astra_alloc(&allocator, 8u) != NULL);
    }
    assert(astra_alloc(&allocator, 200u) != NULL);
    assert(astra_alloc(&allocator, 200u) == NULL);
    assert(astra_alloc_last_status(&allocator) == ASTRA_ALLOC_INJECTED);
    assert(astra_alloc(&allocator, 200u) != NULL);
    assert(astra_alloc_valid(&allocator));
}

/*
 * Every allocation site fails once, and the allocator returns to an exact
 * baseline afterwards. This is the userspace analogue of the kernel's
 * global-Nth allocation failure matrix.
 */
static void
test_injection_matrix(void)
{
    AstraAllocator allocator;
    uint32_t nth;

    for (nth = 1u; nth <= 32u; ++nth) {
        const AstraAllocMetrics *metrics;
        void *blocks[32];
        uint32_t count = 0u;
        uint32_t index;
        int failed = 0;

        init_ok(&allocator);
        metrics = astra_alloc_metrics(&allocator);
        astra_alloc_inject(&allocator, nth);

        for (index = 0u; index < 32u; ++index) {
            void *block = astra_alloc(&allocator, 8u);

            if (block == NULL) {
                assert(astra_alloc_last_status(&allocator) ==
                       ASTRA_ALLOC_INJECTED);
                assert(index + 1u == nth);
                failed = 1;
                continue;
            }
            blocks[count++] = block;
        }
        assert(failed);
        assert(metrics->injected == 1u);
        assert(metrics->live_blocks == count);
        assert(astra_alloc_valid(&allocator));

        while (count-- != 0u) {
            assert(astra_alloc_free(&allocator, blocks[count]) ==
                   ASTRA_ALLOC_OK);
        }
        assert(metrics->live_blocks == 0u);
        assert(metrics->charged_bytes == 0u);
        assert(astra_alloc_valid(&allocator));
    }
}

static uint32_t
next_random(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static size_t
usable_bytes(size_t request)
{
    uint32_t index;

    for (index = 0u; index < PROFILE_CLASSES; ++index) {
        if (request <= profile[index].size) {
            return profile[index].size;
        }
    }
    return 0u;
}

/*
 * Random allocate/free against an oracle. Every live block carries a
 * signature; an overlap or a reused live slot corrupts a neighbour and is
 * caught on free.
 */
static void
test_stress(void)
{
    AstraAllocator allocator;
    const AstraAllocMetrics *metrics;
    uint32_t state = 0x5A5A1234u;
    uint32_t live_count = 0u;
    uint32_t iteration;

    init_ok(&allocator);
    metrics = astra_alloc_metrics(&allocator);
    memset(live, 0, sizeof(live));

    for (iteration = 0u; iteration < 200000u; ++iteration) {
        /* Bit 16, not bit 0: an LCG's low bit alternates deterministically. */
        uint32_t roll = (next_random(&state) >> 16) & 3u;
        /*
         * Alternate fill-heavy and drain-heavy phases so the pools reach
         * exhaustion and recover repeatedly rather than hovering near empty.
         */
        int filling = ((iteration >> 12) & 1u) == 0u;
        int allocate = filling ? (roll != 0u) : (roll == 0u);

        if (allocate || live_count == 0u) {
            /*
             * Weighted like the measured lwext4 profile: mostly small
             * descriptors, occasional block-sized buffers. A uniform size
             * draw never fills the small classes.
             */
            uint32_t pick = next_random(&state) % 100u;
            size_t request;

            if (pick < 70u) {
                request = (size_t)((next_random(&state) % 32u) + 1u);
            } else if (pick < 90u) {
                request = (size_t)((next_random(&state) % 32u) + 33u);
            } else if (pick < 97u) {
                request = (size_t)((next_random(&state) % 192u) + 65u);
            } else {
                request = (size_t)((next_random(&state) % 900u) + 257u);
            }
            size_t span = usable_bytes(request);
            void *block = astra_alloc(&allocator, request);
            uint32_t index;

            if (block == NULL) {
                AstraAllocStatus status = astra_alloc_last_status(&allocator);

                assert(status == ASTRA_ALLOC_EXHAUSTED ||
                       status == ASTRA_ALLOC_TOO_LARGE);
                assert((status == ASTRA_ALLOC_TOO_LARGE) == (span == 0u));
                continue;
            }
            assert(span != 0u);
            for (index = 0u; index < live_count; ++index) {
                assert(live[index] != block);
            }
            assert(live_count < PROFILE_BLOCKS);
            memset(block, (int)(iteration & 0xFFu), span);
            live[live_count] = block;
            live_size[live_count] = span;
            ++live_count;
        } else {
            uint32_t victim = next_random(&state) % live_count;
            const uint8_t *bytes = live[victim];
            size_t index;

            for (index = 0u; index < live_size[victim]; ++index) {
                assert(bytes[index] == bytes[0]);
            }
            assert(astra_alloc_free(&allocator, live[victim]) ==
                   ASTRA_ALLOC_OK);
            live[victim] = live[live_count - 1u];
            live_size[victim] = live_size[live_count - 1u];
            --live_count;
        }

        assert(metrics->live_blocks == live_count);
    }

    while (live_count-- != 0u) {
        assert(astra_alloc_free(&allocator, live[live_count]) ==
               ASTRA_ALLOC_OK);
    }
    assert(astra_alloc_valid(&allocator));
    assert(metrics->live_blocks == 0u);
    assert(metrics->charged_bytes == 0u);
    assert(metrics->allocations == metrics->frees);

    printf("alloc stress: allocations=%" PRIu32 " frees=%" PRIu32
           " failures=%" PRIu32 " peak_blocks=%" PRIu32
           " peak_bytes=%lu\n",
           metrics->allocations, metrics->frees, metrics->failures,
           metrics->peak_live_blocks,
           (unsigned long)metrics->peak_charged_bytes);
}

static void
report_profile(void)
{
    printf("alloc arena for lwext4 profile: %lu bytes across %" PRIu32
           " classes\n",
           (unsigned long)astra_alloc_arena_bytes(profile, PROFILE_CLASSES),
           PROFILE_CLASSES);
}

int
main(void)
{
    test_init_contract();
    test_class_selection();
    test_exhaustion_does_not_spill();
    test_free_rejections();
    test_injection();
    test_injection_matrix();
    test_stress();
    report_profile();
    puts("astra allocator: PASS");
    return 0;
}
