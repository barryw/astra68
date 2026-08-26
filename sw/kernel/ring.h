#ifndef ASTRA_KERNEL_RING_H
#define ASTRA_KERNEL_RING_H

#include <astra/syscall.h>

#include "area.h"
#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_RING_MAX 64u
#define KERNEL_RING_OWNER_MAX 16u
#define KERNEL_RING_AREA_MAX 4u
#define KERNEL_RING_HEADER_SIZE ASTRA_BULK_RING_HEADER_SIZE
#define KERNEL_RING_OFFSET_ALIGNMENT ASTRA_BULK_RING_OFFSET_ALIGNMENT
#define KERNEL_RING_ELEMENT_SIZE_MIN ASTRA_BULK_RING_ELEMENT_SIZE_MIN
#define KERNEL_RING_ELEMENT_SIZE_MAX ASTRA_BULK_RING_ELEMENT_SIZE_MAX
#define KERNEL_RING_CAPACITY_MIN ASTRA_BULK_RING_CAPACITY_MIN
#define KERNEL_RING_CAPACITY_MAX ASTRA_BULK_RING_CAPACITY_MAX
#define KERNEL_RING_MAGIC ASTRA_BULK_RING_MAGIC
#define KERNEL_RING_ABI_VERSION ASTRA_BULK_RING_ABI_VERSION

#define KERNEL_RING_NOTIFY_CORRUPT ASTRA_BULK_RING_NOTIFY_CORRUPT
#define KERNEL_RING_CREATE_KERNEL_COPY \
    ASTRA_BULK_RING_CREATE_KERNEL_COPY
#define KERNEL_RING_COPY_FIXED_BUDGET_CYCLES 30000u
#define KERNEL_RING_COPY_PER_BYTE_BUDGET_CYCLES 100u

#define KERNEL_RING_PRODUCER_RIGHTS \
    ((1u << 1) | (1u << 3) | (1u << 4) | (1u << 5))
#define KERNEL_RING_CONSUMER_RIGHTS \
    ((1u << 0) | (1u << 3) | (1u << 4) | (1u << 5))

typedef enum KernelRingEndpoint {
    KERNEL_RING_ENDPOINT_PRODUCER = 1,
    KERNEL_RING_ENDPOINT_CONSUMER = 2
} KernelRingEndpoint;

typedef enum KernelRingStatus {
    KERNEL_RING_OK = 0,
    KERNEL_RING_WOULD_BLOCK,
    KERNEL_RING_PEER_DEAD,
    KERNEL_RING_CLOSED,
    KERNEL_RING_IO_ERROR,
    KERNEL_RING_INVALID_ARGUMENT,
    KERNEL_RING_INVALID_STATE,
    KERNEL_RING_NO_SLOT,
    KERNEL_RING_QUOTA_EXCEEDED,
    KERNEL_RING_OVERLAP,
    KERNEL_RING_CORRUPT
} KernelRingStatus;

typedef struct KernelRing KernelRing;

typedef struct KernelRingSnapshot {
    uint32_t owner;
    uint32_t generation;
    uint32_t area_generation;
    uint32_t offset;
    uint32_t total_size;
    uint32_t element_size;
    uint32_t capacity;
    uint32_t producer_position;
    uint32_t consumer_position;
    uint32_t producer_terminal;
    uint32_t consumer_terminal;
    uint16_t producer_references;
    uint16_t consumer_references;
    uint16_t producer_waiters;
    uint16_t consumer_waiters;
    uint8_t state;
    uint8_t child_released;
    uint8_t reserved[2];
} KernelRingSnapshot;

typedef struct KernelRingPoolStats {
    uint32_t created_rings;
    uint32_t active_rings;
    uint32_t closing_rings;
    uint32_t max_active_rings;
    uint32_t allocation_failures;
    uint32_t quota_failures;
    uint32_t overlap_failures;
    uint32_t producer_notifications;
    uint32_t consumer_notifications;
    uint32_t producer_waits;
    uint32_t consumer_waits;
    uint32_t wait_wakeups;
    uint32_t peer_closures;
    uint32_t owner_deaths;
    uint32_t corruption_failures;
    uint32_t copied_reads;
    uint32_t copied_writes;
    uint32_t copied_read_bytes;
    uint32_t copied_write_bytes;
    uint32_t copied_would_blocks;
    uint32_t copied_max_cycles;
    uint32_t copied_cycle_overruns;
} KernelRingPoolStats;

void kernel_ring_pool_init(void);
KernelRingStatus kernel_ring_create(uint32_t owner, KernelArea *area,
                                    uint32_t offset, uint32_t element_size,
                                    uint32_t capacity, KernelRing **ring);
KernelRingStatus kernel_ring_create_flagged(
    uint32_t owner, KernelArea *area, uint32_t offset, uint32_t element_size,
    uint32_t capacity, uint32_t flags, KernelRing **ring);
void kernel_ring_abandon_unpublished(KernelRing *ring);
bool kernel_ring_handle_retain(void *object, void *context);
void kernel_ring_handle_release(void *object, void *context);
KernelRingStatus kernel_ring_notify(KernelRing *ring,
                                    KernelRingEndpoint endpoint,
                                    uint32_t position, uint32_t flags,
                                    uint32_t *producer_position,
                                    uint32_t *consumer_position,
                                    uint32_t *woken_threads);
KernelRingStatus kernel_ring_prepare_wait(KernelRing *ring,
                                          KernelRingEndpoint endpoint,
                                          KernelThreadWaitSpec *spec);
KernelRingStatus kernel_ring_commit_wait(KernelRing *ring,
                                         KernelRingEndpoint endpoint);
KernelRingStatus kernel_ring_copy_peek(KernelRing *ring, void *bytes,
                                       uint32_t capacity,
                                       uint32_t *copied);
KernelRingStatus kernel_ring_copy_consume(KernelRing *ring, uint32_t count,
                                          uint32_t *woken_threads);
KernelRingStatus kernel_ring_copy_write(KernelRing *ring, const void *bytes,
                                        uint32_t length, bool atomic,
                                        uint32_t *written,
                                        uint32_t *woken_threads);
void kernel_ring_record_copy_cycles(uint32_t cycles, uint32_t bytes);
KernelRingStatus kernel_ring_process_died(uint32_t process_id,
                                          uint32_t *closed_rings,
                                          uint32_t *woken_threads);
uint32_t kernel_ring_terminal_result(const KernelRing *ring,
                                     KernelRingEndpoint endpoint);
bool kernel_ring_snapshot(uint32_t slot, KernelRingSnapshot *snapshot);
bool kernel_ring_pool_stats(KernelRingPoolStats *stats);
bool kernel_ring_pool_healthy(void);
bool kernel_ring_pool_valid(void);

#if defined(KERNEL_RING_HOST_TEST)
bool kernel_ring_test_set_positions(KernelRing *ring,
                                    uint32_t producer_position,
                                    uint32_t consumer_position);
#endif

#endif
