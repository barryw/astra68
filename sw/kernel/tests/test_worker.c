#include "worker.h"

#include "panic.h"
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

KernelProcessStatus kernel_process_maintenance(void)
{
    assert(interrupts_enabled);
    ++maintenance_calls;
    if (signal_during_maintenance) {
        signal_during_maintenance = false;
        assert(kernel_worker_signal(KERNEL_WORKER_PROCESS_REAP) ==
               KERNEL_WORKER_OK);
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
    assert(kernel_worker_select_idle());
    assert(!kernel_worker_select_idle());
    assert(kernel_worker_test_block_if_idle());
    assert(worker_stats().state == KERNEL_WORKER_BLOCKED);

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
    assert(stats.dispatches == 4u);
    assert(stats.service_passes == 4u);
    assert(stats.deferred_passes == 1u);
    assert(stats.retry_wakeups == 1u);
    assert(maintenance_calls == 4u);
    assert(enable_calls == 4u);
    assert(disable_calls == 4u);
}

int main(void)
{
    test_bounded_worker_state_machine();
    puts("worker tests passed");
    return 0;
}
