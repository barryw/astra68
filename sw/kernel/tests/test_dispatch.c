#include "dispatch.h"

#include "panic.h"
#include "platform.h"
#include "process.h"
#include "user_copy.h"

#include <astra/syscall.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMER_VECTOR_OFFSET (80u * 4u)

static bool interrupt_result;
static bool process_is_active;
static uint32_t interrupt_calls;
static uint32_t maintenance_calls;
static uint32_t timer_calls;
static uint32_t syscall_calls;
static uint32_t fault_calls;
static uint32_t enable_interrupt_calls;
static uint32_t disable_interrupt_calls;
static bool interrupts_enabled;
static bool maintenance_saw_interrupts_enabled;
static uint32_t cpu_cycle_count;
static uint32_t cpu_cycle_step;
static KernelProcessStatus maintenance_result;
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

static void make_timer_frame(uint8_t frame[8], uint16_t status_register)
{
    write_be16(frame, 0u, status_register);
    write_be32(frame, 2u, 0x00101234u);
    write_be16(frame, 6u, TIMER_VECTOR_OFFSET);
}

static void reset_fakes(void)
{
    interrupt_result = true;
    process_is_active = true;
    interrupt_calls = 0u;
    maintenance_calls = 0u;
    timer_calls = 0u;
    syscall_calls = 0u;
    fault_calls = 0u;
    enable_interrupt_calls = 0u;
    disable_interrupt_calls = 0u;
    interrupts_enabled = false;
    maintenance_saw_interrupts_enabled = false;
    cpu_cycle_count = 100u;
    cpu_cycle_step = 37u;
    maintenance_result = KERNEL_PROCESS_OK;
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

void kernel_enable_interrupts(void)
{
    ++enable_interrupt_calls;
    interrupts_enabled = true;
}

void kernel_disable_interrupts(void)
{
    ++disable_interrupt_calls;
    interrupts_enabled = false;
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

KernelProcessStatus kernel_process_maintenance(void)
{
    ++maintenance_calls;
    maintenance_saw_interrupts_enabled = interrupts_enabled;
    return maintenance_result;
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

KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context)
{
    (void)registers;
    (void)user_stack;
    (void)raw_frame;
    ++syscall_calls;
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
    *next_context = fault_result == KERNEL_PROCESS_OK ? &timer_context : NULL;
    if (fault_result == KERNEL_PROCESS_NO_RUNNABLE)
        process_is_active = false;
    return fault_result;
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
    make_timer_frame(frame, 0x2000u);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70001000u) ==
           NULL);
    assert(interrupt_calls == 1u);
    assert(maintenance_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_user_interrupt_enters_scheduler(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    make_timer_frame(frame, 0x0000u);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70000f80u) ==
           &timer_context);
    assert(interrupt_calls == 1u);
    assert(maintenance_calls == 0u);
    assert(timer_calls == 1u);
    assert(timer_registers == registers);
    assert(timer_frame == frame);
    assert(timer_user_stack == 0x70000f80u);
}

static void test_user_interrupt_without_process_returns(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    process_is_active = false;
    make_timer_frame(frame, 0x0000u);

    assert(kernel_timer_entry_dispatch(registers, frame, 0x70001000u) ==
           NULL);
    assert(interrupt_calls == 1u);
    assert(maintenance_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_unhandled_interrupt_does_not_inspect_frame(void)
{
    uint32_t registers[15] = {0u};

    reset_fakes();
    interrupt_result = false;

    assert(kernel_timer_entry_dispatch(registers, NULL, 0u) == NULL);
    assert(interrupt_calls == 1u);
    assert(maintenance_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_last_process_exit_enters_idle(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    syscall_result = KERNEL_PROCESS_NO_RUNNABLE;
    make_timer_frame(frame, 0x0000u);
    write_be16(frame, 6u, ASTRA_SYSCALL_VECTOR * 4u);

    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           NULL);
    assert(syscall_calls == 1u);
    assert(maintenance_calls == 1u);
    assert(enable_interrupt_calls == 1u);
    assert(disable_interrupt_calls == 1u);
    assert(maintenance_saw_interrupts_enabled);
    assert(!interrupts_enabled);
    assert(!process_is_active);
}

static void test_last_process_fault_enters_idle(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    fault_result = KERNEL_PROCESS_NO_RUNNABLE;
    make_timer_frame(frame, 0x0000u);
    write_be16(frame, 6u, 4u * 4u);

    assert(kernel_exception_entry_dispatch(registers, frame, 0x70000f80u) ==
           NULL);
    assert(fault_calls == 1u);
    assert(maintenance_calls == 0u);
    assert(enable_interrupt_calls == 0u);
    assert(disable_interrupt_calls == 0u);
    assert(!interrupts_enabled);
    assert(!process_is_active);
    assert(kernel_dispatch_user_fault_irqoff_max_cycles() == 37u);
}

static void test_idle_runs_deferred_work(void)
{
    reset_fakes();
    maintenance_result = KERNEL_PROCESS_DEFERRED;
    interrupts_enabled = true;

    kernel_idle_maintenance();
    assert(maintenance_calls == 1u);
    assert(maintenance_saw_interrupts_enabled);
    assert(interrupts_enabled);
    assert(enable_interrupt_calls == 0u);
    assert(disable_interrupt_calls == 0u);
}

int main(void)
{
    test_supervisor_interrupt_resumes_handler();
    test_user_interrupt_enters_scheduler();
    test_user_interrupt_without_process_returns();
    test_unhandled_interrupt_does_not_inspect_frame();
    test_last_process_exit_enters_idle();
    test_last_process_fault_enters_idle();
    test_idle_runs_deferred_work();
    puts("dispatch tests passed");
    return 0;
}
