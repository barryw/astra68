#include "dispatch.h"

#include "fault.h"
#include "interrupt.h"
#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "trace.h"
#include "user_copy.h"
#include "worker.h"

#include <astra/syscall.h>
#include <vesta.h>

#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIMER_VECTOR_OFFSET (80u * 4u)

static KernelInterruptDispatchResult interrupt_result;
static bool process_is_active;
static bool maintenance_pending;
static bool worker_select_result;
static bool worker_idle_select_result;
static bool worker_work_pending_result;
static uint32_t interrupt_calls;
static uint32_t interrupt_woken;
static uint32_t timer_calls;
static uint32_t interrupt_wakeup_calls;
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
static KernelFaultReport captured_fault;
static KernelFaultReport panic_fault;
static bool fault_capture_result;
static bool user_copy_result;
static bool panic_expected;
static uint32_t user_copy_calls;
static uint32_t bus_fault_ack_calls;
static uint32_t exception_panic_calls;
static uint32_t classified_panic_calls;
static jmp_buf panic_jump;

typedef struct ObservedTrace {
    uint64_t timestamp;
    uint32_t argument[4];
    uint16_t event;
    uint16_t flags;
    bool explicit_timestamp;
} ObservedTrace;

static ObservedTrace observed_trace[4];
static uint32_t observed_trace_count;

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

static void make_access_frame(uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE],
                              uint16_t status_register,
                              uint16_t special_status,
                              uint32_t fault_address)
{
    memset(frame, 0, KERNEL_EXCEPTION_FRAME_MAX_SIZE);
    write_be16(frame, 0u, status_register);
    write_be32(frame, 2u, 0x02011000u);
    write_be16(frame, 6u, 0xb008u);
    write_be16(frame, 10u, special_status);
    write_be32(frame, 16u, fault_address);
}

static void reset_fakes(void)
{
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 37u);
    interrupt_result = KERNEL_INTERRUPT_TIMER;
    process_is_active = true;
    maintenance_pending = false;
    worker_select_result = false;
    worker_idle_select_result = false;
    worker_work_pending_result = false;
    interrupt_calls = 0u;
    interrupt_woken = 0u;
    timer_calls = 0u;
    interrupt_wakeup_calls = 0u;
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
    memset(&captured_fault, 0, sizeof(captured_fault));
    captured_fault.logical_address = 0x10001000u;
    captured_fault.kind = KERNEL_FAULT_PMMU_TRANSLATION;
    captured_fault.mapping = KERNEL_VM_MAPPING_UNMAPPED;
    fault_capture_result = true;
    user_copy_result = false;
    panic_expected = false;
    user_copy_calls = 0u;
    bus_fault_ack_calls = 0u;
    exception_panic_calls = 0u;
    classified_panic_calls = 0u;
    memset(&panic_fault, 0, sizeof(panic_fault));
    memset(observed_trace, 0, sizeof(observed_trace));
    observed_trace_count = 0u;
}

