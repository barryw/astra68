#include "worker.h"

#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "trace.h"

#include <stddef.h>

#define KERNEL_WORKER_WORK_MASK ((1u << KERNEL_WORKER_CLASS_COUNT) - 1u)
#define KERNEL_WORKER_STACK_CANARY 0x4b575354u
#define KERNEL_WORKER_STACK_POISON 0xa55aa55au
#define KERNEL_WORKER_SAVED_REGISTERS 11u

#if defined(KERNEL_WORKER_HOST_TEST)
static _Alignas(16) uint32_t host_worker_stack[
    KERNEL_WORKER_STACK_SIZE / sizeof(uint32_t)];
#else
extern uint8_t _kernel_worker_stack_bottom[];
extern uint8_t _kernel_worker_stack_top[];
#endif

/* Loaded into MSP by worker.S when the scheduler selects the worker. */
uint32_t kernel_worker_saved_msp;
/* Incremented at the ISP-to-MSP restore boundary in worker.S. */
uint32_t kernel_worker_restore_entries;

static volatile uint32_t pending_work;
static volatile uint32_t retry_work;
static volatile uint8_t worker_state;
static KernelWorkerService class_service[KERNEL_WORKER_CLASS_COUNT];
static void *class_context[KERNEL_WORKER_CLASS_COUNT];
static uint32_t registered_work;
static uint32_t signal_count;
static uint32_t dispatch_count;
static uint32_t service_pass_count;
static uint32_t deferred_pass_count;
static uint32_t retry_wakeup_count;
static uint32_t user_yield_count;
static uint32_t idle_wait_count;
static uint32_t main_entry_count;
static uint32_t ready_started;
static uint32_t dispatch_latency_samples;
static uint32_t max_dispatch_latency_cycles;
static uint32_t class_pass_count[KERNEL_WORKER_CLASS_COUNT];
static uint32_t class_retry_count[KERNEL_WORKER_CLASS_COUNT];
static uint8_t ready_timed;

static volatile uint32_t *stack_bottom(void)
{
#if defined(KERNEL_WORKER_HOST_TEST)
    return host_worker_stack;
#else
    return (volatile uint32_t *)(void *)_kernel_worker_stack_bottom;
#endif
}

static volatile uint32_t *stack_top(void)
{
#if defined(KERNEL_WORKER_HOST_TEST)
    return host_worker_stack +
           KERNEL_WORKER_STACK_SIZE / sizeof(uint32_t);
#else
    return (volatile uint32_t *)(void *)_kernel_worker_stack_top;
#endif
}

static void clear_runtime_state(void)
{
    pending_work = 0u;
    retry_work = 0u;
    signal_count = 0u;
    dispatch_count = 0u;
    service_pass_count = 0u;
    deferred_pass_count = 0u;
    retry_wakeup_count = 0u;
    user_yield_count = 0u;
    idle_wait_count = 0u;
    kernel_worker_restore_entries = 0u;
    main_entry_count = 0u;
    ready_started = 0u;
    dispatch_latency_samples = 0u;
    max_dispatch_latency_cycles = 0u;
    ready_timed = 0u;
    registered_work = KERNEL_WORKER_PROCESS_REAP |
                      KERNEL_WORKER_TRACE_FLUSH;
    for (uint32_t index = 0u; index < KERNEL_WORKER_CLASS_COUNT; ++index) {
        class_service[index] = NULL;
        class_context[index] = NULL;
        class_pass_count[index] = 0u;
        class_retry_count[index] = 0u;
    }
}

static void mark_worker_ready(void)
{
    if (worker_state != KERNEL_WORKER_BLOCKED)
        return;
    ready_started = kernel_performance_cycles_low();
    ready_timed = 1u;
    worker_state = KERNEL_WORKER_READY;
}

static void record_worker_dispatch(void)
{
    uint32_t elapsed;

    if (ready_timed == 0u)
        return;
    elapsed = kernel_performance_cycles_low() - ready_started;
    if (dispatch_latency_samples != UINT32_MAX)
        ++dispatch_latency_samples;
    if (elapsed > max_dispatch_latency_cycles)
        max_dispatch_latency_cycles = elapsed;
    ready_timed = 0u;
}

