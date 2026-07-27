#include "handle.h"
#include "irq_latency.h"
#include "memory.h"
#include "monitor.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "thread.h"
#include "trace.h"
#include "vm.h"
#include "vesta.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static KernelWorkerService registered_service[KERNEL_WORKER_CLASS_COUNT];
static uint32_t registered_work;
static uint32_t worker_signals;
static uint32_t worker_pending;
static uint8_t uart_rx[512];
static uint32_t uart_rx_head;
static uint32_t uart_rx_tail;
static uint8_t uart_tx[2048];
static uint32_t uart_tx_length;
static bool uart_sink_failed;
static bool uart_overrun;
static uint8_t spi_rx[512];
static uint32_t spi_rx_head;
static uint32_t spi_rx_tail;
static uint8_t spi_tx[2048];
static uint32_t spi_tx_length;
static bool spi_sink_ready = true;
static uint32_t spi_errors;
static uint32_t trace_commands;
static uint32_t trace_drops;
static uint32_t cycle_count;

KernelWorkerStatus kernel_worker_register(uint32_t work,
                                          KernelWorkerService service,
                                          void *context)
{
    uint32_t index = 0u;

    assert(context == NULL);
    assert(work != 0u && (work & (work - 1u)) == 0u);
    while ((work >> index) != 1u)
        ++index;
    assert(index < KERNEL_WORKER_CLASS_COUNT);
    assert(registered_service[index] == NULL);
    registered_service[index] = service;
    registered_work |= work;
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_signal(uint32_t work)
{
    assert((registered_work & work) == work);
    worker_pending |= work;
    ++worker_signals;
    return KERNEL_WORKER_OK;
}

bool kernel_worker_stats(KernelWorkerStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->max_dispatch_latency_cycles = 77u;
    return true;
}

uint32_t kernel_platform_diagnostic_rx_status(void)
{
    uint32_t status = uart_rx_head != uart_rx_tail ? UART_RX_READY : 0u;

    if (uart_overrun)
        status |= UART_RX_FIFO_OVERRUN;
    return status;
}

bool kernel_platform_diagnostic_getc(uint8_t *value)
{
    if (value == NULL || uart_rx_head == uart_rx_tail)
        return false;
    *value = uart_rx[uart_rx_head++];
    return true;
}

void kernel_platform_diagnostic_ack_overrun(void)
{
    uart_overrun = false;
}

bool kernel_platform_monitor_spi_present(void)
{
    return true;
}

uint32_t kernel_platform_monitor_spi_status(void)
{
    uint32_t status = spi_rx_head != spi_rx_tail ?
                      MONITOR_STATUS_RX_VALID : 0u;

    if (spi_sink_ready)
        status |= MONITOR_STATUS_TX_READY;
    if ((spi_errors & MONITOR_ERROR_RX_EMPTY) != 0u)
        status |= MONITOR_STATUS_RX_EMPTY;
    if ((spi_errors & MONITOR_ERROR_TX_FULL) != 0u)
        status |= MONITOR_STATUS_TX_FULL;
    return status;
}

bool kernel_platform_monitor_spi_getc(uint8_t *value)
{
    if (value == NULL || spi_rx_head == spi_rx_tail)
        return false;
    *value = spi_rx[spi_rx_head++];
    return true;
}

bool kernel_platform_monitor_spi_try_putc(uint8_t value)
{
    if (!spi_sink_ready)
        return false;
    assert(spi_tx_length < sizeof(spi_tx));
    spi_tx[spi_tx_length++] = value;
    return true;
}

void kernel_platform_monitor_spi_ack_errors(uint32_t errors)
{
    spi_errors &= ~errors;
}

bool kernel_platform_diagnostic_putc(uint8_t value)
{
    if (uart_sink_failed)
        return false;
    assert(uart_tx_length < sizeof(uart_tx));
    uart_tx[uart_tx_length++] = value;
    return true;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    assert(cycles != NULL);
    cycles->high = 0u;
    cycles->low = cycle_count;
    cycle_count += 100u;
}

uint32_t kernel_platform_build_id(void)
{
    return 0x1234abcdu;
}

uint32_t kernel_platform_system_status(void)
{
    return SYS_ASTRA_HOST | SYS_VIDEO_READY;
}

uint32_t kernel_platform_block_state_flags(void)
{
    return BLOCK_STATE_LINK_UP | BLOCK_STATE_MEDIA_PRESENT;
}

bool kernel_platform_input_present(void)
{
    return true;
}

bool kernel_platform_bus_fault_read(KernelPlatformBusFault *fault)
{
    (void)fault;
    return false;
}

bool kernel_thread_pool_stats(KernelThreadPoolStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->created_threads = 3u;
    stats->live_threads = 2u;
    stats->ready_threads = 1u;
    stats->blocked_threads = 1u;
    return true;
}

bool kernel_process_stats(KernelSchedulerStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->current_process_id = 11u;
    stats->current_thread_id = 22u;
    stats->port_active = 1u;
    return true;
}

bool kernel_memory_stats(KernelMemoryStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->ram_base = 0x02000000u;
    stats->total_frames = 8192u;
    stats->free_frames = 0u;
    stats->emergency_total_frames = 32u;
    stats->emergency_available_frames = 32u;
    return true;
}

bool kernel_vm_stats(KernelVmStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->kernel_root_physical = 0x02004000u;
    stats->empty_root_physical = 0x02008000u;
    return true;
}

bool kernel_vm_control_state(KernelVmControlState *state)
{
    memset(state, 0, sizeof(*state));
    state->srp_limit_descriptor = 0x80000002u;
    state->srp_table_address = 0x02004000u;
    state->crp_limit_descriptor = 0x80000002u;
    state->crp_table_address = 0x0200c000u;
    state->translation_control = 0x82c08700u;
    state->cache_control = 0x00003119u;
    state->translation_enabled = 1u;
    return true;
}

bool kernel_handle_transfer_stats(KernelHandleTransferStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    return true;
}

bool kernel_irq_pool_stats(KernelIrqPoolStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->internal_routes = 2u;
    return true;
}

bool kernel_irqoff_latency_stats(KernelIrqOffLatencyStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->maximum_cycles = 88u;
    return true;
}

bool kernel_trace_header(KernelTraceHeader *header)
{
    memset(header, 0, sizeof(*header));
    header->next_sequence = 10u;
    return true;
}

bool kernel_trace_read_recent(uint32_t newest_offset,
                              KernelTraceRecord *record)
{
    assert(newest_offset == 0u);
    memset(record, 0, sizeof(*record));
    record->event = KERNEL_TRACE_EVENT_BOOT;
    return true;
}

bool kernel_trace_write(KernelTraceEvent event, uint16_t flags,
                        uint32_t argument0, uint32_t argument1,
                        uint32_t argument2, uint32_t argument3)
{
    (void)flags;
    (void)argument0;
    (void)argument1;
    (void)argument2;
    (void)argument3;
    if (event == KERNEL_TRACE_EVENT_MONITOR_COMMAND)
        ++trace_commands;
    if (event == KERNEL_TRACE_EVENT_MONITOR_DROP)
        ++trace_drops;
    return true;
}

bool kernel_trace_write_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    (void)timestamp;
    return kernel_trace_write(event, flags, argument0, argument1,
                              argument2, argument3);
}