KernelInterruptDispatchResult kernel_interrupt_dispatch(
    uint32_t *woken_threads)
{
    ++interrupt_calls;
    assert(woken_threads != NULL);
    *woken_threads = interrupt_woken;
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

KernelProcessStatus kernel_process_on_interrupt_wakeup(
    const uint32_t *registers, uint32_t user_stack, const void *raw_frame,
    KernelCpuContext **next_context)
{
    ++interrupt_wakeup_calls;
    timer_registers = registers;
    timer_user_stack = user_stack;
    timer_frame = raw_frame;
    *next_context = &timer_context;
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
    if (registers != NULL &&
        (registers[0] == ASTRA_SYSCALL_EXIT ||
         registers[0] == ASTRA_SYSCALL_THREAD_EXIT))
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

bool kernel_worker_work_pending(void)
{
    return worker_work_pending_result;
}

bool kernel_fault_capture(const KernelExceptionFrame *frame,
                          KernelFaultReport *report)
{
    assert(frame != NULL && frame->access_fault != 0u);
    assert(report != NULL);
    if (!fault_capture_result)
        return false;
    *report = captured_fault;
    return true;
}

void kernel_platform_bus_fault_acknowledge(void)
{
    ++bus_fault_ack_calls;
}

bool kernel_user_copy_handle_fault(void *raw_frame)
{
    assert(raw_frame != NULL);
    ++user_copy_calls;
    return user_copy_result;
}

static bool observe_trace(KernelTraceEvent event, uint16_t flags,
                          uint64_t timestamp, bool explicit_timestamp,
                          uint32_t argument0, uint32_t argument1,
                          uint32_t argument2, uint32_t argument3)
{
    ObservedTrace *trace;

    assert(observed_trace_count <
           sizeof(observed_trace) / sizeof(observed_trace[0]));
    trace = &observed_trace[observed_trace_count++];
    trace->event = (uint16_t)event;
    trace->flags = flags;
    trace->timestamp = timestamp;
    trace->explicit_timestamp = explicit_timestamp;
    trace->argument[0] = argument0;
    trace->argument[1] = argument1;
    trace->argument[2] = argument2;
    trace->argument[3] = argument3;
    return true;
}

bool kernel_trace_write(KernelTraceEvent event, uint16_t flags,
                        uint32_t argument0, uint32_t argument1,
                        uint32_t argument2, uint32_t argument3)
{
    return observe_trace(event, flags, 0u, false, argument0, argument1,
                         argument2, argument3);
}

bool kernel_trace_write_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    return observe_trace(event, flags, timestamp, true, argument0,
                         argument1, argument2, argument3);
}

void kernel_panic(const char *reason)
{
    if (panic_expected)
        longjmp(panic_jump, 1);
    fprintf(stderr, "unexpected kernel panic: %s\n", reason);
    abort();
}

void kernel_exception_panic(const void *frame)
{
    ++exception_panic_calls;
    if (panic_expected)
        longjmp(panic_jump, 1);
    fprintf(stderr, "unexpected exception panic: %p\n", frame);
    abort();
}

void kernel_exception_panic_classified(const void *frame,
                                       const KernelFaultReport *fault)
{
    (void)frame;
    assert(fault != NULL);
    ++classified_panic_calls;
    panic_fault = *fault;
    if (panic_expected)
        longjmp(panic_jump, 1);
    fprintf(stderr, "unexpected classified exception panic\n");
    abort();
}

static void configure_physical_fault(uint8_t kind)
{
    captured_fault.logical_address = 0x40001234u;
    captured_fault.expected_physical = 0xfff00900u;
    captured_fault.bus_address = 0xfff00900u;
    captured_fault.bus_status = 0x00000519u;
    captured_fault.bus_target = 1u;
    captured_fault.bus_lost = 2u;
    captured_fault.bus_cycles_high = 0x01234567u;
    captured_fault.bus_cycles_low = 0x89abcdefu;
    captured_fault.bus_timeout_cycles = 2048u;
    captured_fault.kind = kind;
    captured_fault.mapping = KERNEL_VM_MAPPING_READ_WRITE;
    captured_fault.write = 1u;
    captured_fault.bus_record_present = 1u;
    captured_fault.bus_record_matched = 1u;
}

static void test_supervisor_copy_recovers_only_pmmu_fault(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    user_copy_result = true;
    make_access_frame(frame, 0x2000u, 0x0141u,
                      captured_fault.logical_address);

    assert(kernel_access_entry_dispatch(registers, frame, 0u) ==
           KERNEL_DISPATCH_RESUME);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 0u);
    assert(fault_calls == 0u);
    assert(observed_trace_count == 1u);
    assert(observed_trace[0].event == KERNEL_TRACE_EVENT_PMMU_FAULT);
    assert(!observed_trace[0].explicit_timestamp);
}

static void test_supervisor_copy_recovers_physical_fault(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    configure_physical_fault(KERNEL_FAULT_PHYSICAL_DEVICE);
    user_copy_result = true;
    make_access_frame(frame, 0x2000u, 0x0105u,
                      captured_fault.logical_address);

    assert(kernel_access_entry_dispatch(registers, frame, 0u) ==
           KERNEL_DISPATCH_RESUME);
    assert(classified_panic_calls == 0u);
    assert(exception_panic_calls == 0u);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 1u);
    assert(fault_calls == 0u);
    assert(observed_trace_count == 1u);
    assert(observed_trace[0].event == KERNEL_TRACE_EVENT_PHYSICAL_FAULT);
    assert(observed_trace[0].explicit_timestamp);
    assert(observed_trace[0].timestamp == 0x0123456789abcdefull);
}

static void test_supervisor_physical_fault_without_copy_scope_panics(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    configure_physical_fault(KERNEL_FAULT_PHYSICAL_DEVICE);
    make_access_frame(frame, 0x2000u, 0x0105u,
                      captured_fault.logical_address);
    panic_expected = true;
    if (setjmp(panic_jump) == 0)
        (void)kernel_access_entry_dispatch(registers, frame, 0u);
    panic_expected = false;

    assert(classified_panic_calls == 1u);
    assert(exception_panic_calls == 0u);
    assert(panic_fault.kind == KERNEL_FAULT_PHYSICAL_DEVICE);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 0u);
    assert(fault_calls == 0u);
    assert(observed_trace_count == 1u);
    assert(observed_trace[0].event == KERNEL_TRACE_EVENT_PHYSICAL_FAULT);
    assert(observed_trace[0].explicit_timestamp);
    assert(observed_trace[0].timestamp == 0x0123456789abcdefull);
}