static bool single_work_bit(uint32_t work)
{
    return work != 0u && (work & (work - 1u)) == 0u;
}

static uint32_t work_class(uint32_t work)
{
    uint32_t index = 0u;

    while ((work >> index) != 1u)
        ++index;
    return index;
}

static uint32_t class_batch_limit(uint32_t index)
{
    switch (1u << index) {
    case KERNEL_WORKER_MONITOR_RX:
        return KERNEL_WORKER_MONITOR_RX_BATCH;
    case KERNEL_WORKER_MONITOR_SPI:
        return KERNEL_WORKER_MONITOR_SPI_BATCH;
    case KERNEL_WORKER_DEVICE_RESET:
        return KERNEL_WORKER_DEVICE_RESET_BATCH;
    case KERNEL_WORKER_TRACE_FLUSH:
        return KERNEL_WORKER_TRACE_FLUSH_BATCH;
    case KERNEL_WORKER_IRQ_DISPATCH:
        return KERNEL_WORKER_IRQ_DISPATCH_BATCH;
    default:
        return 1u;
    }
}

static bool stack_canary_valid(void)
{
    return *stack_bottom() == KERNEL_WORKER_STACK_CANARY;
}

static uint32_t stack_high_water(void)
{
    volatile uint32_t *current = stack_bottom() + 1;
    volatile uint32_t *top = stack_top();

    while (current < top && *current == KERNEL_WORKER_STACK_POISON)
        ++current;
    return (uint32_t)((uintptr_t)top - (uintptr_t)current);
}

