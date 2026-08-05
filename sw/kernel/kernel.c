#include <astra/boot.h>
#include <astra/input.h>

#include "kernel_build_info.h"
#include "allocation.h"
#include "block.h"
#include "bytes.h"
#include "dma.h"
#include "device.h"
#include "dispatch.h"
#include "exception.h"
#include "fault.h"
#include "interrupt.h"
#include "irq.h"
#include "irq_latency.h"
#include "memory.h"
#include "monitor.h"
#include "panic.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "qualification.h"
#include "trace.h"
#include "user_copy.h"
#include "vm.h"
#include "worker.h"
#include "vega.h"
#include "vesta.h"

#define SCREEN_TOP_MARGIN 2u
#define SCREEN_LEFT_MARGIN 2u
#define SCREEN_RIGHT_MARGIN 2u
#define SCREEN_BOTTOM_MARGIN 2u
#define KERNEL_SELFTEST_OWNER 0xfffffff0u
#define KERNEL_SELFTEST_USER_ADDRESS 0x10000000u
#define KERNEL_SOAK_REPORT_INTERVAL 1000u

extern uint8_t _kernel_entry[];
extern uint8_t _kernel_image_start[];
extern uint8_t _kernel_file_end[];
extern uint8_t _kernel_memory_end[];
extern uint8_t _kernel_trace_start[];
extern uint8_t _kernel_trace_end[];
extern uint8_t _kernel_stack_guard[];
extern uint8_t _kernel_vectors[];
extern uint8_t _k1_survivor_image_start[];
extern uint8_t _k1_survivor_image_entry[];
extern uint8_t _k2_sibling_image_entry[];
extern uint8_t _k1_survivor_image_end[];
extern uint8_t _k1_offender_image_start[];
extern uint8_t _k1_offender_image_entry[];
extern uint8_t _k1_offender_image_end[];

uint32_t kernel_read_vbr(void);
void kernel_enter_user(KernelCpuContext *context) __attribute__((noreturn));

static AstraBootInfo boot_info;
static AstraEarlyLog *early_log;
static uint32_t screen_row;
static uint32_t screen_col;
static int screen_enabled;
static uint32_t process_bootstrap_irqoff_cycles;
static uint32_t process_init_cycles;
static uint32_t trace_init_cycles;
static uint32_t qualification_survivor_process_id;
static KernelAddressSpace user_copy_selftest_space;
static const KernelMonitorBuildInfo monitor_build_info = {
    ASTRA_KERNEL_VERSION,
    ASTRA_KERNEL_BUILD_UTC,
    ASTRA_KERNEL_GIT_REVISION
};

static bool input_device_quiesce(uint32_t device_id, uint32_t generation,
                                 void *context)
{
    (void)generation;
    (void)context;
    return device_id == ASTRA_DEVICE_ID_INPUT0 &&
           kernel_platform_input_quiesce();
}

static bool input_device_reset(uint32_t device_id, uint32_t generation,
                               void *context)
{
    (void)generation;
    (void)context;
    return device_id == ASTRA_DEVICE_ID_INPUT0 &&
           kernel_platform_input_reset();
}

static bool register_physical_devices(void)
{
    static const KernelDeviceDefinition input = {
        input_device_quiesce,
        input_device_reset,
        NULL,
        ASTRA_DEVICE_ID_INPUT0,
        ASTRA_DEVICE_CLASS_INPUT,
        INPUT_CAP_KEYBOARD | INPUT_CAP_POINTER
    };

    if (kernel_platform_input_present()) {
        if (kernel_device_register(&input) != KERNEL_DEVICE_OK)
            return false;
    }
    return true;
}
#if ASTRA_KERNEL_SOAK_SELFTEST
static KernelPlatformCycleCount soak_started;
#endif

static void screen_clear(void)
{
    if (!screen_enabled) return;
    for (uint32_t index = 0; index < VEGA_POST_COLS * VEGA_POST_ROWS; ++index) {
        if (!kernel_platform_post_text_write(index, ' ')) {
            screen_enabled = 0;
            return;
        }
    }
    screen_row = SCREEN_TOP_MARGIN;
    screen_col = SCREEN_LEFT_MARGIN;
}

static void screen_scroll(void)
{
    const uint32_t last_row = VEGA_POST_ROWS - SCREEN_BOTTOM_MARGIN - 1u;
    const uint32_t last_col = VEGA_POST_COLS - SCREEN_RIGHT_MARGIN;

    for (uint32_t row = SCREEN_TOP_MARGIN; row < last_row; ++row) {
        for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col) {
            uint8_t value;
            uint32_t destination = row * VEGA_POST_COLS + col;
            uint32_t source = (row + 1u) * VEGA_POST_COLS + col;

            if (!kernel_platform_post_text_read(source, &value) ||
                !kernel_platform_post_text_write(destination, value)) {
                screen_enabled = 0;
                return;
            }
        }
    }
    for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col) {
        if (!kernel_platform_post_text_write(
                last_row * VEGA_POST_COLS + col, ' ')) {
            screen_enabled = 0;
            return;
        }
    }
    screen_row = last_row;
    screen_col = SCREEN_LEFT_MARGIN;
}

static void screen_putc(char value)
{
    const uint32_t last_col = VEGA_POST_COLS - SCREEN_RIGHT_MARGIN;
    const uint32_t last_row = VEGA_POST_ROWS - SCREEN_BOTTOM_MARGIN;

    if (!screen_enabled || value == '\r') return;
    if (value == '\n') {
        screen_col = SCREEN_LEFT_MARGIN;
        ++screen_row;
    } else {
        if (!kernel_platform_post_text_write(
                screen_row * VEGA_POST_COLS + screen_col,
                (uint8_t)value)) {
            screen_enabled = 0;
            return;
        }
        if (++screen_col == last_col) {
            screen_col = SCREEN_LEFT_MARGIN;
            ++screen_row;
        }
    }
    if (screen_row == last_row) screen_scroll();
}

static void diagnostic_uart_putc(char value)
{
    (void)kernel_platform_diagnostic_putc((uint8_t)value);
}

static void console_putc(char value)
{
    screen_putc(value);
    astra_early_log_putc(early_log, value);
    diagnostic_uart_putc(value);
}

static void console_puts(const char *text)
{
    while (*text != '\0') console_putc(*text++);
}