bool kernel_trace_stage_at(KernelTraceEvent event, uint16_t flags,
                           uint64_t timestamp, uint32_t argument0,
                           uint32_t argument1, uint32_t argument2,
                           uint32_t argument3)
{
    return kernel_trace_write_at(event, flags, timestamp, argument0,
                                 argument1, argument2, argument3);
}

static void feed_uart(const char *text)
{
    while (*text != '\0') {
        assert(uart_rx_tail < sizeof(uart_rx));
        uart_rx[uart_rx_tail++] = (uint8_t)*text++;
    }
}

static void feed_spi(const char *text)
{
    while (*text != '\0') {
        assert(spi_rx_tail < sizeof(spi_rx));
        spi_rx[spi_rx_tail++] = (uint8_t)*text++;
    }
}

static void drain_uart_irq(const KernelIrqInternalBinding *binding)
{
    while (uart_rx_head != uart_rx_tail)
        assert(binding->service(IRQ_SRC_UART_RX, 0u, binding->context));
}

static void drain_spi_irq(const KernelIrqInternalBinding *binding)
{
    while (spi_rx_head != spi_rx_tail)
        assert(binding->service(IRQ_SRC_ASTRAHOST_MONITOR, 0u,
                                binding->context));
}

static void service_until_complete(KernelMonitorTransport transport,
                                   uint32_t batch)
{
    uint32_t work = transport == KERNEL_MONITOR_TRANSPORT_FTDI ?
                    KERNEL_WORKER_MONITOR_RX :
                    KERNEL_WORKER_MONITOR_SPI;

    for (uint32_t attempt = 0u; attempt < 64u; ++attempt) {
        KernelWorkerServiceResult result =
            kernel_monitor_test_service(transport, batch);

        assert(result != KERNEL_WORKER_SERVICE_FATAL);
        if (result == KERNEL_WORKER_SERVICE_COMPLETE &&
            (worker_pending & work) == 0u)
            return;
        worker_pending &= ~work;
    }
    assert(false);
}

