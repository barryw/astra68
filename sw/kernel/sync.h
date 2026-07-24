#ifndef ASTRA_KERNEL_SYNC_H
#define ASTRA_KERNEL_SYNC_H

#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_SYNC_OBJECT_MAX 32u
#define KERNEL_SYNC_OWNER_MAX 8u
#define KERNEL_SYNC_WAITER_MAX KERNEL_THREAD_MAX
#define KERNEL_SYNC_REFERENCE_MAX UINT16_MAX
#define KERNEL_SYNC_SEMAPHORE_COUNT_MAX 0x7fffffffu

#define KERNEL_SYNC_EVENT_MANUAL_RESET       (1u << 0)
#define KERNEL_SYNC_EVENT_INITIALLY_SIGNALED (1u << 1)
#define KERNEL_SYNC_EVENT_FLAGS \
    (KERNEL_SYNC_EVENT_MANUAL_RESET | \
     KERNEL_SYNC_EVENT_INITIALLY_SIGNALED)

#define KERNEL_SYNC_RIGHT_QUERY      (1u << 0)
#define KERNEL_SYNC_RIGHT_SIGNAL     (1u << 3)
#define KERNEL_SYNC_RIGHT_WAIT       (1u << 4)
#define KERNEL_SYNC_RIGHT_ADMINISTER (1u << 6)
#define KERNEL_SYNC_RIGHTS \
    (KERNEL_SYNC_RIGHT_QUERY | KERNEL_SYNC_RIGHT_SIGNAL | \
     KERNEL_SYNC_RIGHT_WAIT | KERNEL_SYNC_RIGHT_ADMINISTER)

typedef enum KernelSyncType {
    KERNEL_SYNC_NONE = 0,
    KERNEL_SYNC_EVENT_AUTO,
    KERNEL_SYNC_EVENT_MANUAL,
    KERNEL_SYNC_SEMAPHORE
} KernelSyncType;

typedef enum KernelSyncState {
    KERNEL_SYNC_FREE = 0,
    KERNEL_SYNC_LIVE,
    KERNEL_SYNC_CLOSING
} KernelSyncState;

typedef enum KernelSyncStatus {
    KERNEL_SYNC_OK = 0,
    KERNEL_SYNC_BLOCKED,
    KERNEL_SYNC_TIMED_OUT,
    KERNEL_SYNC_CLOSED,
    KERNEL_SYNC_INVALID_ARGUMENT,
    KERNEL_SYNC_INVALID_STATE,
    KERNEL_SYNC_NO_SLOT,
    KERNEL_SYNC_QUOTA_EXCEEDED,
    KERNEL_SYNC_WAITER_LIMIT,
    KERNEL_SYNC_COUNT_LIMIT,
    KERNEL_SYNC_CORRUPT
} KernelSyncStatus;

typedef struct KernelSyncObject KernelSyncObject;

typedef struct KernelSyncSnapshot {
    uint32_t generation;
    uint32_t owner;
    uint32_t count;
    uint32_t maximum;
    uint32_t close_result;
    uint16_t references;
    uint16_t waiters;
    uint8_t type;
    uint8_t state;
    uint8_t reserved[2];
} KernelSyncSnapshot;

typedef struct KernelSyncPoolStats {
    uint32_t created_events;
    uint32_t created_semaphores;
    uint32_t live_objects;
    uint32_t closing_objects;
    uint32_t max_live_objects;
    uint32_t allocation_failures;
    uint32_t quota_failures;
    uint32_t publication_rollbacks;
    uint32_t wait_calls;
    uint32_t immediate_waits;
    uint32_t blocked_waits;
    uint32_t signal_calls;
    uint32_t reset_calls;
    uint32_t close_operations;
    uint32_t owner_deaths;
    uint32_t signal_wakeups;
    uint32_t close_wakeups;
    uint32_t max_waiters;
} KernelSyncPoolStats;

void kernel_sync_pool_init(void);
KernelSyncStatus kernel_sync_create_event(uint32_t owner, uint32_t flags,
                                          KernelSyncObject **object);
KernelSyncStatus kernel_sync_create_semaphore(uint32_t owner,
                                              uint32_t initial_count,
                                              uint32_t maximum_count,
                                              KernelSyncObject **object);
KernelSyncStatus kernel_sync_retain(KernelSyncObject *object);
void kernel_sync_handle_release(void *object, void *context);
void kernel_sync_abandon_unpublished(KernelSyncObject *object);
KernelSyncStatus kernel_sync_wait(KernelSyncObject *object,
                                  KernelThread *thread, uint64_t now,
                                  uint64_t deadline,
                                  uint32_t timeout_result);
KernelSyncStatus kernel_sync_signal(KernelSyncObject *object,
                                    uint32_t release_count,
                                    uint32_t wake_result,
                                    uint32_t *woken_threads);
KernelSyncStatus kernel_sync_reset(KernelSyncObject *object);
KernelSyncStatus kernel_sync_owner_died(uint32_t owner,
                                       uint32_t wake_result,
                                       uint32_t *closed_objects,
                                       uint32_t *woken_threads);
uint32_t kernel_sync_terminal_result(const KernelSyncObject *object);
bool kernel_sync_snapshot(uint32_t slot, KernelSyncSnapshot *snapshot);
bool kernel_sync_pool_stats(KernelSyncPoolStats *stats);
bool kernel_sync_pool_valid(void);

#endif
