#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/alloc.h>
#include <astra/block_device.h>
#include <astra/memory_block.h>
#include <astra/metrics.h>

static uint32_t
two_samples(void *context, AstraMetricSample *out, uint32_t capacity)
{
    uint64_t base = *(const uint64_t *)context;

    if (capacity < 2u) {
        return 0u;
    }
    out[0].name = "alpha";
    out[0].value = base;
    out[1].name = "beta";
    out[1].value = base + 1u;
    return 2u;
}

static uint32_t
overrunning_sampler(void *context, AstraMetricSample *out, uint32_t capacity)
{
    (void)context;
    (void)out;
    return capacity + 1u;
}

static void
test_registry(void)
{
    static uint64_t first = 10;
    static uint64_t second = 20;
    AstraMetricSample samples[4];
    const AstraMetricGroup *group;

    astra_metric_reset_registry();
    assert(astra_metric_group_count() == 0u);

    assert(astra_metric_register(NULL, two_samples, &first) ==
           ASTRA_METRIC_INVALID_ARGUMENT);
    assert(astra_metric_register("", two_samples, &first) ==
           ASTRA_METRIC_INVALID_ARGUMENT);
    assert(astra_metric_register("empty", NULL, &first) ==
           ASTRA_METRIC_INVALID_ARGUMENT);

    assert(astra_metric_register("first", two_samples, &first) ==
           ASTRA_METRIC_OK);
    assert(astra_metric_register("first", two_samples, &second) ==
           ASTRA_METRIC_DUPLICATE);
    assert(astra_metric_register("second", two_samples, &second) ==
           ASTRA_METRIC_OK);
    assert(astra_metric_group_count() == 2u);

    group = astra_metric_find("second");
    assert(group != NULL && group->context == &second);
    assert(astra_metric_find("missing") == NULL);
    assert(astra_metric_group(2u) == NULL);

    assert(astra_metric_sample(group, samples, 4u) == 2u);
    assert(strcmp(samples[0].name, "alpha") == 0 && samples[0].value == 20u);
    assert(strcmp(samples[1].name, "beta") == 0 && samples[1].value == 21u);

    /* A sampler given too little room reports nothing rather than truncating. */
    assert(astra_metric_sample(group, samples, 1u) == 0u);
}

static void
test_registry_bounds(void)
{
    static uint64_t value = 1;
    static char names[ASTRA_METRIC_GROUP_MAX + 1u][8];
    uint32_t index;

    astra_metric_reset_registry();
    for (index = 0u; index <= ASTRA_METRIC_GROUP_MAX; ++index) {
        snprintf(names[index], sizeof(names[index]), "g%u", index);
        if (index < ASTRA_METRIC_GROUP_MAX) {
            assert(astra_metric_register(names[index], two_samples, &value) ==
                   ASTRA_METRIC_OK);
        } else {
            assert(astra_metric_register(names[index], two_samples, &value) ==
                   ASTRA_METRIC_FULL);
        }
    }
    assert(astra_metric_group_count() == ASTRA_METRIC_GROUP_MAX);
}

static void
test_overrun_rejected(void)
{
    AstraMetricSample samples[2];

    astra_metric_reset_registry();
    assert(astra_metric_register("liar", overrunning_sampler, NULL) ==
           ASTRA_METRIC_OK);
    /* A sampler claiming more than capacity is discarded, not trusted. */
    assert(astra_metric_sample(astra_metric_find("liar"), samples, 2u) == 0u);
}

static void
test_op_metrics(void)
{
    static const char *const names[] = {
        "read.calls", "read.failures", "read.units", "read.ticks",
        "read.max_ticks",
    };
    AstraMetricSample samples[5];
    AstraOpMetrics op = {0};

    astra_op_record(&op, 0, 8u, 100u);
    astra_op_record(&op, 1, 0u, 250u);
    astra_op_record(&op, 0, 4u, 50u);

    assert(op.calls == 3u && op.failures == 1u);
    assert(op.units == 12u && op.ticks == 400u && op.maximum_ticks == 250u);

    assert(astra_op_samples(&op, names, samples, 4u) == 0u);
    assert(astra_op_samples(&op, names, samples, 5u) == 5u);
    assert(strcmp(samples[0].name, "read.calls") == 0 && samples[0].value == 3u);
    assert(samples[4].value == 250u);

    assert(astra_clock_read(NULL, NULL) == 0u);
}

/*
 * The contract is only real if unrelated modules with unrelated accounting
 * shapes can both publish through it without changing their own structures.
 */
static uint8_t storage[64u * 512u];
static AstraAllocScalar arena[2048];
static const AstraAllocClass classes[] = {{32u, 16u}, {256u, 4u}};

static void
test_real_modules(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;
    AstraAllocator allocator;
    AstraBlockGeometry geometry;
    AstraMetricSample samples[ASTRA_METRIC_SAMPLE_MAX];
    uint8_t transfer[512];
    uint32_t count;
    uint32_t block_count;
    uint32_t index;
    int saw_read_calls = 0;
    int saw_live = 0;

    astra_metric_reset_registry();

    astra_memory_block_init(&memory, storage, sizeof(storage), 512u, 16u,
                            ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(&device, &astra_memory_block_backend, &memory,
                            NULL, NULL);
    assert(astra_block_query(&device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_block_read(&device, 0u, 1u, transfer, 0u) == ASTRA_BLOCK_OK);

    assert(astra_alloc_init(&allocator, classes, 2u, arena,
                            sizeof(arena)) == ASTRA_ALLOC_OK);
    assert(astra_alloc(&allocator, 16u) != NULL);

    assert(astra_metric_register("storage.image", astra_block_sampler,
                                 &device) == ASTRA_METRIC_OK);
    assert(astra_metric_register("alloc.service", astra_alloc_sampler,
                                 &allocator) == ASTRA_METRIC_OK);
    assert(astra_metric_group_count() == 2u);

    block_count = astra_metric_sample(astra_metric_find("storage.image"),
                                      samples, ASTRA_METRIC_SAMPLE_MAX);
    count = block_count;
    assert(count >= 20u);
    for (index = 0u; index < count; ++index) {
        if (strcmp(samples[index].name, "read.calls") == 0) {
            assert(samples[index].value == 1u);
            saw_read_calls = 1;
        }
    }
    assert(saw_read_calls);

    count = astra_metric_sample(astra_metric_find("alloc.service"), samples,
                                ASTRA_METRIC_SAMPLE_MAX);
    assert(count != 0u);
    for (index = 0u; index < count; ++index) {
        if (strcmp(samples[index].name, "live_blocks") == 0) {
            assert(samples[index].value == 1u);
            saw_live = 1;
        }
    }
    assert(saw_live);

    printf("metrics: %" PRIu32 " groups, block=%" PRIu32 " samples, "
           "alloc=%" PRIu32 " samples\n",
           astra_metric_group_count(), block_count, count);
}

int
main(void)
{
    test_registry();
    test_registry_bounds();
    test_overrun_rejected();
    test_op_metrics();
    test_real_modules();
    puts("astra metrics: PASS");
    return 0;
}