static void test_user_physical_fault_is_acknowledged_and_contained(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    configure_physical_fault(KERNEL_FAULT_PHYSICAL_TIMEOUT);
    make_access_frame(frame, 0x0000u, 0x0101u,
                      captured_fault.logical_address);

    assert(kernel_access_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(classified_panic_calls == 0u);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 1u);
    assert(fault_calls == 1u);
    assert(worker_schedule_calls == 1u);
}

static void test_sdram_fabric_fault_is_system_fatal(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    configure_physical_fault(KERNEL_FAULT_PHYSICAL_TIMEOUT);
    captured_fault.bus_target = BUS_FAULT_TARGET_SDRAM;
    user_copy_result = true;
    make_access_frame(frame, 0x0000u, 0x0101u,
                      captured_fault.logical_address);
    panic_expected = true;
    if (setjmp(panic_jump) == 0)
        (void)kernel_access_entry_dispatch(registers, frame, 0x70000f80u);
    panic_expected = false;

    assert(classified_panic_calls == 1u);
    assert(panic_fault.kind == KERNEL_FAULT_PHYSICAL_TIMEOUT);
    assert(panic_fault.bus_target == BUS_FAULT_TARGET_SDRAM);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 0u);
    assert(fault_calls == 0u);
    assert(worker_schedule_calls == 0u);
}

static void test_stale_bus_record_is_traced_and_retired(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    captured_fault.bus_record_present = 1u;
    captured_fault.bus_address = 0xfff00800u;
    captured_fault.bus_status = 0x00000509u;
    captured_fault.bus_target = 3u;
    captured_fault.bus_cycles_high = 0x11223344u;
    captured_fault.bus_cycles_low = 0x55667788u;
    make_access_frame(frame, 0x0000u, 0x0141u,
                      captured_fault.logical_address);

    assert(kernel_access_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 1u);
    assert(fault_calls == 1u);
    assert(observed_trace_count == 2u);
    assert(observed_trace[0].event == KERNEL_TRACE_EVENT_PHYSICAL_FAULT);
    assert((observed_trace[0].flags &
            KERNEL_FAULT_TRACE_RECORD_STALE) != 0u);
    assert(observed_trace[1].event == KERNEL_TRACE_EVENT_PMMU_FAULT);
}

static void test_unexplained_access_fault_panics(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    reset_fakes();
    captured_fault.kind = KERNEL_FAULT_KERNEL_BUG;
    captured_fault.mapping = KERNEL_VM_MAPPING_READ_WRITE;
    captured_fault.expected_physical = 0x02001000u;
    make_access_frame(frame, 0x0000u, 0x0141u,
                      captured_fault.logical_address);
    panic_expected = true;
    if (setjmp(panic_jump) == 0)
        (void)kernel_access_entry_dispatch(registers, frame, 0x70000f80u);
    panic_expected = false;

    assert(classified_panic_calls == 1u);
    assert(panic_fault.kind == KERNEL_FAULT_KERNEL_BUG);
    assert(user_copy_calls == 1u);
    assert(bus_fault_ack_calls == 0u);
    assert(fault_calls == 0u);
}

static void test_supervisor_interrupt_resumes_handler(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    make_frame(frame, 0x2000u, TIMER_VECTOR_OFFSET);

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70001000u) ==
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

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70000f80u) ==
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

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70000f80u) ==
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

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70001000u) ==
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
    interrupt_result = KERNEL_INTERRUPT_QUARANTINED;

    assert(kernel_interrupt_entry_dispatch(registers, NULL, 0u) ==
           KERNEL_DISPATCH_RESUME);
    assert(interrupt_calls == 1u);
    assert(worker_timer_calls == 0u);
    assert(timer_calls == 0u);
}

static void test_user_device_wakeup_reconsiders_scheduler(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    interrupt_result = KERNEL_INTERRUPT_DEVICE;
    interrupt_woken = 1u;
    make_frame(frame, 0x0000u, TIMER_VECTOR_OFFSET);

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70000f80u) ==
           kernel_dispatch_user_target(&timer_context));
    assert(interrupt_wakeup_calls == 1u);
    assert(timer_registers == registers);
    assert(timer_frame == frame);
    assert(timer_user_stack == 0x70000f80u);
    assert(worker_select_calls == 1u);
    assert(timer_calls == 0u);
}

