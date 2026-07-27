#ifndef ASTRA_KERNEL_MONITOR_H
#define ASTRA_KERNEL_MONITOR_H

#include "irq.h"
#include "worker.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_MONITOR_LINE_SIZE 80u
#define KERNEL_MONITOR_INPUT_SIZE 128u
#define KERNEL_MONITOR_RESPONSE_SIZE 128u
#define KERNEL_MONITOR_UART_IRQ_BATCH 16u
#define KERNEL_MONITOR_SPI_IRQ_BATCH 16u
#define KERNEL_MONITOR_COMMAND_BUDGET_CYCLES 125000u

typedef enum KernelMonitorTransport {
    KERNEL_MONITOR_TRANSPORT_FTDI = 0,
    KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI,
    KERNEL_MONITOR_TRANSPORT_COUNT
} KernelMonitorTransport;

typedef struct KernelMonitorBuildInfo {
    const char *version;
    const char *built_utc;
    const char *git_revision;
} KernelMonitorBuildInfo;

typedef struct KernelMonitorTransportStats {
    uint32_t input_bytes;
    uint32_t input_dropped;
    uint32_t completed_lines;
    uint32_t line_overflows;
    uint32_t commands;
    uint32_t unknown_commands;
    uint32_t output_bytes;
    uint32_t output_dropped;
    uint32_t output_truncations;
    uint32_t sink_failures;
    uint32_t max_command_cycles;
    uint32_t command_overruns;
} KernelMonitorTransportStats;

typedef struct KernelMonitorStats {
    KernelMonitorTransportStats transport[KERNEL_MONITOR_TRANSPORT_COUNT];
    uint8_t initialized;
    uint8_t reserved[3];
} KernelMonitorStats;

bool kernel_monitor_init(const KernelMonitorBuildInfo *build_info);
bool kernel_monitor_uart_binding(KernelIrqInternalBinding *binding);
bool kernel_monitor_spi_binding(KernelIrqInternalBinding *binding);
bool kernel_monitor_uart_irq_service(uint8_t source, uint64_t timestamp,
                                     void *context);
bool kernel_monitor_spi_irq_service(uint8_t source, uint64_t timestamp,
                                    void *context);
bool kernel_monitor_stats(KernelMonitorStats *stats);

#if defined(KERNEL_MONITOR_HOST_TEST)
KernelWorkerServiceResult kernel_monitor_test_service(
    KernelMonitorTransport transport, uint32_t batch_limit);
#endif

#endif
