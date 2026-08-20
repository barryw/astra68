#ifndef ASTRA_KERNEL_PLATFORM_H
#define ASTRA_KERNEL_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

struct AstraDisplayFrameCompletion;

#if defined(__m68k__)
#include "mmio.h"

#include <stddef.h>
#include <vesta.h>
#endif

#define KERNEL_PLATFORM_QUANTUM_MS 5u
#define KERNEL_PLATFORM_QUANTUM_HZ \
    (1000u / KERNEL_PLATFORM_QUANTUM_MS)
#define KERNEL_PLATFORM_CPU_HZ 12500000u
#define KERNEL_PLATFORM_NS_PER_CPU_CYCLE 80u
#define KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE (1u << 31)
#define KERNEL_PLATFORM_USB_IRQ_DMA_FAULT         (1u << 31)
#define KERNEL_PLATFORM_USB_IRQ_CONTROLLER        (1u << 29)

typedef struct KernelPlatformBlockState {
    uint32_t capabilities;
    uint32_t state_flags;
    uint32_t media_generation;
    uint32_t host_generation;
    uint64_t media_sectors;
    uint16_t max_sectors;
    uint16_t reserved;
} KernelPlatformBlockState;

typedef struct KernelPlatformBlockCompletion {
    uint32_t id;
    uint16_t status;
    uint16_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} KernelPlatformBlockCompletion;

typedef struct KernelInputEvent {
    uint32_t header;
    uint32_t value;
    uint32_t timestamp_ms;
    uint32_t device_sequence;
    uint32_t host_generation;
} KernelInputEvent;

typedef struct KernelPlatformCycleCount {
    uint32_t high;
    uint32_t low;
} KernelPlatformCycleCount;

typedef struct KernelPlatformBusFault {
    uint32_t status;
    uint32_t address;
    uint32_t target;
    uint32_t cycles_high;
    uint32_t cycles_low;
    uint32_t lost;
    uint32_t timeout_cycles;
} KernelPlatformBusFault;

void kernel_platform_interrupt_init(uint32_t cpu_hz);
uint32_t kernel_platform_quantum_cycles(void);
void kernel_platform_timer_arm(uint32_t cycles);
void kernel_platform_timer_disarm(void);
uint32_t kernel_platform_ticks(void);
#if defined(__m68k__)
static inline __attribute__((always_inline)) uint32_t
kernel_platform_cpu_cycles_low(void)
{
    return kernel_mmio_read32(
        VESTA_BASE + (uint32_t)offsetof(VestaRegs, CPU_CYCLES_LO));
}
#else
uint32_t kernel_platform_cpu_cycles_low(void);
#endif
void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles);
uint64_t kernel_platform_monotonic_ns(void);
bool kernel_platform_wall_clock_ns(uint64_t *nanoseconds);
bool kernel_platform_wall_clock(uint64_t *nanoseconds, int32_t *utc_offset,
                                uint32_t *zone);
uint64_t kernel_platform_cycles_to_ns(uint64_t cycles);
bool kernel_platform_deadline_to_cycles(int64_t deadline_ns,
                                        uint64_t *deadline_cycles);
uint32_t kernel_platform_system_status(void);
uint32_t kernel_platform_build_id(void);
bool kernel_platform_post_text_present(void);
bool kernel_platform_post_text_read(uint32_t cell, uint8_t *value);
bool kernel_platform_post_text_write(uint32_t cell, uint8_t value);
bool kernel_platform_post_text_cursor(uint32_t row, uint32_t column,
                                      bool visible);
void kernel_platform_post_text_geometry(uint32_t *columns, uint32_t *rows);
uint32_t kernel_platform_display_capabilities(void);
bool kernel_platform_display_submit(uint32_t id, uint32_t operation,
                                    uint32_t source, uint32_t byte_size);
bool kernel_platform_display_collect(
    struct AstraDisplayFrameCompletion *completion);
bool kernel_platform_display_reset(void);
bool kernel_platform_bus_fault_read(KernelPlatformBusFault *fault);
void kernel_platform_bus_fault_acknowledge(void);
void kernel_platform_debug_marker(uint32_t value);
bool kernel_platform_diagnostic_putc(uint8_t value);
bool kernel_platform_diagnostic_try_putc(uint8_t value);
uint32_t kernel_platform_diagnostic_rx_status(void);
bool kernel_platform_diagnostic_getc(uint8_t *value);
void kernel_platform_diagnostic_ack_overrun(void);
bool kernel_platform_monitor_spi_present(void);
uint32_t kernel_platform_monitor_spi_status(void);
bool kernel_platform_monitor_spi_getc(uint8_t *value);
bool kernel_platform_monitor_spi_try_putc(uint8_t value);
void kernel_platform_monitor_spi_ack_errors(uint32_t errors);
bool kernel_platform_irq_current(uint8_t *source, uint8_t *vector);
bool kernel_platform_irq_configure(uint8_t source, uint8_t trigger,
                                   uint8_t ipl, uint8_t vector,
                                   void *context);
bool kernel_platform_irq_mask(uint8_t source, void *context);
bool kernel_platform_irq_enable(uint8_t source, void *context);
bool kernel_platform_irq_acknowledge(uint8_t source, void *context);
bool kernel_platform_timer_irq_service(uint8_t source, uint64_t timestamp,
                                       void *context);
bool kernel_platform_device_irq_capture(uint8_t source, uint32_t *status);
bool kernel_platform_device_irq_complete(uint8_t source,
                                         uint32_t captured_status);
bool kernel_platform_device_irq_quiesce(uint8_t source);
uint32_t kernel_platform_qualification_irq_sources(void);
bool kernel_platform_qualification_irq_prepare(uint8_t source);
bool kernel_platform_qualification_irq_consume(uint8_t source,
                                               uint32_t status);
void kernel_enable_interrupts(void);
void kernel_disable_interrupts(void);
uint16_t kernel_interrupt_save_disable(void);
void kernel_interrupt_restore(uint16_t status_register);

bool kernel_platform_block_present(void);
uint32_t kernel_platform_block_state_flags(void);
bool kernel_platform_block_state(KernelPlatformBlockState *state);
uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer);
bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion);
void kernel_platform_block_ack_state(void);
bool kernel_platform_input_present(void);
uint32_t kernel_platform_input_status(void);
bool kernel_input_peek(KernelInputEvent *event);
bool kernel_input_consume(void);
void kernel_platform_input_ack_overflow(void);
bool kernel_platform_input_quiesce(void);
bool kernel_platform_input_reset(void);

#if defined(KERNEL_PLATFORM_HOST_TEST)
#include "astraea.h"
#include "ohci.h"
#include "vesta.h"
#include "vega.h"
VestaRegs *kernel_platform_test_registers(void);
AstraeaRegs *kernel_platform_test_astraea_registers(void);
VegaRegs *kernel_platform_test_vega_registers(void);
OhciRegs *kernel_platform_test_ohci_registers(void);
OhciHcca *kernel_platform_test_ohci_hcca(void);
#endif

#endif
