#include "platform.h"

#include "astraea.h"
#include "mmio.h"
#include "ohci.h"
#include "qualification.h"
#include "vega.h"
#include "vesta.h"

#include <astra/display.h>
#include <astra/render_batch.h>

#include <stddef.h>

#define VESTA_ADDRESS(member) \
    (VESTA_BASE + (uint32_t)offsetof(VestaRegs, member))
#define VESTA_READ(member) kernel_mmio_read32(VESTA_ADDRESS(member))
#define VESTA_WRITE(member, value) \
    kernel_mmio_write32(VESTA_ADDRESS(member), (value))
#define VESTA_ARRAY_ADDRESS(member, index) \
    (VESTA_ADDRESS(member) + (uint32_t)(index) * sizeof(uint32_t))
#define VESTA_ARRAY_WRITE(member, index, value) \
    kernel_mmio_write32(VESTA_ARRAY_ADDRESS(member, index), (value))
#define VESTA_TIMER_ADDRESS(timer, member) \
    (VESTA_ADDRESS(TIMER) + (uint32_t)(timer) * sizeof(VestaTimer) + \
     (uint32_t)offsetof(VestaTimer, member))
#define VESTA_TIMER_WRITE(timer, member, value) \
    kernel_mmio_write32(VESTA_TIMER_ADDRESS(timer, member), (value))
#define VEGA_ADDRESS(member) \
    (VEGA_BASE + (uint32_t)offsetof(VegaRegs, member))
#define VEGA_READ(member) kernel_mmio_read32(VEGA_ADDRESS(member))
#define VEGA_WRITE(member, value) \
    kernel_mmio_write32(VEGA_ADDRESS(member), (value))
#define ASTRAEA_ADDRESS(member) \
    (ASTRAEA_BASE + (uint32_t)offsetof(AstraeaRegs, member))
#define ASTRAEA_READ(member) kernel_mmio_read32(ASTRAEA_ADDRESS(member))
#define ASTRAEA_WRITE(member, value) \
    kernel_mmio_write32(ASTRAEA_ADDRESS(member), (value))
#define OHCI_ADDRESS(member) \
    (OHCI_BASE + (uint32_t)offsetof(OhciRegs, member))
#define OHCI_READ(member) kernel_mmio_read32(OHCI_ADDRESS(member))
#define OHCI_WRITE(member, value) \
    kernel_mmio_write32(OHCI_ADDRESS(member), (value))

#define OHCI_INTERRUPT_SOURCES \
    (OHCI_INT_SO | OHCI_INT_WDH | OHCI_INT_SF | OHCI_INT_RD | \
     OHCI_INT_UE | OHCI_INT_FNO | OHCI_INT_RHSC | OHCI_INT_OC)
#define OHCI_INTERRUPT_DISABLE_ALL \
    (OHCI_INTERRUPT_SOURCES | OHCI_INT_MIE)
#define OHCI_SOFT_RESET_TIMEOUT_CYCLES \
    (KERNEL_PLATFORM_CPU_HZ / 100000u)

#if defined(KERNEL_PLATFORM_HOST_TEST)
#define PLATFORM_TEST_MMIO_SIZE \
    ((OHCI_BASE - VESTA_BASE) + (uint32_t)sizeof(OhciRegs))
static _Alignas(4) uint8_t platform_test_mmio[PLATFORM_TEST_MMIO_SIZE];
static _Alignas(256) OhciHcca platform_test_ohci_hcca;

VestaRegs *kernel_platform_test_registers(void)
{
    if (!kernel_mmio_test_bind(VESTA_BASE, platform_test_mmio,
                               sizeof(platform_test_mmio)))
        return NULL;
    return (VestaRegs *)(void *)platform_test_mmio;
}

AstraeaRegs *kernel_platform_test_astraea_registers(void)
{
    if (!kernel_mmio_test_bind(VESTA_BASE, platform_test_mmio,
                               sizeof(platform_test_mmio)))
        return NULL;
    return (AstraeaRegs *)(void *)&platform_test_mmio[
        ASTRAEA_BASE - VESTA_BASE];
}

VegaRegs *kernel_platform_test_vega_registers(void)
{
    if (!kernel_mmio_test_bind(VESTA_BASE, platform_test_mmio,
                               sizeof(platform_test_mmio)))
        return NULL;
    return (VegaRegs *)(void *)&platform_test_mmio[VEGA_BASE - VESTA_BASE];
}

OhciRegs *kernel_platform_test_ohci_registers(void)
{
    if (!kernel_mmio_test_bind(VESTA_BASE, platform_test_mmio,
                               sizeof(platform_test_mmio)))
        return NULL;
    return (OhciRegs *)(void *)&platform_test_mmio[OHCI_BASE - VESTA_BASE];
}

OhciHcca *kernel_platform_test_ohci_hcca(void)
{
    return &platform_test_ohci_hcca;
}
#endif

static volatile uint32_t tick_count;
static uint32_t quantum_cycles;

static volatile OhciHcca *ohci_hcca(void)
{
#if defined(KERNEL_PLATFORM_HOST_TEST)
    return &platform_test_ohci_hcca;
#else
    return (volatile OhciHcca *)(uintptr_t)OHCI_DMA_POOL_BASE;
#endif
}

static void ohci_hcca_clear(void)
{
    volatile uint32_t *words = (volatile uint32_t *)ohci_hcca();

    for (uint32_t index = 0u;
         index < sizeof(OhciHcca) / sizeof(uint32_t); ++index)
        words[index] = 0u;
    kernel_mmio_cpu_sync();
}