KernelWorkerStatus kernel_worker_init(void)
{
    volatile uint32_t *bottom = stack_bottom();
    volatile uint32_t *top = stack_top();
    uintptr_t saved;

    if (worker_state != KERNEL_WORKER_UNINITIALIZED ||
        (uint32_t)((uintptr_t)top - (uintptr_t)bottom) !=
            KERNEL_WORKER_STACK_SIZE)
        return KERNEL_WORKER_INVALID_STATE;

    bottom[0] = KERNEL_WORKER_STACK_CANARY;
    for (volatile uint32_t *word = bottom + 1; word < top; ++word)
        *word = KERNEL_WORKER_STACK_POISON;

    saved = (uintptr_t)top - sizeof(uint32_t);
    *(volatile uint32_t *)saved =
        (uint32_t)(uintptr_t)kernel_worker_main;
    for (uint32_t index = 0u; index < KERNEL_WORKER_SAVED_REGISTERS;
         ++index) {
        saved -= sizeof(uint32_t);
        *(volatile uint32_t *)saved = 0u;
    }
    kernel_worker_saved_msp = (uint32_t)saved;
    clear_runtime_state();
    worker_state = KERNEL_WORKER_BLOCKED;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_register(uint32_t work,
                                          KernelWorkerService service,
                                          void *context)
{
    uint32_t index;

    if (worker_state == KERNEL_WORKER_UNINITIALIZED)
        return KERNEL_WORKER_INVALID_STATE;
    if (!single_work_bit(work) ||
        (work & ~KERNEL_WORKER_WORK_MASK) != 0u ||
        work == KERNEL_WORKER_PROCESS_REAP || service == NULL)
        return KERNEL_WORKER_INVALID_ARGUMENT;
    if (worker_state != KERNEL_WORKER_BLOCKED || pending_work != 0u ||
        retry_work != 0u || (registered_work & work) != 0u)
        return KERNEL_WORKER_INVALID_STATE;
    index = work_class(work);
    if (index >= KERNEL_WORKER_CLASS_COUNT)
        return KERNEL_WORKER_CORRUPT;
    class_service[index] = service;
    class_context[index] = context;
    registered_work |= work;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_signal(uint32_t work)
{
    uint16_t saved_status;

    if (worker_state == KERNEL_WORKER_UNINITIALIZED)
        return KERNEL_WORKER_INVALID_STATE;
    if (work == 0u || (work & ~registered_work) != 0u)
        return KERNEL_WORKER_INVALID_ARGUMENT;

    saved_status = kernel_interrupt_save_disable();
    pending_work |= work;
    ++signal_count;
    mark_worker_ready();
    kernel_interrupt_restore(saved_status);
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_schedule(uint32_t work)
{
    if (worker_state == KERNEL_WORKER_UNINITIALIZED ||
        worker_state == KERNEL_WORKER_RUNNING)
        return KERNEL_WORKER_INVALID_STATE;
    if (work == 0u || (work & ~registered_work) != 0u)
        return KERNEL_WORKER_INVALID_ARGUMENT;
    if (!kernel_process_worker_enter())
        return KERNEL_WORKER_CORRUPT;

    pending_work |= work;
    ++signal_count;
    worker_state = KERNEL_WORKER_RUNNING;
    record_worker_dispatch();
    ++dispatch_count;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_on_timer(void)
{
    uint32_t retry;

    if (worker_state == KERNEL_WORKER_UNINITIALIZED)
        return KERNEL_WORKER_INVALID_STATE;
    retry = retry_work;
    if (retry == 0u)
        return KERNEL_WORKER_OK;

    retry_work = 0u;
    pending_work |= retry;
    ++retry_wakeup_count;
    mark_worker_ready();
    return KERNEL_WORKER_OK;
}

bool kernel_worker_try_select(void)
{
    if (worker_state != KERNEL_WORKER_READY || pending_work == 0u)
        return false;
    if (!kernel_process_worker_enter())
        return false;
    worker_state = KERNEL_WORKER_RUNNING;
    record_worker_dispatch();
    ++dispatch_count;
    return true;
}

bool kernel_worker_select_idle(void)
{
    if (worker_state != KERNEL_WORKER_BLOCKED || pending_work != 0u)
        return false;
    if (!kernel_process_worker_enter())
        return false;
    worker_state = KERNEL_WORKER_RUNNING;
    ++dispatch_count;
    return true;
}

bool kernel_worker_work_pending(void)
{
    return worker_state == KERNEL_WORKER_READY && pending_work != 0u;
}

static KernelWorkerStatus service_once(void)
{
    KernelProcessStatus process_status = KERNEL_PROCESS_OK;
    KernelWorkerStatus result = KERNEL_WORKER_OK;
    uint32_t work;

    if (worker_state != KERNEL_WORKER_RUNNING || !stack_canary_valid())
        return KERNEL_WORKER_CORRUPT;
    work = pending_work;
    if (work == 0u)
        return KERNEL_WORKER_INVALID_STATE;
    pending_work = 0u;

    kernel_enable_interrupts();
    if ((work & KERNEL_WORKER_PROCESS_REAP) != 0u)
        process_status = kernel_process_maintenance();
    for (uint32_t index = 1u; index < KERNEL_WORKER_CLASS_COUNT; ++index) {
        uint32_t bit = 1u << index;
        KernelPerformanceToken performance = {
            0u, 0u, 0u, (uint8_t)KERNEL_PERFORMANCE_METRIC_COUNT
        };
        KernelWorkerServiceResult service_result;

        if ((work & bit) == 0u)
            continue;
        if (bit == KERNEL_WORKER_TRACE_FLUSH) {
            (void)kernel_trace_flush_staged(class_batch_limit(index));
            ++class_pass_count[index];
            if (kernel_trace_staged_pending()) {
                retry_work |= bit;
                ++class_retry_count[index];
                ++deferred_pass_count;
            }
            continue;
        }
        if (class_service[index] == NULL) {
            result = KERNEL_WORKER_CORRUPT;
            break;
        }
        if (bit >= KERNEL_WORKER_DEVICE_RESET)
            performance = kernel_performance_begin(
                KERNEL_PERFORMANCE_DEVICE_BATCH);
        service_result = class_service[index](class_batch_limit(index),
                                              class_context[index]);
        kernel_performance_end(performance);
        ++class_pass_count[index];
        if (service_result == KERNEL_WORKER_SERVICE_RETRY) {
            retry_work |= bit;
            ++class_retry_count[index];
            ++deferred_pass_count;
        } else if (service_result != KERNEL_WORKER_SERVICE_COMPLETE) {
            result = KERNEL_WORKER_CORRUPT;
            break;
        }
    }
    kernel_disable_interrupts();
    ++service_pass_count;

    if (result != KERNEL_WORKER_OK)
        return result;

    if (process_status == KERNEL_PROCESS_DEFERRED) {
        retry_work |= KERNEL_WORKER_PROCESS_REAP;
        ++deferred_pass_count;
    } else if (process_status != KERNEL_PROCESS_OK) {
        return KERNEL_WORKER_CORRUPT;
    }
    return KERNEL_WORKER_OK;
}

static bool block_if_idle(void)
{
    if (worker_state != KERNEL_WORKER_RUNNING || pending_work != 0u)
        return false;
    worker_state = KERNEL_WORKER_BLOCKED;
    return true;
}

bool kernel_worker_stats(KernelWorkerStats *stats)
{
    if (stats == NULL || worker_state == KERNEL_WORKER_UNINITIALIZED)
        return false;
    stats->pending_work = pending_work;
    stats->retry_work = retry_work;
    stats->signals = signal_count;
    stats->dispatches = dispatch_count;
    stats->service_passes = service_pass_count;
    stats->deferred_passes = deferred_pass_count;
    stats->retry_wakeups = retry_wakeup_count;
    stats->user_yields = user_yield_count;
    stats->idle_waits = idle_wait_count;
    stats->restore_entries = kernel_worker_restore_entries;
    stats->main_entries = main_entry_count;
    stats->dispatch_latency_samples = dispatch_latency_samples;
    stats->max_dispatch_latency_cycles = max_dispatch_latency_cycles;
    stats->stack_high_water = stack_high_water();
    stats->registered_work = registered_work;
    for (uint32_t index = 0u; index < KERNEL_WORKER_CLASS_COUNT; ++index) {
        stats->class_passes[index] = class_pass_count[index];
        stats->class_retries[index] = class_retry_count[index];
    }
    stats->state = worker_state;
    stats->stack_canary_ok = stack_canary_valid() ? 1u : 0u;
    stats->reserved[0] = 0u;
    stats->reserved[1] = 0u;
    return true;
}

void kernel_worker_main(void)
{
    ++main_entry_count;
    for (;;) {
        KernelCpuContext *next;

        if (pending_work != 0u && service_once() != KERNEL_WORKER_OK)
            kernel_panic("deferred worker service failed");
        if (pending_work != 0u)
            continue;
        if (!block_if_idle())
            kernel_panic("deferred worker state corrupt");

        next = kernel_process_worker_resume();
        if (next != NULL) {
            ++user_yield_count;
            kernel_worker_switch_to_user(next);
            if (worker_state != KERNEL_WORKER_RUNNING)
                kernel_panic("deferred worker resume corrupt");
            continue;
        }

        for (;;) {
            ++idle_wait_count;
            kernel_worker_arch_wait();
            if (worker_state == KERNEL_WORKER_READY && pending_work != 0u) {
                worker_state = KERNEL_WORKER_RUNNING;
                record_worker_dispatch();
                break;
            }
            next = kernel_process_worker_resume();
            if (next != NULL) {
                ++user_yield_count;
                kernel_worker_switch_to_user(next);
                if (worker_state != KERNEL_WORKER_RUNNING)
                    kernel_panic("deferred worker resume corrupt");
                break;
            }
            if (worker_state != KERNEL_WORKER_BLOCKED)
                kernel_panic("deferred worker wake corrupt");
        }
    }
}

#if defined(KERNEL_WORKER_HOST_TEST)
KernelWorkerStatus kernel_worker_test_service_once(void)
{
    return service_once();
}

bool kernel_worker_test_block_if_idle(void)
{
    return block_if_idle();
}
#endif
