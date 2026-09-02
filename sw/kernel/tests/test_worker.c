#include "worker.h"

#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static KernelProcessStatus maintenance_result;
static bool signal_during_maintenance;
static bool interrupts_enabled;
static uint32_t maintenance_calls;
static uint32_t enable_calls;
static uint32_t disable_calls;
static uint32_t save_disable_calls;
static uint32_t restore_calls;
static KernelWorkerServiceResult monitor_result;
static uint32_t monitor_calls;
static uint32_t monitor_last_batch;
static uint32_t device_calls;
static uint32_t staged_trace_records;

uint32_t kernel_trace_flush_staged(uint32_t batch_limit)
{
    uint32_t flushed = staged_trace_records < batch_limit ?
        staged_trace_records : batch_limit;

    staged_trace_records -= flushed;
    return flushed;
}

bool kernel_trace_staged_pending(void)
{
    return staged_trace_records != 0u;
}

void kernel_enable_interrupts(void)
{
    assert(!interrupts_enabled);
    interrupts_enabled = true;
    ++enable_calls;
}

void kernel_disable_interrupts(void)
{
    assert(interrupts_enabled);
    interrupts_enabled = false;
    ++disable_calls;
}

uint16_t kernel_interrupt_save_disable(void)
{
    uint16_t status = interrupts_enabled ? 0x2000u : 0x2700u;

    interrupts_enabled = false;
    ++save_disable_calls;
    return status;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    interrupts_enabled = (status_register & 0x0700u) != 0x0700u;
    ++restore_calls;
}

KernelProcessStatus kernel_process_maintenance(void)
{
    assert(interrupts_enabled);
    ++maintenance_calls;
    if (signal_during_maintenance) {
        signal_during_maintenance = false;
        assert(kernel_worker_signal(KERNEL_WORKER_PROCESS_REAP) ==
               KERNEL_WORKER_OK);
        assert(interrupts_enabled);
    }
    return maintenance_result;
}

bool kernel_process_worker_enter(void)
{
    return true;
}

KernelCpuContext *kernel_process_worker_resume(void)
{
    return NULL;
}

void kernel_worker_switch_to_user(KernelCpuContext *context)
{
    (void)context;
    abort();
}

void kernel_worker_arch_wait(void)
{
    abort();
}

void kernel_panic(const char *reason)
{
    fprintf(stderr, "unexpected kernel panic: %s\n", reason);
    abort();
}

static KernelWorkerServiceResult monitor_service(uint32_t batch_limit,
                                                 void *context)
{
    assert(interrupts_enabled);
    assert(context == &monitor_calls);
    ++monitor_calls;
    monitor_last_batch = batch_limit;
    return monitor_result;
}

static KernelWorkerServiceResult device_service(uint32_t batch_limit,
                                                void *context)
{
    assert(interrupts_enabled);
    assert(batch_limit == KERNEL_WORKER_DEVICE_RESET_BATCH);
    assert(context == &device_calls);
    ++device_calls;
    return KERNEL_WORKER_SERVICE_COMPLETE;
}

static KernelWorkerStats worker_stats(void)
{
    KernelWorkerStats stats;

    assert(kernel_worker_stats(&stats));
    return stats;
}

static void test_bounded_worker_state_machine(void)
{
    KernelWorkerStats stats;

    assert(kernel_worker_schedule(KERNEL_WORKER_PROCESS_REAP) ==
           KERNEL_WORKER_INVALID_STATE);
    assert(kernel_worker_init() == KERNEL_WORKER_OK);
    assert(kernel_worker_init() == KERNEL_WORKER_INVALID_STATE);
    stats = worker_stats();
    assert(stats.state == KERNEL_WORKER_BLOCKED);
    assert(stats.stack_canary_ok == 1u);
    assert(stats.stack_high_water == 48u);
    assert(kernel_worker_signal(0u) == KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_signal(2u) == KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_signal(KERNEL_WORKER_PROCESS_REAP) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_signal(KERNEL_WORKER_PROCESS_REAP) ==
           KERNEL_WORKER_OK);
    stats = worker_stats();
    assert(stats.state == KERNEL_WORKER_READY);
    assert(stats.pending_work == KERNEL_WORKER_PROCESS_REAP);
    assert(stats.signals == 2u);

    assert(kernel_worker_try_select());
    assert(!kernel_worker_try_select());

    maintenance_result = KERNEL_PROCESS_DEFERRED;
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(!interrupts_enabled);
    stats = worker_stats();
    assert(stats.pending_work == 0u);
    assert(stats.retry_work == KERNEL_WORKER_PROCESS_REAP);
    assert(stats.service_passes == 1u);
    assert(stats.deferred_passes == 1u);
    assert(kernel_worker_test_block_if_idle());
    assert(worker_stats().state == KERNEL_WORKER_BLOCKED);

    assert(kernel_worker_on_timer() == KERNEL_WORKER_OK);
    stats = worker_stats();
    assert(stats.state == KERNEL_WORKER_READY);
    assert(stats.pending_work == KERNEL_WORKER_PROCESS_REAP);
    assert(stats.retry_work == 0u);
    assert(stats.retry_wakeups == 1u);
    assert(kernel_worker_try_select());

    maintenance_result = KERNEL_PROCESS_OK;
    signal_during_maintenance = true;
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    stats = worker_stats();
    assert(stats.state == KERNEL_WORKER_RUNNING);
    assert(stats.pending_work == KERNEL_WORKER_PROCESS_REAP);
    assert(!kernel_worker_test_block_if_idle());

    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(kernel_worker_test_block_if_idle());
    assert(kernel_worker_schedule(0u) == KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_schedule(2u) == KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_schedule(KERNEL_WORKER_PROCESS_REAP) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_schedule(KERNEL_WORKER_PROCESS_REAP) ==
           KERNEL_WORKER_INVALID_STATE);
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(kernel_worker_test_block_if_idle());
    stats = worker_stats();
    assert(stats.state == KERNEL_WORKER_BLOCKED);
    assert(stats.pending_work == 0u);
    assert(stats.retry_work == 0u);
    assert(stats.signals == 4u);
    assert(stats.dispatches == 3u);
    assert(stats.service_passes == 4u);
    assert(stats.deferred_passes == 1u);
    assert(stats.retry_wakeups == 1u);
    assert(maintenance_calls == 4u);
    assert(enable_calls == 4u);
    assert(disable_calls == 4u);
}