static bool ohci_controller_soft_reset(void)
{
    uint32_t status = OHCI_READ(INTERRUPT_STATUS) &
                      OHCI_INTERRUPT_SOURCES;
    uint32_t astra_status = OHCI_READ(ASTRA_STATUS);
    uint32_t started = VESTA_READ(CPU_CYCLES_LO);
    uint32_t attempts = OHCI_SOFT_RESET_TIMEOUT_CYCLES;

    OHCI_WRITE(INTERRUPT_DISABLE, OHCI_INTERRUPT_DISABLE_ALL);
    if (status != 0u)
        OHCI_WRITE(INTERRUPT_STATUS, status);
    if ((astra_status & OHCI_ASTRA_DMA_FAULT) != 0u)
        OHCI_WRITE(ASTRA_STATUS, OHCI_ASTRA_DMA_FAULT);
    OHCI_WRITE(COMMAND_STATUS, OHCI_COMMAND_HCR);
#if defined(KERNEL_PLATFORM_HOST_TEST)
    // The host MMIO buffer has no register side effects; model HCR completion.
    OHCI_WRITE(COMMAND_STATUS, 0u);
    OHCI_WRITE(CONTROL, OHCI_CONTROL_HCFS_SUSPEND);
    OHCI_WRITE(INTERRUPT_STATUS, 0u);
    OHCI_WRITE(INTERRUPT_ENABLE, 0u);
    OHCI_WRITE(HCCA, 0u);
    OHCI_WRITE(ASTRA_STATUS, 0u);
#endif
    while ((OHCI_READ(COMMAND_STATUS) & OHCI_COMMAND_HCR) != 0u) {
        if ((uint32_t)(VESTA_READ(CPU_CYCLES_LO) - started) >=
                OHCI_SOFT_RESET_TIMEOUT_CYCLES ||
            --attempts == 0u)
            return false;
    }
    kernel_mmio_cpu_sync();
    return (kernel_mmio_fence32(OHCI_ADDRESS(CONTROL)) &
            OHCI_CONTROL_HCFS_MASK) == OHCI_CONTROL_HCFS_SUSPEND &&
           kernel_mmio_fence32(OHCI_ADDRESS(HCCA)) == 0u;
}

static uint32_t divide_ns_limb(uint32_t remainder, uint32_t limb,
                               uint32_t *next_remainder)
{
    uint32_t dividend = (remainder << 16) | (limb & 0xffffu);
    uint32_t quotient = dividend / KERNEL_PLATFORM_NS_PER_CPU_CYCLE;

    *next_remainder = dividend -
        quotient * KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    return quotient;
}

static uint64_t ceil_nanoseconds_to_cycles(uint64_t nanoseconds)
{
    uint32_t high = (uint32_t)(nanoseconds >> 32);
    uint32_t low = (uint32_t)nanoseconds;
    uint32_t quotient_high;
    uint32_t quotient_low;
    uint32_t remainder = 0u;

    quotient_high = divide_ns_limb(remainder, high >> 16, &remainder) << 16;
    quotient_high |= divide_ns_limb(remainder, high, &remainder);
    quotient_low = divide_ns_limb(remainder, low >> 16, &remainder) << 16;
    quotient_low |= divide_ns_limb(remainder, low, &remainder);
    if (remainder != 0u && ++quotient_low == 0u)
        ++quotient_high;
    return ((uint64_t)quotient_high << 32) | quotient_low;
}

uint32_t kernel_platform_quantum_cycles(void)
{
    return quantum_cycles;
}

void kernel_platform_timer_arm(uint32_t cycles)
{
    if (cycles == 0u)
        cycles = 1u;
    VESTA_TIMER_WRITE(0u, LOAD, cycles);
    VESTA_TIMER_WRITE(0u, CTRL, TMR_ENABLE | TMR_IRQ_EN);
}

void kernel_platform_timer_disarm(void)
{
    VESTA_TIMER_WRITE(0u, CTRL, 0u);
}

void kernel_platform_interrupt_init(uint32_t cpu_hz)
{
    quantum_cycles = cpu_hz / KERNEL_PLATFORM_QUANTUM_HZ;
    if (quantum_cycles == 0u)
        quantum_cycles = 1u;

    tick_count = 0u;
    VESTA_WRITE(IRQ_ENABLE, 0u);
    kernel_platform_timer_disarm();
    VESTA_TIMER_WRITE(0u, STATUS, TMR_EXPIRED);
    VESTA_WRITE(IRQ_ACK, IRQ_BIT(IRQ_SRC_TIMER0));
    VESTA_WRITE(BUS_FAULT_ACK, BUS_FAULT_VALID);
    kernel_mmio_cpu_sync();
}

uint32_t kernel_platform_ticks(void)
{
    return tick_count;
}

#if !defined(__m68k__)
uint32_t kernel_platform_cpu_cycles_low(void)
{
    return VESTA_READ(CPU_CYCLES_LO);
}
#endif

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    // Reading LO latches the coherent HI value for the following MMIO read.
    uint32_t low = VESTA_READ(CPU_CYCLES_LO);
    uint32_t high = VESTA_READ(CPU_CYCLES_HI);

    cycles->high = high;
    cycles->low = low;
}

uint64_t kernel_platform_cycles_to_ns(uint64_t cycles)
{
    const uint64_t maximum = (uint64_t)INT64_MAX - 1u;

    if (cycles > maximum / KERNEL_PLATFORM_NS_PER_CPU_CYCLE)
        return maximum;
    return cycles * KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
}

uint64_t kernel_platform_monotonic_ns(void)
{
    KernelPlatformCycleCount snapshot;
    uint64_t cycles;

    kernel_platform_cpu_cycles(&snapshot);
    cycles = ((uint64_t)snapshot.high << 32) | snapshot.low;
    return kernel_platform_cycles_to_ns(cycles);
}

bool kernel_platform_deadline_to_cycles(int64_t deadline_ns,
                                        uint64_t *deadline_cycles)
{
    if (deadline_cycles == NULL || deadline_ns < 0)
        return false;
    if (deadline_ns == INT64_MAX) {
        *deadline_cycles = UINT64_MAX;
        return true;
    }
    *deadline_cycles = ceil_nanoseconds_to_cycles((uint64_t)deadline_ns);
    return true;
}

