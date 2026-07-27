#include "monitor.h"

#include "bytes.h"
#include "handle.h"
#include "irq_latency.h"
#include "memory.h"
#include "performance.h"
#include "platform.h"
#include "process.h"
#include "thread.h"
#include "trace.h"
#include "vm.h"
#include "vesta.h"

#include <stddef.h>

typedef struct KernelMonitorInput {
    uint8_t bytes[KERNEL_MONITOR_INPUT_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} KernelMonitorInput;

typedef struct KernelMonitorChannel {
    KernelMonitorInput input;
    char line[KERNEL_MONITOR_LINE_SIZE];
    uint8_t response[KERNEL_MONITOR_RESPONSE_SIZE];
    volatile uint16_t response_length;
    volatile uint16_t response_offset;
    uint8_t line_length;
    uint8_t line_overflow;
    uint8_t transport;
    uint8_t reserved;
    KernelMonitorTransportStats stats;
} KernelMonitorChannel;

typedef struct KernelMonitorBuilder {
    KernelMonitorChannel *channel;
    uint16_t length;
    uint8_t truncated;
} KernelMonitorBuilder;

typedef void (*KernelMonitorRenderer)(KernelMonitorBuilder *builder);

typedef struct KernelMonitorCommand {
    const char *name;
    KernelMonitorRenderer render;
} KernelMonitorCommand;

enum {
    KERNEL_MONITOR_DROP_INPUT_FULL = 1,
    KERNEL_MONITOR_DROP_LINE_FULL,
    KERNEL_MONITOR_DROP_RESPONSE_TRUNCATED,
    KERNEL_MONITOR_DROP_OUTPUT_SINK,
    KERNEL_MONITOR_DROP_TRANSPORT_ERROR
};

static KernelMonitorChannel channels[KERNEL_MONITOR_TRANSPORT_COUNT];
static KernelMonitorBuildInfo monitor_build;
static uint8_t monitor_initialized;

_Static_assert((KERNEL_MONITOR_INPUT_SIZE &
                (KERNEL_MONITOR_INPUT_SIZE - 1u)) == 0u &&
               KERNEL_MONITOR_INPUT_SIZE <= 256u,
               "monitor input ring must be a byte-indexed power of two");

static void monitor_barrier(void)
{
    __asm__ volatile ("" ::: "memory");
}

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX)
        ++*value;
}

static uint32_t text_length(const char *text)
{
    uint32_t length = 0u;

    if (text == NULL)
        return 0u;
    while (text[length] != '\0')
        ++length;
    return length;
}

static bool input_empty(const KernelMonitorInput *input)
{
    return input->head == input->tail;
}

static bool input_push(KernelMonitorChannel *channel, uint8_t value)
{
    uint8_t tail = channel->input.tail;
    uint8_t next = (uint8_t)((tail + 1u) &
                             (KERNEL_MONITOR_INPUT_SIZE - 1u));

    if (next == channel->input.head) {
        increment_saturating(&channel->stats.input_dropped);
        return false;
    }
    channel->input.bytes[tail] = value;
    monitor_barrier();
    channel->input.tail = next;
    increment_saturating(&channel->stats.input_bytes);
    return true;
}

static bool input_pop(KernelMonitorChannel *channel, uint8_t *value)
{
    uint8_t head = channel->input.head;

    if (value == NULL || head == channel->input.tail)
        return false;
    *value = channel->input.bytes[head];
    monitor_barrier();
    channel->input.head = (uint8_t)((head + 1u) &
                                    (KERNEL_MONITOR_INPUT_SIZE - 1u));
    return true;
}

static void builder_putc(KernelMonitorBuilder *builder, char value)
{
    if (builder->length >= KERNEL_MONITOR_RESPONSE_SIZE - 2u) {
        builder->truncated = 1u;
        return;
    }
    builder->channel->response[builder->length++] = (uint8_t)value;
}

static void builder_puts(KernelMonitorBuilder *builder, const char *text)
{
    if (text == NULL)
        text = "?";
    while (*text != '\0')
        builder_putc(builder, *text++);
}

static void builder_hex32(KernelMonitorBuilder *builder, uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;

        builder_putc(builder, (char)(digit < 10u ?
            '0' + digit : 'A' + digit - 10u));
    }
}

