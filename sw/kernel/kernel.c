#include <astra/boot.h>

#include "kernel_build_info.h"
#include "vega.h"
#include "vesta.h"

#define SCREEN_TOP_MARGIN 2u
#define SCREEN_LEFT_MARGIN 2u
#define SCREEN_RIGHT_MARGIN 2u
#define SCREEN_BOTTOM_MARGIN 2u

extern uint8_t _kernel_entry[];
extern uint8_t _kernel_image_start[];
extern uint8_t _kernel_file_end[];
extern uint8_t _kernel_memory_end[];
extern uint8_t _kernel_vectors[];

uint32_t kernel_read_vbr(void);
void kernel_panic(const char *reason) __attribute__((noreturn));
void kernel_exception_panic(const void *frame) __attribute__((noreturn));

static AstraBootInfo boot_info;
static AstraEarlyLog *early_log;
static uint32_t screen_row;
static uint32_t screen_col;
static int screen_enabled;

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

static void console_putc(char value)
{
    screen_putc(value);
    astra_early_log_putc(early_log, value);
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

static uint16_t frame_u16(const uint8_t *frame, uint32_t offset)
{
    return (uint16_t)((uint16_t)frame[offset] << 8) | frame[offset + 1u];
}

static uint32_t frame_u32(const uint8_t *frame, uint32_t offset)
{
    return (uint32_t)frame[offset] << 24 |
           (uint32_t)frame[offset + 1u] << 16 |
           (uint32_t)frame[offset + 2u] << 8 |
           frame[offset + 3u];
}

void kernel_exception_panic(const void *raw_frame)
{
    const uint8_t *frame = raw_frame;
    uint16_t format_vector = frame_u16(frame, 6u);

    panic_begin("unhandled processor exception");
    console_puts("Vector: ");
    console_dec32((format_vector & 0x0fffu) >> 2);
    console_puts("  Format: 0x");
    console_putc(console_hex_digit(format_vector >> 12));
    console_puts("\nSR:     0x");
    console_hex32(frame_u16(frame, 0u));
    console_puts("\nPC:     0x");
    console_hex32(frame_u32(frame, 2u));
    console_putc('\n');
    panic_finish();
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

void kernel_main(uint32_t handoff_magic, const AstraBootInfo *firmware_info)
{
    AstraBootValidation validation;

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
    if (kernel_read_vbr() != (uint32_t)_kernel_vectors)
        kernel_panic("kernel VBR installation failed");

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
    console_puts("PMMU ............... present, disabled\n");

#if ASTRA_KERNEL_PANIC_SELFTEST
    kernel_panic("deliberate panic self-test");
#endif

    console_puts("\nK0 ENTRY PASS\n");
    console_puts("HALTED: NEXT STEP IS ARCHITECTURAL PROBES\n");
    VESTA->SCRATCH = ASTRA_KERNEL_STATUS_READY;
    halt_forever();
}