uint32_t kernel_platform_system_status(void)
{
    return VESTA_READ(SYS_STATUS);
}

uint32_t kernel_platform_build_id(void)
{
    return VESTA_READ(BUILD_ID);
}

bool kernel_platform_post_text_present(void)
{
    if ((VESTA_READ(SYS_STATUS) & SYS_VIDEO_READY) == 0u)
        return false;
    return VEGA_READ(ID) == VEGA_ID_MAGIC &&
           (VEGA_READ(CAPS) & VEGA_CAP_POST_TEXT) != 0u;
}

bool kernel_platform_post_text_read(uint32_t cell, uint8_t *value)
{
    if (value == NULL || cell >= VEGA_POST_COLS * VEGA_POST_ROWS)
        return false;
    *value = kernel_mmio_read8(VEGA_POST_TEXT_BASE + cell);
    return true;
}

bool kernel_platform_post_text_write(uint32_t cell, uint8_t value)
{
    if (cell >= VEGA_POST_COLS * VEGA_POST_ROWS)
        return false;
    kernel_mmio_write8(VEGA_POST_TEXT_BASE + cell, value);
    return true;
}

bool kernel_platform_post_text_cursor(uint32_t row, uint32_t column,
                                      bool visible)
{
    static uint8_t sequence;
    uint8_t next = (uint8_t)(sequence + 2u);
    uint32_t base = VEGA_POST_TEXT_BASE + ASTRA_TEXT_CURSOR_OFFSET;

    kernel_mmio_write8(VEGA_POST_TEXT_BASE +
                           ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET,
                       (uint8_t)(next - 1u));
    kernel_mmio_write8(base + 0u, ASTRA_TEXT_CURSOR_MAGIC_0);
    kernel_mmio_write8(base + 1u, ASTRA_TEXT_CURSOR_MAGIC_1);
    kernel_mmio_write8(base + 2u, ASTRA_TEXT_CURSOR_MAGIC_2);
    kernel_mmio_write8(base + 3u, ASTRA_TEXT_CURSOR_MAGIC_3);
    kernel_mmio_write8(VEGA_POST_TEXT_BASE + ASTRA_TEXT_CURSOR_ROW_OFFSET,
                       (uint8_t)row);
    kernel_mmio_write8(VEGA_POST_TEXT_BASE + ASTRA_TEXT_CURSOR_COLUMN_OFFSET,
                       (uint8_t)column);
    kernel_mmio_write8(VEGA_POST_TEXT_BASE + ASTRA_TEXT_CURSOR_FLAGS_OFFSET,
                       visible ? ASTRA_TEXT_CURSOR_VISIBLE : 0u);
    kernel_mmio_write8(VEGA_POST_TEXT_BASE +
                           ASTRA_TEXT_CURSOR_SEQUENCE_OFFSET,
                       next);
    sequence = next;
    return true;
}

void kernel_platform_post_text_geometry(uint32_t *columns, uint32_t *rows)
{
    if (columns != NULL)
        *columns = VEGA_POST_COLS;
    if (rows != NULL)
        *rows = VEGA_POST_ROWS;
}

uint32_t kernel_platform_display_capabilities(void)
{
    uint32_t capabilities = kernel_platform_post_text_present() ?
        ASTRA_DISPLAY_CAP_TEXT : 0u;

    if (VESTA_READ(DISPLAY_ID) == ASTRA_DISPLAY_HOST_ID_MAGIC &&
        VESTA_READ(DISPLAY_VERSION) == ASTRA_DISPLAY_HOST_VERSION_1_0) {
        uint32_t host = VESTA_READ(DISPLAY_CAPS);

        if ((host & ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME) != 0u)
            capabilities |= ASTRA_DISPLAY_CAP_SOLID_FRAME;
        if ((host & ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT) != 0u)
            capabilities |= ASTRA_DISPLAY_CAP_FENCED_PRESENT;
        if ((host & ASTRA_DISPLAY_HOST_CAP_RENDER_BATCH) != 0u)
            capabilities |= ASTRA_DISPLAY_CAP_RENDER_BATCH;
        if ((host & ASTRA_DISPLAY_HOST_CAP_HARDWARE_CURSOR) != 0u)
            capabilities |= ASTRA_DISPLAY_CAP_HARDWARE_CURSOR;
    }
    return capabilities;
}