static void builder_dec32(KernelMonitorBuilder *builder, uint32_t value)
{
    static const uint32_t divisors[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u
    };
    uint8_t started = 0u;

    for (uint32_t index = 0u;
         index < sizeof(divisors) / sizeof(divisors[0]); ++index) {
        uint8_t digit = 0u;

        while (value >= divisors[index]) {
            value -= divisors[index];
            ++digit;
        }
        if (digit != 0u || started != 0u || divisors[index] == 1u) {
            builder_putc(builder, (char)('0' + digit));
            started = 1u;
        }
    }
}

static void builder_key_dec(KernelMonitorBuilder *builder,
                            const char *key, uint32_t value)
{
    builder_puts(builder, key);
    builder_dec32(builder, value);
}

static void builder_key_hex(KernelMonitorBuilder *builder,
                            const char *key, uint32_t value)
{
    builder_puts(builder, key);
    builder_hex32(builder, value);
}

static void render_unavailable(KernelMonitorBuilder *builder)
{
    builder_puts(builder, "unavailable");
}

static void render_help(KernelMonitorBuilder *builder)
{
    builder_puts(builder,
        "help build threads runq current waits mem pages maps handles "
        "ports irqs perf trace mmu faults devices");
}

static void render_build(KernelMonitorBuilder *builder)
{
    builder_puts(builder, "kernel=");
    builder_puts(builder, monitor_build.version);
    builder_puts(builder, " built=");
    builder_puts(builder, monitor_build.built_utc);
    builder_puts(builder, " git=");
    builder_puts(builder, monitor_build.git_revision);
    builder_key_hex(builder, " hw=", kernel_platform_build_id());
}

static void render_threads(KernelMonitorBuilder *builder)
{
    KernelThreadPoolStats stats;

    if (!kernel_thread_pool_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "created=", stats.created_threads);
    builder_key_dec(builder, " live=", stats.live_threads);
    builder_key_dec(builder, " dead=", stats.dead_threads);
    builder_key_dec(builder, " ready=", stats.ready_threads);
    builder_key_dec(builder, " blocked=", stats.blocked_threads);
    builder_key_dec(builder, " kstack_max=", stats.kernel_stack_max_used);
    builder_key_dec(builder, " irq_wake_max=",
                    stats.irq_wake_to_run_max_cycles);
}