static void test_ftdi_parser_and_zero_memory_command(void)
{
    KernelIrqInternalBinding binding;
    KernelMonitorStats stats;

    assert(kernel_monitor_uart_binding(&binding));
    assert(binding.source == IRQ_SRC_UART_RX);
    assert(binding.trigger == KERNEL_IRQ_TRIGGER_LEVEL);
    feed_uart("help\nmem\nmmu\n");
    drain_uart_irq(&binding);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
    assert(uart_tx_length != 0u);
    uart_tx[uart_tx_length] = '\0';
    assert(strstr((char *)uart_tx, "help build threads") != NULL);
    assert(strstr((char *)uart_tx, "total=8192 free=0") != NULL);
    assert(strstr((char *)uart_tx,
                  "srp=0x02004000 crp=0x0200C000 tc=0x82C08700 "
                  "cacr=0x00003119 enabled=1") != NULL);
    assert(kernel_monitor_stats(&stats));
    assert(stats.transport[KERNEL_MONITOR_TRANSPORT_FTDI].commands == 3u);
    assert(stats.transport[KERNEL_MONITOR_TRANSPORT_FTDI].command_overruns ==
           0u);
    assert(trace_commands == 3u);
}

static void test_spi_uses_same_parser_and_bounded_response(void)
{
    KernelIrqInternalBinding binding;
    uint32_t signals_before;

    assert(kernel_monitor_spi_binding(&binding));
    assert(binding.source == IRQ_SRC_ASTRAHOST_MONITOR);
    assert(binding.trigger == KERNEL_IRQ_TRIGGER_LEVEL);
    feed_spi("build\n");
    drain_spi_irq(&binding);
    spi_sink_ready = false;
    assert(kernel_monitor_test_service(
        KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI,
        32u) == KERNEL_WORKER_SERVICE_RETRY);
    assert(spi_tx_length == 0u);
    spi_sink_ready = true;
    signals_before = worker_signals;
    assert(kernel_monitor_test_service(
        KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI,
        KERNEL_WORKER_MONITOR_SPI_BATCH) == KERNEL_WORKER_SERVICE_COMPLETE);
    assert(worker_signals == signals_before + 1u);
    assert((worker_pending & KERNEL_WORKER_MONITOR_SPI) != 0u);
    worker_pending &= ~KERNEL_WORKER_MONITOR_SPI;
    service_until_complete(KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI,
                           KERNEL_WORKER_MONITOR_SPI_BATCH);
    spi_tx[spi_tx_length] = '\0';
    assert(strstr((char *)spi_tx, "kernel=0.1-test") != NULL);
    assert(strstr((char *)spi_tx, "hw=1234ABCD") != NULL);
}