bool kernel_platform_display_submit(uint32_t id, uint32_t operation,
                                    uint32_t source, uint32_t byte_size)
{
    uint32_t queue;
    uint32_t host_operation = operation;

    if (id == 0u ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RGB565 &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         operation != ASTRA_DISPLAY_CURSOR_UPDATE) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         ((source & UINT32_C(0xffff0000)) != 0u || byte_size != 0u)) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 && byte_size != 0u) ||
        (operation == ASTRA_DISPLAY_CURSOR_UPDATE &&
         ((byte_size &
           ~(ASTRA_DISPLAY_CURSOR_VISIBLE |
             ASTRA_DISPLAY_CURSOR_DEFER_COMMIT)) != 0u ||
          (source & ASTRA_DISPLAY_HOST_CURSOR_X_MASK) >= ASTRA_DISPLAY_WIDTH ||
          ((source & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
               ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT) >= ASTRA_DISPLAY_HEIGHT)) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         (byte_size < ASTRA_RENDER_BATCH_MIN_BYTES ||
          byte_size > ASTRA_DISPLAY_HOST_BYTE_SIZE_MAX)) ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         operation != ASTRA_DISPLAY_CURSOR_UPDATE &&
         (source == 0u || (source & 3u) != 0u)) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         (kernel_platform_display_capabilities() &
          ASTRA_DISPLAY_CAP_RENDER_BATCH) == 0u) ||
        (operation == ASTRA_DISPLAY_CURSOR_UPDATE &&
         (kernel_platform_display_capabilities() &
          ASTRA_DISPLAY_CAP_HARDWARE_CURSOR) == 0u) ||
        (kernel_platform_display_capabilities() &
         (ASTRA_DISPLAY_CAP_SOLID_FRAME |
          ASTRA_DISPLAY_CAP_FENCED_PRESENT)) !=
            (ASTRA_DISPLAY_CAP_SOLID_FRAME |
             ASTRA_DISPLAY_CAP_FENCED_PRESENT))
        return false;
    queue = VESTA_READ(DISPLAY_QUEUE);
    if ((queue & ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY) == 0u ||
        (queue & (ASTRA_DISPLAY_HOST_QUEUE_BUSY |
                  ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID)) != 0u)
        return false;
    VESTA_WRITE(DISPLAY_REQ_ID, id);
    if (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ||
        operation == ASTRA_DISPLAY_CURSOR_UPDATE)
        host_operation |= byte_size << ASTRA_DISPLAY_HOST_BYTE_SIZE_SHIFT;
    VESTA_WRITE(DISPLAY_REQ_OP, host_operation);
    VESTA_WRITE(DISPLAY_REQ_COLOR, source);
    ASTRAEA_WRITE(IRQ_STAT, ASTRAEA_IRQ_DRAW_DONE);
    ASTRAEA_WRITE(IRQ_EN,
                  ASTRAEA_READ(IRQ_EN) | ASTRAEA_IRQ_DRAW_DONE);
    VESTA_WRITE(DISPLAY_REQ_SUBMIT, ASTRA_DISPLAY_HOST_SUBMIT);
#if defined(KERNEL_PLATFORM_HOST_TEST)
    VESTA_WRITE(DISPLAY_QUEUE, ASTRA_DISPLAY_HOST_QUEUE_BUSY);
#endif
    kernel_mmio_cpu_sync();
    return (VESTA_READ(DISPLAY_QUEUE) &
            ASTRA_DISPLAY_HOST_QUEUE_BUSY) != 0u;
}

bool kernel_platform_display_collect(AstraDisplayFrameCompletion *completion)
{
    if (completion == NULL ||
        (VESTA_READ(DISPLAY_QUEUE) &
         ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID) == 0u)
        return false;
    completion->size = ASTRA_DISPLAY_FRAME_COMPLETION_SIZE;
    completion->fence = VESTA_READ(DISPLAY_CPL_ID);
    completion->status = VESTA_READ(DISPLAY_CPL_STATUS);
    completion->generation = VESTA_READ(DISPLAY_CPL_GENERATION);
    completion->reserved = 0u;
    VESTA_WRITE(DISPLAY_REQ_SUBMIT, ASTRA_DISPLAY_HOST_POP);
#if defined(KERNEL_PLATFORM_HOST_TEST)
    VESTA_WRITE(DISPLAY_QUEUE, ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY);
#endif
    ASTRAEA_WRITE(IRQ_STAT, ASTRAEA_IRQ_DRAW_DONE);
    kernel_mmio_cpu_sync();
    return (VESTA_READ(DISPLAY_QUEUE) &
            ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID) == 0u;
}

bool kernel_platform_display_reset(void)
{
    VESTA_WRITE(DISPLAY_REQ_SUBMIT, ASTRA_DISPLAY_HOST_RESET);
#if defined(KERNEL_PLATFORM_HOST_TEST)
    VESTA_WRITE(DISPLAY_QUEUE, ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY);
#endif
    ASTRAEA_WRITE(IRQ_EN,
                  ASTRAEA_READ(IRQ_EN) & ~ASTRAEA_IRQ_DRAW_DONE);
    ASTRAEA_WRITE(IRQ_STAT, ASTRAEA_IRQ_DRAW_DONE);
    kernel_mmio_cpu_sync();
    return (VESTA_READ(DISPLAY_QUEUE) &
            (ASTRA_DISPLAY_HOST_QUEUE_BUSY |
             ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID)) == 0u;
}

bool kernel_platform_bus_fault_read(KernelPlatformBusFault *fault)
{
    uint32_t status;

    if (fault == NULL)
        return false;
    status = VESTA_READ(BUS_FAULT_STATUS);
    if ((status & BUS_FAULT_VALID) == 0u)
        return false;
    fault->status = status;
    fault->address = VESTA_READ(BUS_FAULT_ADDRESS);
    fault->target = VESTA_READ(BUS_FAULT_TARGET);
    fault->cycles_low = VESTA_READ(BUS_FAULT_CYCLES_LO);
    fault->cycles_high = VESTA_READ(BUS_FAULT_CYCLES_HI);
    fault->lost = VESTA_READ(BUS_FAULT_LOST);
    fault->timeout_cycles = VESTA_READ(BUS_TIMEOUT_CYCLES);
    return true;
}

void kernel_platform_bus_fault_acknowledge(void)
{
    VESTA_WRITE(BUS_FAULT_ACK, BUS_FAULT_VALID);
    kernel_mmio_cpu_sync();
}

void kernel_platform_debug_marker(uint32_t value)
{
    VESTA_WRITE(SCRATCH, value);
    kernel_mmio_cpu_sync();
}

bool kernel_platform_diagnostic_putc(uint8_t value)
{
    uint32_t timeout = VESTA_READ(CPU_HZ) / 1000u;
    uint32_t started = VESTA_READ(CPU_CYCLES_LO);
    uint32_t attempts;

    if (timeout == 0u)
        timeout = 1u;
    attempts = timeout;
    while ((VESTA_READ(UART_STATUS) & UART_TX_READY) == 0u) {
        if ((uint32_t)(VESTA_READ(CPU_CYCLES_LO) - started) >= timeout ||
            --attempts == 0u)
            return false;
    }
    return kernel_platform_diagnostic_try_putc(value);
}

