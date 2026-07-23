#include <astra/boot.h>

#include "kernel_build_info.h"
#include "block.h"
#include "dma.h"
#include "dispatch.h"
#include "exception.h"
#include "memory.h"
#include "panic.h"
#include "platform.h"
#include "process.h"
#include "user_copy.h"
#include "vm.h"
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
extern uint8_t _kernel_stack_guard[];
extern uint8_t _kernel_vectors[];
extern uint8_t _k1_survivor_image_start[];
extern uint8_t _k1_survivor_image_entry[];
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
static KernelAddressSpace user_copy_selftest_space;
#if ASTRA_KERNEL_SOAK_SELFTEST
static KernelPlatformCycleCount soak_started;
#endif

static void copy_bytes(void *destination, const void *source, uint32_t size)
{
    uint8_t *out = destination;
    const uint8_t *in = source;

    while (size-- != 0u) *out++ = *in++;
}

static void screen_clear(void)
{
    if (!screen_enabled) return;
    for (uint32_t index = 0; index < VEGA_POST_COLS * VEGA_POST_ROWS; ++index)
        VEGA_POST_TEXT[index] = ' ';
    screen_row = SCREEN_TOP_MARGIN;
    screen_col = SCREEN_LEFT_MARGIN;
}

static void screen_scroll(void)
{
    const uint32_t last_row = VEGA_POST_ROWS - SCREEN_BOTTOM_MARGIN - 1u;
    const uint32_t last_col = VEGA_POST_COLS - SCREEN_RIGHT_MARGIN;

    for (uint32_t row = SCREEN_TOP_MARGIN; row < last_row; ++row) {
        for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col) {
            VEGA_POST_TEXT[row * VEGA_POST_COLS + col] =
                VEGA_POST_TEXT[(row + 1u) * VEGA_POST_COLS + col];
        }
    }
    for (uint32_t col = SCREEN_LEFT_MARGIN; col < last_col; ++col)
        VEGA_POST_TEXT[last_row * VEGA_POST_COLS + col] = ' ';
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
        VEGA_POST_TEXT[screen_row * VEGA_POST_COLS + screen_col] =
            (uint8_t)value;
        if (++screen_col == last_col) {
            screen_col = SCREEN_LEFT_MARGIN;
            ++screen_row;
        }
    }
    if (screen_row == last_row) screen_scroll();
}

static void diagnostic_uart_putc(char value)
{
    uint32_t start = VESTA->CPU_CYCLES_LO;
    uint32_t timeout = VESTA->CPU_HZ / 1000u;

    if (timeout == 0u) timeout = 1u;
    while ((VESTA->UART_STATUS & UART_TX_READY) == 0u) {
        if ((uint32_t)(VESTA->CPU_CYCLES_LO - start) >= timeout)
            return;
    }
    VESTA->UART_DATA = (uint8_t)value;
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
    screen_enabled = VEGA->ID == VEGA_ID_MAGIC &&
                     (VEGA->CAPS & VEGA_CAP_POST_TEXT) != 0u;
    screen_clear();
}

static void halt_forever(void) __attribute__((noreturn));
static void halt_forever(void)
{
    for (;;) __asm__ volatile ("stop #0x2700");
}

static void panic_begin(const char *reason)
{
    screen_clear();
    early_log->flags |= ASTRA_EARLY_LOG_FLAG_PANIC;
    ++early_log->sequence;
    console_puts("*** ASTRA KERNEL PANIC ***\n\n");
    console_puts("Reason: ");
    console_puts(reason);
    console_putc('\n');
    console_puts("Kernel: v" ASTRA_KERNEL_VERSION "\n");
    console_puts("Built:  " ASTRA_KERNEL_BUILD_UTC "\n");
    console_puts("Git:    " ASTRA_KERNEL_GIT_REVISION "\n");
    console_puts("HW:     0x");
    console_hex32(VESTA->BUILD_ID);
    console_putc('\n');
}

static void panic_finish(void) __attribute__((noreturn));
static void panic_finish(void)
{
    console_puts("\nSYSTEM HALTED\n");
    VESTA->SCRATCH = ASTRA_KERNEL_STATUS_PANIC;
    halt_forever();
}

void kernel_panic(const char *reason)
{
    panic_begin(reason);
    panic_finish();
}

void kernel_exception_panic(const void *raw_frame)
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
    }
    console_putc('\n');
    panic_finish();
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
        kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS,
                            KERNEL_SELFTEST_OWNER, &physical) !=
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
    if (boot_info.early_log_base != ASTRA_EARLY_LOG_ADDRESS ||
        boot_info.early_log_size != ASTRA_EARLY_LOG_SIZE)
        kernel_panic("early log contract mismatch");
}

void kernel_process_milestone_reached(void)
{
    KernelSchedulerStats stats;

    if (!kernel_process_stats(&stats))
        kernel_panic("scheduler statistics unavailable");
    console_puts("\nUser tasks .......... OK, 100 Hz preemption\n");
    console_puts("Fault containment ... OK, offender reaped\n");
    console_puts("Context switches .... ");
    console_dec32(stats.context_switches);
    console_putc('\n');
    console_puts("\nK1 PROTECTED ENTRY PASS\n");
    console_puts("KERNEL MULTITASKING\n");
    VESTA->SCRATCH = ASTRA_KERNEL_STATUS_K1_READY;
}

