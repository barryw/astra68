#ifndef ASTRA_KERNEL_IRQ_H
#define ASTRA_KERNEL_IRQ_H

#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_IRQ_SOURCE_COUNT 32u
#define KERNEL_IRQ_ENDPOINT_MAX 16u
#define KERNEL_IRQ_OWNER_MAX 8u
#define KERNEL_IRQ_RECORD_DEPTH 4u
#define KERNEL_IRQ_STORM_BUDGET 64u
#define KERNEL_IRQ_STORM_WINDOW_CYCLES 125000u
#define KERNEL_IRQ_COMMON_VECTOR 80u
#define KERNEL_IRQ_WAITER_MAX KERNEL_THREAD_MAX

#define KERNEL_IRQ_RIGHT_READ       (1u << 0)
#define KERNEL_IRQ_RIGHT_SIGNAL     (1u << 3)
#define KERNEL_IRQ_RIGHT_WAIT       (1u << 4)
#define KERNEL_IRQ_RIGHT_TRANSFER   (1u << 5)
#define KERNEL_IRQ_RIGHT_ADMINISTER (1u << 6)
#define KERNEL_IRQ_RIGHTS \
    (KERNEL_IRQ_RIGHT_READ | KERNEL_IRQ_RIGHT_SIGNAL | \
     KERNEL_IRQ_RIGHT_WAIT | KERNEL_IRQ_RIGHT_TRANSFER | \
     KERNEL_IRQ_RIGHT_ADMINISTER)

#define KERNEL_IRQ_EVENT_OVERFLOW     (1u << 0)
#define KERNEL_IRQ_EVENT_STORM        (1u << 1)
#define KERNEL_IRQ_EVENT_DEVICE_ERROR (1u << 2)

#define KERNEL_IRQ_TRACE_SOURCE_MASK 0x001fu
#define KERNEL_IRQ_TRACE_STATE_SHIFT 5u
#define KERNEL_IRQ_TRACE_STATE_MASK  0x00e0u
#define KERNEL_IRQ_TRACE_EDGE        0x0100u
#define KERNEL_IRQ_TRACE_EVENT_SHIFT 9u
#define KERNEL_IRQ_TRACE_EVENT_MASK  0x0e00u
#define KERNEL_IRQ_TRACE_INTERNAL    0x8000u

typedef enum KernelIrqTrigger {
    KERNEL_IRQ_TRIGGER_LEVEL = 0,
    KERNEL_IRQ_TRIGGER_EDGE = 1
} KernelIrqTrigger;

typedef enum KernelIrqState {
    KERNEL_IRQ_FREE = 0,
    KERNEL_IRQ_MASKED,
    KERNEL_IRQ_ARMED,
    KERNEL_IRQ_PENDING,
    KERNEL_IRQ_REVOKING
} KernelIrqState;

typedef enum KernelIrqStatus {
    KERNEL_IRQ_OK = 0,
    KERNEL_IRQ_WOULD_BLOCK,
    KERNEL_IRQ_CLOSED,
    KERNEL_IRQ_INVALID_ARGUMENT,
    KERNEL_IRQ_INVALID_STATE,
    KERNEL_IRQ_NO_SLOT,
    KERNEL_IRQ_QUOTA_EXCEEDED,
    KERNEL_IRQ_SOURCE_BUSY,
    KERNEL_IRQ_SEQUENCE_MISMATCH,
    KERNEL_IRQ_OVERFLOW,
    KERNEL_IRQ_STORM,
    KERNEL_IRQ_DEVICE_ERROR,
    KERNEL_IRQ_UNCLAIMED,
    KERNEL_IRQ_CORRUPT
} KernelIrqStatus;

typedef struct KernelIrqEndpoint KernelIrqEndpoint;

typedef struct KernelIrqRecord {
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t status;
    uint32_t sequence;
} KernelIrqRecord;

typedef struct KernelIrqTrace {
    uint32_t argument[4];
    uint16_t event;
    uint16_t flags;
    uint8_t valid;
    uint8_t reserved[3];
} KernelIrqTrace;

typedef bool (*KernelIrqCapture)(uint8_t source, uint32_t *status,
                                 void *context);
typedef bool (*KernelIrqComplete)(uint8_t source,
                                  const KernelIrqRecord *record,
                                  void *context);
typedef bool (*KernelIrqQuiesce)(uint8_t source, void *context);
typedef bool (*KernelIrqInternalService)(uint8_t source, uint64_t timestamp,
                                         void *context);

typedef struct KernelIrqBinding {
    KernelIrqCapture capture;
    KernelIrqComplete complete;
    KernelIrqQuiesce quiesce;
    void *context;
    uint8_t source;
    uint8_t trigger;
    uint8_t ipl;
    uint8_t vector;
} KernelIrqBinding;