static void test_overflow_and_failed_sink_are_bounded(void)
{
    KernelIrqInternalBinding binding;
    KernelMonitorStats stats;
    char oversized[96];

    assert(kernel_monitor_uart_binding(&binding));
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 2u] = '\n';
    oversized[sizeof(oversized) - 1u] = '\0';
    feed_uart(oversized);
    drain_uart_irq(&binding);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
    assert(kernel_monitor_stats(&stats));
    assert(stats.transport[KERNEL_MONITOR_TRANSPORT_FTDI].line_overflows ==
           1u);

    uart_sink_failed = true;
    feed_uart("help\n");
    drain_uart_irq(&binding);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
    uart_sink_failed = false;
    assert(kernel_monitor_stats(&stats));
    assert(stats.transport[KERNEL_MONITOR_TRANSPORT_FTDI].sink_failures ==
           1u);
    assert(stats.transport[KERNEL_MONITOR_TRANSPORT_FTDI].output_dropped !=
           0u);
    assert(trace_drops >= 2u);
}

static void test_input_ring_drop_trace_is_coalesced_per_irq(void)
{
    KernelIrqInternalBinding binding;
    KernelMonitorStats before;
    KernelMonitorStats after;
    uint32_t traces_before = trace_drops;
    char burst[161];

    assert(kernel_monitor_uart_binding(&binding));
    memset(burst, 'x', sizeof(burst) - 1u);
    burst[sizeof(burst) - 1u] = '\0';
    assert(kernel_monitor_stats(&before));
    feed_uart(burst);
    drain_uart_irq(&binding);
    assert(kernel_monitor_stats(&after));
    assert(after.transport[KERNEL_MONITOR_TRANSPORT_FTDI].input_dropped -
           before.transport[KERNEL_MONITOR_TRANSPORT_FTDI].input_dropped ==
           33u);
    assert(trace_drops - traces_before == 3u);

    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_MONITOR_INPUT_SIZE);
    feed_uart("\n");
    drain_uart_irq(&binding);
    service_until_complete(KERNEL_MONITOR_TRANSPORT_FTDI,
                           KERNEL_WORKER_MONITOR_RX_BATCH);
}

int main(void)
{
    const KernelMonitorBuildInfo build = {
        "0.1-test", "2026-07-26T00:00:00Z", "0123456789abcdef"
    };
    KernelPerformanceStats performance;

    kernel_performance_init();
    kernel_performance_test_set_cycles(1000u, 97u);
    assert(kernel_monitor_init(&build));
    assert(!kernel_monitor_init(&build));
    assert((registered_work & KERNEL_WORKER_MONITOR_RX) != 0u);
    assert((registered_work & KERNEL_WORKER_MONITOR_SPI) != 0u);
    test_ftdi_parser_and_zero_memory_command();
    test_spi_uses_same_parser_and_bounded_response();
    test_overflow_and_failed_sink_are_bounded();
    test_input_ring_drop_trace_is_coalesced_per_irq();
    assert(kernel_performance_stats(&performance));
    assert(performance.metric[KERNEL_PERFORMANCE_MONITOR_COMMAND].calls ==
           5u);
    assert(performance.metric[KERNEL_PERFORMANCE_MONITOR_COMMAND].samples ==
           5u);
    assert(performance.metric[
        KERNEL_PERFORMANCE_MONITOR_COMMAND].maximum_cycles == 97u);
    assert(performance.metric[
        KERNEL_PERFORMANCE_MONITOR_COMMAND].overruns == 0u);
    assert(worker_signals != 0u);
    puts("monitor tests passed");
    return 0;
}
