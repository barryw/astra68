#include "allocation.h"

#include <stddef.h>

typedef struct KernelAllocationFailureSelector {
    uint32_t target;
    uint32_t observed;
    uint8_t site;
    uint8_t enabled;
    uint8_t reserved[2];
} KernelAllocationFailureSelector;

static const KernelAllocationSiteInfo site_info[KERNEL_ALLOCATION_SITE_COUNT] = {
    [KERNEL_ALLOCATION_SITE_INVALID] = {
        "invalid", KERNEL_ALLOCATION_TAG_INVALID, 0u, 0u, 0u
    },
    [KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE] = {
        "boot-selftest-page", KERNEL_ALLOCATION_TAG_BOOT, 1u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE] = {
        "vm-page-table", KERNEL_ALLOCATION_TAG_VM, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_PROCESS_RECORD] = {
        "process-record", KERNEL_ALLOCATION_TAG_PROCESS, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE] = {
        "process-code-page", KERNEL_ALLOCATION_TAG_PROCESS, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_THREAD_RECORD] = {
        "thread-record", KERNEL_ALLOCATION_TAG_THREAD, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_THREAD_STACK_PAGE] = {
        "thread-stack-page", KERNEL_ALLOCATION_TAG_THREAD, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_HANDLE_SLOT] = {
        "handle-slot", KERNEL_ALLOCATION_TAG_HANDLE, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_DETACHED_HANDLE] = {
        "detached-handle", KERNEL_ALLOCATION_TAG_HANDLE, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_SYNC_OBJECT] = {
        "sync-object", KERNEL_ALLOCATION_TAG_SYNC, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_PORT_OBJECT] = {
        "port-object", KERNEL_ALLOCATION_TAG_IPC, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_PORT_MESSAGE] = {
        "port-message", KERNEL_ALLOCATION_TAG_IPC, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_AREA_OBJECT] = {
        "area-object", KERNEL_ALLOCATION_TAG_AREA, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_AREA_PAGES] = {
        "area-pages", KERNEL_ALLOCATION_TAG_AREA, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_AREA_MAPPING] = {
        "area-mapping", KERNEL_ALLOCATION_TAG_AREA, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_RING_OBJECT] = {
        "ring-object", KERNEL_ALLOCATION_TAG_RING, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_DMA_OBJECT] = {
        "dma-object", KERNEL_ALLOCATION_TAG_DMA, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_DMA_PAGES] = {
        "dma-pages", KERNEL_ALLOCATION_TAG_DMA, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_BLOCK_REQUEST] = {
        "block-request", KERNEL_ALLOCATION_TAG_BLOCK, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_EMERGENCY_FAULT_PAGE] = {
        "emergency-fault-page", KERNEL_ALLOCATION_TAG_EMERGENCY,
        0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_EMERGENCY_CLEANUP_PAGE] = {
        "emergency-cleanup-page", KERNEL_ALLOCATION_TAG_EMERGENCY,
        0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_EMERGENCY_LOG_PAGE] = {
        "emergency-log-page", KERNEL_ALLOCATION_TAG_EMERGENCY,
        0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_EMERGENCY_RESERVE] = {
        "emergency-reserve", KERNEL_ALLOCATION_TAG_EMERGENCY,
        0u, 0u, 0u
    },
    [KERNEL_ALLOCATION_SITE_MEMORY_GENERIC] = {
        "memory-generic", KERNEL_ALLOCATION_TAG_MEMORY, 0u, 1u, 0u
    },
    [KERNEL_ALLOCATION_SITE_IRQ_ENDPOINT] = {
        "irq-endpoint", KERNEL_ALLOCATION_TAG_DEVICE, 0u, 1u, 0u
    }
};

static KernelAllocationStats site_stats[KERNEL_ALLOCATION_SITE_COUNT];
static uint32_t tag_current_units[KERNEL_ALLOCATION_TAG_COUNT];
static uint32_t tag_peak_units[KERNEL_ALLOCATION_TAG_COUNT];
static uint32_t tag_current_bytes[KERNEL_ALLOCATION_TAG_COUNT];
static uint32_t tag_peak_bytes[KERNEL_ALLOCATION_TAG_COUNT];
static KernelAllocationFailureSelector failure_selector;
static KernelAllocationPhase phase;
static uint8_t initialized;
static uint8_t corrupt;