typedef struct KernelIrqControllerOps {
    bool (*configure)(uint8_t source, uint8_t trigger, uint8_t ipl,
                      uint8_t vector, void *context);
    bool (*mask)(uint8_t source, void *context);
    bool (*enable)(uint8_t source, void *context);
    bool (*acknowledge)(uint8_t source, void *context);
    void *context;
} KernelIrqControllerOps;

typedef struct KernelIrqInternalBinding {
    KernelIrqInternalService service;
    void *context;
    uint8_t source;
    uint8_t trigger;
    uint8_t ipl;
    uint8_t vector;
} KernelIrqInternalBinding;

typedef struct KernelIrqSnapshot {
    KernelIrqRecord oldest;
    uint32_t owner;
    uint32_t generation;
    uint32_t delivered;
    uint32_t acknowledged;
    uint32_t dropped;
    uint16_t references;
    uint16_t waiters;
    uint8_t source;
    uint8_t state;
    uint8_t trigger;
    uint8_t ipl;
    uint8_t vector;
    uint8_t pending_records;
    uint8_t event_flags;
    uint8_t consecutive;
} KernelIrqSnapshot;

typedef struct KernelIrqPoolStats {
    uint32_t created_endpoints;
    uint32_t live_endpoints;
    uint32_t revoking_endpoints;
    uint32_t max_live_endpoints;
    uint32_t allocation_failures;
    uint32_t quota_failures;
    uint32_t source_busy_failures;
    uint32_t internal_routes;
    uint32_t internal_deliveries;
    uint32_t publication_rollbacks;
    uint32_t deliveries;
    uint32_t acknowledgements;
    uint32_t dropped_records;
    uint32_t overflow_quarantines;
    uint32_t storm_quarantines;
    uint32_t unclaimed_interrupts;
    uint32_t masked_interrupts;
    uint32_t revoking_interrupts;
    uint32_t bad_vector_interrupts;
    uint32_t wait_wakeups;
    uint32_t owner_deaths;
    uint32_t revocations;
    uint32_t controller_failures;
    uint32_t device_failures;
    uint32_t quiesce_retries;
    uint32_t max_pending_records;
} KernelIrqPoolStats;

bool kernel_irq_pool_init(const KernelIrqControllerOps *ops);
KernelIrqStatus kernel_irq_bind_internal(
    const KernelIrqInternalBinding *binding);
KernelIrqStatus kernel_irq_arm_internal(uint8_t source);
KernelIrqStatus kernel_irq_mask_internal(uint8_t source);
KernelIrqStatus kernel_irq_bind(uint32_t owner,
                                const KernelIrqBinding *binding,
                                KernelIrqEndpoint **endpoint);
bool kernel_irq_handle_retain(void *object, void *context);
void kernel_irq_handle_release(void *object, void *context);
void kernel_irq_abandon_unpublished(KernelIrqEndpoint *endpoint);
KernelIrqStatus kernel_irq_arm(KernelIrqEndpoint *endpoint);
KernelIrqStatus kernel_irq_mask(KernelIrqEndpoint *endpoint);
KernelIrqStatus kernel_irq_recover(KernelIrqEndpoint *endpoint);
KernelIrqStatus kernel_irq_dispatch(uint8_t source, uint8_t vector,
                                    uint64_t timestamp,
                                    uint32_t *woken_threads);
KernelIrqStatus kernel_irq_dispatch_traced(uint8_t source, uint8_t vector,
                                           uint64_t timestamp,
                                           uint32_t *woken_threads,
                                           KernelIrqTrace *trace);
KernelIrqStatus kernel_irq_dispatch_claimed_traced(
    uint8_t source, uint8_t vector, uint64_t timestamp,
    uint32_t *woken_threads, KernelIrqTrace *trace);
KernelIrqStatus kernel_irq_read(KernelIrqEndpoint *endpoint,
                                KernelIrqRecord *record,
                                uint32_t *event_flags);
KernelIrqStatus kernel_irq_ack(KernelIrqEndpoint *endpoint,
                               uint32_t sequence);
KernelIrqStatus kernel_irq_prepare_wait(KernelIrqEndpoint *endpoint,
                                        KernelThreadWaitSpec *spec);
KernelIrqStatus kernel_irq_commit_wait(KernelIrqEndpoint *endpoint);
KernelIrqStatus kernel_irq_revoke(KernelIrqEndpoint *endpoint,
                                  uint32_t *woken_threads);
KernelIrqStatus kernel_irq_owner_died(uint32_t owner,
                                      uint32_t *revoked_endpoints,
                                      uint32_t *woken_threads);
KernelIrqStatus kernel_irq_service_revocations(uint32_t batch_limit,
                                               uint32_t *completed);
bool kernel_irq_revocation_pending(void);
bool kernel_irq_snapshot(uint32_t slot, KernelIrqSnapshot *snapshot);
bool kernel_irq_pool_stats(KernelIrqPoolStats *stats);
bool kernel_irq_pool_healthy(void);
bool kernel_irq_pool_valid(void);

#endif
