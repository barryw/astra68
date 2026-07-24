#include "dispatch.h"

#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "user_copy.h"
#include "worker.h"

#include <astra/syscall.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMER_VECTOR_OFFSET (80u * 4u)

static bool interrupt_result;
static bool process_is_active;
static bool maintenance_pending;
static bool worker_select_result;
static bool worker_idle_select_result;
static uint32_t interrupt_calls;
static uint32_t timer_calls;
static uint32_t syscall_calls;
static uint32_t fault_calls;
static uint32_t worker_signal_calls;
static uint32_t worker_schedule_calls;
static uint32_t worker_timer_calls;
static uint32_t worker_select_calls;
static uint32_t worker_idle_select_calls;
static uint32_t supervisor_timer_calls;
static uint32_t cpu_cycle_count;
static uint32_t cpu_cycle_step;
static KernelProcessStatus syscall_result;
static KernelProcessStatus fault_result;
static const uint32_t *timer_registers;
static const void *timer_frame;
static uint32_t timer_user_stack;
static KernelCpuContext timer_context;

static void write_be16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1u] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1u] = (uint8_t)(value >> 16);
    bytes[offset + 2u] = (uint8_t)(value >> 8);
    bytes[offset + 3u] = (uint8_t)value;
}

static void make_frame(uint8_t frame[8], uint16_t status_register,
                       uint16_t vector_offset)
{
    write_be16(frame, 0u, status_register);
    write_be32(frame, 2u, 0x00101234u);
    write_be16(frame, 6u, vector_offset);
}

static void reset_fakes(void)
{
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 37u);
    interrupt_result = true;
    process_is_active = true;
    maintenance_pending = false;
    worker_select_result = false;
    worker_idle_select_result = false;
    interrupt_calls = 0u;
    timer_calls = 0u;
    syscall_calls = 0u;
    fault_calls = 0u;
    worker_signal_calls = 0u;
    worker_schedule_calls = 0u;
    worker_timer_calls = 0u;
    worker_select_calls = 0u;
    worker_idle_select_calls = 0u;
    supervisor_timer_calls = 0u;
    cpu_cycle_count = 100u;
    cpu_cycle_step = 37u;
    syscall_result = KERNEL_PROCESS_OK;
    fault_result = KERNEL_PROCESS_OK;
    timer_registers = NULL;
    timer_frame = NULL;
    timer_user_stack = 0u;
}

bool kernel_interrupt_dispatch(void)
{
    ++interrupt_calls;
    return interrupt_result;
}

uint32_t kernel_platform_cpu_cycles_low(void)
{
    uint32_t current = cpu_cycle_count;

    cpu_cycle_count += cpu_cycle_step;
    return current;
}

bool kernel_process_active(void)
{
    return process_is_active;
}

bool kernel_process_maintenance_pending(void)
{
    return maintenance_pending;
}

KernelProcessStatus kernel_process_on_timer(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context)
{
    ++timer_calls;
    timer_registers = registers;
    timer_frame = raw_frame;
    timer_user_stack = user_stack;
    *next_context = &timer_context;
    return KERNEL_PROCESS_OK;
}

KernelProcessStatus kernel_process_on_supervisor_timer(void)
{
    ++supervisor_timer_calls;
    return KERNEL_PROCESS_OK;
}

KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context)
{
    (void)user_stack;
    (void)raw_frame;
    ++syscall_calls;
    if (registers != NULL && registers[0] == ASTRA_SYSCALL_EXIT)
        maintenance_pending = true;
    *next_context = syscall_result == KERNEL_PROCESS_OK ? &timer_context : NULL;
    if (syscall_result == KERNEL_PROCESS_NO_RUNNABLE)
        process_is_active = false;
    return syscall_result;
}

KernelProcessStatus kernel_process_on_fault(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context)
{
    (void)registers;
    (void)user_stack;
    (void)raw_frame;
    ++fault_calls;
    maintenance_pending = true;
    *next_context = fault_result == KERNEL_PROCESS_OK ? &timer_context : NULL;
    if (fault_result == KERNEL_PROCESS_NO_RUNNABLE)
        process_is_active = false;
    return fault_result;
}

