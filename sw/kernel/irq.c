#include "irq.h"

#define ASTRA_INTEGER_FORCE_INLINE 1
#include <astra/integer.h>
#undef ASTRA_INTEGER_FORCE_INLINE
#include <astra/syscall.h>

#include "bytes.h"
#include "generation.h"
#include "object_cache.h"
#include "performance.h"
#include "trace.h"

#include <stddef.h>

#define KERNEL_IRQ_FLAG_MASKED       (1u << 4)
#define KERNEL_IRQ_FLAG_QUIESCED     (1u << 5)
#define KERNEL_IRQ_FLAG_ADMIN_MASKED (1u << 6)
#define KERNEL_IRQ_FLAG_DEVICE_COMPLETED (1u << 7)
#define KERNEL_IRQ_PUBLIC_FLAGS \
    (KERNEL_IRQ_EVENT_OVERFLOW | KERNEL_IRQ_EVENT_STORM | \
     KERNEL_IRQ_EVENT_DEVICE_ERROR)

struct KernelIrqEndpoint {
    KernelThreadWaitQueue waiters;
    KernelIrqRecord records[KERNEL_IRQ_RECORD_DEPTH];
    uint32_t owner;
    uint32_t generation;
    uint32_t delivered;
    uint32_t acknowledged;
    uint32_t dropped;
    uint32_t next_sequence;
    uint16_t references;
    uint8_t source;
    uint8_t state;
    uint8_t trigger;
    uint8_t ipl;
    uint8_t vector;
    uint8_t head;
    uint8_t tail;
    uint8_t record_count;
    uint8_t flags;
    uint8_t consecutive;
    uint8_t reserved[8];
    uint32_t storm_window_high;
    uint32_t storm_window_low;
};

typedef struct KernelIrqRoute {
    KernelIrqEndpoint *endpoint;
    KernelIrqInternalService internal_service;
    KernelIrqCapture capture;
    KernelIrqComplete complete;
    KernelIrqQuiesce quiesce;
    void *context;
    uint8_t trigger;
    uint8_t ipl;
    uint8_t vector;
    uint8_t internal_armed;
} KernelIrqRoute;

static KernelIrqEndpoint endpoints[KERNEL_IRQ_ENDPOINT_MAX];
static KernelIrqRoute routes[KERNEL_IRQ_SOURCE_COUNT];
static KernelObjectCache endpoint_cache;
static uint32_t endpoint_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_IRQ_ENDPOINT_MAX)];
static KernelIrqControllerOps controller;
static KernelIrqPoolStats pool_stats;
static uint8_t pool_initialized;
static uint8_t pool_corrupt;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(KernelIrqEndpoint) == 128u,
               "IRQ endpoint memory budget changed");
#endif
_Static_assert(sizeof(KernelIrqRecord) == 16u,
               "IRQ record ABI size changed");

static uint16_t endpoint_trace_flags(const KernelIrqEndpoint *endpoint)
{
    uint16_t flags = endpoint->source & KERNEL_IRQ_TRACE_SOURCE_MASK;

    flags |= (uint16_t)((endpoint->state << KERNEL_IRQ_TRACE_STATE_SHIFT) &
                        KERNEL_IRQ_TRACE_STATE_MASK);
    flags |= (uint16_t)(((endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS) <<
                         KERNEL_IRQ_TRACE_EVENT_SHIFT) &
                        KERNEL_IRQ_TRACE_EVENT_MASK);
    if (endpoint->trigger == KERNEL_IRQ_TRIGGER_EDGE)
        flags |= KERNEL_IRQ_TRACE_EDGE;
    return flags;
}

static uint32_t endpoint_slot(const KernelIrqEndpoint *endpoint)
{
    return (uint32_t)(endpoint - &endpoints[0]);
}

static void trace_endpoint(uint8_t level, KernelTraceEvent event,
                           const KernelIrqEndpoint *endpoint,
                           uint32_t argument2, uint32_t argument3)
{
    KERNEL_TRACE(level, event, endpoint_trace_flags(endpoint),
                 endpoint->owner, endpoint->generation,
                 argument2, argument3);
}

static void set_dispatch_trace(KernelIrqTrace *trace, uint8_t level,
                               KernelTraceEvent event, uint16_t flags,
                               uint32_t argument0, uint32_t argument1,
                               uint32_t argument2, uint32_t argument3)
{
    if (trace == NULL)
        return;
    trace->level = level;
    trace->argument[0] = argument0;
    trace->argument[1] = argument1;
    trace->argument[2] = argument2;
    trace->argument[3] = argument3;
    trace->event = (uint16_t)event;
    trace->flags = flags;
    trace->valid = 1u;
}

static void trace_quarantine(uint8_t source, KernelIrqStatus reason,
                             const KernelIrqEndpoint *endpoint,
                             KernelIrqTrace *trace)
{
    uint16_t flags = source & KERNEL_IRQ_TRACE_SOURCE_MASK;
    uint32_t owner = 0u;
    uint32_t generation = 0u;

    if (endpoint != NULL) {
        flags = endpoint_trace_flags(endpoint);
        owner = endpoint->owner;
        generation = endpoint->generation;
    }
    /*
     * A quarantine is why a device stopped serving, so it survives a release
     * build. It is the one IRQ record that does.
     */
    set_dispatch_trace(trace, KERNEL_TRACE_LEVEL_WARNING,
                       KERNEL_TRACE_EVENT_IRQ_QUARANTINE, flags,
                       source, (uint32_t)reason, owner, generation);
}

static bool valid_trigger(uint8_t trigger)
{
    return trigger == KERNEL_IRQ_TRIGGER_LEVEL ||
           trigger == KERNEL_IRQ_TRIGGER_EDGE;
}

static bool valid_endpoint_pointer(const KernelIrqEndpoint *endpoint)
{
    uintptr_t address = (uintptr_t)endpoint;
    uintptr_t first = (uintptr_t)&endpoints[0];
    uintptr_t limit = (uintptr_t)&endpoints[KERNEL_IRQ_ENDPOINT_MAX];

    return endpoint != NULL && address >= first && address < limit &&
           (address - first) % sizeof(endpoints[0]) == 0u;
}

static bool active_state(uint8_t state)
{
    return state == KERNEL_IRQ_MASKED || state == KERNEL_IRQ_ARMED ||
           state == KERNEL_IRQ_PENDING;
}

static bool route_matches(const KernelIrqEndpoint *endpoint)
{
    return endpoint->source < KERNEL_IRQ_SOURCE_COUNT &&
           routes[endpoint->source].endpoint == endpoint;
}

static bool route_in_use(uint8_t source)
{
    return routes[source].endpoint != NULL ||
           routes[source].internal_service != NULL;
}

static bool valid_active_endpoint(const KernelIrqEndpoint *endpoint)
{
    uint32_t waiters;

    if (!valid_endpoint_pointer(endpoint) || !active_state(endpoint->state) ||
        endpoint->owner == 0u || endpoint->generation == 0u ||
        endpoint->references == 0u ||
        endpoint->source >= KERNEL_IRQ_SOURCE_COUNT ||
        !valid_trigger(endpoint->trigger) || endpoint->ipl == 0u ||
        endpoint->ipl > 7u || endpoint->vector != KERNEL_IRQ_COMMON_VECTOR ||
        endpoint->head >= KERNEL_IRQ_RECORD_DEPTH ||
        endpoint->tail >= KERNEL_IRQ_RECORD_DEPTH ||
        endpoint->record_count > KERNEL_IRQ_RECORD_DEPTH ||
        endpoint->next_sequence == 0u || !route_matches(endpoint))
        return false;
    waiters = kernel_thread_wait_queue_count(&endpoint->waiters);
    return waiters != UINT32_MAX && waiters <= KERNEL_IRQ_WAITER_MAX;
}