static void render_runq(KernelMonitorBuilder *builder)
{
    KernelThreadPoolStats stats;

    if (!kernel_thread_pool_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_hex(builder, "bitmap=0x", stats.ready_bitmap);
    builder_key_dec(builder, " ready=", stats.ready_threads);
    builder_key_dec(builder, " blocked=", stats.blocked_threads);
}

static void render_current(KernelMonitorBuilder *builder)
{
    KernelSchedulerStats stats;

    if (!kernel_process_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "process=", stats.current_process_id);
    builder_key_dec(builder, " thread=", stats.current_thread_id);
    builder_key_dec(builder, " switches=", stats.context_switches);
    builder_key_dec(builder, " crp_switches=",
                    stats.cross_address_space_switches);
}

static void render_waits(KernelMonitorBuilder *builder)
{
    KernelThreadPoolStats stats;

    if (!kernel_thread_pool_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "blocked=", stats.blocked_threads);
    builder_key_dec(builder, " deadlines=", stats.deadline_waits);
    builder_key_dec(builder, " expired=", stats.deadline_expirations);
    builder_key_dec(builder, " cancelled=", stats.wait_cancellations);
}

static void render_mem(KernelMonitorBuilder *builder)
{
    KernelMemoryStats stats;

    if (!kernel_memory_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "total=", stats.total_frames);
    builder_key_dec(builder, " free=", stats.free_frames);
    builder_key_dec(builder, " high_water=", stats.high_water_frames);
    builder_key_dec(builder, " failures=", stats.allocation_failures);
    builder_key_dec(builder, " owners=", stats.owner_slots_used);
}

static void render_pages(KernelMonitorBuilder *builder)
{
    KernelMemoryStats stats;

    if (!kernel_memory_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_hex(builder, "base=0x", stats.ram_base);
    builder_key_dec(builder, " frames=", stats.total_frames);
    builder_key_dec(builder, " free=", stats.free_frames);
    builder_key_dec(builder, " emergency=",
                    stats.emergency_available_frames);
    builder_putc(builder, '/');
    builder_dec32(builder, stats.emergency_total_frames);
}

static void render_maps(KernelMonitorBuilder *builder)
{
    KernelVmStats stats;

    if (!kernel_vm_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "spaces=", stats.address_spaces);
    builder_key_dec(builder, " mappings=", stats.user_mappings);
    builder_key_dec(builder, " tables=", stats.user_table_pages);
    builder_key_dec(builder, " flushes=", stats.flushes);
    builder_key_dec(builder, " switches=", stats.switches);
}

static void render_handles(KernelMonitorBuilder *builder)
{
    KernelHandleTransferStats stats;

    if (!kernel_handle_transfer_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "detached=", stats.live_detached);
    builder_key_dec(builder, " reserved=", stats.reserved_detached);
    builder_key_dec(builder, " max=", stats.max_live_detached);
    builder_key_dec(builder, " exhausted=", stats.pool_exhaustions);
}

static void render_ports(KernelMonitorBuilder *builder)
{
    KernelSchedulerStats stats;

    if (!kernel_process_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "active=", stats.port_active);
    builder_key_dec(builder, " queued=", stats.port_queued_messages);
    builder_key_dec(builder, " bytes=", stats.port_queued_bytes);
    builder_key_dec(builder, " sends=", stats.port_sends);
    builder_key_dec(builder, " receives=", stats.port_receives);
}

static void render_irqs(KernelMonitorBuilder *builder)
{
    KernelIrqOffLatencyStats latency;
    KernelIrqPoolStats stats;

    if (!kernel_irq_pool_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "live=", stats.live_endpoints);
    builder_key_dec(builder, " delivered=", stats.deliveries);
    builder_key_dec(builder, " acked=", stats.acknowledgements);
    builder_key_dec(builder, " dropped=", stats.dropped_records);
    builder_key_dec(builder, " storms=", stats.storm_quarantines);
    builder_key_dec(builder, " spurious=", stats.unclaimed_interrupts);
    if (kernel_irqoff_latency_stats(&latency))
        builder_key_dec(builder, " irqoff_max=", latency.maximum_cycles);
}

static void render_perf(KernelMonitorBuilder *builder)
{
    KernelPerformanceStats stats;
    uint32_t maximum = 0u;
    uint32_t maximum_metric = 0u;
    uint32_t overruns = 0u;

    if (!kernel_performance_stats(&stats)) {
        render_unavailable(builder);
        return;
    }
    for (uint32_t metric = 0u;
         metric < KERNEL_PERFORMANCE_METRIC_COUNT; ++metric) {
        overruns += stats.metric[metric].overruns;
        if (stats.metric[metric].maximum_cycles > maximum) {
            maximum = stats.metric[metric].maximum_cycles;
            maximum_metric = metric;
        }
    }
    builder_key_dec(builder, "max_metric=", maximum_metric);
    builder_key_dec(builder, " max_cycles=", maximum);
    builder_key_dec(builder, " overruns=", overruns);
}

static void render_trace(KernelMonitorBuilder *builder)
{
    KernelTraceHeader header;
    KernelTraceRecord record;

    if (!kernel_trace_header(&header)) {
        render_unavailable(builder);
        return;
    }
    builder_key_dec(builder, "next=", header.next_sequence);
    builder_key_dec(builder, " wraps=", header.wrap_count);
    builder_key_dec(builder, " dropped=", header.dropped_count);
    if (kernel_trace_read_recent(0u, &record)) {
        builder_key_dec(builder, " event=", record.event);
        builder_key_hex(builder, " flags=0x", record.flags);
        builder_key_hex(builder, " arg0=0x", record.argument[0]);
    }
}

static void render_mmu(KernelMonitorBuilder *builder)
{
    KernelVmControlState control;
    KernelVmStats stats;

    if (!kernel_vm_stats(&stats) || !kernel_vm_control_state(&control)) {
        render_unavailable(builder);
        return;
    }
    builder_key_hex(builder, "srp=0x", control.srp_table_address);
    builder_key_hex(builder, " crp=0x", control.crp_table_address);
    builder_key_hex(builder, " tc=0x", control.translation_control);
    builder_key_hex(builder, " cacr=0x", control.cache_control);
    builder_key_dec(builder, " enabled=", control.translation_enabled);
    builder_key_dec(builder, " flushes=", stats.flushes);
    builder_key_dec(builder, " cache_inv=", stats.cache_invalidations);
}

static void render_faults(KernelMonitorBuilder *builder)
{
    KernelPlatformBusFault fault;
    KernelSchedulerStats scheduler;
    bool scheduler_valid = kernel_process_stats(&scheduler);

    builder_key_dec(builder, "user_faults=",
                    scheduler_valid ? scheduler.user_faults : 0u);
    if (!kernel_platform_bus_fault_read(&fault)) {
        builder_puts(builder, " bus=none");
        return;
    }
    builder_key_hex(builder, " bus=0x", fault.address);
    builder_key_hex(builder, " status=0x", fault.status);
    builder_key_dec(builder, " target=", fault.target);
    builder_key_dec(builder, " lost=", fault.lost);
}

static void render_devices(KernelMonitorBuilder *builder)
{
    KernelMonitorStats monitor;
    KernelWorkerStats worker;
    uint32_t system = kernel_platform_system_status();

    builder_key_hex(builder, "system=0x", system);
    builder_key_hex(builder, " block=0x",
                    kernel_platform_block_state_flags());
    builder_key_dec(builder, " input=",
                    kernel_platform_input_present() ? 1u : 0u);
    if (kernel_worker_stats(&worker))
        builder_key_dec(builder, " worker_max=",
                        worker.max_dispatch_latency_cycles);
    if (kernel_monitor_stats(&monitor)) {
        builder_key_dec(
            builder, " mon_ftdi=",
            monitor.transport[KERNEL_MONITOR_TRANSPORT_FTDI].commands);
        builder_key_dec(
            builder, " mon_spi=",
            monitor.transport[
                KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI].commands);
    }
}

static const KernelMonitorCommand commands[] = {
    {"help", render_help}, {"build", render_build},
    {"threads", render_threads}, {"runq", render_runq},
    {"current", render_current}, {"waits", render_waits},
    {"mem", render_mem}, {"pages", render_pages},
    {"maps", render_maps}, {"handles", render_handles},
    {"ports", render_ports}, {"irqs", render_irqs},
    {"perf", render_perf}, {"trace", render_trace},
    {"mmu", render_mmu}, {"faults", render_faults},
    {"devices", render_devices}
};

static bool command_matches(const KernelMonitorChannel *channel,
                            const char *name)
{
    uint32_t name_length = text_length(name);

    if (name_length != channel->line_length)
        return false;
    for (uint32_t index = 0u; index < name_length; ++index) {
        if (channel->line[index] != name[index])
            return false;
    }
    return true;
}

static void publish_response(KernelMonitorBuilder *builder)
{
    KernelMonitorChannel *channel = builder->channel;

    if (builder->truncated != 0u) {
        channel->response[builder->length++] = '!';
        increment_saturating(&channel->stats.output_truncations);
        (void)kernel_trace_write(
            KERNEL_TRACE_EVENT_MONITOR_DROP, channel->transport,
            KERNEL_MONITOR_DROP_RESPONSE_TRUNCATED,
            channel->stats.output_truncations, 0u, 0u);
    }
    if (builder->length == 0u ||
        channel->response[builder->length - 1u] != '\n')
        channel->response[builder->length++] = '\n';
    channel->response_offset = 0u;
    monitor_barrier();
    channel->response_length = builder->length;
}

static void dispatch_command(KernelMonitorChannel *channel)
{
    KernelMonitorBuilder builder = {channel, 0u, 0u};
    KernelPerformanceToken performance = kernel_performance_begin(
        KERNEL_PERFORMANCE_MONITOR_COMMAND);
    KernelPlatformCycleCount started;
    KernelPlatformCycleCount finished;
    uint32_t command_index = UINT32_MAX;
    uint32_t elapsed;

    kernel_platform_cpu_cycles(&started);
    for (uint32_t index = 0u;
         index < sizeof(commands) / sizeof(commands[0]); ++index) {
        if (command_matches(channel, commands[index].name)) {
            command_index = index;
            commands[index].render(&builder);
            break;
        }
    }
    if (command_index == UINT32_MAX) {
        builder_puts(&builder, "ERR unknown command");
        increment_saturating(&channel->stats.unknown_commands);
    }
    kernel_platform_cpu_cycles(&finished);
    elapsed = finished.low - started.low;
    if (finished.high != started.high && finished.low >= started.low)
        elapsed = UINT32_MAX;
    if (elapsed > channel->stats.max_command_cycles)
        channel->stats.max_command_cycles = elapsed;
    if (elapsed > KERNEL_MONITOR_COMMAND_BUDGET_CYCLES)
        increment_saturating(&channel->stats.command_overruns);
    kernel_performance_end(performance);
    increment_saturating(&channel->stats.commands);
    (void)kernel_trace_write(
        KERNEL_TRACE_EVENT_MONITOR_COMMAND, channel->transport,
        command_index, channel->line_length, elapsed,
        channel->stats.command_overruns);
    publish_response(&builder);
}

static void complete_line(KernelMonitorChannel *channel)
{
    increment_saturating(&channel->stats.completed_lines);
    if (channel->line_overflow != 0u) {
        KernelMonitorBuilder builder = {channel, 0u, 0u};

        builder_puts(&builder, "ERR line too long");
        publish_response(&builder);
        increment_saturating(&channel->stats.line_overflows);
        (void)kernel_trace_write(
            KERNEL_TRACE_EVENT_MONITOR_DROP, channel->transport,
            KERNEL_MONITOR_DROP_LINE_FULL,
            channel->stats.line_overflows, 0u, 0u);
    } else if (channel->line_length != 0u) {
        dispatch_command(channel);
    }
    channel->line_length = 0u;
    channel->line_overflow = 0u;
}

static void consume_input(KernelMonitorChannel *channel, uint8_t value)
{
    if (value == '\r' || value == '\n') {
        complete_line(channel);
        return;
    }
    if (value == 8u || value == 127u) {
        if (channel->line_overflow == 0u && channel->line_length != 0u)
            --channel->line_length;
        return;
    }
    if (value < 0x20u || value > 0x7eu)
        return;
    if (channel->line_overflow != 0u)
        return;
    if (channel->line_length >= KERNEL_MONITOR_LINE_SIZE - 1u) {
        channel->line_overflow = 1u;
        return;
    }
    channel->line[channel->line_length++] = (char)value;
}

static bool uart_flush(KernelMonitorChannel *channel,
                       uint32_t *operations, uint32_t batch_limit)
{
    while (channel->response_offset < channel->response_length &&
           *operations < batch_limit) {
        uint8_t value = channel->response[channel->response_offset];

        if (!kernel_platform_diagnostic_putc(value)) {
            uint32_t remaining = channel->response_length -
                                 channel->response_offset;

            if (UINT32_MAX - channel->stats.output_dropped < remaining)
                channel->stats.output_dropped = UINT32_MAX;
            else
                channel->stats.output_dropped += remaining;
            increment_saturating(&channel->stats.sink_failures);
            (void)kernel_trace_write(
                KERNEL_TRACE_EVENT_MONITOR_DROP, channel->transport,
                KERNEL_MONITOR_DROP_OUTPUT_SINK, remaining,
                channel->stats.sink_failures, 0u);
            channel->response_offset = 0u;
            channel->response_length = 0u;
            return true;
        }
        ++channel->response_offset;
        ++*operations;
        increment_saturating(&channel->stats.output_bytes);
    }
    if (channel->response_offset == channel->response_length) {
        channel->response_offset = 0u;
        channel->response_length = 0u;
    }
    return channel->response_length == 0u;
}

static bool spi_flush(KernelMonitorChannel *channel,
                      uint32_t *operations, uint32_t batch_limit)
{
    while (channel->response_offset < channel->response_length &&
           *operations < batch_limit) {
        uint8_t value = channel->response[channel->response_offset];

        if (!kernel_platform_monitor_spi_try_putc(value))
            return false;
        ++channel->response_offset;
        ++*operations;
        increment_saturating(&channel->stats.output_bytes);
    }
    if (channel->response_offset == channel->response_length) {
        channel->response_offset = 0u;
        channel->response_length = 0u;
    }
    return channel->response_length == 0u;
}

static KernelWorkerServiceResult schedule_channel_continuation(
    KernelMonitorTransport transport)
{
    uint32_t work = transport == KERNEL_MONITOR_TRANSPORT_FTDI ?
                    KERNEL_WORKER_MONITOR_RX :
                    KERNEL_WORKER_MONITOR_SPI;

    return kernel_worker_signal(work) == KERNEL_WORKER_OK ?
           KERNEL_WORKER_SERVICE_COMPLETE :
           KERNEL_WORKER_SERVICE_FATAL;
}

static KernelWorkerServiceResult service_channel(
    KernelMonitorTransport transport, uint32_t batch_limit)
{
    KernelMonitorChannel *channel;
    uint32_t operations = 0u;
    uint8_t value;

    if (!monitor_initialized || transport >= KERNEL_MONITOR_TRANSPORT_COUNT ||
        batch_limit == 0u)
        return KERNEL_WORKER_SERVICE_FATAL;
    channel = &channels[transport];
    if (transport == KERNEL_MONITOR_TRANSPORT_FTDI &&
        !uart_flush(channel, &operations, batch_limit))
        return schedule_channel_continuation(transport);
    if (transport == KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI &&
        !spi_flush(channel, &operations, batch_limit)) {
        if (operations == batch_limit)
            return schedule_channel_continuation(transport);
        return KERNEL_WORKER_SERVICE_RETRY;
    }
    if (channel->response_length != 0u)
        return KERNEL_WORKER_SERVICE_COMPLETE;

    while (operations < batch_limit && input_pop(channel, &value)) {
        ++operations;
        consume_input(channel, value);
        if (channel->response_length != 0u)
            break;
    }
    if (transport == KERNEL_MONITOR_TRANSPORT_FTDI &&
        channel->response_length != 0u &&
        !uart_flush(channel, &operations, batch_limit))
        return schedule_channel_continuation(transport);
    if (transport == KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI &&
        channel->response_length != 0u &&
        !spi_flush(channel, &operations, batch_limit)) {
        if (operations == batch_limit)
            return schedule_channel_continuation(transport);
        return KERNEL_WORKER_SERVICE_RETRY;
    }
    if (channel->response_length != 0u)
        return KERNEL_WORKER_SERVICE_COMPLETE;
    return input_empty(&channel->input) ? KERNEL_WORKER_SERVICE_COMPLETE :
           schedule_channel_continuation(transport);
}

static KernelWorkerServiceResult service_uart(uint32_t batch_limit,
                                               void *context)
{
    (void)context;
    return service_channel(KERNEL_MONITOR_TRANSPORT_FTDI, batch_limit);
}

static KernelWorkerServiceResult service_spi(uint32_t batch_limit,
                                              void *context)
{
    (void)context;
    return service_channel(KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI,
                           batch_limit);
}

bool kernel_monitor_init(const KernelMonitorBuildInfo *build_info)
{
    if (build_info == NULL || build_info->version == NULL ||
        build_info->built_utc == NULL || build_info->git_revision == NULL ||
        monitor_initialized != 0u)
        return false;
    kernel_bytes_clear(channels, sizeof(channels));
    kernel_bytes_copy(&monitor_build, build_info, sizeof(monitor_build));
    for (uint32_t transport = 0u;
         transport < KERNEL_MONITOR_TRANSPORT_COUNT; ++transport)
        channels[transport].transport = (uint8_t)transport;
    if (kernel_worker_register(KERNEL_WORKER_MONITOR_RX,
                               service_uart, NULL) != KERNEL_WORKER_OK ||
        kernel_worker_register(KERNEL_WORKER_MONITOR_SPI,
                               service_spi, NULL) != KERNEL_WORKER_OK)
        return false;
    monitor_initialized = 1u;
    return true;
}

bool kernel_monitor_uart_binding(KernelIrqInternalBinding *binding)
{
    if (!monitor_initialized || binding == NULL)
        return false;
    binding->service = kernel_monitor_uart_irq_service;
    binding->context = NULL;
    binding->source = IRQ_SRC_UART_RX;
    binding->trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding->ipl = 3u;
    binding->vector = KERNEL_IRQ_COMMON_VECTOR;
    return true;
}

bool kernel_monitor_spi_binding(KernelIrqInternalBinding *binding)
{
    if (!monitor_initialized || binding == NULL ||
        !kernel_platform_monitor_spi_present())
        return false;
    binding->service = kernel_monitor_spi_irq_service;
    binding->context = NULL;
    binding->source = IRQ_SRC_ASTRAHOST_MONITOR;
    binding->trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding->ipl = 3u;
    binding->vector = KERNEL_IRQ_COMMON_VECTOR;
    return true;
}

bool kernel_monitor_uart_irq_service(uint8_t source, uint64_t timestamp,
                                     void *context)
{
    KernelMonitorChannel *channel =
        &channels[KERNEL_MONITOR_TRANSPORT_FTDI];
    uint32_t status;
    uint32_t dropped = 0u;
    uint32_t received = 0u;
    uint8_t value;

    (void)context;
    if (!monitor_initialized || source != IRQ_SRC_UART_RX)
        return false;
    status = kernel_platform_diagnostic_rx_status();
    while (received < KERNEL_MONITOR_UART_IRQ_BATCH &&
           kernel_platform_diagnostic_getc(&value)) {
        if (!input_push(channel, value))
            ++dropped;
        ++received;
    }
    if ((status & UART_RX_FIFO_OVERRUN) != 0u) {
        kernel_platform_diagnostic_ack_overrun();
        increment_saturating(&channel->stats.input_dropped);
        ++dropped;
    }
    if (dropped != 0u)
        (void)kernel_trace_stage_at(
            KERNEL_TRACE_EVENT_MONITOR_DROP, channel->transport, timestamp,
            KERNEL_MONITOR_DROP_INPUT_FULL,
            channel->stats.input_dropped, dropped, status);
    if (received != 0u || (status & UART_RX_FIFO_OVERRUN) != 0u)
        return kernel_worker_signal(KERNEL_WORKER_MONITOR_RX) ==
               KERNEL_WORKER_OK;
    return true;
}

bool kernel_monitor_spi_irq_service(uint8_t source, uint64_t timestamp,
                                    void *context)
{
    KernelMonitorChannel *channel =
        &channels[KERNEL_MONITOR_TRANSPORT_ASTRAHOST_SPI];
    uint32_t status;
    uint32_t errors;
    uint32_t dropped = 0u;
    uint32_t received = 0u;
    uint8_t value;

    (void)context;
    if (!monitor_initialized || source != IRQ_SRC_ASTRAHOST_MONITOR)
        return false;
    status = kernel_platform_monitor_spi_status();
    while (received < KERNEL_MONITOR_SPI_IRQ_BATCH &&
           kernel_platform_monitor_spi_getc(&value)) {
        if (!input_push(channel, value))
            ++dropped;
        ++received;
    }
    errors = ((status & MONITOR_STATUS_RX_EMPTY) != 0u ?
              MONITOR_ERROR_RX_EMPTY : 0u) |
             ((status & MONITOR_STATUS_TX_FULL) != 0u ?
              MONITOR_ERROR_TX_FULL : 0u);
    if (errors != 0u) {
        kernel_platform_monitor_spi_ack_errors(errors);
        if ((errors & MONITOR_ERROR_RX_EMPTY) != 0u) {
            increment_saturating(&channel->stats.input_dropped);
            ++dropped;
        }
        if ((errors & MONITOR_ERROR_TX_FULL) != 0u)
            increment_saturating(&channel->stats.output_dropped);
    }
    if (dropped != 0u || errors != 0u)
        (void)kernel_trace_stage_at(
            KERNEL_TRACE_EVENT_MONITOR_DROP, channel->transport, timestamp,
            errors != 0u ? KERNEL_MONITOR_DROP_TRANSPORT_ERROR :
                           KERNEL_MONITOR_DROP_INPUT_FULL,
            channel->stats.input_dropped, dropped, errors);
    if (received != 0u)
        return kernel_worker_signal(KERNEL_WORKER_MONITOR_SPI) ==
               KERNEL_WORKER_OK;
    return true;
}

bool kernel_monitor_stats(KernelMonitorStats *stats)
{
    if (!monitor_initialized || stats == NULL)
        return false;
    for (uint32_t transport = 0u;
         transport < KERNEL_MONITOR_TRANSPORT_COUNT; ++transport) {
        kernel_bytes_copy(&stats->transport[transport],
                          &channels[transport].stats,
                          sizeof(stats->transport[transport]));
    }
    stats->initialized = 1u;
    stats->reserved[0] = 0u;
    stats->reserved[1] = 0u;
    stats->reserved[2] = 0u;
    return true;
}

#if defined(KERNEL_MONITOR_HOST_TEST)
KernelWorkerServiceResult kernel_monitor_test_service(
    KernelMonitorTransport transport, uint32_t batch_limit)
{
    return service_channel(transport, batch_limit);
}
#endif