bool kernel_platform_diagnostic_try_putc(uint8_t value)
{
    if ((VESTA_READ(UART_STATUS) & UART_TX_READY) == 0u)
        return false;
    VESTA_WRITE(UART_DATA, value);
    kernel_mmio_cpu_sync();
    return true;
}

uint32_t kernel_platform_diagnostic_rx_status(void)
{
    return VESTA_READ(UART_RX_STATUS);
}

bool kernel_platform_diagnostic_getc(uint8_t *value)
{
    if (value == NULL ||
        (VESTA_READ(UART_RX_STATUS) & UART_RX_READY) == 0u)
        return false;
    *value = (uint8_t)VESTA_READ(UART_RX_DATA);
    return true;
}

void kernel_platform_diagnostic_ack_overrun(void)
{
    VESTA_WRITE(UART_RX_STATUS, UART_RX_FIFO_OVERRUN);
    kernel_mmio_cpu_sync();
}

bool kernel_platform_monitor_spi_present(void)
{
    return VESTA_READ(MONITOR_ID) == MONITOR_ID_MAGIC &&
           (VESTA_READ(MONITOR_VERSION) >> 16) ==
                (MONITOR_VERSION_1_0 >> 16) &&
           (VESTA_READ(MONITOR_CAPS) &
                (MONITOR_CAP_RX | MONITOR_CAP_TX | MONITOR_CAP_IRQ)) ==
                (MONITOR_CAP_RX | MONITOR_CAP_TX | MONITOR_CAP_IRQ);
}

uint32_t kernel_platform_monitor_spi_status(void)
{
    return VESTA_READ(MONITOR_STATUS);
}

bool kernel_platform_monitor_spi_getc(uint8_t *value)
{
    if (value == NULL ||
        (VESTA_READ(MONITOR_STATUS) & MONITOR_STATUS_RX_VALID) == 0u)
        return false;
    *value = (uint8_t)VESTA_READ(MONITOR_RX_DATA);
    VESTA_WRITE(MONITOR_RX_POP, MONITOR_RX_POP_BIT);
    kernel_mmio_cpu_sync();
    return true;
}

bool kernel_platform_monitor_spi_try_putc(uint8_t value)
{
    if ((VESTA_READ(MONITOR_STATUS) & MONITOR_STATUS_TX_READY) == 0u)
        return false;
    VESTA_WRITE(MONITOR_TX_DATA, value);
    kernel_mmio_cpu_sync();
    return true;
}

void kernel_platform_monitor_spi_ack_errors(uint32_t errors)
{
    VESTA_WRITE(MONITOR_ERROR,
                errors & (MONITOR_ERROR_RX_EMPTY |
                          MONITOR_ERROR_TX_FULL));
    kernel_mmio_cpu_sync();
}

bool kernel_platform_irq_current(uint8_t *source, uint8_t *vector)
{
    uint32_t current = VESTA_READ(IRQ_CURRENT);

    if (source == NULL || vector == NULL ||
        (current & 0x80000000u) == 0u)
        return false;
    *source = (uint8_t)((current >> 8) & 0x1fu);
    *vector = (uint8_t)((current >> 16) & 0xffu);
#if defined(KERNEL_PLATFORM_HOST_TEST)
    // Target Vesta performs this atomic mask as the IRQ_CURRENT read commits.
    VESTA_WRITE(IRQ_ENABLE,
                VESTA_READ(IRQ_ENABLE) & ~IRQ_BIT(*source));
#endif
    return true;
}

bool kernel_platform_irq_configure(uint8_t source, uint8_t trigger,
                                   uint8_t ipl, uint8_t vector,
                                   void *context)
{
    uint32_t configuration;

    (void)context;
    if (source >= 32u || trigger > 1u || ipl == 0u || ipl > 7u)
        return false;
    configuration = IRQ_CFG_LEVEL(ipl) | IRQ_CFG_VECTOR(vector);
    if (trigger != 0u)
        configuration |= IRQ_CFG_EDGE;
    VESTA_ARRAY_WRITE(IRQ_CFG, source, configuration);
    kernel_mmio_cpu_sync();
    return true;
}

static bool irq_enable_update(uint8_t source, bool enable)
{
    uint16_t saved_status;
    uint32_t enabled;

    if (source >= 32u)
        return false;
    saved_status = kernel_interrupt_save_disable();
    enabled = VESTA_READ(IRQ_ENABLE);
    if (enable)
        enabled |= IRQ_BIT(source);
    else
        enabled &= ~IRQ_BIT(source);
    VESTA_WRITE(IRQ_ENABLE, enabled);
    kernel_mmio_cpu_sync();
    kernel_interrupt_restore(saved_status);
    return true;
}

bool kernel_platform_irq_mask(uint8_t source, void *context)
{
    (void)context;
    return irq_enable_update(source, false);
}

bool kernel_platform_irq_enable(uint8_t source, void *context)
{
    (void)context;
    return irq_enable_update(source, true);
}

bool kernel_platform_irq_acknowledge(uint8_t source, void *context)
{
    (void)context;
    if (source >= 32u)
        return false;
    VESTA_WRITE(IRQ_ACK, IRQ_BIT(source));
    kernel_mmio_cpu_sync();
    return true;
}

bool kernel_platform_timer_irq_service(uint8_t source, uint64_t timestamp,
                                       void *context)
{
    (void)timestamp;
    (void)context;
    if (source != IRQ_SRC_TIMER0 ||
        (kernel_mmio_read32(VESTA_TIMER_ADDRESS(0u, STATUS)) &
         TMR_EXPIRED) == 0u)
        return false;
    VESTA_TIMER_WRITE(0u, STATUS, TMR_EXPIRED);
    ++tick_count;
    kernel_platform_timer_arm(quantum_cycles);
    return true;
}

