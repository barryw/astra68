#ifndef ASTRA_KERNEL_WORKER_H
#define ASTRA_KERNEL_WORKER_H

#include "context.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_WORKER_PROCESS_REAP (1u << 0)
#define KERNEL_WORKER_MONITOR_RX  (1u << 1)
#define KERNEL_WORKER_MONITOR_SPI (1u << 2)
#define KERNEL_WORKER_DEVICE_RESET (1u << 3)
#define KERNEL_WORKER_TRACE_FLUSH (1u << 4)
#define KERNEL_WORKER_IRQ_DISPATCH (1u << 5)
#define KERNEL_WORKER_CLASS_COUNT 8u
#define KERNEL_WORKER_MONITOR_RX_BATCH 32u
#define KERNEL_WORKER_MONITOR_SPI_BATCH 16u
#define KERNEL_WORKER_DEVICE_RESET_BATCH 1u
#define KERNEL_WORKER_TRACE_FLUSH_BATCH 8u
#define KERNEL_WORKER_IRQ_DISPATCH_BATCH 4u
#define KERNEL_WORKER_STACK_SIZE 8192u

typedef enum KernelWorkerState {
    KERNEL_WORKER_UNINITIALIZED = 0,
    KERNEL_WORKER_BLOCKED,
    KERNEL_WORKER_READY,
    KERNEL_WORKER_RUNNING
} KernelWorkerState;

typedef enum KernelWorkerStatus {
    KERNEL_WORKER_OK = 0,
    KERNEL_WORKER_INVALID_ARGUMENT,
    KERNEL_WORKER_INVALID_STATE,
    KERNEL_WORKER_CORRUPT
} KernelWorkerStatus;

typedef enum KernelWorkerServiceResult {
    KERNEL_WORKER_SERVICE_COMPLETE = 0,
    KERNEL_WORKER_SERVICE_RETRY,
    KERNEL_WORKER_SERVICE_FATAL
} KernelWorkerServiceResult;

typedef KernelWorkerServiceResult (*KernelWorkerService)(
    uint32_t batch_limit, void *context);

typedef struct KernelWorkerStats {
    uint32_t pending_work;
    uint32_t retry_work;
    uint32_t signals;
    uint32_t dispatches;
    uint32_t service_passes;
    uint32_t deferred_passes;
    uint32_t retry_wakeups;
    uint32_t user_yields;
    uint32_t idle_waits;
    uint32_t restore_entries;
    uint32_t main_entries;
    uint32_t dispatch_latency_samples;
    uint32_t max_dispatch_latency_cycles;
    uint32_t stack_high_water;
    uint32_t registered_work;
    uint32_t class_passes[KERNEL_WORKER_CLASS_COUNT];
    uint32_t class_retries[KERNEL_WORKER_CLASS_COUNT];
    uint8_t state;
    uint8_t stack_canary_ok;
    uint8_t reserved[2];
} KernelWorkerStats;

KernelWorkerStatus kernel_worker_init(void);
KernelWorkerStatus kernel_worker_register(uint32_t work,
                                          KernelWorkerService service,
                                          void *context);
KernelWorkerStatus kernel_worker_signal(uint32_t work);
KernelWorkerStatus kernel_worker_schedule(uint32_t work);
KernelWorkerStatus kernel_worker_on_timer(void);
bool kernel_worker_try_select(void);
bool kernel_worker_select_idle(void);
bool kernel_worker_work_pending(void);
bool kernel_worker_stats(KernelWorkerStats *stats);
void kernel_worker_main(void) __attribute__((noreturn));

/* Architecture coroutine entry points. See worker.S for the stack contract. */
void kernel_worker_switch_to_user(KernelCpuContext *context);
void kernel_worker_arch_wait(void);

#if defined(KERNEL_WORKER_HOST_TEST)
KernelWorkerStatus kernel_worker_test_service_once(void);
bool kernel_worker_test_block_if_idle(void);
#endif

#endif
