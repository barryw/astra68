#include "allocation.h"
#include "object_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct TestObject {
    uint32_t generation;
    uint32_t owner;
    uint32_t value;
} TestObject;

static void test_site_contract_and_accounting(void)
{
    KernelAllocationSiteInfo info;
    KernelAllocationStats stats;
    KernelAllocationStats tag;

    kernel_allocation_init();
    assert(kernel_allocation_phase() == KERNEL_ALLOCATION_PHASE_BOOT);
    assert(kernel_allocation_site_info(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, &info));
    assert(info.tag == KERNEL_ALLOCATION_TAG_PROCESS);
    assert(info.boot_only == 0u && info.injectable != 0u);
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 0x10000001u));
    assert(kernel_allocation_commit(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u, 540u, 0x10000001u));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, &stats));
    assert(stats.attempts == 1u && stats.successes == 1u);
    assert(stats.current_units == 1u && stats.current_bytes == 540u);
    assert(stats.peak_units == 1u && stats.peak_bytes == 540u);
    assert(stats.last_owner == 0x10000001u);
    assert(kernel_allocation_tag_stats(KERNEL_ALLOCATION_TAG_PROCESS, &tag));
    assert(tag.current_units == 1u && tag.current_bytes == 540u);
    assert(kernel_allocation_release(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u, 540u));
    assert(kernel_allocation_valid());
}

static void test_deterministic_injection(void)
{
    KernelAllocationStats process;
    KernelAllocationStats thread;

    kernel_allocation_init();
    kernel_allocation_test_fail_global(2u);
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u));
    assert(!kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, 2u));
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, 2u));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, &process));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, &thread));
    assert(process.attempts == 1u && process.failures == 0u);
    assert(thread.attempts == 2u && thread.failures == 1u &&
           thread.injected_failures == 1u);

    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 2u);
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_THREAD_RECORD, 2u));
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u));
    assert(!kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u));
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_PROCESS_RECORD, 1u));
    kernel_allocation_test_clear_failure();
    assert(kernel_allocation_valid());
}

static void test_boot_retirement(void)
{
    KernelAllocationStats stats;

    kernel_allocation_init();
    assert(kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u));
    assert(kernel_allocation_commit(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 4096u, 1u));
    assert(!kernel_allocation_retire_boot());
    assert(kernel_allocation_release(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 4096u));
    assert(kernel_allocation_retire_boot());
    assert(kernel_allocation_phase() == KERNEL_ALLOCATION_PHASE_RUNTIME);
    assert(!kernel_allocation_retire_boot());
    assert(!kernel_allocation_attempt(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u));
    assert(kernel_allocation_site_stats(
        KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, &stats));
    assert(stats.current_units == 0u && stats.current_bytes == 0u);
    assert(stats.failures == 1u);
    assert(kernel_allocation_valid());
}

static void test_typed_cache(void)
{
    TestObject objects[3];
    uint32_t bitmap[KERNEL_OBJECT_CACHE_BITMAP_WORDS(3u)];
    KernelObjectCache cache = {0};
    KernelObjectCacheStats cache_stats;
    void *claimed[3];
    void *extra;
    uint16_t slots[3];
    uint16_t extra_slot;

    kernel_allocation_init();
    assert(kernel_object_cache_init(
        &cache, objects, sizeof(objects[0]), 3u, bitmap,
        KERNEL_OBJECT_CACHE_BITMAP_WORDS(3u),
        KERNEL_ALLOCATION_SITE_SYNC_OBJECT));
    for (uint32_t index = 0u; index < 3u; ++index) {
        assert(kernel_object_cache_claim(&cache, 7u, &claimed[index],
                                         &slots[index]) ==
               KERNEL_OBJECT_CACHE_OK);
        assert(claimed[index] == &objects[index]);
        assert(slots[index] == index);
        assert(kernel_object_cache_contains(&cache, claimed[index]));
        assert(kernel_object_cache_is_claimed(&cache, claimed[index]));
    }
    assert(kernel_object_cache_claim(&cache, 7u, &extra, &extra_slot) ==
           KERNEL_OBJECT_CACHE_UNAVAILABLE);
    assert(extra == NULL && extra_slot == UINT16_MAX);
    assert(kernel_object_cache_stats(&cache, &cache_stats));
    assert(cache_stats.live == 3u && cache_stats.high_water == 3u);
    assert(kernel_object_cache_valid(&cache));

    assert(kernel_object_cache_release(&cache, claimed[1]) ==
           KERNEL_OBJECT_CACHE_OK);
    kernel_allocation_test_fail_site(
        KERNEL_ALLOCATION_SITE_SYNC_OBJECT, 1u);
    assert(kernel_object_cache_claim(&cache, 9u, &extra, &extra_slot) ==
           KERNEL_OBJECT_CACHE_UNAVAILABLE);
    assert(!kernel_object_cache_is_claimed(&cache, claimed[1]));
    assert(kernel_object_cache_claim(&cache, 9u, &extra, &extra_slot) ==
           KERNEL_OBJECT_CACHE_OK);
    assert(extra == claimed[1] && extra_slot == slots[1]);
    for (uint32_t index = 0u; index < 3u; ++index)
        assert(kernel_object_cache_release(&cache, claimed[index]) ==
               KERNEL_OBJECT_CACHE_OK);
    assert(kernel_object_cache_valid(&cache));
    assert(kernel_allocation_valid());
}

int main(void)
{
    test_site_contract_and_accounting();
    test_deterministic_injection();
    test_boot_retirement();
    test_typed_cache();
    puts("KERNEL ALLOCATION PASS");
    return 0;
}