_Static_assert(KERNEL_ALLOCATION_SITE_COUNT <= UINT8_MAX,
               "allocation site IDs must fit in frame metadata");

static bool valid_site(KernelAllocationSite site)
{
    return site > KERNEL_ALLOCATION_SITE_INVALID &&
           site < KERNEL_ALLOCATION_SITE_COUNT &&
           site_info[site].tag > KERNEL_ALLOCATION_TAG_INVALID &&
           site_info[site].tag < KERNEL_ALLOCATION_TAG_COUNT;
}

static void clear_stats(KernelAllocationStats *stats)
{
    stats->attempts = 0u;
    stats->successes = 0u;
    stats->failures = 0u;
    stats->injected_failures = 0u;
    stats->releases = 0u;
    stats->current_units = 0u;
    stats->peak_units = 0u;
    stats->current_bytes = 0u;
    stats->peak_bytes = 0u;
    stats->last_owner = 0u;
}

static void copy_stats(KernelAllocationStats *destination,
                       const KernelAllocationStats *source)
{
    destination->attempts = source->attempts;
    destination->successes = source->successes;
    destination->failures = source->failures;
    destination->injected_failures = source->injected_failures;
    destination->releases = source->releases;
    destination->current_units = source->current_units;
    destination->peak_units = source->peak_units;
    destination->current_bytes = source->current_bytes;
    destination->peak_bytes = source->peak_bytes;
    destination->last_owner = source->last_owner;
}

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static bool add_checked(uint32_t left, uint32_t right, uint32_t *result)
{
    if (result == NULL || right > UINT32_MAX - left)
        return false;
    *result = left + right;
    return true;
}

static void ensure_initialized(void)
{
    if (initialized == 0u)
        kernel_allocation_init();
}

void kernel_allocation_init(void)
{
    for (uint32_t site = 0u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site)
        clear_stats(&site_stats[site]);
    for (uint32_t tag = 0u; tag < KERNEL_ALLOCATION_TAG_COUNT; ++tag) {
        tag_current_units[tag] = 0u;
        tag_peak_units[tag] = 0u;
        tag_current_bytes[tag] = 0u;
        tag_peak_bytes[tag] = 0u;
    }
    failure_selector.target = 0u;
    failure_selector.observed = 0u;
    failure_selector.site = KERNEL_ALLOCATION_SITE_INVALID;
    failure_selector.enabled = 0u;
    failure_selector.reserved[0] = 0u;
    failure_selector.reserved[1] = 0u;
    phase = KERNEL_ALLOCATION_PHASE_BOOT;
    corrupt = 0u;
    initialized = 1u;
}

bool kernel_allocation_initialized(void)
{
    return initialized != 0u;
}

KernelAllocationPhase kernel_allocation_phase(void)
{
    ensure_initialized();
    return phase;
}

bool kernel_allocation_retire_boot(void)
{
    ensure_initialized();
    if (phase != KERNEL_ALLOCATION_PHASE_BOOT)
        return false;
    for (uint32_t site = 1u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site) {
        if (site_info[site].boot_only != 0u &&
            (site_stats[site].current_units != 0u ||
             site_stats[site].current_bytes != 0u))
            return false;
    }
    phase = KERNEL_ALLOCATION_PHASE_RUNTIME;
    return true;
}

bool kernel_allocation_attempt(KernelAllocationSite site, uint32_t owner)
{
    KernelAllocationStats *stats;
    bool selected;

    ensure_initialized();
    if (!valid_site(site) || site_info[site].injectable == 0u)
        return false;
    stats = &site_stats[site];
    increment_saturating(&stats->attempts);
    stats->last_owner = owner;
    if (site_info[site].boot_only != 0u &&
        phase != KERNEL_ALLOCATION_PHASE_BOOT) {
        increment_saturating(&stats->failures);
        return false;
    }
    selected = failure_selector.enabled != 0u &&
        (failure_selector.site == KERNEL_ALLOCATION_SITE_INVALID ||
         failure_selector.site == (uint8_t)site);
    if (!selected)
        return true;
    increment_saturating(&failure_selector.observed);
    if (failure_selector.observed != failure_selector.target)
        return true;
    failure_selector.enabled = 0u;
    increment_saturating(&stats->failures);
    increment_saturating(&stats->injected_failures);
    return false;
}