static void console_hex32(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        console_putc((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
    }
}

static char console_hex_digit(uint32_t value)
{
    return (char)(value < 10u ? (uint32_t)'0' + value :
                  (uint32_t)'A' + value - 10u);
}

static void console_dec32(uint32_t value)
{
    static const uint32_t divisors[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    int started = 0;

    for (unsigned index = 0; index < sizeof(divisors) / sizeof(divisors[0]);
         ++index) {
        uint32_t digit = 0u;
        while (value >= divisors[index]) {
            value -= divisors[index];
            ++digit;
        }
        if (digit != 0u || started || divisors[index] == 1u) {
            console_putc((char)('0' + digit));
            started = 1;
        }
    }
}

static void console_init(void)
{
    early_log = (AstraEarlyLog *)ASTRA_EARLY_LOG_ADDRESS;
    if (!astra_early_log_validate(early_log, ASTRA_EARLY_LOG_SIZE))
        astra_early_log_init(early_log, ASTRA_EARLY_LOG_SIZE);
    screen_enabled = kernel_platform_post_text_present();
    screen_clear();
}

static void halt_forever(void) __attribute__((noreturn));
static void halt_forever(void)
{
    for (;;) __asm__ volatile ("stop #0x2700");
}

static void panic_worker_state(void)
{
    KernelInterruptStats interrupt_stats;
    KernelIrqPoolStats irq_stats;
    KernelProcessMaintenanceDiagnostics maintenance;
    KernelWorkerStats stats;

    if (!kernel_worker_stats(&stats))
        return;
    console_puts("Worker: state=");
    console_dec32(stats.state);
    console_puts(" pending=0x");
    console_hex32(stats.pending_work);
    console_puts(" retry=0x");
    console_hex32(stats.retry_work);
    console_puts(" registered=0x");
    console_hex32(stats.registered_work);
    console_puts(" signals=");
    console_dec32(stats.signals);
    console_puts(" dispatches=");
    console_dec32(stats.dispatches);
    console_puts(" passes=");
    console_dec32(stats.service_passes);
    console_puts(" yields=");
    console_dec32(stats.user_yields);
    console_puts(" restores=");
    console_dec32(stats.restore_entries);
    console_puts(" entries=");
    console_dec32(stats.main_entries);
    console_putc('\n');
    if (kernel_interrupt_stats(&interrupt_stats)) {
        console_puts("Deferred IRQ: pending=");
        console_dec32(interrupt_stats.pending);
        console_puts(" max=");
        console_dec32(interrupt_stats.maximum_pending);
        console_puts(" queued=");
        console_dec32(interrupt_stats.queued);
        console_puts(" dispatched=");
        console_dec32(interrupt_stats.dispatched);
        console_puts(" dropped=");
        console_dec32(interrupt_stats.dropped);
        console_putc('\n');
    }
    if (kernel_irq_pool_stats(&irq_stats)) {
        console_puts("IRQ errors: vector=");
        console_dec32(irq_stats.bad_vector_interrupts);
        console_puts(" controller=");
        console_dec32(irq_stats.controller_failures);
        console_puts(" device=");
        console_dec32(irq_stats.device_failures);
        console_puts(" unclaimed=");
        console_dec32(irq_stats.unclaimed_interrupts);
        console_puts(" masked=");
        console_dec32(irq_stats.masked_interrupts);
        console_putc('\n');
    }
    if (!kernel_process_maintenance_diagnostics(&maintenance) ||
        maintenance.failure == KERNEL_PROCESS_MAINTENANCE_NONE)
        return;
    console_puts("Maint: failure=");
    console_dec32(maintenance.failure);
    console_puts(" status=");
    console_dec32(maintenance.status);
    console_puts(" observed=");
    console_dec32(maintenance.observed);
    console_puts(" expected=");
    console_dec32(maintenance.expected);
    console_putc('\n');
}

static void panic_trace_records(void)
{
    KernelTraceRecord records[16];
    uint32_t count = kernel_trace_copy_recent(records, 16u);

    console_puts("Trace: newest ");
    console_dec32(count);
    console_puts(" records\n");
    for (uint32_t index = 0u; index < count; ++index) {
        const KernelTraceRecord *record = &records[index];

        console_puts(" T ");
        console_hex32(record->commit_sequence);
        console_putc(' ');
        console_hex32(record->timestamp_high);
        console_hex32(record->timestamp_low);
        console_puts(" e=");
        console_hex32(record->event);
        console_puts(" f=");
        console_hex32(record->flags);
        for (uint32_t argument = 0u; argument < 4u; ++argument) {
            console_puts(" a=");
            console_hex32(record->argument[argument]);
        }
        console_putc('\n');
    }
}

static void panic_begin(const char *reason)
{
    (void)kernel_trace_write(
        KERNEL_TRACE_EVENT_PANIC, 0u, (uint32_t)(uintptr_t)reason,
        kernel_dispatch_last_supervisor_irq_pc(),
        kernel_dispatch_last_supervisor_irq_sr(),
        kernel_platform_build_id());
    screen_clear();
    early_log->flags |= ASTRA_EARLY_LOG_FLAG_PANIC;
    ++early_log->sequence;
    console_puts("*** AXIOM KERNEL PANIC ***\n\n");
    console_puts("Reason: ");
    console_puts(reason);
    console_putc('\n');
    console_puts("Kernel: v" ASTRA_KERNEL_VERSION "\n");
    console_puts("Built:  " ASTRA_KERNEL_BUILD_UTC "\n");
    console_puts("Git:    " ASTRA_KERNEL_GIT_REVISION "\n");
    console_puts("HW:     0x");
    console_hex32(kernel_platform_build_id());
    console_putc('\n');
    if (kernel_dispatch_last_supervisor_irq_pc() != 0u) {
        console_puts("Last supervisor IRQ: SR=0x");
        console_hex32(kernel_dispatch_last_supervisor_irq_sr());
        console_puts(" PC=0x");
        console_hex32(kernel_dispatch_last_supervisor_irq_pc());
        console_putc('\n');
    }
    panic_worker_state();
    panic_trace_records();
}

static void panic_finish(void) __attribute__((noreturn));
static void panic_finish(void)
{
    console_puts("\nSYSTEM HALTED\n");
    kernel_platform_debug_marker(ASTRA_KERNEL_STATUS_PANIC);
    halt_forever();
}

void kernel_panic(const char *reason)
{
    panic_begin(reason);
    panic_finish();
}

static const char *fault_kind_name(uint8_t kind)
{
    switch ((KernelFaultKind)kind) {
    case KERNEL_FAULT_PMMU_TRANSLATION:
        return "PMMU translation";
    case KERNEL_FAULT_PMMU_PROTECTION:
        return "PMMU protection";
    case KERNEL_FAULT_PHYSICAL_UNMAPPED:
        return "physical unmapped";
    case KERNEL_FAULT_PHYSICAL_TIMEOUT:
        return "physical timeout";
    case KERNEL_FAULT_PHYSICAL_DEVICE:
        return "physical device";
    case KERNEL_FAULT_PHYSICAL_EXTERNAL:
        return "physical external";
    case KERNEL_FAULT_KERNEL_BUG:
        return "kernel access bug";
    default:
        return "invalid";
    }
}

static void panic_fault_report(const KernelFaultReport *fault)
{
    if (fault == NULL)
        return;
    console_puts("\nClass:  ");
    console_puts(fault_kind_name(fault->kind));
    console_puts("\nPhysical: 0x");
    console_hex32(fault->expected_physical);
    if (fault->bus_record_present == 0u)
        return;
    console_puts("\nBus:      0x");
    console_hex32(fault->bus_address);
    console_puts("  status=0x");
    console_hex32(fault->bus_status);
    console_puts("  target=");
    console_dec32(fault->bus_target);
    console_puts("\nBus time: 0x");
    console_hex32(fault->bus_cycles_high);
    console_hex32(fault->bus_cycles_low);
    console_puts("  lost=");
    console_dec32(fault->bus_lost);
    console_puts("  deadline=");
    console_dec32(fault->bus_timeout_cycles);
    console_puts("  matched=");
    console_dec32(fault->bus_record_matched);
}

static void exception_panic(const void *raw_frame,
                            const KernelFaultReport *fault)
    __attribute__((noreturn));
static void exception_panic(const void *raw_frame,
                            const KernelFaultReport *fault)
{
    KernelExceptionFrame frame;
    KernelExceptionStatus status = kernel_exception_decode(
        raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE, &frame);

    panic_begin("unhandled processor exception");
    if (status != KERNEL_EXCEPTION_OK) {
        console_puts("Invalid exception frame: ");
        console_dec32((uint32_t)status);
        console_putc('\n');
        panic_finish();
    }
    console_puts("Vector: ");
    console_dec32(frame.vector_offset >> 2);
    console_puts("  Format: 0x");
    console_putc(console_hex_digit(frame.format));
    console_puts("\nSR:     0x");
    console_hex32(frame.status_register);
    console_puts("\nPC:     0x");
    console_hex32(frame.program_counter);
    if (frame.access_fault != 0u) {
        console_puts("\nSSW:    0x");
        console_hex32(frame.special_status);
        console_puts("\nFault:  0x");
        console_hex32(frame.fault_address);
        panic_fault_report(fault);
    }
    console_putc('\n');
    panic_finish();
}

void kernel_exception_panic(const void *raw_frame)
{
    exception_panic(raw_frame, NULL);
}

void kernel_exception_panic_classified(const void *raw_frame,
                                       const KernelFaultReport *fault)
{
    exception_panic(raw_frame, fault);
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        uint32_t size)
{
    while (size-- != 0u) {
        if (*left++ != *right++)
            return false;
    }
    return true;
}

static void kernel_user_copy_selftest(void)
{
    KernelAddressSpace *space = &user_copy_selftest_space;
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint8_t expected[32];
    uint8_t observed[32];
    uint32_t physical;

    if (!kernel_memory_stats(&before) ||
        kernel_vm_create_address_space(KERNEL_SELFTEST_OWNER, space) !=
            KERNEL_VM_OK ||
        kernel_memory_alloc_tagged(
            KERNEL_ALLOCATION_SITE_BOOT_SELFTEST_PAGE, 1u, 1u,
            KERNEL_FRAME_PROCESS, KERNEL_SELFTEST_OWNER, &physical) !=
            KERNEL_MEMORY_OK)
        kernel_panic("user-copy self-test setup failed");

    for (uint32_t index = 0u; index < sizeof(expected); ++index) {
        expected[index] = (uint8_t)(0x31u + index * 7u);
        ((volatile uint8_t *)(uintptr_t)physical)[index] = expected[index];
    }
    if (kernel_vm_map_page(space, KERNEL_SELFTEST_USER_ADDRESS, physical,
                           KERNEL_VM_READ | KERNEL_VM_WRITE) != KERNEL_VM_OK ||
        kernel_memory_release(physical, 1u, KERNEL_SELFTEST_OWNER) !=
            KERNEL_MEMORY_OK ||
        kernel_vm_switch(space) != KERNEL_VM_OK)
        kernel_panic("user-copy self-test mapping failed");

    if (kernel_copy_from_user(observed, KERNEL_SELFTEST_USER_ADDRESS,
                              sizeof(observed)) != KERNEL_USER_COPY_OK ||
        !bytes_equal(observed, expected, sizeof(observed)))
        kernel_panic("copy-from-user self-test failed");

    for (uint32_t index = 0u; index < sizeof(expected); ++index)
        expected[index] = (uint8_t)(0xe3u - index * 5u);
    if (kernel_copy_to_user(KERNEL_SELFTEST_USER_ADDRESS + 64u, expected,
                            sizeof(expected)) != KERNEL_USER_COPY_OK ||
        kernel_copy_from_user(observed,
                              KERNEL_SELFTEST_USER_ADDRESS + 64u,
                              sizeof(observed)) != KERNEL_USER_COPY_OK ||
        !bytes_equal(observed, expected, sizeof(observed)))
        kernel_panic("copy-to-user self-test failed");

    if (kernel_copy_from_user(observed,
                              KERNEL_SELFTEST_USER_ADDRESS + KERNEL_PAGE_SIZE,
                              1u) != KERNEL_USER_COPY_BAD_ADDRESS ||
        kernel_copy_to_user(KERNEL_SELFTEST_USER_ADDRESS + KERNEL_PAGE_SIZE,
                            expected, 1u) != KERNEL_USER_COPY_BAD_ADDRESS ||
        kernel_copy_from_user(observed, KERNEL_SELFTEST_USER_ADDRESS, 1u) !=
            KERNEL_USER_COPY_OK)
        kernel_panic("user-copy fault recovery failed");

    if (kernel_vm_switch_to_empty() != KERNEL_VM_OK ||
        kernel_vm_destroy_address_space(space) != KERNEL_VM_OK ||
        !kernel_memory_stats(&after) ||
        after.free_frames != before.free_frames)
        kernel_panic("user-copy self-test teardown failed");
}

static void validate_image_contract(void)
{
    uint32_t linked_image_size =
        (uint32_t)(_kernel_file_end - _kernel_image_start);
    uint32_t linked_memory_size =
        (uint32_t)(_kernel_memory_end - _kernel_image_start);

    if (boot_info.cpu_model != CPU_MODEL_68030 ||
        (boot_info.cpu_features & CPU_FEAT_PMMU) == 0u)
        kernel_panic("MC68030 PMMU platform required");
    if (boot_info.kernel_base != (uint32_t)_kernel_image_start ||
        boot_info.kernel_entry != (uint32_t)_kernel_entry ||
        boot_info.kernel_image_size != linked_image_size ||
        boot_info.kernel_memory_size < linked_memory_size)
        kernel_panic("kernel image contract mismatch");
    if ((uint32_t)_kernel_trace_start != ASTRA_KERNEL_TRACE_ADDRESS ||
        (uint32_t)(_kernel_trace_end - _kernel_trace_start) !=
            ASTRA_KERNEL_TRACE_SIZE)
        kernel_panic("retained trace contract mismatch");
    if (boot_info.early_log_base != ASTRA_EARLY_LOG_ADDRESS ||
        boot_info.early_log_size != ASTRA_EARLY_LOG_SIZE)
        kernel_panic("early log contract mismatch");
}

static uint32_t add_saturating_u32(uint32_t left, uint32_t right)
{
    return right > UINT32_MAX - left ? UINT32_MAX : left + right;
}

static void report_kernel_performance(
    const KernelPerformanceStats *performance)
{
    uint32_t wait_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_WAIT_BLOCK].overruns,
        performance->metric[KERNEL_PERFORMANCE_WAKE].overruns);
    uint32_t thread_overruns = add_saturating_u32(
        add_saturating_u32(
            performance->metric[KERNEL_PERFORMANCE_THREAD_CREATE].overruns,
            performance->metric[KERNEL_PERFORMANCE_THREAD_EXIT].overruns),
        performance->metric[KERNEL_PERFORMANCE_THREAD_REAP].overruns);
    uint32_t wait_set_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_WAIT_SET_BLOCK].overruns,
        performance->metric[KERNEL_PERFORMANCE_WAIT_SET_WAKE].overruns);
    uint32_t port_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_PORT_SEND].overruns,
        performance->metric[KERNEL_PERFORMANCE_PORT_RECEIVE].overruns);
    uint32_t shared_ipc_overruns = add_saturating_u32(
        add_saturating_u32(
            performance->metric[KERNEL_PERFORMANCE_AREA_CREATE].overruns,
            performance->metric[KERNEL_PERFORMANCE_AREA_MAP].overruns),
        add_saturating_u32(
            performance->metric[KERNEL_PERFORMANCE_AREA_UNMAP].overruns,
            performance->metric[KERNEL_PERFORMANCE_RING_NOTIFY].overruns));
    uint32_t irq_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_HARD_IRQ].overruns,
        performance->metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].overruns);
    uint32_t endpoint_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_IRQ_READ].overruns,
        performance->metric[KERNEL_PERFORMANCE_IRQ_ACK].overruns);
    uint32_t deferred_overruns = add_saturating_u32(
        performance->metric[KERNEL_PERFORMANCE_DEVICE_BATCH].overruns,
        performance->metric[KERNEL_PERFORMANCE_MONITOR_COMMAND].overruns);

    console_puts("K2 PERF irq syscall=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_SYSCALL_DISPATCH].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_SYSCALL_DISPATCH);
    console_puts(" timer=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_TIMER_DISPATCH].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_TIMER_DISPATCH);
    console_puts(" fault=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_USER_FAULT].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_USER_FAULT);
    console_putc('\n');
    console_puts("K2 PERF sched pick=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_SCHEDULER_PICK].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_SCHEDULER_PICK);
    console_puts(" same=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_SAME_CRP_SWITCH].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_SAME_CRP_SWITCH);
    console_puts(" cross=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_CROSS_CRP_SWITCH].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_CROSS_CRP_SWITCH);
    console_putc('\n');
    console_puts("K2 PERF wait block=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_WAIT_BLOCK].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_WAIT_BLOCK);
    console_puts(" wake=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_WAKE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_WAKE);
    console_puts(" overruns=");
    console_dec32(wait_overruns);
    console_putc('\n');
    console_puts("K3 PERF deadline expire=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_DEADLINE_EXPIRE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_DEADLINE_EXPIRE);
    console_puts(" overruns=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_DEADLINE_EXPIRE].overruns);
    console_putc('\n');
    console_puts("K5 PERF thread create=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_THREAD_CREATE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_THREAD_CREATE);
    console_puts(" exit=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_THREAD_EXIT].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_THREAD_EXIT);
    console_puts(" reap=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_THREAD_REAP].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_THREAD_REAP);
    console_puts(" overruns=");
    console_dec32(thread_overruns);
    console_putc('\n');
    console_puts("K6 PERF wait-set block=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_WAIT_SET_BLOCK].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_WAIT_SET_BLOCK);
    console_puts(" wake=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_WAIT_SET_WAKE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_WAIT_SET_WAKE);
    console_puts(" overruns=");
    console_dec32(wait_set_overruns);
    console_putc('\n');
    console_puts("K7 PERF port send=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_PORT_SEND].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_PORT_SEND);
    console_puts(" receive=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_PORT_RECEIVE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_PORT_RECEIVE);
    console_puts(" overruns=");
    console_dec32(port_overruns);
    console_putc('\n');
    console_puts("K8 PERF area create=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_AREA_CREATE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_AREA_CREATE);
    console_puts(" map=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_AREA_MAP].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_AREA_MAP);
    console_puts(" unmap=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_AREA_UNMAP].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_AREA_UNMAP);
    console_puts(" notify=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_RING_NOTIFY].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_RING_NOTIFY);
    console_puts(" overruns=");
    console_dec32(shared_ipc_overruns);
    console_putc('\n');
    console_puts("K10 PERF irq=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_HARD_IRQ].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_HARD_IRQ);
    console_puts(" wake=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_HARD_IRQ_WAKE].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE);
    console_puts(" overruns=");
    console_dec32(irq_overruns);
    console_putc('\n');
    console_puts("K10 PERF irq min=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_HARD_IRQ].minimum_cycles);
    console_puts(" latest=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_HARD_IRQ].latest_cycles);
    console_puts(" samples=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_HARD_IRQ].samples);
    console_putc('\n');
    console_puts("K10 PERF endpoint read=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_IRQ_READ].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_IRQ_READ);
    console_puts(" ack=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_IRQ_ACK].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_IRQ_ACK);
    console_puts(" overruns=");
    console_dec32(endpoint_overruns);
    console_putc('\n');
    console_puts("K10 PERF worker=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_DEVICE_BATCH].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_DEVICE_BATCH);
    console_puts(" monitor=");
    console_dec32(performance->metric[
        KERNEL_PERFORMANCE_MONITOR_COMMAND].maximum_cycles);
    console_putc('/');
    console_dec32(KERNEL_PERFORMANCE_BUDGET_MONITOR_COMMAND);
    console_puts(" overruns=");
    console_dec32(deferred_overruns);
    console_putc('\n');
#if ASTRA_KERNEL_SCHED_TRACE
    console_puts("K2 TRACE syscall max id=");
    console_dec32(kernel_dispatch_syscall_max_number());
    console_puts(" arg=");
    console_hex32(kernel_dispatch_syscall_max_argument());
    console_puts(" body=");
    console_dec32(kernel_dispatch_syscall_max_body_cycles());
    console_putc('\n');
    console_puts("K6 TRACE wait-set body=");
    for (uint32_t index = 0u;
         index < kernel_dispatch_wait_set_trace_count(); ++index) {
        if (index != 0u)
            console_putc(',');
        console_dec32(kernel_dispatch_wait_set_trace_cycles(index));
    }
    console_putc('\n');
    console_puts("K5 TRACE thread-create body/ticks=");
    for (uint32_t index = 0u;
         index < kernel_dispatch_thread_create_trace_count(); ++index) {
        if (index != 0u)
            console_putc(',');
        console_dec32(kernel_dispatch_thread_create_trace_cycles(index));
        console_putc('/');
        console_dec32(kernel_dispatch_thread_create_trace_ticks(index));
    }
    console_putc('\n');
#endif
}

static void report_kernel_performance_failure(
    const KernelPerformanceStats *performance,
    KernelPerformanceMetric failed_metric)
{
    const KernelPerformanceMetricStats *metric;

    report_kernel_performance(performance);
    console_puts("K2 PERF FAIL metric=");
    console_dec32((uint32_t)failed_metric);
    if ((uint32_t)failed_metric >= KERNEL_PERFORMANCE_METRIC_COUNT) {
        console_putc('\n');
        return;
    }
    metric = &performance->metric[failed_metric];
    console_puts(" samples=");
    console_dec32(metric->samples);
    console_puts(" max=");
    console_dec32(metric->maximum_cycles);
    console_puts(" budget=");
    console_dec32(metric->budget_cycles);
    console_puts(" overruns=");
    console_dec32(metric->overruns);
    console_putc('\n');
}

void kernel_process_milestone_reached(const KernelSchedulerStats *validated)
{
    KernelPerformanceMetric failed_metric;
    KernelPerformanceStats performance;
    KernelIrqOffLatencyStats irqoff_stats;
    KernelSchedulerStats stats;
    KernelTraceStageStats trace_stage_stats;
    KernelWorkerStats worker_stats;
    KernelIrqPoolStats irq_stats;
    uint32_t performance_mask;
    uint32_t qualification_authorized;
    uint32_t qualification_completed;

    kernel_performance_freeze();
    if (validated == NULL)
        kernel_panic("scheduler statistics unavailable");
    kernel_bytes_copy(&stats, validated, sizeof(stats));
    if (!kernel_performance_stats(&performance) ||
        !kernel_irqoff_latency_stats(&irqoff_stats) ||
        !kernel_irq_pool_stats(&irq_stats) ||
        !kernel_trace_stage_stats(&trace_stage_stats) ||
        !kernel_worker_stats(&worker_stats))
        kernel_panic("scheduler statistics unavailable");
    if (!kernel_process_qualification_status(
            qualification_survivor_process_id,
            &qualification_authorized, &qualification_completed) ||
        qualification_completed != qualification_authorized ||
        irq_stats.live_endpoints != 0u)
        kernel_panic("K10 device qualification incomplete");
    performance_mask =
        qualification_completed == KERNEL_QUALIFICATION_IRQ_SOURCE_MASK ?
            KERNEL_PERFORMANCE_RELEASE_MASK :
            KERNEL_PERFORMANCE_REQUIRED_MASK;
    if (!kernel_performance_pass(&performance, performance_mask,
                                 &failed_metric)) {
        report_kernel_performance_failure(&performance, failed_metric);
        kernel_panic("kernel performance budget exceeded");
    }
    if (performance_mask == KERNEL_PERFORMANCE_RELEASE_MASK)
        console_puts("K10 PERFORMANCE PASS\n");
    console_puts("\nUser tasks .......... OK, 5 ms one-shot quantum\n");
    console_puts("Fault containment ... OK, offender reaped\n");
    console_puts("Device IRQs ......... ");
    if (qualification_completed ==
        KERNEL_QUALIFICATION_IRQ_SOURCE_MASK)
        console_puts("K10 PASS");
    else
        console_puts("K10 partial");
    console_puts(", mask=0x");
    console_hex32(qualification_completed);
    console_puts(" delivered/acked=");
    console_dec32(irq_stats.deliveries);
    console_putc('/');
    console_dec32(irq_stats.acknowledgements);
    console_puts(" owner-death=");
    console_dec32(irq_stats.owner_deaths);
    console_putc('\n');
    console_puts("Context switches .... ");
    console_dec32(stats.context_switches);
    console_puts("\nThread scheduler .... ");
    console_dec32(stats.live_threads);
    console_puts(" live, priority queues OK\n");
    console_puts("Same-CRP switches ... ");
    console_dec32(stats.same_address_space_switches);
    console_putc('\n');
    console_puts("Wait/wake ........... ");
    console_dec32(stats.wait_blocks);
    console_puts(" blocks, ");
    console_dec32(stats.sync_wakeups);
    console_puts(" wake, ");
    console_dec32(stats.wake_preemptions);
    console_puts(" priority handoff\n");
    console_puts("Deadlines ........... ");
    console_dec32(stats.deadline_expirations);
    console_puts(" expired, ");
    console_dec32(stats.deadline_preemptions);
    console_puts(" priority handoff\n");
    console_puts("Sync objects ........ ");
    console_dec32(stats.sync_created_events);
    console_puts(" event, ");
    console_dec32(stats.sync_created_semaphores);
    console_puts(" sem; cancel/close/death ");
    console_dec32(stats.sync_cancellations);
    console_putc('/');
    console_dec32(stats.sync_close_wakeups);
    console_putc('/');
    console_dec32(stats.sync_owner_deaths);
    console_putc('\n');
    console_puts("Thread ISP max ...... ");
    console_dec32(stats.kernel_stack_max_used);
    console_puts(" / ");
    console_dec32(KERNEL_THREAD_SUPERVISOR_STACK_SIZE);
    console_puts(" bytes\n");
    console_puts("Thread lifecycle .... ");
    console_dec32(stats.thread_exits);
    console_puts(" exit, ");
    console_dec32(stats.thread_death_waits);
    console_puts(" waits, ");
    console_dec32(stats.completed_thread_reaps);
    console_puts(" reaped\n");
    console_puts("Wait multiple ....... ");
    console_dec32(stats.wait_set_calls);
    console_puts(" calls, ");
    console_dec32(stats.wait_set_blocks);
    console_puts(" block, ");
    console_dec32(stats.wait_set_wakeups);
    console_puts(" wake; max ");
    console_dec32(stats.wait_set_max_members);
    console_puts(" members\n");
    console_puts("Wait registrations .. ");
    console_dec32(stats.wait_set_registrations);
    console_puts(" live, ");
    console_dec32(stats.wait_set_registration_max);
    console_puts(" max\n");
    console_puts("Waitable timers ..... ");
    console_dec32(stats.timer_created);
    console_puts(" created, ");
    console_dec32(stats.timer_arms);
    console_puts(" armed, ");
    console_dec32(stats.timer_expirations);
    console_puts(" expired\n");
    console_puts("Process death ....... ");
    console_dec32(stats.process_death_waits);
    console_puts(" waits, ");
    console_dec32(stats.process_death_wakeups);
    console_puts(" blocked wakes\n");
    console_puts("Message ports ....... ");
    console_dec32(stats.port_sends);
    console_puts(" send, ");
    console_dec32(stats.port_receives);
    console_puts(" receive, ");
    console_dec32(stats.port_send_would_block);
    console_puts(" backpressure\n");
    console_puts("Handle transfer ..... ");
    console_dec32(stats.handle_transfers);
    console_puts(" committed, max detached ");
    console_dec32(stats.handle_transfer_max_detached);
    console_putc('\n');
    console_puts("Shared areas ........ ");
    console_dec32(stats.area_created);
    console_puts(" create, ");
    console_dec32(stats.area_map_operations);
    console_putc('/');
    console_dec32(stats.area_unmap_operations);
    console_puts(" map/unmap, ");
    console_dec32(stats.area_active);
    console_puts(" active\n");
    console_puts("Bulk rings .......... ");
    console_dec32(stats.ring_created);
    console_puts(" create, ");
    console_dec32(stats.ring_producer_notifications);
    console_putc('/');
    console_dec32(stats.ring_consumer_notifications);
    console_puts(" notify, ");
    console_dec32(stats.ring_wait_wakeups);
    console_puts(" blocked wake, ");
    console_dec32(stats.ring_active);
    console_puts(" active\n");
    report_kernel_performance(&performance);
    console_puts("K10 LATENCY worker_dispatch_max=");
    console_dec32(worker_stats.max_dispatch_latency_cycles);
    console_puts(" endpoint_wake_run_max=");
    console_dec32(stats.irq_wake_to_run_max_cycles);
    console_puts(" irqoff_max=");
    console_dec32(irqoff_stats.maximum_cycles);
    console_putc('\n');
    console_puts("K10 BOOT trace_init=");
    console_dec32(trace_init_cycles);
    console_puts(" process_init=");
    console_dec32(process_init_cycles);
    console_puts(" process_start_irqoff=");
    console_dec32(process_bootstrap_irqoff_cycles);
    console_putc('\n');
    console_puts("K10 TRACE staged=");
    console_dec32(trace_stage_stats.staged);
    console_puts(" flushed=");
    console_dec32(trace_stage_stats.flushed);
    console_puts(" pending=");
    console_dec32(trace_stage_stats.pending);
    console_puts(" max=");
    console_dec32(trace_stage_stats.maximum_pending);
    console_puts(" dropped=");
    console_dec32(trace_stage_stats.dropped);
    console_putc('\n');
    console_puts("K2 PERFORMANCE PASS\n");
    console_puts("\nK8 SHARED BULK IPC PASS\n");
    console_puts("\nK7 MESSAGE PORTS PASS\n");
    console_puts("\nK6 BOUNDED WAIT-MULTIPLE PASS\n");
    console_puts("\nK5 THREAD LIFECYCLE PASS\n");
    console_puts("\nK4 HANDLE SYNCHRONIZATION PASS\n");
    console_puts("\nK3 ONE-SHOT SCHEDULER PASS\n");
    console_puts("\nK3 DEADLINE QUEUE PASS\n");
    console_puts("\nK2 BLOCKING SUBSTRATE PASS\n");
    console_puts("\nK2 THREAD SUBSTRATE PASS\n");
    console_puts("\nK1 PROTECTED ENTRY PASS\n");
    console_puts("KERNEL MULTITASKING\n");
    kernel_platform_debug_marker(ASTRA_KERNEL_STATUS_K1_READY);
}

#if ASTRA_KERNEL_SOAK_SELFTEST
void kernel_process_soak_checkpoint(uint32_t cycles,
                                    uint32_t baseline_free_frames)
{
    KernelMemoryStats memory_stats;
    KernelPerformanceMetric failed_metric;
    KernelPerformanceStats performance;
    KernelSchedulerStats scheduler_stats;
    KernelPlatformCycleCount now;
    KernelPlatformCycleCount elapsed;

    if (!kernel_memory_stats(&memory_stats) ||
        !kernel_performance_stats(&performance) ||
        !kernel_process_stats(&scheduler_stats) ||
        memory_stats.free_frames != baseline_free_frames ||
        scheduler_stats.soak_cycles != cycles ||
        scheduler_stats.user_faults != cycles ||
        scheduler_stats.completed_user_fault_teardowns != cycles ||
        scheduler_stats.completed_teardowns != cycles)
        kernel_panic("K1 soak resource baseline drift");
    if (!kernel_performance_pass(&performance,
                                 KERNEL_PERFORMANCE_REQUIRED_MASK,
                                 &failed_metric)) {
        report_kernel_performance_failure(&performance, failed_metric);
        kernel_panic("K2 soak performance budget exceeded");
    }

    kernel_platform_cpu_cycles(&now);
    elapsed.low = now.low - soak_started.low;
    elapsed.high = now.high - soak_started.high -
                   (now.low < soak_started.low ? 1u : 0u);

    console_puts("K1 LATENCY user_fault_irqoff_max=");
    console_dec32(kernel_dispatch_user_fault_irqoff_max_cycles());
    console_puts(" cycles\n");
    console_puts("K1 SOAK cycles=");
    console_dec32(cycles);
    console_puts(" switches=");
    console_dec32(scheduler_stats.context_switches);
    console_puts(" ticks=");
    console_dec32(kernel_platform_ticks());
    console_puts(" syscalls=0x");
    console_hex32(scheduler_stats.total_syscalls_high);
    console_hex32(scheduler_stats.total_syscalls_low);
    console_puts(" free=");
    console_dec32(memory_stats.free_frames);
    console_puts(" elapsed_cycles=0x");
    console_hex32(elapsed.high);
    console_hex32(elapsed.low);
    console_putc('\n');
    report_kernel_performance(&performance);
    if (!kernel_performance_start_window(KERNEL_PERFORMANCE_SOAK_MASK))
        kernel_panic("K2 soak sampling window incomplete");
    console_puts("K2 PERFORMANCE SOAK PASS cycle=");
    console_dec32(cycles);
    console_putc('\n');
    kernel_platform_debug_marker(ASTRA_KERNEL_STATUS_K1_SOAK);
}
#endif

void kernel_main(uint32_t handoff_magic, const AstraBootInfo *firmware_info)
{
    AstraBootValidation validation;
    KernelMemoryStats memory_stats;
    KernelVmStats vm_stats;
    KernelCpuContext *first_context;
    uint32_t process_id;
    uint32_t survivor_process_id;
    uint32_t sibling_thread_id;
    KernelHandle offender_process_handle;
    uint32_t offender_image_size;
    uint32_t offender_entry_offset;
    uint32_t survivor_image_size;
    uint32_t survivor_entry_offset;
    uint32_t sibling_entry_offset;
    uint32_t process_bootstrap_started;
    uint32_t trace_started;
    uint32_t qualification_sources;
#if ASTRA_KERNEL_SOAK_SELFTEST
    KernelMemoryStats soak_baseline;
#endif

    kernel_platform_debug_marker(ASTRA_KERNEL_STATUS_BOOTING);
    console_init();
    kernel_irqoff_latency_init();
    trace_started = kernel_platform_cpu_cycles_low();
    if (!kernel_trace_init() ||
        !kernel_trace_write(
            KERNEL_TRACE_EVENT_BOOT, 0u, handoff_magic,
            (uint32_t)(uintptr_t)firmware_info,
            kernel_platform_build_id(), 0u))
        kernel_panic("retained trace initialization failed");
    trace_init_cycles = kernel_platform_cpu_cycles_low() - trace_started;
    console_puts("AXIOM KERNEL v" ASTRA_KERNEL_VERSION "\n");
    console_puts("Built: " ASTRA_KERNEL_BUILD_UTC "\n");
    console_puts("Git:   " ASTRA_KERNEL_GIT_REVISION "\n\n");

    if (handoff_magic != ASTRA_BOOT_HANDOFF_MAGIC)
        kernel_panic("invalid handoff magic");
    if ((uint32_t)firmware_info != ASTRA_BOOT_INFO_ADDRESS)
        kernel_panic("invalid BootInfo address");

    validation = astra_boot_info_validate(firmware_info);
    if (validation != ASTRA_BOOT_VALID) {
        console_puts("BootInfo rejected: ");
        console_puts(astra_boot_validation_name(validation));
        console_putc('\n');
        kernel_panic("invalid BootInfo");
    }
    kernel_bytes_copy(&boot_info, firmware_info, sizeof(boot_info));
    validate_image_contract();
    if (kernel_memory_init(&boot_info) != KERNEL_MEMORY_OK)
        kernel_panic("physical memory map rejected");
    if (kernel_vm_init() != KERNEL_VM_OK)
        kernel_panic("kernel page-table construction failed");
    if (kernel_vm_enable() != KERNEL_VM_OK || !kernel_vm_enabled())
        kernel_panic("PMMU enable failed");
    if (kernel_worker_init() != KERNEL_WORKER_OK)
        kernel_panic("kernel worker initialization failed");
    if (!kernel_monitor_init(&monitor_build_info))
        kernel_panic("kernel monitor initialization failed");
    if (!kernel_memory_stats(&memory_stats))
        kernel_panic("physical memory stats unavailable");
    if (!kernel_vm_stats(&vm_stats))
        kernel_panic("virtual memory stats unavailable");
    if (vm_stats.kernel_thread_stack_guards != KERNEL_THREAD_MAX ||
        vm_stats.kernel_thread_stack_arena_end -
                vm_stats.kernel_thread_stack_arena !=
            KERNEL_THREAD_MAX * KERNEL_THREAD_SUPERVISOR_SLOT_SIZE)
        kernel_panic("thread ISP arena contract mismatch");
    kernel_dma_init();
    kernel_block_init();
    if (!kernel_device_init() || !register_physical_devices() ||
        !kernel_device_seal_registry())
        kernel_panic("device registry initialization failed");
    if (kernel_read_vbr() != (uint32_t)_kernel_vectors)
        kernel_panic("kernel VBR installation failed");
    kernel_user_copy_selftest();
    if (!kernel_allocation_retire_boot())
        kernel_panic("boot allocator retirement failed");
    if (kernel_allocation_phase() != KERNEL_ALLOCATION_PHASE_RUNTIME ||
        !kernel_allocation_valid() || !kernel_dma_valid() ||
        !kernel_block_valid() || !kernel_device_pool_valid() ||
        !kernel_memory_stats(&memory_stats) ||
        memory_stats.emergency_total_frames !=
            KERNEL_EMERGENCY_RESERVE_FRAMES ||
        memory_stats.emergency_available_frames !=
            KERNEL_EMERGENCY_RESERVE_FRAMES)
        kernel_panic("runtime allocator validation failed");

    console_puts("BootInfo ........... OK\n");
    console_puts("Early log .......... OK @ 0x");
    console_hex32(boot_info.early_log_base);
    console_putc('\n');
    console_puts("VBR ................ OK @ 0x");
    console_hex32(kernel_read_vbr());
    console_putc('\n');
    console_puts("Kernel image ....... ");
    console_dec32(boot_info.kernel_image_size);
    console_puts(" bytes @ 0x");
    console_hex32(boot_info.kernel_base);
    console_putc('\n');
    console_puts("CPU ................ MC68030 @ ");
    console_dec32(boot_info.cpu_hz);
    console_puts(" Hz\n");
    console_puts("PMMU ............... enabled, SRP 0x");
    console_hex32(vm_stats.kernel_root_physical);
    console_putc('\n');
    console_puts("Physical pages ..... ");
    console_dec32(memory_stats.free_frames);
    console_puts(" free / ");
    console_dec32(memory_stats.total_frames);
    console_puts(" total\n");
    console_puts("Emergency reserve .. ");
    console_dec32(memory_stats.emergency_available_frames);
    console_puts(" / ");
    console_dec32(memory_stats.emergency_total_frames);
    console_puts(" pages\n");
    console_puts("Allocators ......... runtime, ledger OK\n");
    console_puts("User copy .......... OK, fault recovery verified\n");
    console_puts("Kernel worker ...... OK, guarded MSP\n");
    console_puts("Thread ISPs ........ ");
    console_dec32(vm_stats.kernel_thread_stack_guards);
    console_puts(" guarded, 8 KiB each\n");

    if (!kernel_interrupt_init(boot_info.cpu_hz))
        kernel_panic("interrupt controller initialization failed");
    process_bootstrap_started = kernel_platform_cpu_cycles_low();
    kernel_process_init();
    process_init_cycles =
        kernel_platform_cpu_cycles_low() - process_bootstrap_started;
    kernel_enable_interrupts();
    uint32_t timer_start = kernel_platform_cpu_cycles_low();
    while (kernel_platform_ticks() < 2u) {
        if ((uint32_t)(kernel_platform_cpu_cycles_low() - timer_start) >
            boot_info.cpu_hz)
            kernel_panic("Vesta timer interrupt timeout");
    }
    console_puts("Vesta timer ........ OK, one-shot 5 ms\n");

    if ((kernel_platform_system_status() & SYS_ASTRA_HOST) != 0u) {
        if (!kernel_platform_block_present())
            kernel_panic("AstraHost block controller missing");
        uint32_t host_start = kernel_platform_cpu_cycles_low();
        while ((kernel_platform_block_state_flags() &
                BLOCK_STATE_LINK_UP) == 0u) {
            if ((uint32_t)(kernel_platform_cpu_cycles_low() - host_start) >
                boot_info.cpu_hz)
                kernel_panic("AstraHost runtime handshake timeout");
        }
        console_puts("AstraHost runtime ... OK, media ");
        console_puts((kernel_platform_block_state_flags() &
                      BLOCK_STATE_MEDIA_PRESENT) != 0u ?
                     "present\n" : "not provisioned\n");
        if (!kernel_platform_input_present())
            kernel_panic("AstraHost input controller missing");
        console_puts("Input queue ......... OK\n");
    } else {
        console_puts("AstraHost runtime ... not present\n");
    }

#if ASTRA_KERNEL_PANIC_SELFTEST == 2
    console_puts("Supervisor guard .... fault injection\n");
    *(volatile uint32_t *)(uintptr_t)_kernel_stack_guard = 0x47554152u;
    kernel_panic("supervisor stack guard write returned");
#elif ASTRA_KERNEL_PANIC_SELFTEST == 1
    kernel_panic("deliberate panic self-test");
#elif ASTRA_KERNEL_PANIC_SELFTEST != 0
#error "ASTRA_KERNEL_PANIC_SELFTEST must be 0, 1, or 2"
#endif

    survivor_image_size =
        (uint32_t)(_k1_survivor_image_end - _k1_survivor_image_start);
    survivor_entry_offset =
        (uint32_t)(_k1_survivor_image_entry - _k1_survivor_image_start);
    sibling_entry_offset =
        (uint32_t)(_k2_sibling_image_entry - _k1_survivor_image_start);
    offender_image_size =
        (uint32_t)(_k1_offender_image_end - _k1_offender_image_start);
    offender_entry_offset =
        (uint32_t)(_k1_offender_image_entry - _k1_offender_image_start);
    if (kernel_process_create(_k1_survivor_image_start, survivor_image_size,
                              survivor_entry_offset, 0u,
                              &survivor_process_id) != KERNEL_PROCESS_OK)
        kernel_panic("survivor process creation failed");
    qualification_sources = kernel_platform_qualification_irq_sources();
    qualification_survivor_process_id = survivor_process_id;
    if (kernel_process_qualification_authorize(
            survivor_process_id, qualification_sources) !=
            KERNEL_PROCESS_OK)
        kernel_panic("survivor IRQ qualification authorization failed");
    if (kernel_process_create_thread(
            survivor_process_id, sibling_entry_offset, 0u,
            KERNEL_THREAD_PRIORITY_NORMAL + 1u, &sibling_thread_id) !=
            KERNEL_PROCESS_OK || sibling_thread_id == 0u)
        kernel_panic("sibling thread creation failed");
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (!kernel_memory_stats(&soak_baseline))
        kernel_panic("K1 soak baseline unavailable");
#endif
    if (kernel_process_create(_k1_offender_image_start, offender_image_size,
                              offender_entry_offset, 0u,
                              &process_id) != KERNEL_PROCESS_OK)
        kernel_panic("fault process creation failed");
    if (kernel_process_qualification_authorize(
            process_id,
            qualification_sources & IRQ_BIT(IRQ_SRC_ASTRAEA)) !=
            KERNEL_PROCESS_OK)
        kernel_panic("fault-process IRQ qualification authorization failed");
    if (kernel_process_grant_handle(
            survivor_process_id, process_id,
            KERNEL_PROCESS_RIGHT_QUERY | KERNEL_PROCESS_RIGHT_WAIT,
            &offender_process_handle) != KERNEL_PROCESS_OK ||
        offender_process_handle == KERNEL_HANDLE_INVALID)
        kernel_panic("process wait handle grant failed");
    if (kernel_process_set_thread_bootstrap_argument(
            survivor_process_id, sibling_thread_id,
            offender_process_handle) != KERNEL_PROCESS_OK)
        kernel_panic("process wait handle injection failed");
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (kernel_process_soak_configure(
            _k1_offender_image_start, offender_image_size,
            offender_entry_offset, soak_baseline.free_frames,
            KERNEL_SOAK_REPORT_INTERVAL) != KERNEL_PROCESS_OK)
        kernel_panic("K1 soak configuration failed");
    kernel_platform_cpu_cycles(&soak_started);
    console_puts("K1 soak ............. armed, baseline ");
    console_dec32(soak_baseline.free_frames);
    console_puts(" pages\n");
#endif
    console_puts("User processes ...... 2 ready, cache isolation armed\n");
    console_puts("User threads ........ 3 ready, priority scheduler armed\n");
    process_bootstrap_started = kernel_platform_cpu_cycles_low();
    kernel_disable_interrupts();
    if (kernel_process_start(&first_context) != KERNEL_PROCESS_OK ||
        first_context == NULL)
        kernel_panic("initial process scheduling failed");
    process_bootstrap_irqoff_cycles =
        kernel_platform_cpu_cycles_low() - process_bootstrap_started;
    kernel_enter_user(first_context);
}