#if ASTRA_KERNEL_SOAK_SELFTEST
void kernel_process_soak_checkpoint(uint32_t cycles,
                                    uint32_t baseline_free_frames)
{
    KernelMemoryStats memory_stats;
    KernelSchedulerStats scheduler_stats;
    KernelPlatformCycleCount now;
    KernelPlatformCycleCount elapsed;

    if (!kernel_memory_stats(&memory_stats) ||
        !kernel_process_stats(&scheduler_stats) ||
        memory_stats.free_frames != baseline_free_frames ||
        scheduler_stats.soak_cycles != cycles ||
        scheduler_stats.user_faults != cycles ||
        scheduler_stats.completed_user_fault_teardowns != cycles ||
        scheduler_stats.completed_teardowns != cycles)
        kernel_panic("K1 soak resource baseline drift");

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
    VESTA->SCRATCH = ASTRA_KERNEL_STATUS_K1_SOAK;
}
#endif

void kernel_main(uint32_t handoff_magic, const AstraBootInfo *firmware_info)
{
    AstraBootValidation validation;
    KernelMemoryStats memory_stats;
    KernelVmStats vm_stats;
    KernelCpuContext *first_context;
    uint32_t process_id;
    uint32_t offender_image_size;
    uint32_t offender_entry_offset;
    uint32_t survivor_image_size;
    uint32_t survivor_entry_offset;
#if ASTRA_KERNEL_SOAK_SELFTEST
    KernelMemoryStats soak_baseline;
#endif

    VESTA->SCRATCH = ASTRA_KERNEL_STATUS_BOOTING;
    console_init();
    console_puts("ASTRA 68 KERNEL v" ASTRA_KERNEL_VERSION "\n");
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
    copy_bytes(&boot_info, firmware_info, sizeof(boot_info));
    validate_image_contract();
    if (kernel_memory_init(&boot_info) != KERNEL_MEMORY_OK)
        kernel_panic("physical memory map rejected");
    if (kernel_vm_init() != KERNEL_VM_OK)
        kernel_panic("kernel page-table construction failed");
    if (kernel_vm_enable() != KERNEL_VM_OK || !kernel_vm_enabled())
        kernel_panic("PMMU enable failed");
    if (!kernel_memory_stats(&memory_stats))
        kernel_panic("physical memory stats unavailable");
    if (!kernel_vm_stats(&vm_stats))
        kernel_panic("virtual memory stats unavailable");
    kernel_dma_init();
    kernel_block_init();
    if (kernel_read_vbr() != (uint32_t)_kernel_vectors)
        kernel_panic("kernel VBR installation failed");
    kernel_user_copy_selftest();

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
    console_puts("User copy .......... OK, fault recovery verified\n");

    kernel_platform_interrupt_init(boot_info.cpu_hz);
    kernel_enable_interrupts();
    uint32_t timer_start = VESTA->CPU_CYCLES_LO;
    while (kernel_platform_ticks() < 2u) {
        if ((uint32_t)(VESTA->CPU_CYCLES_LO - timer_start) >
            boot_info.cpu_hz)
            kernel_panic("Vesta timer interrupt timeout");
    }
    console_puts("Vesta timer ........ OK @ 100 Hz\n");

    if ((VESTA->SYS_STATUS & SYS_ASTRA_HOST) != 0u) {
        if (!kernel_platform_block_present())
            kernel_panic("AstraHost block controller missing");
        uint32_t host_start = VESTA->CPU_CYCLES_LO;
        while ((VESTA->BLOCK_STATE & BLOCK_STATE_LINK_UP) == 0u) {
            if ((uint32_t)(VESTA->CPU_CYCLES_LO - host_start) >
                boot_info.cpu_hz)
                kernel_panic("AstraHost runtime handshake timeout");
        }
        console_puts("AstraHost runtime ... OK, media ");
        console_puts((VESTA->BLOCK_STATE & BLOCK_STATE_MEDIA_PRESENT) != 0u ?
                     "present\n" : "not provisioned\n");
        kernel_platform_block_ack_state();
        if (VESTA->INPUT_ID != INPUT_ID_MAGIC)
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
    offender_image_size =
        (uint32_t)(_k1_offender_image_end - _k1_offender_image_start);
    offender_entry_offset =
        (uint32_t)(_k1_offender_image_entry - _k1_offender_image_start);
    kernel_disable_interrupts();
    kernel_process_init();
    if (kernel_process_create(_k1_survivor_image_start, survivor_image_size,
                              survivor_entry_offset, 0u,
                              &process_id) != KERNEL_PROCESS_OK)
        kernel_panic("survivor process creation failed");
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (!kernel_memory_stats(&soak_baseline))
        kernel_panic("K1 soak baseline unavailable");
#endif
    if (kernel_process_create(_k1_offender_image_start, offender_image_size,
                              offender_entry_offset, 1u,
                              &process_id) != KERNEL_PROCESS_OK)
        kernel_panic("fault process creation failed");
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
    if (kernel_process_start(&first_context) != KERNEL_PROCESS_OK ||
        first_context == NULL)
        kernel_panic("initial process scheduling failed");
    console_puts("User processes ...... 2 ready, cache isolation armed\n");
    kernel_enter_user(first_context);
}