bool kernel_allocation_commit(KernelAllocationSite site, uint32_t units,
                              uint32_t bytes, uint32_t owner)
{
    KernelAllocationStats *stats;
    KernelAllocationTag tag;
    uint32_t next_units;
    uint32_t next_bytes;
    uint32_t next_tag_units;
    uint32_t next_tag_bytes;

    ensure_initialized();
    if (!valid_site(site) || units == 0u || bytes == 0u ||
        !add_checked(site_stats[site].current_units, units, &next_units) ||
        !add_checked(site_stats[site].current_bytes, bytes, &next_bytes)) {
        corrupt = 1u;
        return false;
    }
    tag = (KernelAllocationTag)site_info[site].tag;
    if (!add_checked(tag_current_units[tag], units, &next_tag_units) ||
        !add_checked(tag_current_bytes[tag], bytes, &next_tag_bytes)) {
        corrupt = 1u;
        return false;
    }
    stats = &site_stats[site];
    stats->current_units = next_units;
    stats->current_bytes = next_bytes;
    stats->last_owner = owner;
    increment_saturating(&stats->successes);
    if (stats->current_units > stats->peak_units)
        stats->peak_units = stats->current_units;
    if (stats->current_bytes > stats->peak_bytes)
        stats->peak_bytes = stats->current_bytes;
    tag_current_units[tag] = next_tag_units;
    tag_current_bytes[tag] = next_tag_bytes;
    if (next_tag_units > tag_peak_units[tag])
        tag_peak_units[tag] = next_tag_units;
    if (next_tag_bytes > tag_peak_bytes[tag])
        tag_peak_bytes[tag] = next_tag_bytes;
    return true;
}

void kernel_allocation_fail(KernelAllocationSite site, uint32_t owner)
{
    ensure_initialized();
    if (!valid_site(site)) {
        corrupt = 1u;
        return;
    }
    site_stats[site].last_owner = owner;
    increment_saturating(&site_stats[site].failures);
}

bool kernel_allocation_release(KernelAllocationSite site, uint32_t units,
                               uint32_t bytes)
{
    KernelAllocationStats *stats;
    KernelAllocationTag tag;

    ensure_initialized();
    if (!valid_site(site) || units == 0u || bytes == 0u) {
        corrupt = 1u;
        return false;
    }
    stats = &site_stats[site];
    tag = (KernelAllocationTag)site_info[site].tag;
    if (units > stats->current_units || bytes > stats->current_bytes ||
        units > tag_current_units[tag] || bytes > tag_current_bytes[tag]) {
        corrupt = 1u;
        return false;
    }
    stats->current_units -= units;
    stats->current_bytes -= bytes;
    tag_current_units[tag] -= units;
    tag_current_bytes[tag] -= bytes;
    increment_saturating(&stats->releases);
    return true;
}

bool kernel_allocation_seed(KernelAllocationSite site, uint32_t units,
                            uint32_t bytes)
{
    ensure_initialized();
    if (!valid_site(site) || site_info[site].injectable != 0u)
        return false;
    return kernel_allocation_commit(site, units, bytes, 0u);
}

bool kernel_allocation_site_info(KernelAllocationSite site,
                                 KernelAllocationSiteInfo *info)
{
    if (!valid_site(site) || info == NULL)
        return false;
    info->name = site_info[site].name;
    info->tag = site_info[site].tag;
    info->boot_only = site_info[site].boot_only;
    info->injectable = site_info[site].injectable;
    info->reserved = site_info[site].reserved;
    return true;
}

bool kernel_allocation_site_stats(KernelAllocationSite site,
                                  KernelAllocationStats *stats)
{
    ensure_initialized();
    if (!valid_site(site) || stats == NULL)
        return false;
    copy_stats(stats, &site_stats[site]);
    return true;
}