static uint32_t endpoint_live_count(void)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        if (endpoints[slot].state != KERNEL_IRQ_FREE)
            ++count;
    }
    return count;
}

static uint32_t owner_endpoint_count(uint32_t owner)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        if (endpoints[slot].state != KERNEL_IRQ_FREE &&
            endpoints[slot].owner == owner)
            ++count;
    }
    return count;
}

static inline __attribute__((always_inline)) bool
controller_mask(uint8_t source)
{
    if (!controller.mask(source, controller.context)) {
        astra_u32_increment_saturating(&pool_stats.controller_failures);
        return false;
    }
    return true;
}

static inline __attribute__((always_inline)) bool
controller_configure(uint8_t source, uint8_t trigger, uint8_t ipl,
                     uint8_t vector)
{
    if (!controller.configure(source, trigger, ipl, vector,
                              controller.context)) {
        astra_u32_increment_saturating(&pool_stats.controller_failures);
        return false;
    }
    return true;
}

static inline __attribute__((always_inline)) bool
controller_enable(uint8_t source)
{
    if (!controller.enable(source, controller.context)) {
        astra_u32_increment_saturating(&pool_stats.controller_failures);
        return false;
    }
    return true;
}

static inline __attribute__((always_inline)) bool
controller_acknowledge(uint8_t source)
{
    if (!controller.acknowledge(source, controller.context)) {
        astra_u32_increment_saturating(&pool_stats.controller_failures);
        return false;
    }
    return true;
}

static void clear_route(uint8_t source)
{
    routes[source].endpoint = NULL;
    routes[source].internal_service = NULL;
    routes[source].capture = NULL;
    routes[source].complete = NULL;
    routes[source].quiesce = NULL;
    routes[source].context = NULL;
    routes[source].trigger = 0u;
    routes[source].ipl = 0u;
    routes[source].vector = 0u;
    routes[source].internal_armed = 0u;
}

static bool finish_quiesce(KernelIrqEndpoint *endpoint)
{
    KernelIrqRoute *route;

    if (!valid_endpoint_pointer(endpoint) ||
        endpoint->state != KERNEL_IRQ_REVOKING ||
        endpoint->source >= KERNEL_IRQ_SOURCE_COUNT)
        return false;
    if ((endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) != 0u)
        return true;
    route = &routes[endpoint->source];
    if (route->endpoint != endpoint)
        return false;
    if (!controller_mask(endpoint->source))
        return false;
    endpoint->flags |= KERNEL_IRQ_FLAG_MASKED;
    if (route->quiesce != NULL &&
        !route->quiesce(endpoint->source, route->context)) {
        astra_u32_increment_saturating(&pool_stats.device_failures);
        return false;
    }
    if (!controller_acknowledge(endpoint->source))
        return false;
    endpoint->flags |= KERNEL_IRQ_FLAG_QUIESCED;
    trace_endpoint(KERNEL_TRACE_LEVEL_NOTICE,
                   KERNEL_TRACE_EVENT_DEVICE_RESET, endpoint,
                   endpoint->source, 0u);
    clear_route(endpoint->source);
    return true;
}

static bool release_endpoint(KernelIrqEndpoint *endpoint)
{
    uint32_t generation;

    if (!valid_endpoint_pointer(endpoint) ||
        endpoint->state != KERNEL_IRQ_REVOKING ||
        endpoint->references != 0u ||
        (endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) == 0u ||
        endpoint->source >= KERNEL_IRQ_SOURCE_COUNT ||
        routes[endpoint->source].endpoint != NULL ||
        kernel_thread_wait_queue_count(&endpoint->waiters) != 0u)
        return false;
    generation = endpoint->generation;
    kernel_bytes_clear(endpoint, sizeof(*endpoint));
    kernel_thread_wait_queue_init(&endpoint->waiters);
    endpoint->generation = generation;
    endpoint->state = KERNEL_IRQ_FREE;
    if (kernel_object_cache_release(&endpoint_cache, endpoint) !=
        KERNEL_OBJECT_CACHE_OK)
        return false;
    if (pool_stats.live_endpoints == 0u ||
        pool_stats.revoking_endpoints == 0u)
        return false;
    --pool_stats.live_endpoints;
    --pool_stats.revoking_endpoints;
    return true;
}

static KernelIrqStatus finalize_if_possible(KernelIrqEndpoint *endpoint)
{
    if (!finish_quiesce(endpoint))
        return KERNEL_IRQ_DEVICE_ERROR;
    if (endpoint->references == 0u && !release_endpoint(endpoint)) {
        pool_corrupt = 1u;
        return KERNEL_IRQ_CORRUPT;
    }
    return KERNEL_IRQ_OK;
}

bool kernel_irq_pool_init(const KernelIrqControllerOps *ops)
{
    if (ops == NULL || ops->configure == NULL || ops->mask == NULL ||
        ops->enable == NULL || ops->acknowledge == NULL)
        return false;
    if (!kernel_object_cache_init(
            &endpoint_cache, endpoints, sizeof(endpoints[0]),
            KERNEL_IRQ_ENDPOINT_MAX, endpoint_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_IRQ_ENDPOINT_MAX),
            KERNEL_ALLOCATION_SITE_IRQ_ENDPOINT)) {
        pool_corrupt = 1u;
        return false;
    }
    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        uint32_t generation = endpoints[slot].generation;

        kernel_bytes_clear(&endpoints[slot], sizeof(endpoints[slot]));
        kernel_thread_wait_queue_init(&endpoints[slot].waiters);
        endpoints[slot].generation = generation;
        endpoints[slot].state = KERNEL_IRQ_FREE;
    }
    for (uint32_t source = 0u; source < KERNEL_IRQ_SOURCE_COUNT; ++source)
        clear_route((uint8_t)source);
    kernel_bytes_copy(&controller, ops, sizeof(controller));
    kernel_bytes_clear(&pool_stats, sizeof(pool_stats));
    pool_corrupt = 0u;
    pool_initialized = 1u;
    return true;
}