bool kernel_platform_device_irq_capture(uint8_t source, uint32_t *status)
{
    uint32_t pending;

    if (status == NULL)
        return false;
    switch (source) {
    case IRQ_SRC_STORAGE:
        pending = VESTA_READ(BLOCK_QUEUE);
        if ((VESTA_READ(BLOCK_STATE_ACK) & BLOCK_STATE_ACK_BIT) != 0u)
            pending |= KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE;
        if ((pending & (BLOCK_QUEUE_COMPLETION_VALID |
                        KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE)) == 0u)
            return false;
        break;
    case IRQ_SRC_INPUT:
        pending = VESTA_READ(INPUT_STATUS);
        if ((pending & INPUT_EVENT_VALID) == 0u)
            return false;
        /* The head identity lets completion accept a newly arrived event
         * without mistaking an undrained old one for progress. */
        pending = VESTA_READ(INPUT_DEVICE_SEQ);
        break;
    case IRQ_SRC_VEGA:
        pending = VEGA_READ(IRQ_STAT) & VEGA_READ(IRQ_EN);
        if (pending == 0u)
            return false;
        break;
    case IRQ_SRC_USB: {
        uint32_t astra_status = OHCI_READ(ASTRA_STATUS);

        if ((astra_status & (OHCI_ASTRA_DMA_FAULT | OHCI_ASTRA_IRQ)) == 0u)
            return false;
        pending = OHCI_READ(INTERRUPT_STATUS) & OHCI_INTERRUPT_SOURCES;
        if ((astra_status & OHCI_ASTRA_IRQ) != 0u)
            pending |= KERNEL_PLATFORM_USB_IRQ_CONTROLLER;
        if ((astra_status & OHCI_ASTRA_DMA_FAULT) != 0u)
            pending |= KERNEL_PLATFORM_USB_IRQ_DMA_FAULT;
        break;
    }
    case IRQ_SRC_ASTRAEA:
        pending = ASTRAEA_READ(IRQ_STAT) & ASTRAEA_READ(IRQ_EN);
        if (pending == 0u)
            return false;
        break;
    default:
        return false;
    }
    *status = pending;
    return true;
}

bool kernel_platform_device_irq_complete(uint8_t source,
                                         uint32_t captured_status)
{
    switch (source) {
    case IRQ_SRC_STORAGE:
        return (VESTA_READ(BLOCK_QUEUE) &
                BLOCK_QUEUE_COMPLETION_VALID) == 0u &&
               (VESTA_READ(BLOCK_STATE_ACK) &
                BLOCK_STATE_ACK_BIT) == 0u;
    case IRQ_SRC_INPUT:
        return (VESTA_READ(INPUT_STATUS) & INPUT_EVENT_VALID) == 0u ||
               VESTA_READ(INPUT_DEVICE_SEQ) != captured_status;
    case IRQ_SRC_VEGA:
        return (VEGA_READ(IRQ_STAT) & VEGA_READ(IRQ_EN)) == 0u;
    case IRQ_SRC_USB:
        return (OHCI_READ(ASTRA_STATUS) &
                (OHCI_ASTRA_DMA_FAULT | OHCI_ASTRA_IRQ)) == 0u;
    case IRQ_SRC_ASTRAEA:
        return (ASTRAEA_READ(IRQ_STAT) & ASTRAEA_READ(IRQ_EN)) == 0u;
    default:
        return false;
    }
}

bool kernel_platform_device_irq_quiesce(uint8_t source)
{
    uint32_t pending;

    switch (source) {
    case IRQ_SRC_STORAGE:
    case IRQ_SRC_INPUT:
        return true;
    case IRQ_SRC_VEGA:
        pending = VEGA_READ(IRQ_STAT);
        VEGA_WRITE(IRQ_EN, 0u);
        if (pending != 0u)
            VEGA_WRITE(IRQ_STAT, pending);
        kernel_mmio_cpu_sync();
        return true;
    case IRQ_SRC_USB: {
        return ohci_controller_soft_reset();
    }
    case IRQ_SRC_ASTRAEA:
        pending = ASTRAEA_READ(IRQ_STAT);
        ASTRAEA_WRITE(IRQ_EN, 0u);
        ASTRAEA_WRITE(COP_CTRL, 0u);
        if (pending != 0u)
            ASTRAEA_WRITE(IRQ_STAT, pending);
        kernel_mmio_cpu_sync();
        return true;
    default:
        return false;
    }
}

uint32_t kernel_platform_qualification_irq_sources(void)
{
    uint32_t sources = 0u;
    uint32_t system_status = kernel_platform_system_status();

    if ((system_status & SYS_ASTRA_HOST) != 0u &&
        kernel_platform_block_present() && kernel_platform_input_present()) {
        sources |= IRQ_BIT(IRQ_SRC_STORAGE) | IRQ_BIT(IRQ_SRC_INPUT);
    }
    if ((system_status & SYS_VIDEO_READY) != 0u &&
        VEGA_READ(ID) == VEGA_ID_MAGIC)
        sources |= IRQ_BIT(IRQ_SRC_VEGA);
    if ((system_status & SYS_USB_READY) != 0u &&
        OHCI_READ(ASTRA_ID) == OHCI_ASTRA_ID_MAGIC)
        sources |= IRQ_BIT(IRQ_SRC_USB);
    if (ASTRAEA_READ(ID) == ASTRAEA_ID_MAGIC)
        sources |= IRQ_BIT(IRQ_SRC_ASTRAEA);
    return sources & KERNEL_QUALIFICATION_IRQ_SOURCE_MASK;
}