static void test_registered_service_is_bounded_and_retried(void)
{
    KernelWorkerStats stats;

    assert(kernel_worker_register(KERNEL_WORKER_PROCESS_REAP,
                                  monitor_service, &monitor_calls) ==
           KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_register(KERNEL_WORKER_MONITOR_RX, NULL,
                                  &monitor_calls) ==
           KERNEL_WORKER_INVALID_ARGUMENT);
    assert(kernel_worker_register(KERNEL_WORKER_MONITOR_RX,
                                  monitor_service, &monitor_calls) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_register(KERNEL_WORKER_MONITOR_RX,
                                  monitor_service, &monitor_calls) ==
           KERNEL_WORKER_INVALID_STATE);
    assert(kernel_worker_signal(KERNEL_WORKER_MONITOR_RX) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_try_select());
    monitor_result = KERNEL_WORKER_SERVICE_RETRY;
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(monitor_calls == 1u);
    assert(monitor_last_batch == KERNEL_WORKER_MONITOR_RX_BATCH);
    assert(kernel_worker_test_block_if_idle());
    stats = worker_stats();
    assert(stats.retry_work == KERNEL_WORKER_MONITOR_RX);
    assert(stats.registered_work ==
           (KERNEL_WORKER_PROCESS_REAP | KERNEL_WORKER_MONITOR_RX |
            KERNEL_WORKER_TRACE_FLUSH));
    assert(stats.class_passes[1] == 1u);
    assert(stats.class_retries[1] == 1u);

    assert(kernel_worker_on_timer() == KERNEL_WORKER_OK);
    assert(kernel_worker_try_select());
    monitor_result = KERNEL_WORKER_SERVICE_COMPLETE;
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(kernel_worker_test_block_if_idle());
    stats = worker_stats();
    assert(stats.pending_work == 0u && stats.retry_work == 0u);
    assert(stats.class_passes[1] == 2u);
    assert(stats.class_retries[1] == 1u);
}

static void test_device_batch_is_measured(void)
{
    KernelPerformanceStats performance;

    assert(kernel_worker_register(KERNEL_WORKER_DEVICE_RESET,
                                  device_service, &device_calls) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_signal(KERNEL_WORKER_DEVICE_RESET) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_try_select());
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(kernel_worker_test_block_if_idle());
    assert(device_calls == 1u);
    assert(worker_stats().dispatch_latency_samples == 5u);
    assert(worker_stats().max_dispatch_latency_cycles == 31u);
    assert(kernel_performance_stats(&performance));
    assert(performance.metric[KERNEL_PERFORMANCE_DEVICE_BATCH].calls == 1u);
    assert(performance.metric[KERNEL_PERFORMANCE_DEVICE_BATCH].samples == 1u);
    assert(performance.metric[
        KERNEL_PERFORMANCE_DEVICE_BATCH].maximum_cycles == 31u);
    assert(performance.metric[KERNEL_PERFORMANCE_DEVICE_BATCH].overruns ==
           0u);
}

static void test_trace_flush_is_bounded_and_retried(void)
{
    KernelWorkerStats stats;

    staged_trace_records = KERNEL_WORKER_TRACE_FLUSH_BATCH + 2u;
    assert(kernel_worker_signal(KERNEL_WORKER_TRACE_FLUSH) ==
           KERNEL_WORKER_OK);
    assert(kernel_worker_try_select());
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(staged_trace_records == 2u);
    assert(kernel_worker_test_block_if_idle());
    stats = worker_stats();
    assert(stats.retry_work == KERNEL_WORKER_TRACE_FLUSH);
    assert(stats.class_retries[4] == 1u);

    assert(kernel_worker_on_timer() == KERNEL_WORKER_OK);
    assert(kernel_worker_try_select());
    assert(kernel_worker_test_service_once() == KERNEL_WORKER_OK);
    assert(staged_trace_records == 0u);
    assert(kernel_worker_test_block_if_idle());
    stats = worker_stats();
    assert(stats.class_passes[4] == 2u);
    assert(stats.class_retries[4] == 1u);
}

int main(void)
{
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 31u);
    test_bounded_worker_state_machine();
    test_registered_service_is_bounded_and_retried();
    test_device_batch_is_measured();
    test_trace_flush_is_bounded_and_retried();
    assert(save_disable_calls == 6u);
    assert(restore_calls == save_disable_calls);
    puts("worker tests passed");
    return 0;
}