static void test_device_irq_can_select_deferred_worker(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    interrupt_result = KERNEL_INTERRUPT_DEVICE;
    worker_work_pending_result = true;
    worker_select_result = true;
    make_frame(frame, 0x0000u, TIMER_VECTOR_OFFSET);

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(interrupt_wakeup_calls == 1u);
    assert(worker_select_calls == 1u);
}

static void test_supervisor_device_irq_defers_reschedule(void)
{
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    interrupt_result = KERNEL_INTERRUPT_DEVICE;
    interrupt_woken = 1u;
    make_frame(frame, 0x2000u, TIMER_VECTOR_OFFSET);

    assert(kernel_interrupt_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_RESUME);
    assert(interrupt_wakeup_calls == 0u);
    assert(worker_select_calls == 0u);
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

static void test_thread_lifecycle_uses_dedicated_metrics(void)
{
    KernelPerformanceStats stats;
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    make_frame(frame, 0x0000u, ASTRA_SYSCALL_VECTOR * 4u);

    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           kernel_dispatch_user_target(&timer_context));
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_THREAD_CREATE].samples == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_SYSCALL_DISPATCH].samples == 0u);

    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    assert(kernel_syscall_entry_dispatch(registers, frame, 0x70000f80u) ==
           KERNEL_DISPATCH_WORKER);
    assert(kernel_performance_stats(&stats));
    assert(stats.metric[KERNEL_PERFORMANCE_THREAD_EXIT].samples == 1u);
    assert(stats.metric[KERNEL_PERFORMANCE_SYSCALL_DISPATCH].samples == 0u);
}

static void test_shared_ipc_uses_dedicated_metrics(void)
{
    static const struct {
        uint32_t syscall;
        KernelPerformanceMetric metric;
    } cases[] = {
        { ASTRA_SYSCALL_AREA_CREATE, KERNEL_PERFORMANCE_AREA_CREATE },
        { ASTRA_SYSCALL_AREA_MAP, KERNEL_PERFORMANCE_AREA_MAP },
        { ASTRA_SYSCALL_AREA_UNMAP, KERNEL_PERFORMANCE_AREA_UNMAP },
        { ASTRA_SYSCALL_RING_NOTIFY, KERNEL_PERFORMANCE_RING_NOTIFY },
        { ASTRA_SYSCALL_LIBRARY_ATTACH, KERNEL_PERFORMANCE_LIBRARY_ATTACH },
    };
    uint32_t registers[15] = {0u};
    uint8_t frame[8];

    reset_fakes();
    make_frame(frame, 0x0000u, ASTRA_SYSCALL_VECTOR * 4u);
    for (uint32_t index = 0u;
         index < sizeof(cases) / sizeof(cases[0]); ++index) {
        KernelPerformanceStats stats;

        registers[0] = cases[index].syscall;
        assert(kernel_syscall_entry_dispatch(registers, frame,
                                             0x70000f80u) ==
               kernel_dispatch_user_target(&timer_context));
        assert(kernel_performance_stats(&stats));
        assert(stats.metric[cases[index].metric].samples == 1u);
        assert(stats.metric[KERNEL_PERFORMANCE_SYSCALL_DISPATCH].samples ==
               0u);
    }
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
    test_supervisor_copy_recovers_only_pmmu_fault();
    test_supervisor_copy_recovers_physical_fault();
    test_supervisor_physical_fault_without_copy_scope_panics();
    test_user_physical_fault_is_acknowledged_and_contained();
    test_sdram_fabric_fault_is_system_fatal();
    test_stale_bus_record_is_traced_and_retired();
    test_unexplained_access_fault_panics();
    test_supervisor_interrupt_resumes_handler();
    test_user_interrupt_enters_scheduler();
    test_timer_retry_preempts_user_for_worker();
    test_user_interrupt_without_process_returns();
    test_unhandled_interrupt_does_not_inspect_frame();
    test_user_device_wakeup_reconsiders_scheduler();
    test_device_irq_can_select_deferred_worker();
    test_supervisor_device_irq_defers_reschedule();
    test_last_process_exit_selects_worker();
    test_last_process_fault_selects_worker();
    test_nonexit_syscall_resumes_user();
    test_thread_lifecycle_uses_dedicated_metrics();
    test_shared_ipc_uses_dedicated_metrics();
    test_last_runnable_block_selects_idle_worker();
    puts("dispatch tests passed");
    return 0;
}