bool kernel_platform_qualification_irq_prepare(uint8_t source)
{
    uint32_t pending;

    if (source >= 32u)
        return false;
    if ((kernel_platform_qualification_irq_sources() & IRQ_BIT(source)) ==
        0u)
        return false;
    switch (source) {
    case IRQ_SRC_STORAGE:
    case IRQ_SRC_INPUT:
        return kernel_platform_device_irq_capture(source, &pending);
    case IRQ_SRC_VEGA:
        pending = VEGA_READ(IRQ_STAT);
        VEGA_WRITE(IRQ_EN, 0u);
        if (pending != 0u)
            VEGA_WRITE(IRQ_STAT, pending);
        VEGA_WRITE(IRQ_EN, VEGA_IRQ_VBLANK);
        kernel_mmio_cpu_sync();
        (void)kernel_mmio_fence32(VEGA_ADDRESS(IRQ_EN));
        return true;
    case IRQ_SRC_USB: {
        uint32_t control;

        if (OHCI_READ(ASTRA_DMA_POOL_BASE) != OHCI_DMA_POOL_BASE ||
            OHCI_READ(ASTRA_DMA_POOL_SIZE) != OHCI_DMA_POOL_SIZE)
            return false;
        if (!ohci_controller_soft_reset())
            return false;
        ohci_hcca_clear();
        control = OHCI_READ(CONTROL);
        control &= ~OHCI_CONTROL_HCFS_MASK;
        control |= OHCI_CONTROL_HCFS_OPERATIONAL;
        OHCI_WRITE(HCCA, OHCI_DMA_POOL_BASE);
        OHCI_WRITE(CONTROL, control);
        OHCI_WRITE(INTERRUPT_ENABLE, OHCI_INT_MIE | OHCI_INT_SF);
        kernel_mmio_cpu_sync();
        return kernel_mmio_fence32(OHCI_ADDRESS(HCCA)) ==
                   OHCI_DMA_POOL_BASE &&
               (kernel_mmio_fence32(OHCI_ADDRESS(CONTROL)) &
                OHCI_CONTROL_HCFS_MASK) ==
                   OHCI_CONTROL_HCFS_OPERATIONAL &&
               (kernel_mmio_fence32(OHCI_ADDRESS(INTERRUPT_ENABLE)) &
                (OHCI_INT_MIE | OHCI_INT_SF)) ==
                   (OHCI_INT_MIE | OHCI_INT_SF);
    }
    case IRQ_SRC_ASTRAEA:
        pending = ASTRAEA_READ(IRQ_STAT);
        ASTRAEA_WRITE(IRQ_EN, 0u);
        if (pending != 0u)
            ASTRAEA_WRITE(IRQ_STAT, pending);
        ASTRAEA_WRITE(BLIT_DIM, 0u);
        ASTRAEA_WRITE(IRQ_EN, ASTRAEA_IRQ_BLIT_DONE);
        ASTRAEA_WRITE(BLIT_CTRL, BLIT_START | BLIT_IRQ_EN);
        kernel_mmio_cpu_sync();
        (void)kernel_mmio_fence32(ASTRAEA_ADDRESS(BLIT_STATUS));
        return true;
    default:
        return false;
    }
}

bool kernel_platform_qualification_irq_consume(uint8_t source,
                                               uint32_t status)
{
    switch (source) {
    case IRQ_SRC_STORAGE:
        if ((status & KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE) == 0u ||
            (status & BLOCK_QUEUE_COMPLETION_VALID) != 0u)
            return false;
        kernel_platform_block_ack_state();
        kernel_mmio_cpu_sync();
        return true;
    case IRQ_SRC_INPUT: {
        KernelInputEvent event;

        if (!kernel_input_peek(&event) ||
            event.device_sequence != status || !kernel_input_consume())
            return false;
        kernel_mmio_cpu_sync();
        return INPUT_EVENT_CLASS(event.header) ==
                   KERNEL_QUALIFICATION_INPUT_CLASS &&
               INPUT_EVENT_KIND(event.header) ==
                   KERNEL_QUALIFICATION_INPUT_KIND &&
               INPUT_EVENT_FLAGS(event.header) ==
                   KERNEL_QUALIFICATION_INPUT_FLAGS &&
               event.value == KERNEL_QUALIFICATION_INPUT_VALUE &&
               INPUT_EVENT_DEVICE(event.device_sequence) ==
                   KERNEL_QUALIFICATION_INPUT_DEVICE;
    }
    case IRQ_SRC_VEGA:
        return (status & VEGA_IRQ_VBLANK) != 0u &&
               kernel_platform_device_irq_quiesce(source);
    case IRQ_SRC_USB:
        return (status & (KERNEL_PLATFORM_USB_IRQ_CONTROLLER |
                          OHCI_INT_SF)) ==
                   (KERNEL_PLATFORM_USB_IRQ_CONTROLLER | OHCI_INT_SF) &&
               kernel_platform_device_irq_quiesce(source);
    case IRQ_SRC_ASTRAEA:
        return (status & ASTRAEA_IRQ_BLIT_DONE) != 0u &&
               kernel_platform_device_irq_quiesce(source);
    default:
        return false;
    }
}

bool kernel_platform_block_present(void)
{
    return VESTA_READ(BLOCK_ID) == BLOCK_ID_MAGIC &&
           (VESTA_READ(BLOCK_VERSION) >> 16) ==
                (BLOCK_VERSION_1_0 >> 16);
}

uint32_t kernel_platform_block_state_flags(void)
{
    return VESTA_READ(BLOCK_STATE);
}

