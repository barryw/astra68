#include "platform.h"
#include "qualification.h"

#include <astra/display.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t interrupt_save_count;
static uint32_t interrupt_restore_count;
static uint8_t interrupts_masked;

uint16_t kernel_interrupt_save_disable(void)
{
    assert(interrupts_masked == 0u);
    interrupts_masked = 1u;
    ++interrupt_save_count;
    return 0x2000u;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    assert(interrupts_masked == 1u);
    assert(status_register == 0x2000u);
    interrupts_masked = 0u;
    ++interrupt_restore_count;
}

static void clear_registers(VestaRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static void test_one_shot_configuration_and_restart(void)
{
    VestaRegs *registers = kernel_platform_test_registers();

    clear_registers(registers);
    kernel_platform_interrupt_init(12500000u);
    assert(kernel_platform_quantum_cycles() == 62500u);
    assert(registers->IRQ_ENABLE == 0u);
    assert(registers->TIMER[0].CTRL == 0u);
    assert(registers->BUS_FAULT_ACK == BUS_FAULT_VALID);
    assert(kernel_platform_irq_configure(IRQ_SRC_TIMER0, 0u, 4u, 80u,
                                         NULL));
    assert(registers->IRQ_CFG[IRQ_SRC_TIMER0] ==
           (IRQ_CFG_LEVEL(4u) | IRQ_CFG_VECTOR(80u)));
    assert(kernel_platform_irq_configure(IRQ_SRC_UART_RX, 1u, 3u, 80u,
                                         NULL));
    assert(registers->IRQ_CFG[IRQ_SRC_UART_RX] ==
           (IRQ_CFG_LEVEL(3u) | IRQ_CFG_VECTOR(80u) | IRQ_CFG_EDGE));
    assert(!kernel_platform_irq_configure(32u, 0u, 4u, 80u, NULL));
    assert(!kernel_platform_irq_configure(0u, 2u, 4u, 80u, NULL));

    kernel_platform_timer_arm(kernel_platform_quantum_cycles());
    assert(registers->TIMER[0].LOAD == 62500u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
    assert((registers->TIMER[0].CTRL & TMR_PERIODIC) == 0u);
    assert(kernel_platform_irq_enable(IRQ_SRC_TIMER0, NULL));
    assert(registers->IRQ_ENABLE == IRQ_BIT(IRQ_SRC_TIMER0));

    registers->TIMER[0].STATUS = TMR_EXPIRED;
    assert(kernel_platform_timer_irq_service(IRQ_SRC_TIMER0, 1u, NULL));
    assert(kernel_platform_ticks() == 1u);
    assert(registers->TIMER[0].STATUS == TMR_EXPIRED);
    assert(registers->TIMER[0].LOAD == 62500u);
    assert(!kernel_platform_timer_irq_service(IRQ_SRC_TIMER1, 2u, NULL));
    assert(kernel_platform_irq_acknowledge(IRQ_SRC_TIMER0, NULL));
    assert(registers->IRQ_ACK == IRQ_BIT(IRQ_SRC_TIMER0));
    assert(kernel_platform_irq_mask(IRQ_SRC_TIMER0, NULL));
    assert(registers->IRQ_ENABLE == 0u);

    kernel_platform_timer_arm(0u);
    assert(registers->TIMER[0].LOAD == 1u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
    kernel_platform_timer_disarm();
    assert(registers->TIMER[0].CTRL == 0u);

}

static void test_sticky_bus_fault_snapshot_and_acknowledge(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelPlatformBusFault fault;

    clear_registers(registers);
    assert(!kernel_platform_bus_fault_read(NULL));
    assert(!kernel_platform_bus_fault_read(&fault));

    registers->BUS_FAULT_STATUS =
        BUS_FAULT_VALID | BUS_FAULT_TIMEOUT | BUS_FAULT_DEVICE |
        BUS_FAULT_WRITE | (2u << BUS_FAULT_SIZE_SHIFT) |
        (5u << BUS_FAULT_FC_SHIFT);
    registers->BUS_FAULT_ADDRESS = 0xfff40020u;
    registers->BUS_FAULT_TARGET = BUS_FAULT_TARGET_USB;
    registers->BUS_FAULT_CYCLES_LO = 0x89abcdefu;
    registers->BUS_FAULT_CYCLES_HI = 0x01234567u;
    registers->BUS_FAULT_LOST = 3u;
    registers->BUS_TIMEOUT_CYCLES = 2048u;
    assert(kernel_platform_bus_fault_read(&fault));
    assert(fault.status == registers->BUS_FAULT_STATUS);
    assert(fault.address == 0xfff40020u);
    assert(fault.target == BUS_FAULT_TARGET_USB);
    assert(fault.cycles_high == 0x01234567u);
    assert(fault.cycles_low == 0x89abcdefu);
    assert(fault.lost == 3u);
    assert(fault.timeout_cycles == 2048u);

    kernel_platform_bus_fault_acknowledge();
    assert(registers->BUS_FAULT_ACK == BUS_FAULT_VALID);
}

static void test_cycle_snapshot_and_current_irq_decode(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelPlatformCycleCount cycles;
    uint8_t source;
    uint8_t vector;

    clear_registers(registers);
    registers->CPU_CYCLES_LO = 0x89abcdefu;
    registers->CPU_CYCLES_HI = 0x01234567u;
    kernel_platform_cpu_cycles(&cycles);
    assert(cycles.low == 0x89abcdefu);
    assert(cycles.high == 0x01234567u);

    registers->IRQ_CURRENT = 0u;
    assert(!kernel_platform_irq_current(&source, &vector));
    registers->IRQ_CURRENT = 0x80000000u | (99u << 16) | (7u << 8) | 3u;
    assert(kernel_platform_irq_current(&source, &vector));
    assert(source == 7u && vector == 99u);
    assert(!kernel_platform_irq_current(NULL, &vector));
    assert(!kernel_platform_irq_current(&source, NULL));
}

static void test_typed_identity_console_and_device_queries(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    VegaRegs *video = kernel_platform_test_vega_registers();
    uint8_t value = 0u;

    clear_registers(registers);
    video->ID = VEGA_ID_MAGIC;
    video->CAPS = VEGA_CAP_POST_TEXT;
    registers->SYS_STATUS = SYS_ASTRA_HOST;
    registers->BUILD_ID = 0x1234abcdu;
    assert(kernel_platform_system_status() ==
           SYS_ASTRA_HOST);
    assert(kernel_platform_build_id() == 0x1234abcdu);
    assert(!kernel_platform_post_text_present());
    registers->SYS_STATUS |= SYS_VIDEO_READY;
    assert(kernel_platform_post_text_present());
    assert(kernel_platform_post_text_write(0u, 0x41u));
    assert(kernel_platform_post_text_read(0u, &value));
    assert(value == 0x41u);
    assert(kernel_platform_post_text_write(
        VEGA_POST_COLS * VEGA_POST_ROWS - 1u, 0x5au));
    assert(kernel_platform_post_text_read(
        VEGA_POST_COLS * VEGA_POST_ROWS - 1u, &value));
    assert(value == 0x5au);
    assert(!kernel_platform_post_text_read(
        VEGA_POST_COLS * VEGA_POST_ROWS, &value));
    assert(!kernel_platform_post_text_read(0u, NULL));
    assert(!kernel_platform_post_text_write(
        VEGA_POST_COLS * VEGA_POST_ROWS, 0u));

    kernel_platform_debug_marker(0x4b313054u);
    assert(registers->SCRATCH == 0x4b313054u);

    registers->CPU_HZ = KERNEL_PLATFORM_CPU_HZ;
    registers->UART_STATUS = UART_TX_READY;
    assert(kernel_platform_diagnostic_putc(0xa5u));
    assert(registers->UART_DATA == 0xa5u);
    registers->UART_STATUS = 0u;
    assert(!kernel_platform_diagnostic_putc(0x5au));
    assert(!kernel_platform_diagnostic_try_putc(0x5au));
    registers->UART_STATUS = UART_TX_READY;
    assert(kernel_platform_diagnostic_try_putc(0x5au));
    assert(registers->UART_DATA == 0x5au);

    registers->UART_RX_STATUS = 0u;
    assert(kernel_platform_diagnostic_rx_status() == 0u);
    assert(!kernel_platform_diagnostic_getc(&value));
    assert(!kernel_platform_diagnostic_getc(NULL));
    registers->UART_RX_DATA = 0x000001c3u;
    registers->UART_RX_STATUS = UART_RX_READY | UART_RX_FIFO_OVERRUN |
                                (1u << 8);
    assert(kernel_platform_diagnostic_getc(&value));
    assert(value == 0xc3u);
    kernel_platform_diagnostic_ack_overrun();
    assert(registers->UART_RX_STATUS == UART_RX_FIFO_OVERRUN);

    assert(!kernel_platform_monitor_spi_present());
    registers->MONITOR_ID = MONITOR_ID_MAGIC;
    registers->MONITOR_VERSION = MONITOR_VERSION_1_0;
    registers->MONITOR_CAPS =
        MONITOR_CAP_RX | MONITOR_CAP_TX | MONITOR_CAP_IRQ;
    assert(kernel_platform_monitor_spi_present());
    registers->MONITOR_STATUS = 0u;
    assert(kernel_platform_monitor_spi_status() == 0u);
    assert(!kernel_platform_monitor_spi_getc(&value));
    assert(!kernel_platform_monitor_spi_getc(NULL));
    assert(!kernel_platform_monitor_spi_try_putc(0x11u));
    registers->MONITOR_RX_DATA = 0x000001a6u;
    registers->MONITOR_STATUS =
        MONITOR_STATUS_RX_VALID | MONITOR_STATUS_TX_READY |
        (3u << 8) | (4u << 16);
    assert(kernel_platform_monitor_spi_getc(&value));
    assert(value == 0xa6u);
    assert(registers->MONITOR_RX_POP == MONITOR_RX_POP_BIT);
    assert(kernel_platform_monitor_spi_try_putc(0x5cu));
    assert(registers->MONITOR_TX_DATA == 0x5cu);
    kernel_platform_monitor_spi_ack_errors(
        MONITOR_ERROR_RX_EMPTY | MONITOR_ERROR_TX_FULL | 0x80u);
    assert(registers->MONITOR_ERROR ==
           (MONITOR_ERROR_RX_EMPTY | MONITOR_ERROR_TX_FULL));

    registers->BLOCK_STATE = BLOCK_STATE_LINK_UP |
                             BLOCK_STATE_MEDIA_PRESENT;
    assert(kernel_platform_block_state_flags() == registers->BLOCK_STATE);
    assert(!kernel_platform_input_present());
    registers->INPUT_ID = INPUT_ID_MAGIC;
    assert(kernel_platform_input_present());
}

static void test_monotonic_nanosecond_deadline_conversion(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    uint64_t deadline;

    clear_registers(registers);
    registers->CPU_CYCLES_LO = 0x00000002u;
    registers->CPU_CYCLES_HI = 0u;
    assert(kernel_platform_monotonic_ns() == 160u);
    assert(kernel_platform_cycles_to_ns(0u) == 0u);
    assert(kernel_platform_cycles_to_ns(1u) == 80u);
    assert(kernel_platform_deadline_to_cycles(0, &deadline));
    assert(deadline == 0u);
    assert(kernel_platform_deadline_to_cycles(1, &deadline));
    assert(deadline == 1u);
    assert(kernel_platform_deadline_to_cycles(80, &deadline));
    assert(deadline == 1u);
    assert(kernel_platform_deadline_to_cycles(81, &deadline));
    assert(deadline == 2u);
    assert(kernel_platform_deadline_to_cycles(
        INT64_C(0x0123456789abcdef), &deadline));
    assert(deadline == UINT64_C(0x0003a4114b5225c7));
    assert(kernel_platform_deadline_to_cycles(INT64_MAX - 1, &deadline));
    assert(deadline == UINT64_C(0x019999999999999a));
    assert(kernel_platform_deadline_to_cycles(INT64_MAX, &deadline));
    assert(deadline == UINT64_MAX);
    assert(!kernel_platform_deadline_to_cycles(-1, &deadline));
    assert(!kernel_platform_deadline_to_cycles(1, NULL));
    assert(kernel_platform_cycles_to_ns(UINT64_MAX) ==
           (uint64_t)INT64_MAX - 1u);
}

static void test_fenced_display_transport(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    AstraeaRegs *astraea = kernel_platform_test_astraea_registers();
    AstraDisplayFrameCompletion completion;

    clear_registers(registers);
    assert(kernel_platform_display_capabilities() == 0u);
    registers->DISPLAY_ID = ASTRA_DISPLAY_HOST_ID_MAGIC;
    registers->DISPLAY_VERSION = ASTRA_DISPLAY_HOST_VERSION_1_0;
    registers->DISPLAY_CAPS = ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME |
                              ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT;
    registers->DISPLAY_QUEUE = ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY;
    assert(kernel_platform_display_capabilities() ==
           (ASTRA_DISPLAY_CAP_SOLID_FRAME |
            ASTRA_DISPLAY_CAP_FENCED_PRESENT));
    assert(!kernel_platform_display_submit(0u,
        ASTRA_DISPLAY_FRAME_PRESENT_SOLID, 0x135du));
    assert(kernel_platform_display_submit(7u,
        ASTRA_DISPLAY_FRAME_PRESENT_SOLID, 0x135du));
    assert(registers->DISPLAY_REQ_ID == 7u);
    assert(registers->DISPLAY_REQ_OP ==
           ASTRA_DISPLAY_FRAME_PRESENT_SOLID);
    assert(registers->DISPLAY_REQ_COLOR == 0x135du);
    registers->DISPLAY_QUEUE = ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY;
    assert(kernel_platform_display_submit(
        8u, ASTRA_DISPLAY_FRAME_PRESENT_RGB565, 0x02000000u));
    assert(registers->DISPLAY_REQ_COLOR == 0x02000000u);
    assert((astraea->IRQ_EN & ASTRAEA_IRQ_DRAW_DONE) != 0u);

    assert(!kernel_platform_display_collect(NULL));
    registers->DISPLAY_QUEUE = ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID;
    registers->DISPLAY_CPL_ID = 7u;
    registers->DISPLAY_CPL_STATUS = ASTRA_DISPLAY_COMPLETION_OK;
    registers->DISPLAY_CPL_GENERATION = 9u;
    assert(kernel_platform_display_collect(&completion));
    assert(completion.size == ASTRA_DISPLAY_FRAME_COMPLETION_SIZE);
    assert(completion.fence == 7u);
    assert(completion.status == ASTRA_DISPLAY_COMPLETION_OK);
    assert(completion.generation == 9u);
    assert(completion.reserved == 0u);
    assert(registers->DISPLAY_QUEUE ==
           ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY);
    assert(kernel_platform_display_reset());
    assert((astraea->IRQ_EN & ASTRAEA_IRQ_DRAW_DONE) == 0u);
}

static void test_production_irq_qualification_controls(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    VegaRegs *video = kernel_platform_test_vega_registers();
    AstraeaRegs *astraea = kernel_platform_test_astraea_registers();
    OhciRegs *usb = kernel_platform_test_ohci_registers();
    OhciHcca *hcca = kernel_platform_test_ohci_hcca();
    uint32_t status;

    clear_registers(registers);
    video->ID = 0u;
    astraea->ID = 0u;
    usb->ASTRA_ID = 0u;
    assert(kernel_platform_qualification_irq_sources() == 0u);

    registers->SYS_STATUS =
        SYS_ASTRA_HOST | SYS_VIDEO_READY | SYS_USB_READY;
    registers->BLOCK_ID = BLOCK_ID_MAGIC;
    registers->BLOCK_VERSION = BLOCK_VERSION_1_0;
    registers->INPUT_ID = INPUT_ID_MAGIC;
    video->ID = VEGA_ID_MAGIC;
    astraea->ID = ASTRAEA_ID_MAGIC;
    usb->ASTRA_ID = OHCI_ASTRA_ID_MAGIC;
    assert(kernel_platform_qualification_irq_sources() ==
           KERNEL_QUALIFICATION_IRQ_SOURCE_MASK);
    assert(!kernel_platform_qualification_irq_prepare(32u));

    registers->BLOCK_STATE_ACK = BLOCK_STATE_ACK_BIT;
    assert(kernel_platform_qualification_irq_prepare(IRQ_SRC_STORAGE));
    assert(kernel_platform_device_irq_capture(IRQ_SRC_STORAGE, &status));
    assert((status & KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE) != 0u);
    assert(kernel_platform_qualification_irq_consume(
        IRQ_SRC_STORAGE, status));
    assert(registers->BLOCK_STATE_ACK == BLOCK_STATE_ACK_BIT);

    registers->INPUT_STATUS = INPUT_EVENT_VALID | 1u;
    registers->INPUT_HEADER =
        ((uint32_t)KERNEL_QUALIFICATION_INPUT_CLASS << 24) |
        ((uint32_t)KERNEL_QUALIFICATION_INPUT_KIND << 16) |
        KERNEL_QUALIFICATION_INPUT_FLAGS;
    registers->INPUT_VALUE = KERNEL_QUALIFICATION_INPUT_VALUE;
    registers->INPUT_DEVICE_SEQ =
        (uint32_t)KERNEL_QUALIFICATION_INPUT_DEVICE << 16 | 1u;
    assert(kernel_platform_qualification_irq_prepare(IRQ_SRC_INPUT));
    assert(kernel_platform_device_irq_capture(IRQ_SRC_INPUT, &status));
    assert(kernel_platform_qualification_irq_consume(IRQ_SRC_INPUT,
                                                       status));
    assert(registers->INPUT_POP == INPUT_POP_BIT);

    video->IRQ_STAT = VEGA_IRQ_RASTER;
    assert(kernel_platform_qualification_irq_prepare(IRQ_SRC_VEGA));
    assert(video->IRQ_EN == VEGA_IRQ_VBLANK);
    video->IRQ_STAT = VEGA_IRQ_VBLANK;
    assert(kernel_platform_device_irq_capture(IRQ_SRC_VEGA, &status));
    assert(status == VEGA_IRQ_VBLANK);
    assert(kernel_platform_qualification_irq_consume(IRQ_SRC_VEGA,
                                                       status));
    assert(video->IRQ_EN == 0u);

    usb->CONTROL = OHCI_CONTROL_HCFS_RESET;
    usb->INTERRUPT_STATUS = OHCI_INT_RHSC;
    usb->ASTRA_DMA_POOL_BASE = OHCI_DMA_POOL_BASE;
    usb->ASTRA_DMA_POOL_SIZE = OHCI_DMA_POOL_SIZE;
    memset(hcca, 0xa5, sizeof(*hcca));
    assert(kernel_platform_qualification_irq_prepare(IRQ_SRC_USB));
    assert(usb->HCCA == OHCI_DMA_POOL_BASE);
    assert((usb->CONTROL & OHCI_CONTROL_HCFS_MASK) ==
           OHCI_CONTROL_HCFS_OPERATIONAL);
    assert(usb->COMMAND_STATUS == 0u);
    assert(usb->INTERRUPT_ENABLE == (OHCI_INT_MIE | OHCI_INT_SF));
    for (uint32_t index = 0u;
         index < sizeof(*hcca) / sizeof(uint32_t); ++index)
        assert(((const uint32_t *)(const void *)hcca)[index] == 0u);
    usb->INTERRUPT_STATUS = OHCI_INT_SF;
    usb->ASTRA_STATUS = OHCI_ASTRA_IRQ;
    assert(kernel_platform_device_irq_capture(IRQ_SRC_USB, &status));
    assert(status == (KERNEL_PLATFORM_USB_IRQ_CONTROLLER | OHCI_INT_SF));
    assert(kernel_platform_qualification_irq_consume(IRQ_SRC_USB,
                                                       status));
    assert((usb->CONTROL & OHCI_CONTROL_HCFS_MASK) ==
           OHCI_CONTROL_HCFS_SUSPEND);
    assert(usb->COMMAND_STATUS == 0u);
    assert(usb->HCCA == 0u);

    astraea->IRQ_STAT = ASTRAEA_IRQ_COPPER;
    assert(kernel_platform_qualification_irq_prepare(IRQ_SRC_ASTRAEA));
    assert(astraea->IRQ_EN == ASTRAEA_IRQ_BLIT_DONE);
    assert(astraea->BLIT_DIM == 0u);
    assert(astraea->BLIT_CTRL == (BLIT_START | BLIT_IRQ_EN));
    astraea->IRQ_STAT = ASTRAEA_IRQ_BLIT_DONE;
    assert(kernel_platform_device_irq_capture(IRQ_SRC_ASTRAEA, &status));
    assert(status == ASTRAEA_IRQ_BLIT_DONE);
    assert(kernel_platform_qualification_irq_consume(IRQ_SRC_ASTRAEA,
                                                       status));
    assert(astraea->IRQ_EN == 0u);
}

int main(void)
{
    test_one_shot_configuration_and_restart();
    test_cycle_snapshot_and_current_irq_decode();
    test_typed_identity_console_and_device_queries();
    test_sticky_bus_fault_snapshot_and_acknowledge();
    test_monotonic_nanosecond_deadline_conversion();
    test_fenced_display_transport();
    test_production_irq_qualification_controls();
    assert(interrupt_save_count != 0u);
    assert(interrupt_save_count == interrupt_restore_count);
    assert(interrupts_masked == 0u);
    puts("platform tests passed");
    return 0;
}