KernelIrqStatus kernel_irq_bind_internal(
    const KernelIrqInternalBinding *binding)
{
    bool configured;
    bool masked;
    KernelIrqRoute *route;

    if (binding == NULL || binding->service == NULL ||
        binding->source >= KERNEL_IRQ_SOURCE_COUNT ||
        !valid_trigger(binding->trigger) || binding->ipl == 0u ||
        binding->ipl > 7u || binding->vector != KERNEL_IRQ_COMMON_VECTOR)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (pool_initialized == 0u || pool_corrupt != 0u)
        return KERNEL_IRQ_CORRUPT;
    if (route_in_use(binding->source)) {
        astra_u32_increment_saturating(&pool_stats.source_busy_failures);
        return KERNEL_IRQ_SOURCE_BUSY;
    }
    configured = controller_configure(
        binding->source, binding->trigger, binding->ipl, binding->vector);
    masked = controller_mask(binding->source);
    if (!configured || !masked)
        return KERNEL_IRQ_DEVICE_ERROR;
    route = &routes[binding->source];
    route->internal_service = binding->service;
    route->context = binding->context;
    route->trigger = binding->trigger;
    route->ipl = binding->ipl;
    route->vector = binding->vector;
    route->internal_armed = 0u;
    astra_u32_increment_saturating(&pool_stats.internal_routes);
    KERNEL_TRACE(
        KERNEL_TRACE_LEVEL_NOTICE, KERNEL_TRACE_EVENT_IRQ_BIND,
        KERNEL_IRQ_TRACE_INTERNAL |
            (binding->source & KERNEL_IRQ_TRACE_SOURCE_MASK),
        binding->source, binding->trigger, binding->ipl, binding->vector);
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_arm_internal(uint8_t source)
{
    KernelIrqRoute *route;

    if (source >= KERNEL_IRQ_SOURCE_COUNT)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    route = &routes[source];
    if (route->endpoint != NULL || route->internal_service == NULL)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (route->internal_armed != 0u)
        return KERNEL_IRQ_INVALID_STATE;
    if (!controller_enable(source))
        return KERNEL_IRQ_DEVICE_ERROR;
    route->internal_armed = 1u;
    KERNEL_TRACE(
        KERNEL_TRACE_LEVEL_DEBUG, KERNEL_TRACE_EVENT_IRQ_ARM,
        KERNEL_IRQ_TRACE_INTERNAL |
            (source & KERNEL_IRQ_TRACE_SOURCE_MASK),
        source, 1u, route->ipl, route->vector);
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_mask_internal(uint8_t source)
{
    KernelIrqRoute *route;

    if (source >= KERNEL_IRQ_SOURCE_COUNT)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    route = &routes[source];
    if (route->endpoint != NULL || route->internal_service == NULL)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (!controller_mask(source))
        return KERNEL_IRQ_DEVICE_ERROR;
    route->internal_armed = 0u;
    KERNEL_TRACE(
        KERNEL_TRACE_LEVEL_DEBUG, KERNEL_TRACE_EVENT_IRQ_ARM,
        KERNEL_IRQ_TRACE_INTERNAL |
            (source & KERNEL_IRQ_TRACE_SOURCE_MASK),
        source, 0u, route->ipl, route->vector);
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_bind(uint32_t owner,
                                const KernelIrqBinding *binding,
                                KernelIrqEndpoint **endpoint)
{
    KernelObjectCacheStatus cache_status;
    KernelIrqEndpoint *candidate;
    void *raw_endpoint;
    uint16_t slot;
    uint32_t generation;
    uint32_t live;

    if (endpoint == NULL || binding == NULL || owner == 0u ||
        binding->source >= KERNEL_IRQ_SOURCE_COUNT ||
        !valid_trigger(binding->trigger) || binding->ipl == 0u ||
        binding->ipl > 7u || binding->vector != KERNEL_IRQ_COMMON_VECTOR)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    *endpoint = NULL;
    if (pool_initialized == 0u || pool_corrupt != 0u)
        return KERNEL_IRQ_CORRUPT;
    if (route_in_use(binding->source)) {
        astra_u32_increment_saturating(&pool_stats.source_busy_failures);
        return KERNEL_IRQ_SOURCE_BUSY;
    }
    if (owner_endpoint_count(owner) >= KERNEL_IRQ_OWNER_MAX) {
        astra_u32_increment_saturating(&pool_stats.quota_failures);
        return KERNEL_IRQ_QUOTA_EXCEEDED;
    }
    cache_status = kernel_object_cache_claim(
        &endpoint_cache, owner, &raw_endpoint, &slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE) {
        astra_u32_increment_saturating(&pool_stats.allocation_failures);
        return KERNEL_IRQ_NO_SLOT;
    }
    if (cache_status != KERNEL_OBJECT_CACHE_OK ||
        slot >= KERNEL_IRQ_ENDPOINT_MAX) {
        pool_corrupt = 1u;
        return KERNEL_IRQ_CORRUPT;
    }
    candidate = raw_endpoint;
    if (candidate->state != KERNEL_IRQ_FREE) {
        pool_corrupt = 1u;
        return KERNEL_IRQ_CORRUPT;
    }
    generation = kernel_generation_next(candidate->generation);
    kernel_bytes_clear(candidate, sizeof(*candidate));
    kernel_thread_wait_queue_init(&candidate->waiters);
    candidate->owner = owner;
    candidate->generation = generation;
    candidate->next_sequence = 1u;
    candidate->references = 1u;
    candidate->source = binding->source;
    candidate->state = KERNEL_IRQ_MASKED;
    candidate->trigger = binding->trigger;
    candidate->ipl = binding->ipl;
    candidate->vector = binding->vector;
    candidate->flags = KERNEL_IRQ_FLAG_MASKED |
                       KERNEL_IRQ_FLAG_ADMIN_MASKED;

    bool configured = controller_configure(
        binding->source, binding->trigger, binding->ipl, binding->vector);
    bool masked = controller_mask(binding->source);

    if (!configured || !masked) {
        kernel_bytes_clear(candidate, sizeof(*candidate));
        kernel_thread_wait_queue_init(&candidate->waiters);
        candidate->generation = generation;
        candidate->state = KERNEL_IRQ_FREE;
        if (kernel_object_cache_release(&endpoint_cache, candidate) !=
            KERNEL_OBJECT_CACHE_OK)
            pool_corrupt = 1u;
        return pool_corrupt != 0u ? KERNEL_IRQ_CORRUPT :
                                    KERNEL_IRQ_DEVICE_ERROR;
    }
    routes[binding->source].endpoint = candidate;
    routes[binding->source].capture = binding->capture;
    routes[binding->source].complete = binding->complete;
    routes[binding->source].quiesce = binding->quiesce;
    routes[binding->source].context = binding->context;
    astra_u32_increment_saturating(&pool_stats.created_endpoints);
    ++pool_stats.live_endpoints;
    live = endpoint_live_count();
    if (live > pool_stats.max_live_endpoints)
        pool_stats.max_live_endpoints = live;
    *endpoint = candidate;
    trace_endpoint(KERNEL_TRACE_LEVEL_NOTICE,
                   KERNEL_TRACE_EVENT_IRQ_BIND, candidate,
                   endpoint_slot(candidate),
                   ((uint32_t)candidate->trigger << 24) |
                       ((uint32_t)candidate->ipl << 16) |
                       candidate->vector);
    return KERNEL_IRQ_OK;
}

bool kernel_irq_handle_retain(void *object, void *context)
{
    KernelIrqEndpoint *endpoint = object;

    (void)context;
    if (!valid_active_endpoint(endpoint) ||
        endpoint->references == UINT16_MAX)
        return false;
    ++endpoint->references;
    return true;
}

void kernel_irq_handle_release(void *object, void *context)
{
    KernelIrqEndpoint *endpoint = object;
    uint32_t woken = 0u;

    (void)context;
    if (!valid_endpoint_pointer(endpoint) ||
        endpoint->state == KERNEL_IRQ_FREE || endpoint->references == 0u) {
        pool_corrupt = 1u;
        return;
    }
    if (endpoint->references > 1u) {
        --endpoint->references;
        return;
    }
    if (endpoint->state != KERNEL_IRQ_REVOKING) {
        KernelIrqStatus status = kernel_irq_revoke(endpoint, &woken);

        if (status == KERNEL_IRQ_CORRUPT) {
            pool_corrupt = 1u;
            return;
        }
    }
    endpoint->references = 0u;
    if ((endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) != 0u &&
        !release_endpoint(endpoint))
        pool_corrupt = 1u;
}

void kernel_irq_abandon_unpublished(KernelIrqEndpoint *endpoint)
{
    if (!valid_active_endpoint(endpoint) || endpoint->references != 1u ||
        endpoint->record_count != 0u ||
        kernel_thread_wait_queue_count(&endpoint->waiters) != 0u) {
        pool_corrupt = 1u;
        return;
    }
    astra_u32_increment_saturating(&pool_stats.publication_rollbacks);
    kernel_irq_handle_release(endpoint, NULL);
}

KernelIrqStatus kernel_irq_arm(KernelIrqEndpoint *endpoint)
{
    if (valid_endpoint_pointer(endpoint) &&
        endpoint->state == KERNEL_IRQ_REVOKING)
        return KERNEL_IRQ_CLOSED;
    if (!valid_active_endpoint(endpoint))
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (endpoint->state != KERNEL_IRQ_MASKED || endpoint->record_count != 0u ||
        (endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS) != 0u)
        return KERNEL_IRQ_INVALID_STATE;
    if (!controller_enable(endpoint->source)) {
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR;
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    endpoint->flags &= (uint8_t)~(KERNEL_IRQ_FLAG_MASKED |
                                  KERNEL_IRQ_FLAG_ADMIN_MASKED);
    endpoint->state = KERNEL_IRQ_ARMED;
    trace_endpoint(KERNEL_TRACE_LEVEL_DEBUG,
                   KERNEL_TRACE_EVENT_IRQ_ARM, endpoint,
                   endpoint_slot(endpoint), endpoint->record_count);
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_mask(KernelIrqEndpoint *endpoint)
{
    if (valid_endpoint_pointer(endpoint) &&
        endpoint->state == KERNEL_IRQ_REVOKING)
        return KERNEL_IRQ_CLOSED;
    if (!valid_active_endpoint(endpoint))
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (!controller_mask(endpoint->source)) {
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR;
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    endpoint->flags |= KERNEL_IRQ_FLAG_MASKED |
                       KERNEL_IRQ_FLAG_ADMIN_MASKED;
    endpoint->state = endpoint->record_count == 0u ?
        KERNEL_IRQ_MASKED : KERNEL_IRQ_PENDING;
    trace_endpoint(KERNEL_TRACE_LEVEL_DEBUG,
                   KERNEL_TRACE_EVENT_IRQ_ARM, endpoint,
                   endpoint_slot(endpoint), endpoint->record_count);
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_recover(KernelIrqEndpoint *endpoint)
{
    if (valid_endpoint_pointer(endpoint) &&
        endpoint->state == KERNEL_IRQ_REVOKING)
        return KERNEL_IRQ_CLOSED;
    if (!valid_active_endpoint(endpoint))
        return KERNEL_IRQ_INVALID_ARGUMENT;
    if (endpoint->record_count != 0u)
        return KERNEL_IRQ_INVALID_STATE;
    endpoint->flags &= (uint8_t)~KERNEL_IRQ_PUBLIC_FLAGS;
    endpoint->consecutive = 0u;
    endpoint->state = KERNEL_IRQ_MASKED;
    return kernel_irq_arm(endpoint);
}

static KernelIrqStatus quarantine_unclaimed(uint8_t source,
                                             KernelIrqTrace *trace)
{
    bool masked = controller_mask(source);
    bool acknowledged = controller_acknowledge(source);
    KernelIrqStatus status = masked && acknowledged ?
        KERNEL_IRQ_UNCLAIMED : KERNEL_IRQ_DEVICE_ERROR;

    astra_u32_increment_saturating(&pool_stats.unclaimed_interrupts);
    trace_quarantine(source, status, NULL, trace);
    return status;
}

static KernelIrqStatus quarantine_masked(KernelIrqEndpoint *endpoint,
                                         bool revoking,
                                         KernelIrqTrace *trace)
{
    bool masked = controller_mask(endpoint->source);
    bool acknowledged = controller_acknowledge(endpoint->source);
    KernelIrqStatus status;

    if (revoking)
        astra_u32_increment_saturating(&pool_stats.revoking_interrupts);
    else
        astra_u32_increment_saturating(&pool_stats.masked_interrupts);
    if (!masked || !acknowledged)
        status = KERNEL_IRQ_DEVICE_ERROR;
    else
        status = revoking ? KERNEL_IRQ_CLOSED : KERNEL_IRQ_INVALID_STATE;
    trace_quarantine(endpoint->source, status, endpoint, trace);
    return status;
}

static void note_storm_delivery(KernelIrqEndpoint *endpoint,
                                uint64_t timestamp)
{
    uint64_t window_start =
        ((uint64_t)endpoint->storm_window_high << 32) |
        endpoint->storm_window_low;

    if (endpoint->consecutive == 0u || timestamp < window_start ||
        timestamp - window_start > KERNEL_IRQ_STORM_WINDOW_CYCLES) {
        endpoint->storm_window_high = (uint32_t)(timestamp >> 32);
        endpoint->storm_window_low = (uint32_t)timestamp;
        endpoint->consecutive = 1u;
        return;
    }
    if (endpoint->consecutive != UINT8_MAX)
        ++endpoint->consecutive;
}

static __attribute__((noinline)) KernelIrqStatus dispatch_internal(
    KernelIrqRoute *route, uint8_t source, uint8_t vector,
    uint64_t timestamp, bool source_claimed, KernelIrqTrace *trace)
{
    bool masked = true;

    if (route->vector != vector) {
        astra_u32_increment_saturating(&pool_stats.bad_vector_interrupts);
        route->internal_armed = 0u;
        masked = controller_mask(source);
        if (!controller_acknowledge(source) || !masked) {
            trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, NULL, trace);
            return KERNEL_IRQ_DEVICE_ERROR;
        }
        trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, NULL, trace);
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (route->internal_armed == 0u) {
        astra_u32_increment_saturating(&pool_stats.masked_interrupts);
        masked = controller_mask(source);
        if (!controller_acknowledge(source) || !masked) {
            trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, NULL, trace);
            return KERNEL_IRQ_DEVICE_ERROR;
        }
        trace_quarantine(source, KERNEL_IRQ_INVALID_STATE, NULL, trace);
        return KERNEL_IRQ_INVALID_STATE;
    }
    if (route->trigger == KERNEL_IRQ_TRIGGER_LEVEL) {
        masked = source_claimed || controller_mask(source);
        route->internal_armed = 0u;
        if (!masked)
            return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (!route->internal_service(source, timestamp, route->context)) {
        route->internal_armed = 0u;
        (void)controller_mask(source);
        (void)controller_acknowledge(source);
        astra_u32_increment_saturating(&pool_stats.device_failures);
        trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, NULL, trace);
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (!controller_acknowledge(source)) {
        route->internal_armed = 0u;
        (void)controller_mask(source);
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (route->trigger == KERNEL_IRQ_TRIGGER_LEVEL || source_claimed) {
        if (!controller_enable(source))
            return KERNEL_IRQ_DEVICE_ERROR;
    }
    route->internal_armed = 1u;
    astra_u32_increment_saturating(&pool_stats.internal_deliveries);
    set_dispatch_trace(
        trace, KERNEL_TRACE_LEVEL_DEBUG, KERNEL_TRACE_EVENT_IRQ_DELIVER,
        KERNEL_IRQ_TRACE_INTERNAL |
            (source & KERNEL_IRQ_TRACE_SOURCE_MASK),
        source, vector, 0u, 0u);
    return KERNEL_IRQ_OK;
}

static __attribute__((noinline)) KernelIrqStatus dispatch_endpoint(
    KernelIrqRoute *route, KernelIrqEndpoint *endpoint, uint8_t source,
    uint8_t vector, uint64_t timestamp, uint32_t *woken_threads,
    bool source_claimed, KernelIrqTrace *trace)
{
    KernelIrqRecord *record;
    uint32_t status = 0u;
    uint32_t woken = 0u;

    if (!valid_active_endpoint(endpoint)) {
        if (endpoint->state == KERNEL_IRQ_REVOKING)
            return quarantine_masked(endpoint, true, trace);
        pool_corrupt = 1u;
        return KERNEL_IRQ_CORRUPT;
    }
    if (vector != endpoint->vector) {
        astra_u32_increment_saturating(&pool_stats.bad_vector_interrupts);
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                           KERNEL_IRQ_FLAG_MASKED;
        endpoint->state = KERNEL_IRQ_PENDING;
        (void)controller_mask(source);
        (void)controller_acknowledge(source);
        trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, endpoint, trace);
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (endpoint->state == KERNEL_IRQ_MASKED ||
        (endpoint->flags & KERNEL_IRQ_FLAG_MASKED) != 0u)
        return quarantine_masked(endpoint, false, trace);

    if (endpoint->trigger == KERNEL_IRQ_TRIGGER_LEVEL) {
        if (!source_claimed && !controller_mask(source)) {
            endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR;
            trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, endpoint,
                             trace);
            return KERNEL_IRQ_DEVICE_ERROR;
        }
        endpoint->flags |= KERNEL_IRQ_FLAG_MASKED;
    }
    if (route->capture != NULL &&
        !route->capture(source, &status, route->context)) {
        /*
         * Nothing was pending, which is not a fault.
         *
         * A capture answers false for one reason -- its status register shows
         * no work -- and no capture in sw/kernel/platform.c has any way to
         * report a hardware failure, so false cannot mean one. This used to
         * set KERNEL_IRQ_EVENT_DEVICE_ERROR, which is sticky: kernel_irq_read
         * answers with it forever after, so a single spurious interrupt ended
         * the device for the rest of the boot.
         *
         * It is reachable whenever a driver polls for the same completion the
         * interrupt was raised for. astra_block_lease_collect does exactly
         * that, and beating the CPU to the queue killed storage partway
         * through a session: every transfer afterwards was ASTRA_BLOCK_IO_ERROR
         * and nothing brought it back. A spurious interrupt is ordinary on real
         * hardware. It is acknowledged, counted, and survived.
         *
         * A device that fails is still reported -- through `complete`, in
         * kernel_irq_ack, which is the callback a device answers with.
         */
        bool acknowledged = controller_acknowledge(source);
        bool re_enabled = true;

        if (endpoint->trigger == KERNEL_IRQ_TRIGGER_LEVEL) {
            /*
             * The mask taken above this call has to come back off. Leaving it
             * on would be worse than the quarantine it replaces: the endpoint
             * would go on believing it is armed while the source is masked,
             * and the next interrupt would never arrive at all.
             */
            endpoint->flags &= (uint8_t)~KERNEL_IRQ_FLAG_MASKED;
            re_enabled = controller_enable(source);
        }
        if (!acknowledged || !re_enabled) {
            endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                               KERNEL_IRQ_FLAG_MASKED;
            endpoint->state = KERNEL_IRQ_PENDING;
            (void)controller_mask(source);
            astra_u32_increment_saturating(&pool_stats.device_failures);
            trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, endpoint,
                             trace);
            return KERNEL_IRQ_DEVICE_ERROR;
        }
        astra_u32_increment_saturating(&pool_stats.unclaimed_interrupts);
        /*
         * One of these is ordinary; a flood of them is the storm.
         *
         * An interrupt that carries no work is the shape a wedged line takes:
         * it re-asserts, nothing is pending, and answering it produces nothing
         * to acknowledge -- so it would spin here forever, and no other guard
         * would see it. The record ring's overflow only catches deliveries
         * that queue, and these never queue. So the budget that used to be
         * spent on a busy device is spent here instead, where the interrupts
         * really are noise.
         */
        note_storm_delivery(endpoint, timestamp);
        if (endpoint->consecutive >= KERNEL_IRQ_STORM_BUDGET) {
            endpoint->flags |= KERNEL_IRQ_EVENT_STORM |
                               KERNEL_IRQ_FLAG_MASKED;
            (void)controller_mask(source);
            astra_u32_increment_saturating(&pool_stats.storm_quarantines);
            trace_quarantine(source, KERNEL_IRQ_STORM, endpoint, trace);
            return KERNEL_IRQ_STORM;
        }
        return KERNEL_IRQ_UNCLAIMED;
    }
    if (endpoint->trigger == KERNEL_IRQ_TRIGGER_EDGE &&
        !controller_acknowledge(source)) {
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                           KERNEL_IRQ_FLAG_MASKED;
        endpoint->state = KERNEL_IRQ_PENDING;
        (void)controller_mask(source);
        trace_quarantine(source, KERNEL_IRQ_DEVICE_ERROR, endpoint, trace);
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    if (endpoint->record_count == KERNEL_IRQ_RECORD_DEPTH) {
        astra_u32_increment_saturating(&endpoint->dropped);
        astra_u32_increment_saturating(&pool_stats.dropped_records);
        astra_u32_increment_saturating(&pool_stats.overflow_quarantines);
        endpoint->flags |= KERNEL_IRQ_EVENT_OVERFLOW |
                           KERNEL_IRQ_FLAG_MASKED;
        endpoint->state = KERNEL_IRQ_PENDING;
        (void)controller_mask(source);
        if (kernel_thread_wake_all(&endpoint->waiters, ASTRA_SYSCALL_OK,
                                   &woken) != KERNEL_THREAD_OK) {
            pool_corrupt = 1u;
            return KERNEL_IRQ_CORRUPT;
        }
        pool_stats.wait_wakeups += woken;
        *woken_threads = woken;
        trace_quarantine(source, KERNEL_IRQ_OVERFLOW, endpoint, trace);
        return KERNEL_IRQ_OVERFLOW;
    }

    record = &endpoint->records[endpoint->tail];
    record->timestamp_high = (uint32_t)(timestamp >> 32);
    record->timestamp_low = (uint32_t)timestamp;
    record->status = status;
    record->sequence = endpoint->next_sequence;
    endpoint->next_sequence = kernel_generation_next(endpoint->next_sequence);
    endpoint->tail = (uint8_t)((endpoint->tail + 1u) %
                               KERNEL_IRQ_RECORD_DEPTH);
    ++endpoint->record_count;
    astra_u32_increment_saturating(&endpoint->delivered);
    astra_u32_increment_saturating(&pool_stats.deliveries);
    note_storm_delivery(endpoint, timestamp);
    endpoint->state = KERNEL_IRQ_PENDING;
    if (endpoint->record_count > pool_stats.max_pending_records)
        pool_stats.max_pending_records = endpoint->record_count;
    if (endpoint->consecutive >= KERNEL_IRQ_STORM_BUDGET) {
        endpoint->flags |= KERNEL_IRQ_EVENT_STORM |
                           KERNEL_IRQ_FLAG_MASKED;
        (void)controller_mask(source);
        astra_u32_increment_saturating(&pool_stats.storm_quarantines);
        trace_quarantine(source, KERNEL_IRQ_STORM, endpoint, trace);
    }
    if (kernel_thread_wake_all_irq(&endpoint->waiters, ASTRA_SYSCALL_OK,
                                   &woken) != KERNEL_THREAD_OK) {
        pool_corrupt = 1u;
        return KERNEL_IRQ_CORRUPT;
    }
    pool_stats.wait_wakeups += woken;
    *woken_threads = woken;
    if (trace == NULL || trace->valid == 0u)
        set_dispatch_trace(
            trace, KERNEL_TRACE_LEVEL_DEBUG, KERNEL_TRACE_EVENT_IRQ_DELIVER,
            endpoint_trace_flags(endpoint), endpoint->owner,
            endpoint->generation, record->sequence, record->status);
    if (source_claimed &&
        endpoint->trigger == KERNEL_IRQ_TRIGGER_EDGE &&
        (endpoint->flags & (KERNEL_IRQ_EVENT_STORM |
                            KERNEL_IRQ_FLAG_MASKED)) == 0u &&
        !controller_enable(source)) {
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                           KERNEL_IRQ_FLAG_MASKED;
        endpoint->state = KERNEL_IRQ_PENDING;
        return KERNEL_IRQ_DEVICE_ERROR;
    }
    return (endpoint->flags & KERNEL_IRQ_EVENT_STORM) != 0u ?
        KERNEL_IRQ_STORM : KERNEL_IRQ_OK;
}

static KernelIrqStatus dispatch_common(uint8_t source, uint8_t vector,
                                       uint64_t timestamp,
                                       uint32_t *woken_threads,
                                       KernelIrqTrace *trace,
                                       bool source_claimed)
{
    KernelIrqEndpoint *endpoint;
    KernelIrqRoute *route;

    if (trace != NULL)
        trace->valid = 0u;
    if (woken_threads == NULL || source >= KERNEL_IRQ_SOURCE_COUNT)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    *woken_threads = 0u;
    if (pool_initialized == 0u || pool_corrupt != 0u)
        return KERNEL_IRQ_CORRUPT;
    route = &routes[source];
    endpoint = route->endpoint;
    if (endpoint == NULL && route->internal_service != NULL)
        return dispatch_internal(route, source, vector, timestamp,
                                 source_claimed, trace);
    if (endpoint == NULL)
        return quarantine_unclaimed(source, trace);
    return dispatch_endpoint(route, endpoint, source, vector, timestamp,
                             woken_threads, source_claimed, trace);
}

KernelIrqStatus kernel_irq_dispatch_traced(uint8_t source, uint8_t vector,
                                           uint64_t timestamp,
                                           uint32_t *woken_threads,
                                           KernelIrqTrace *trace)
{
    return dispatch_common(source, vector, timestamp, woken_threads, trace,
                           false);
}

KernelIrqStatus kernel_irq_dispatch_claimed_traced(
    uint8_t source, uint8_t vector, uint64_t timestamp,
    uint32_t *woken_threads, KernelIrqTrace *trace)
{
    return dispatch_common(source, vector, timestamp, woken_threads, trace,
                           true);
}

KernelIrqStatus kernel_irq_dispatch(uint8_t source, uint8_t vector,
                                    uint64_t timestamp,
                                    uint32_t *woken_threads)
{
    KernelIrqTrace trace;
    KernelIrqStatus status = kernel_irq_dispatch_traced(
        source, vector, timestamp, woken_threads, &trace);

    /*
     * The same drop the deferred writer makes, for the same reason: the level
     * travels with the staged record because the site that built it was inside
     * an interrupt and could not gate on it.
     */
    if (trace.valid != 0u && KERNEL_TRACE_KEEPS(trace.level))
        (void)kernel_trace_stage_at(
            (KernelTraceEvent)trace.event, trace.flags, timestamp,
            trace.argument[0], trace.argument[1], trace.argument[2],
            trace.argument[3]);
    return status;
}

KernelIrqStatus kernel_irq_read(KernelIrqEndpoint *endpoint,
                                KernelIrqRecord *record,
                                uint32_t *event_flags)
{
    KernelPerformanceToken performance = kernel_performance_begin(
        KERNEL_PERFORMANCE_IRQ_READ);
    KernelIrqStatus result;
    uint32_t flags;

    if (valid_endpoint_pointer(endpoint) &&
        endpoint->state == KERNEL_IRQ_REVOKING) {
        result = KERNEL_IRQ_CLOSED;
    } else if (!valid_active_endpoint(endpoint) || record == NULL ||
               event_flags == NULL) {
        result = KERNEL_IRQ_INVALID_ARGUMENT;
    } else {
        flags = endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS;
        *event_flags = flags;
        kernel_bytes_clear(record, sizeof(*record));
        if (endpoint->record_count != 0u) {
            kernel_bytes_copy(record, &endpoint->records[endpoint->head],
                              sizeof(*record));
            result = KERNEL_IRQ_OK;
        } else if ((flags & KERNEL_IRQ_EVENT_DEVICE_ERROR) != 0u) {
            result = KERNEL_IRQ_DEVICE_ERROR;
        } else if ((flags & KERNEL_IRQ_EVENT_STORM) != 0u) {
            result = KERNEL_IRQ_STORM;
        } else if ((flags & KERNEL_IRQ_EVENT_OVERFLOW) != 0u) {
            result = KERNEL_IRQ_OVERFLOW;
        } else {
            result = KERNEL_IRQ_WOULD_BLOCK;
        }
    }
    kernel_performance_end(performance);
    return result;
}

KernelIrqStatus kernel_irq_ack(KernelIrqEndpoint *endpoint,
                               uint32_t sequence)
{
    KernelPerformanceToken performance = kernel_performance_begin(
        KERNEL_PERFORMANCE_IRQ_ACK);
    KernelIrqStatus result;
    KernelIrqRoute *route;
    KernelIrqRecord *record;
    uint32_t acknowledged_sequence;

    if (valid_endpoint_pointer(endpoint) &&
        endpoint->state == KERNEL_IRQ_REVOKING) {
        result = KERNEL_IRQ_CLOSED;
        goto finished;
    }
    if (!valid_active_endpoint(endpoint) || sequence == 0u) {
        result = KERNEL_IRQ_INVALID_ARGUMENT;
        goto finished;
    }
    if (endpoint->record_count == 0u) {
        result = KERNEL_IRQ_WOULD_BLOCK;
        goto finished;
    }
    record = &endpoint->records[endpoint->head];
    if (record->sequence != sequence) {
        result = KERNEL_IRQ_SEQUENCE_MISMATCH;
        goto finished;
    }
    route = &routes[endpoint->source];
    if ((endpoint->flags & KERNEL_IRQ_FLAG_DEVICE_COMPLETED) == 0u) {
        if (route->complete != NULL &&
            !route->complete(endpoint->source, record, route->context)) {
            endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                               KERNEL_IRQ_FLAG_MASKED;
            (void)controller_mask(endpoint->source);
            astra_u32_increment_saturating(&pool_stats.device_failures);
            /*
             * Said out loud. These three are the only places that set the
             * sticky DEVICE_ERROR flag without leaving a record of doing it,
             * and a device that dies here dies silently: the endpoint answers
             * every later read with DEVICE_ERROR and nothing anywhere says
             * when it started or why. Finding that out once cost a session.
             */
            trace_endpoint(KERNEL_TRACE_LEVEL_WARNING,
                   KERNEL_TRACE_EVENT_IRQ_QUARANTINE, endpoint,
                           KERNEL_IRQ_DEVICE_ERROR, 1u);
            result = KERNEL_IRQ_DEVICE_ERROR;
            goto finished;
        }
        endpoint->flags |= KERNEL_IRQ_FLAG_DEVICE_COMPLETED;
    }
    if (endpoint->trigger == KERNEL_IRQ_TRIGGER_LEVEL &&
        !controller_acknowledge(endpoint->source)) {
        endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                           KERNEL_IRQ_FLAG_MASKED;
        trace_endpoint(KERNEL_TRACE_LEVEL_WARNING,
                   KERNEL_TRACE_EVENT_IRQ_QUARANTINE, endpoint,
                       KERNEL_IRQ_DEVICE_ERROR, 2u);
        result = KERNEL_IRQ_DEVICE_ERROR;
        goto finished;
    }
    acknowledged_sequence = record->sequence;
    endpoint->flags &= (uint8_t)~KERNEL_IRQ_FLAG_DEVICE_COMPLETED;
    kernel_bytes_clear(record, sizeof(*record));
    endpoint->head = (uint8_t)((endpoint->head + 1u) %
                               KERNEL_IRQ_RECORD_DEPTH);
    --endpoint->record_count;
    /*
     * The owner retired this interrupt, so the run of them stops here.
     *
     * **A serviced interrupt is work, not noise.** The storm counter used to
     * count every delivery in the window and be cleared by nothing except
     * kernel_irq_recover, so sixty-four completions that were each read and
     * acknowledged the moment they arrived quarantined the endpoint exactly as
     * hard as a line nobody was answering. A busy disk is not a storm: on the
     * machine this killed storage partway through a session -- one burst of
     * sequential transfers, every one of them handled -- and the volume never
     * came back, because the flag is sticky.
     *
     * What is left is what the word means: interrupts arriving that nobody
     * retires. Deliveries nobody drains are caught sooner by the record ring's
     * overflow, and deliveries that carry no work are counted where they are
     * recognised, in dispatch_endpoint.
     */
    endpoint->consecutive = 0u;
    astra_u32_increment_saturating(&endpoint->acknowledged);
    astra_u32_increment_saturating(&pool_stats.acknowledgements);
    trace_endpoint(KERNEL_TRACE_LEVEL_DEBUG,
                   KERNEL_TRACE_EVENT_IRQ_ACK, endpoint,
                   acknowledged_sequence, endpoint->record_count);
    if (endpoint->record_count != 0u) {
        endpoint->state = KERNEL_IRQ_PENDING;
        result = KERNEL_IRQ_OK;
        goto finished;
    }
    endpoint->head = 0u;
    endpoint->tail = 0u;
    if ((endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS) != 0u) {
        endpoint->state = KERNEL_IRQ_MASKED;
        endpoint->flags |= KERNEL_IRQ_FLAG_MASKED;
        result = (endpoint->flags & KERNEL_IRQ_EVENT_DEVICE_ERROR) != 0u ?
            KERNEL_IRQ_DEVICE_ERROR :
            ((endpoint->flags & KERNEL_IRQ_EVENT_STORM) != 0u ?
                 KERNEL_IRQ_STORM : KERNEL_IRQ_OVERFLOW);
        goto finished;
    }
    if ((endpoint->flags & KERNEL_IRQ_FLAG_ADMIN_MASKED) != 0u) {
        endpoint->state = KERNEL_IRQ_MASKED;
        result = KERNEL_IRQ_OK;
        goto finished;
    }
    if ((endpoint->flags & KERNEL_IRQ_FLAG_MASKED) != 0u) {
        if (!controller_enable(endpoint->source)) {
            endpoint->flags |= KERNEL_IRQ_EVENT_DEVICE_ERROR |
                               KERNEL_IRQ_FLAG_MASKED;
            endpoint->state = KERNEL_IRQ_MASKED;
            trace_endpoint(KERNEL_TRACE_LEVEL_WARNING,
                   KERNEL_TRACE_EVENT_IRQ_QUARANTINE, endpoint,
                           KERNEL_IRQ_DEVICE_ERROR, 3u);
            result = KERNEL_IRQ_DEVICE_ERROR;
            goto finished;
        }
        endpoint->flags &= (uint8_t)~KERNEL_IRQ_FLAG_MASKED;
    }
    endpoint->state = KERNEL_IRQ_ARMED;
    result = KERNEL_IRQ_OK;

finished:
    kernel_performance_end(performance);
    return result;
}

KernelIrqStatus kernel_irq_prepare_wait(KernelIrqEndpoint *endpoint,
                                        KernelThreadWaitSpec *spec)
{
    uint32_t waiters;

    if (!valid_endpoint_pointer(endpoint) || spec == NULL)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    if (endpoint->state == KERNEL_IRQ_REVOKING)
        return KERNEL_IRQ_CLOSED;
    if (!valid_active_endpoint(endpoint))
        return KERNEL_IRQ_CORRUPT;
    if (endpoint->record_count != 0u ||
        (endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS) != 0u)
        return KERNEL_IRQ_OK;
    waiters = kernel_thread_wait_queue_count(&endpoint->waiters);
    if (waiters == UINT32_MAX)
        return KERNEL_IRQ_CORRUPT;
    if (waiters >= KERNEL_IRQ_WAITER_MAX)
        return KERNEL_IRQ_QUOTA_EXCEEDED;
    spec->queue = &endpoint->waiters;
    spec->sequence = kernel_thread_wait_queue_sequence(spec->queue);
    return spec->sequence == 0u ? KERNEL_IRQ_CORRUPT :
                                 KERNEL_IRQ_WOULD_BLOCK;
}

KernelIrqStatus kernel_irq_commit_wait(KernelIrqEndpoint *endpoint)
{
    uint32_t waiters;

    if (!valid_active_endpoint(endpoint))
        return KERNEL_IRQ_INVALID_ARGUMENT;
    waiters = kernel_thread_wait_queue_count(&endpoint->waiters);
    return waiters != 0u && waiters <= KERNEL_IRQ_WAITER_MAX ?
        KERNEL_IRQ_OK : KERNEL_IRQ_INVALID_STATE;
}

KernelIrqStatus kernel_irq_revoke(KernelIrqEndpoint *endpoint,
                                  uint32_t *woken_threads)
{
    uint32_t woken = 0u;

    if (!valid_endpoint_pointer(endpoint) || woken_threads == NULL)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    *woken_threads = 0u;
    if (endpoint->state == KERNEL_IRQ_FREE)
        return KERNEL_IRQ_INVALID_STATE;
    if (endpoint->state != KERNEL_IRQ_REVOKING) {
        bool masked;

        if (!valid_active_endpoint(endpoint)) {
            pool_corrupt = 1u;
            return KERNEL_IRQ_CORRUPT;
        }
        endpoint->state = KERNEL_IRQ_REVOKING;
        endpoint->record_count = 0u;
        endpoint->head = 0u;
        endpoint->tail = 0u;
        endpoint->flags |= KERNEL_IRQ_FLAG_MASKED;
        astra_u32_increment_saturating(&pool_stats.revocations);
        ++pool_stats.revoking_endpoints;
        masked = controller_mask(endpoint->source);
        if (kernel_thread_wake_all(&endpoint->waiters,
                                   ASTRA_SYSCALL_PEER_DEAD, &woken) !=
            KERNEL_THREAD_OK) {
            pool_corrupt = 1u;
            return KERNEL_IRQ_CORRUPT;
        }
        pool_stats.wait_wakeups += woken;
        *woken_threads = woken;
        trace_endpoint(KERNEL_TRACE_LEVEL_NOTICE,
                   KERNEL_TRACE_EVENT_IRQ_REVOKE, endpoint,
                       endpoint_slot(endpoint), woken);
        if (!masked)
            return KERNEL_IRQ_DEVICE_ERROR;
    }
    *woken_threads = woken;
    return KERNEL_IRQ_OK;
}

KernelIrqStatus kernel_irq_owner_died(uint32_t owner,
                                      uint32_t *revoked_endpoints,
                                      uint32_t *woken_threads)
{
    uint32_t revoked = 0u;
    uint32_t woken = 0u;
    KernelIrqStatus result = KERNEL_IRQ_OK;

    if (owner == 0u || revoked_endpoints == NULL || woken_threads == NULL)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    *revoked_endpoints = 0u;
    *woken_threads = 0u;
    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        KernelIrqEndpoint *endpoint = &endpoints[slot];
        uint32_t endpoint_woken = 0u;
        KernelIrqStatus status;

        if (!active_state(endpoint->state) || endpoint->owner != owner)
            continue;
        status = kernel_irq_revoke(endpoint, &endpoint_woken);
        if (status == KERNEL_IRQ_CORRUPT)
            return status;
        if (status == KERNEL_IRQ_DEVICE_ERROR)
            result = status;
        ++revoked;
        woken += endpoint_woken;
    }
    if (revoked != 0u)
        astra_u32_increment_saturating(&pool_stats.owner_deaths);
    *revoked_endpoints = revoked;
    *woken_threads = woken;
    return result;
}

KernelIrqStatus kernel_irq_service_revocations(uint32_t batch_limit,
                                               uint32_t *completed)
{
    KernelIrqStatus result = KERNEL_IRQ_OK;
    uint32_t serviced = 0u;

    if (completed == NULL || batch_limit == 0u)
        return KERNEL_IRQ_INVALID_ARGUMENT;
    *completed = 0u;
    for (uint32_t slot = 0u;
         slot < KERNEL_IRQ_ENDPOINT_MAX && serviced < batch_limit; ++slot) {
        KernelIrqEndpoint *endpoint = &endpoints[slot];

        if (endpoint->state != KERNEL_IRQ_REVOKING ||
            (endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) != 0u)
            continue;
        ++serviced;
        astra_u32_increment_saturating(&pool_stats.quiesce_retries);
        KernelIrqStatus status = finalize_if_possible(endpoint);

        if (status == KERNEL_IRQ_CORRUPT)
            return status;
        if (status == KERNEL_IRQ_DEVICE_ERROR) {
            result = status;
            continue;
        }
        ++*completed;
    }
    return result;
}

bool kernel_irq_revocation_pending(void)
{
    if (pool_initialized == 0u || pool_corrupt != 0u)
        return false;
    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        if (endpoints[slot].state == KERNEL_IRQ_REVOKING &&
            (endpoints[slot].flags & KERNEL_IRQ_FLAG_QUIESCED) == 0u)
            return true;
    }
    return false;
}

bool kernel_irq_snapshot(uint32_t slot, KernelIrqSnapshot *snapshot)
{
    KernelIrqEndpoint *endpoint;

    if (slot >= KERNEL_IRQ_ENDPOINT_MAX || snapshot == NULL)
        return false;
    endpoint = &endpoints[slot];
    kernel_bytes_clear(snapshot, sizeof(*snapshot));
    snapshot->owner = endpoint->owner;
    snapshot->generation = endpoint->generation;
    snapshot->delivered = endpoint->delivered;
    snapshot->acknowledged = endpoint->acknowledged;
    snapshot->dropped = endpoint->dropped;
    snapshot->references = endpoint->references;
    snapshot->source = endpoint->source;
    snapshot->state = endpoint->state;
    snapshot->trigger = endpoint->trigger;
    snapshot->ipl = endpoint->ipl;
    snapshot->vector = endpoint->vector;
    snapshot->pending_records = endpoint->record_count;
    snapshot->event_flags = endpoint->flags & KERNEL_IRQ_PUBLIC_FLAGS;
    snapshot->consecutive = endpoint->consecutive;
    if (endpoint->state != KERNEL_IRQ_FREE) {
        uint32_t waiters = kernel_thread_wait_queue_count(&endpoint->waiters);

        if (waiters == UINT32_MAX || waiters > UINT16_MAX)
            return false;
        snapshot->waiters = (uint16_t)waiters;
        if (endpoint->record_count != 0u)
            kernel_bytes_copy(&snapshot->oldest,
                              &endpoint->records[endpoint->head],
                              sizeof(snapshot->oldest));
    }
    return true;
}

/*
 * The two spellings of an endpoint's state and its sticky events are one
 * numbering, and these are what keep them one. They are separate definitions
 * because the kernel's are internal and userspace's are ABI, and a renumbering
 * on either side would otherwise be a silent misreport rather than a build
 * failure.
 */
_Static_assert((int)KERNEL_IRQ_FREE == (int)ASTRA_IRQ_ENDPOINT_FREE &&
                   (int)KERNEL_IRQ_MASKED == (int)ASTRA_IRQ_ENDPOINT_MASKED &&
                   (int)KERNEL_IRQ_ARMED == (int)ASTRA_IRQ_ENDPOINT_ARMED &&
                   (int)KERNEL_IRQ_PENDING == (int)ASTRA_IRQ_ENDPOINT_PENDING &&
                   (int)KERNEL_IRQ_REVOKING ==
                       (int)ASTRA_IRQ_ENDPOINT_REVOKING,
               "endpoint states must mean the same on both sides of the ABI");
_Static_assert(KERNEL_IRQ_EVENT_OVERFLOW == ASTRA_IRQ_ENDPOINT_EVENT_OVERFLOW &&
                   KERNEL_IRQ_EVENT_STORM == ASTRA_IRQ_ENDPOINT_EVENT_STORM &&
                   KERNEL_IRQ_EVENT_DEVICE_ERROR ==
                       ASTRA_IRQ_ENDPOINT_EVENT_DEVICE_ERROR,
               "endpoint events must mean the same on both sides of the ABI");

bool kernel_irq_endpoint_info(uint32_t slot, AstraIrqEndpointInfo *info)
{
    KernelIrqSnapshot snapshot;

    if (info == NULL || slot >= KERNEL_IRQ_ENDPOINT_MAX)
        return false;
    kernel_bytes_clear(info, sizeof(*info));
    info->size = ASTRA_IRQ_ENDPOINT_INFO_SIZE;
    if (!kernel_irq_snapshot(slot, &snapshot))
        return false;
    /*
     * A free slot answers with its size and nothing else. Iterating callers
     * read the owner to know whether the rest means anything.
     */
    if (snapshot.owner == 0u || endpoints[slot].state == KERNEL_IRQ_FREE)
        return true;
    info->owner = snapshot.owner;
    info->generation = snapshot.generation;
    info->delivered = snapshot.delivered;
    info->acknowledged = snapshot.acknowledged;
    info->dropped = snapshot.dropped;
    info->references = snapshot.references;
    info->waiters = snapshot.waiters;
    info->source = snapshot.source;
    info->state = snapshot.state;
    info->trigger = snapshot.trigger;
    info->ipl = snapshot.ipl;
    info->pending_records = snapshot.pending_records;
    info->event_flags = snapshot.event_flags;
    info->consecutive = snapshot.consecutive;
    return true;
}

bool kernel_irq_pool_stats(KernelIrqPoolStats *stats)
{
    if (stats == NULL || pool_initialized == 0u)
        return false;
    kernel_bytes_copy(stats, &pool_stats, sizeof(*stats));
    return true;
}

bool kernel_irq_pool_healthy(void)
{
    return pool_initialized != 0u && pool_corrupt == 0u &&
           endpoint_cache.corrupt == 0u;
}

bool kernel_irq_pool_valid(void)
{
    uint32_t live = 0u;
    uint32_t revoking = 0u;
    uint32_t internal = 0u;

    if (!kernel_irq_pool_healthy() ||
        !kernel_object_cache_valid(&endpoint_cache))
        return false;
    for (uint32_t slot = 0u; slot < KERNEL_IRQ_ENDPOINT_MAX; ++slot) {
        KernelIrqEndpoint *endpoint = &endpoints[slot];

        if (endpoint->state == KERNEL_IRQ_FREE) {
            if (kernel_object_cache_slot_claimed(
                    &endpoint_cache, (uint16_t)slot) || endpoint->owner != 0u ||
                endpoint->references != 0u)
                return false;
            continue;
        }
        ++live;
        if (!kernel_object_cache_slot_claimed(
                &endpoint_cache, (uint16_t)slot) || endpoint->owner == 0u ||
            endpoint->generation == 0u ||
            endpoint->source >= KERNEL_IRQ_SOURCE_COUNT)
            return false;
        if (endpoint->state == KERNEL_IRQ_REVOKING) {
            ++revoking;
            if ((endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) == 0u &&
                routes[endpoint->source].endpoint != endpoint)
                return false;
            if ((endpoint->flags & KERNEL_IRQ_FLAG_QUIESCED) != 0u &&
                routes[endpoint->source].endpoint != NULL)
                return false;
        } else if (!valid_active_endpoint(endpoint)) {
            return false;
        }
    }
    for (uint32_t source = 0u; source < KERNEL_IRQ_SOURCE_COUNT; ++source) {
        KernelIrqRoute *route = &routes[source];
        KernelIrqEndpoint *endpoint = route->endpoint;

        if (endpoint != NULL && route->internal_service != NULL)
            return false;
        if (endpoint != NULL &&
            (!valid_endpoint_pointer(endpoint) || endpoint->source != source ||
             endpoint->state == KERNEL_IRQ_FREE))
            return false;
        if (route->internal_service != NULL &&
            (!valid_trigger(route->trigger) || route->ipl == 0u ||
             route->ipl > 7u || route->vector != KERNEL_IRQ_COMMON_VECTOR ||
             route->internal_armed > 1u))
            return false;
        if (route->internal_service != NULL)
            ++internal;
    }
    return live == pool_stats.live_endpoints &&
           revoking == pool_stats.revoking_endpoints &&
           internal == pool_stats.internal_routes &&
           live == endpoint_cache.live &&
           pool_stats.live_endpoints <= KERNEL_IRQ_ENDPOINT_MAX &&
           pool_stats.max_live_endpoints >= pool_stats.live_endpoints &&
           pool_stats.max_live_endpoints <= KERNEL_IRQ_ENDPOINT_MAX &&
           pool_stats.max_pending_records <= KERNEL_IRQ_RECORD_DEPTH;
}