bool kernel_allocation_tag_stats(KernelAllocationTag tag,
                                 KernelAllocationStats *stats)
{
    KernelAllocationStats total;

    ensure_initialized();
    if (tag <= KERNEL_ALLOCATION_TAG_INVALID ||
        tag >= KERNEL_ALLOCATION_TAG_COUNT || stats == NULL)
        return false;
    clear_stats(&total);
    for (uint32_t site = 1u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site) {
        const KernelAllocationStats *source = &site_stats[site];

        if (site_info[site].tag != tag)
            continue;
        if (!add_checked(total.attempts, source->attempts,
                         &total.attempts) ||
            !add_checked(total.successes, source->successes,
                         &total.successes) ||
            !add_checked(total.failures, source->failures,
                         &total.failures) ||
            !add_checked(total.injected_failures,
                         source->injected_failures,
                         &total.injected_failures) ||
            !add_checked(total.releases, source->releases,
                         &total.releases) ||
            !add_checked(total.current_units, source->current_units,
                         &total.current_units) ||
            !add_checked(total.current_bytes, source->current_bytes,
                         &total.current_bytes))
            return false;
        if (source->last_owner != 0u)
            total.last_owner = source->last_owner;
    }
    total.peak_units = tag_peak_units[tag];
    total.peak_bytes = tag_peak_bytes[tag];
    if (total.current_units != tag_current_units[tag] ||
        total.current_bytes != tag_current_bytes[tag])
        return false;
    copy_stats(stats, &total);
    return true;
}

bool kernel_allocation_valid(void)
{
    uint32_t observed_units[KERNEL_ALLOCATION_TAG_COUNT];
    uint32_t observed_bytes[KERNEL_ALLOCATION_TAG_COUNT];

    ensure_initialized();
    if (corrupt != 0u)
        return false;
    for (uint32_t tag = 0u; tag < KERNEL_ALLOCATION_TAG_COUNT; ++tag) {
        observed_units[tag] = 0u;
        observed_bytes[tag] = 0u;
    }
    for (uint32_t site = 1u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site) {
        const KernelAllocationStats *stats = &site_stats[site];

        if (stats->current_units > stats->peak_units ||
            stats->current_bytes > stats->peak_bytes ||
            stats->injected_failures > stats->failures)
            return false;
        KernelAllocationTag tag =
            (KernelAllocationTag)site_info[site].tag;
        if (!add_checked(observed_units[tag], stats->current_units,
                         &observed_units[tag]) ||
            !add_checked(observed_bytes[tag], stats->current_bytes,
                         &observed_bytes[tag]))
            return false;
    }
    for (uint32_t tag = 1u; tag < KERNEL_ALLOCATION_TAG_COUNT; ++tag) {
        if (observed_units[tag] != tag_current_units[tag] ||
            observed_bytes[tag] != tag_current_bytes[tag] ||
            tag_current_units[tag] > tag_peak_units[tag] ||
            tag_current_bytes[tag] > tag_peak_bytes[tag])
            return false;
    }
    return true;
}

void kernel_allocation_test_fail_global(uint32_t attempt)
{
    ensure_initialized();
    failure_selector.target = attempt;
    failure_selector.observed = 0u;
    failure_selector.site = KERNEL_ALLOCATION_SITE_INVALID;
    failure_selector.enabled = attempt != 0u ? 1u : 0u;
}

void kernel_allocation_test_fail_site(KernelAllocationSite site,
                                      uint32_t attempt)
{
    ensure_initialized();
    failure_selector.target = attempt;
    failure_selector.observed = 0u;
    failure_selector.site = valid_site(site) &&
                            site_info[site].injectable != 0u ?
        (uint8_t)site : KERNEL_ALLOCATION_SITE_INVALID;
    failure_selector.enabled = attempt != 0u &&
                               failure_selector.site !=
                                   KERNEL_ALLOCATION_SITE_INVALID ? 1u : 0u;
}

void kernel_allocation_test_clear_failure(void)
{
    ensure_initialized();
    failure_selector.target = 0u;
    failure_selector.observed = 0u;
    failure_selector.site = KERNEL_ALLOCATION_SITE_INVALID;
    failure_selector.enabled = 0u;
}