KernelWorkerStatus kernel_worker_signal(uint32_t work)
{
    assert(work == KERNEL_WORKER_PROCESS_REAP);
    ++worker_signal_calls;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_schedule(uint32_t work)
{
    assert(work == KERNEL_WORKER_PROCESS_REAP);
    ++worker_schedule_calls;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_on_timer(void)
{
    ++worker_timer_calls;
    return KERNEL_WORKER_OK;
}

bool kernel_worker_try_select(void)
{
    ++worker_select_calls;
    return worker_select_result;
}

bool kernel_worker_select_idle(void)
{
    ++worker_idle_select_calls;
    return worker_idle_select_result;
}

bool kernel_user_copy_handle_fault(void *raw_frame)
{
    (void)raw_frame;
    return false;
}

void kernel_panic(const char *reason)
{
    fprintf(stderr, "unexpected kernel panic: %s\n", reason);
    abort();
}

void kernel_exception_panic(const void *frame)
{
    fprintf(stderr, "unexpected exception panic: %p\n", frame);
    abort();
}

static void test_supervisor_interrupt_resumes_handler(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    make_frame(frame, 0x2000u, TIMER_VECTOR_OFFSET);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70001000u) ==
           KERNEL_DISPATCH_RESUME);
    assert(interrupt_calls == 1u);
    assert(worker_timer_calls == 1u);
    assert(worker_select_calls == 0u);
    assert(supervisor_timer_calls == 1u);
    assert(timer_calls == 0u);
    assert(kernel_dispatch_last_supervisor_irq_pc() == 0x00101234u);
    assert(kernel_dispatch_last_supervisor_irq_sr() == 0x2000u);
}

static void test_user_interrupt_enters_scheduler(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    make_frame(frame, 0x0000u, TIMER_VECTOR_OFFSET);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70000f80u) ==
           kernel_dispatch_user_target(&timer_context));
    assert(interrupt_calls == 1u);
    assert(worker_timer_calls == 1u);
    assert(worker_select_calls == 1u);
    assert(timer_calls == 1u);
    assert(timer_registers == registers);
    assert(timer_frame == frame);
    assert(timer_user_stack == 0x70000f80u);
    assert(supervisor_timer_calls == 0u);
}

static void test_timer_retry_preempts_user_for_worker(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    worker_select_result = true;
    make_frame(frame, 0x0000u, TIMER_VECTOR_OFFSET);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(timer_calls == 1u);
    assert(worker_timer_calls == 1u);
    assert(worker_select_calls == 1u);
}

static void test_user_interrupt_without_process_returns(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    process_is_active = false;
    make_frame(frame, 0x0000u, TIMER_VECTOR_OFFSET);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70001000u) ==
           KERNEL_DISPATCH_RESUME);
    assert(interrupt_calls == 1u);
    assert(worker_timer_calls == 1u);
    assert(worker_select_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_unhandled_interrupt_does_not_inspect_frame(void)
{
    uint32_t registers[15] = {0u};

    reset_fakes();
    interrupt_result = false;

    assert(kernel_timer_entry_dispatch(registers, NULL, 0u) ==
           KERNEL_DISPATCH_RESUME);
    assert(interrupt_calls == 1u);
    assert(worker_timer_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_last_process_exit_selects_worker(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    syscall_result = KERNEL_PROCESS_NO_RUNNABLE;
    registers[0] = ASTRA_SYSCALL_EXIT;
    make_frame(frame, 0x0000u, ASTRA_SYSCALL_VECTOR * 4u);

    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(syscall_calls == 1u);
    assert(worker_schedule_calls == 1u);
    assert(worker_signal_calls == 0u);
    assert(worker_select_calls == 0u);
    assert(!process_is_active);
}

static void test_last_process_fault_selects_worker(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    fault_result = KERNEL_PROCESS_NO_RUNNABLE;
    make_frame(frame, 0x0000u, 4u * 4u);

    assert(kernel_exception_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(fault_calls == 1u);
    assert(worker_schedule_calls == 1u);
    assert(worker_signal_calls == 0u);
    assert(worker_select_calls == 0u);
    assert(!process_is_active);
    assert(kernel_dispatch_user_fault_irqoff_max_cycles() == 37u);
}

static void test_nonexit_syscall_resumes_user(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    make_frame(frame, 0x0000u, ASTRA_SYSCALL_VECTOR * 4u);

    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           kernel_dispatch_user_target(&timer_context));
    assert(syscall_calls == 1u);
    assert(worker_signal_calls == 0u);
    assert(worker_select_calls == 1u);
    assert(worker_idle_select_calls == 0u);
}

static void test_last_runnable_block_selects_idle_worker(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    syscall_result = KERNEL_PROCESS_NO_RUNNABLE;
    worker_idle_select_result = true;
    registers[0] = 0x7f000003u;
    make_frame(frame, 0x0000u, ASTRA_SYSCALL_VECTOR * 4u);

    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(syscall_calls == 1u);
    assert(worker_select_calls == 1u);
    assert(worker_idle_select_calls == 1u);
    assert(worker_schedule_calls == 0u);
    assert(!process_is_active);
}

int main(void)
{
    test_supervisor_interrupt_resumes_handler();
    test_user_interrupt_enters_scheduler();
    test_timer_retry_preempts_user_for_worker();
    test_user_interrupt_without_process_returns();
    test_unhandled_interrupt_does_not_inspect_frame();
    test_last_process_exit_selects_worker();
    test_last_process_fault_selects_worker();
    test_nonexit_syscall_resumes_user();
    test_last_runnable_block_selects_idle_worker();
    puts("dispatch tests passed");
    return 0;
}