bool kernel_platform_block_state(KernelPlatformBlockState *state)
{
    uint32_t capabilities_before;
    uint32_t flags_before;
    uint32_t media_before;
    uint32_t host_before;
    uint32_t size_hi_before;
    uint32_t size_lo_before;
    uint32_t max_before;
    uint32_t capabilities_after;
    uint32_t flags_after;
    uint32_t media_after;
    uint32_t host_after;
    uint32_t size_hi_after;
    uint32_t size_lo_after;
    uint32_t max_after;

    if (state == 0 || !kernel_platform_block_present())
        return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        capabilities_before = VESTA_READ(BLOCK_CAPS);
        flags_before = VESTA_READ(BLOCK_STATE);
        host_before = VESTA_READ(BLOCK_HOST_GEN);
        media_before = VESTA_READ(BLOCK_MEDIA_GEN);
        size_hi_before = VESTA_READ(BLOCK_MEDIA_SIZE_HI);
        size_lo_before = VESTA_READ(BLOCK_MEDIA_SIZE_LO);
        max_before = VESTA_READ(BLOCK_MAX_SECTORS);

        capabilities_after = VESTA_READ(BLOCK_CAPS);
        flags_after = VESTA_READ(BLOCK_STATE);
        host_after = VESTA_READ(BLOCK_HOST_GEN);
        media_after = VESTA_READ(BLOCK_MEDIA_GEN);
        size_hi_after = VESTA_READ(BLOCK_MEDIA_SIZE_HI);
        size_lo_after = VESTA_READ(BLOCK_MEDIA_SIZE_LO);
        max_after = VESTA_READ(BLOCK_MAX_SECTORS);
        if (capabilities_before == capabilities_after &&
            flags_before == flags_after && host_before == host_after &&
            media_before == media_after && size_hi_before == size_hi_after &&
            size_lo_before == size_lo_after && max_before == max_after) {
            state->capabilities = capabilities_after;
            state->state_flags = flags_after;
            state->media_generation = media_after;
            state->host_generation = host_after;
            state->media_sectors = ((uint64_t)size_hi_after << 32) |
                                   size_lo_after;
            state->max_sectors = (uint16_t)max_after;
            state->reserved = 0u;
            return true;
        }
    }
    return false;
}

uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer)
{
    if (!kernel_platform_block_present())
        return BLOCK_ERROR_NO_MEDIA;
    if ((VESTA_READ(BLOCK_QUEUE) & BLOCK_QUEUE_REQUEST_READY) == 0u)
        return BLOCK_ERROR_QUEUE_FULL;

    VESTA_WRITE(BLOCK_ERROR, 0xffffffffu);
    VESTA_WRITE(BLOCK_REQ_ID, id);
    VESTA_WRITE(BLOCK_REQ_OP, ((uint32_t)flags << 8) | operation);
    VESTA_WRITE(BLOCK_REQ_LBA_HI, (uint32_t)(lba >> 32));
    VESTA_WRITE(BLOCK_REQ_LBA_LO, (uint32_t)lba);
    VESTA_WRITE(BLOCK_REQ_SECTORS, sectors);
    VESTA_WRITE(BLOCK_REQ_BUFFER, physical_buffer);
    VESTA_WRITE(BLOCK_REQ_SUBMIT, BLOCK_SUBMIT);
    return kernel_mmio_fence32(VESTA_ADDRESS(BLOCK_ERROR));
}

bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion)
{
    uint32_t queue;
    uint32_t status;
    if (completion == 0)
        return false;
    queue = VESTA_READ(BLOCK_QUEUE);
    if ((queue & BLOCK_QUEUE_COMPLETION_VALID) == 0u)
        return false;

    completion->id = VESTA_READ(BLOCK_CPL_ID);
    status = VESTA_READ(BLOCK_CPL_STATUS);
    completion->status = (uint16_t)(status >> 16);
    completion->sectors = (uint16_t)status;
    completion->detail = VESTA_READ(BLOCK_CPL_DETAIL);
    completion->media_generation = VESTA_READ(BLOCK_CPL_MEDIA_GEN);
    completion->host_generation = VESTA_READ(BLOCK_CPL_HOST_GEN);
    VESTA_WRITE(BLOCK_CPL_POP, BLOCK_CPL_POP_BIT);
    return true;
}

void kernel_platform_block_ack_state(void)
{
    VESTA_WRITE(BLOCK_STATE_ACK, BLOCK_STATE_ACK_BIT);
}

bool kernel_platform_input_present(void)
{
    return VESTA_READ(INPUT_ID) == INPUT_ID_MAGIC;
}

uint32_t kernel_platform_input_status(void)
{
    return VESTA_READ(INPUT_STATUS);
}

bool kernel_input_peek(KernelInputEvent *event)
{
    if (event == 0 || (VESTA_READ(INPUT_STATUS) & INPUT_EVENT_VALID) == 0u)
        return false;
    event->header = VESTA_READ(INPUT_HEADER);
    event->value = VESTA_READ(INPUT_VALUE);
    event->timestamp_ms = VESTA_READ(INPUT_TIMESTAMP);
    event->device_sequence = VESTA_READ(INPUT_DEVICE_SEQ);
    event->host_generation = VESTA_READ(INPUT_HOST_GEN);
    return true;
}

bool kernel_input_consume(void)
{
    if ((VESTA_READ(INPUT_STATUS) & INPUT_EVENT_VALID) == 0u)
        return false;
    VESTA_WRITE(INPUT_POP, INPUT_POP_BIT);
    return true;
}

void kernel_platform_input_ack_overflow(void)
{
    VESTA_WRITE(INPUT_POP, ASTRA_INPUT_ACK_OVERFLOW);
    kernel_mmio_cpu_sync();
}

bool kernel_platform_input_quiesce(void)
{
    return kernel_platform_irq_mask(IRQ_SRC_INPUT, NULL);
}

bool kernel_platform_input_reset(void)
{
    uint32_t drained = 0u;

    if (!kernel_platform_input_quiesce())
        return false;
    while ((VESTA_READ(INPUT_STATUS) & INPUT_EVENT_VALID) != 0u &&
           drained < 31u) {
        VESTA_WRITE(INPUT_POP, INPUT_POP_BIT);
        ++drained;
    }
    kernel_platform_input_ack_overflow();
    return (VESTA_READ(INPUT_STATUS) &
            (INPUT_EVENT_VALID | ASTRA_INPUT_STATUS_OVERFLOW)) == 0u;
}
