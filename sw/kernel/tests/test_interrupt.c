#include "allocation.h"
#include "interrupt.h"
#include "irq.h"
#include "monitor.h"
#include "performance.h"
#include "platform.h"
#include "trace.h"
#include "worker.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t trace_event_count[KERNEL_TRACE_EVENT_MONITOR_DROP + 1u];
static uint32_t monitor_irq_services;
static uint32_t monitor_spi_irq_services;
static bool monitor_spi_binding_available = true;
static bool monitor_ready = true;
static bool staged_trace_pending;
static uint32_t trace_worker_signals;
static uint32_t device_worker_signals;
static uint32_t irq_worker_signals;
static KernelWorkerService device_worker_service;
static void *device_worker_context;
static KernelWorkerService irq_worker_service;
static void *irq_worker_context;
static KernelHandle next_thread_handle = 0x00010101u;
static uint8_t interrupts_masked;

uint16_t kernel_interrupt_save_disable(void)
{
    assert(interrupts_masked == 0u);
    interrupts_masked = 1u;
    return 0x2000u;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    assert(interrupts_masked == 1u);
    assert(status_register == 0x2000u);
    interrupts_masked = 0u;
}

static bool monitor_irq_service(uint8_t source, uint64_t timestamp,
                                void *context)
{
    (void)timestamp;
    (void)context;
    assert(source == IRQ_SRC_UART_RX);
    ++monitor_irq_services;
    return true;
}

bool kernel_monitor_ready(void)
{
    return monitor_ready;
}

bool kernel_monitor_uart_binding(KernelIrqInternalBinding *binding)
{
    if (binding == NULL || !monitor_ready)
        return false;
    binding->service = monitor_irq_service;
    binding->context = NULL;
    binding->source = IRQ_SRC_UART_RX;
    binding->trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding->ipl = 3u;
    binding->vector = KERNEL_IRQ_COMMON_VECTOR;
    return true;
}

static bool monitor_spi_irq_service(uint8_t source, uint64_t timestamp,
                                    void *context)
{
    (void)timestamp;
    (void)context;
    assert(source == IRQ_SRC_ASTRAHOST_MONITOR);
    ++monitor_spi_irq_services;
    return true;
}

bool kernel_monitor_spi_binding(KernelIrqInternalBinding *binding)
{
    if (binding == NULL || !monitor_ready ||
        !monitor_spi_binding_available)
        return false;
    binding->service = monitor_spi_irq_service;
    binding->context = NULL;
    binding->source = IRQ_SRC_ASTRAHOST_MONITOR;
    binding->trigger = KERNEL_IRQ_TRIGGER_LEVEL;
    binding->ipl = 3u;
    binding->vector = KERNEL_IRQ_COMMON_VECTOR;
    return true;
}

static bool record_trace(KernelTraceEvent event)
{
    assert(event > 0 && event <= KERNEL_TRACE_EVENT_MONITOR_DROP);
    ++trace_event_count[event];
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
    return record_trace(event);
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
    staged_trace_pending = true;
    return kernel_trace_write_at(event, flags, timestamp, argument0,
                                 argument1, argument2, argument3);
}

bool kernel_trace_staged_pending(void)
{
    return staged_trace_pending;
}

KernelWorkerStatus kernel_worker_register(uint32_t work,
                                          KernelWorkerService service,
                                          void *context)
{
    assert(service != NULL);
    if (work == KERNEL_WORKER_DEVICE_RESET) {
        device_worker_service = service;
        device_worker_context = context;
    } else {
        assert(work == KERNEL_WORKER_IRQ_DISPATCH);
        irq_worker_service = service;
        irq_worker_context = context;
    }
    return KERNEL_WORKER_OK;
}

KernelWorkerStatus kernel_worker_signal(uint32_t work)
{
    if (work == KERNEL_WORKER_TRACE_FLUSH) {
        assert(staged_trace_pending);
        staged_trace_pending = false;
        ++trace_worker_signals;
    } else if (work == KERNEL_WORKER_DEVICE_RESET) {
        assert(work == KERNEL_WORKER_DEVICE_RESET);
        assert(kernel_irq_revocation_pending());
        ++device_worker_signals;
    } else {
        VestaRegs *registers = kernel_platform_test_registers();

        assert(work == KERNEL_WORKER_IRQ_DISPATCH);
        assert(registers != NULL);
        registers->CPU_CYCLES_LO += 100u;
        ++irq_worker_signals;
    }
    return KERNEL_WORKER_OK;
}

static void clear_trace(void)
{
    for (uint32_t event = 0u;
         event <= KERNEL_TRACE_EVENT_MONITOR_DROP; ++event)
        trace_event_count[event] = 0u;
    staged_trace_pending = false;
    trace_worker_signals = 0u;
    irq_worker_signals = 0u;
}

static void clear_registers(VestaRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static void advertise_monitor_spi(VestaRegs *registers)
{
    registers->MONITOR_ID = MONITOR_ID_MAGIC;
    registers->MONITOR_VERSION = MONITOR_VERSION_1_0;
    registers->MONITOR_CAPS =
        MONITOR_CAP_RX | MONITOR_CAP_TX | MONITOR_CAP_IRQ;
}

static void clear_vega(VegaRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static void clear_astraea(AstraeaRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static void clear_ohci(OhciRegs *registers)
{
    volatile uint32_t *words = (volatile uint32_t *)registers;

    for (uint32_t index = 0u;
         index < sizeof(*registers) / sizeof(uint32_t); ++index)
        words[index] = 0u;
}

static uint32_t ohci_interrupt_sources(void)
{
    return OHCI_INT_SO | OHCI_INT_WDH | OHCI_INT_SF | OHCI_INT_RD |
           OHCI_INT_UE | OHCI_INT_FNO | OHCI_INT_RHSC | OHCI_INT_OC;
}

static void select_interrupt(VestaRegs *registers, uint8_t source)
{
    registers->IRQ_CURRENT = 0x80000000u |
        (KERNEL_IRQ_COMMON_VECTOR << 16) | ((uint32_t)source << 8) | 3u;
}

static KernelIrqEndpoint *bind_device(uint32_t owner, uint8_t source)
{
    KernelIrqBinding binding;
    KernelIrqEndpoint *endpoint = NULL;

    assert(kernel_interrupt_device_binding(source, &binding));
    assert(binding.source == source);
    assert(binding.trigger == KERNEL_IRQ_TRIGGER_LEVEL);
    assert(binding.ipl == 3u);
    assert(binding.vector == KERNEL_IRQ_COMMON_VECTOR);
    assert(kernel_irq_bind(owner, &binding, &endpoint) == KERNEL_IRQ_OK);
    assert(endpoint != NULL);
    assert(kernel_irq_arm(endpoint) == KERNEL_IRQ_OK);
    return endpoint;
}

static KernelThread *allocate_running_thread(void)
{
    KernelThread *thread;

    assert(kernel_thread_allocate(
               0u, 0x10000001u, 0u, 0x00100000u, 0x70001000u, 0u,
               KERNEL_THREAD_PRIORITY_NORMAL, &thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_attach_handle(thread, next_thread_handle) ==
           KERNEL_THREAD_OK);
    next_thread_handle += 0x00000100u;
    assert(kernel_thread_publish(thread) == KERNEL_THREAD_OK);
    assert(kernel_thread_take_next(&thread) == KERNEL_THREAD_OK);
    return thread;
}

static void release_device(KernelIrqEndpoint *endpoint)
{
    KernelWorkerServiceResult result;

    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_interrupt_schedule_device_reset());
    assert(device_worker_service != NULL);
    do {
        result = device_worker_service(KERNEL_WORKER_DEVICE_RESET_BATCH,
                                       device_worker_context);
        assert(result != KERNEL_WORKER_SERVICE_FATAL);
    } while (result == KERNEL_WORKER_SERVICE_RETRY);
    assert(kernel_irq_pool_valid());
}

static void service_scheduled_device_reset(void)
{
    assert(kernel_interrupt_schedule_device_reset());
    assert(device_worker_service != NULL);
    assert(device_worker_service(KERNEL_WORKER_DEVICE_RESET_BATCH,
                                 device_worker_context) ==
           KERNEL_WORKER_SERVICE_COMPLETE);
    assert(!kernel_irq_revocation_pending());
}

static void service_deferred_irq(void)
{
    KernelInterruptStats stats;

    assert(irq_worker_service != NULL);
    assert(irq_worker_service(KERNEL_WORKER_IRQ_DISPATCH_BATCH,
                              irq_worker_context) ==
           KERNEL_WORKER_SERVICE_COMPLETE);
    assert(kernel_interrupt_stats(&stats));
    assert(stats.pending == 0u);
    assert(stats.queued == stats.dispatched);
    assert(stats.dropped == 0u);
}

static void test_init_profiles_optional_spi(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelIrqPoolStats stats;

    assert(registers != NULL);
    clear_registers(registers);
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_thread_pool_init();
    monitor_spi_binding_available = true;
    assert(kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));
    assert(registers->IRQ_ENABLE ==
           (IRQ_BIT(IRQ_SRC_TIMER0) | IRQ_BIT(IRQ_SRC_UART_RX)));
    assert(registers->IRQ_CFG[IRQ_SRC_ASTRAHOST_MONITOR] == 0u);
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.internal_routes == 2u);

    clear_registers(registers);
    advertise_monitor_spi(registers);
    monitor_spi_binding_available = false;
    assert(!kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));
    assert(registers->IRQ_ENABLE == 0u);
    assert(registers->TIMER[0].CTRL == 0u);
    monitor_spi_binding_available = true;
}

/*
 * A build with no debug surface -- the K1/K10 qualification harness -- never
 * stands the monitor up, and the machine still has to come up. Binding the
 * monitor's sources unconditionally made kernel_interrupt_init refuse, which
 * panicked that kernel before it reached the qualification it exists to run.
 */
static void test_init_without_monitor(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelIrqPoolStats stats;

    assert(registers != NULL);
    clear_registers(registers);
    advertise_monitor_spi(registers);
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_thread_pool_init();
    monitor_ready = false;
    assert(kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));
    assert(registers->IRQ_ENABLE == IRQ_BIT(IRQ_SRC_TIMER0));
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.internal_routes == 1u);
    monitor_ready = true;
}

static void test_timer_and_quarantine_use_common_dispatch(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    KernelIrqPoolStats stats;
    uint32_t woken;

    assert(registers != NULL);
    clear_registers(registers);
    advertise_monitor_spi(registers);
    clear_trace();
    monitor_irq_services = 0u;
    monitor_spi_irq_services = 0u;
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 100u);
    kernel_thread_pool_init();
    assert(kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));
    assert(registers->IRQ_CFG[IRQ_SRC_TIMER0] ==
           (IRQ_CFG_LEVEL(4u) | IRQ_CFG_VECTOR(KERNEL_IRQ_COMMON_VECTOR) |
            IRQ_CFG_EDGE));
    assert(registers->IRQ_ENABLE ==
           (IRQ_BIT(IRQ_SRC_TIMER0) | IRQ_BIT(IRQ_SRC_UART_RX) |
            IRQ_BIT(IRQ_SRC_ASTRAHOST_MONITOR)));
    assert(registers->TIMER[0].LOAD == 62500u);
    assert(registers->TIMER[0].CTRL == (TMR_ENABLE | TMR_IRQ_EN));

    registers->CPU_CYCLES_HI = 1u;
    registers->CPU_CYCLES_LO = 2u;
    registers->TIMER[0].STATUS = TMR_EXPIRED;
    registers->IRQ_CURRENT = 0x80000000u |
        (KERNEL_IRQ_COMMON_VECTOR << 16) | (IRQ_SRC_TIMER0 << 8) | 4u;
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_TIMER);
    assert(woken == 0u);
    assert(kernel_platform_ticks() == 1u);
    assert(registers->IRQ_ACK == IRQ_BIT(IRQ_SRC_TIMER0));
    assert(registers->IRQ_ENABLE ==
           (IRQ_BIT(IRQ_SRC_TIMER0) | IRQ_BIT(IRQ_SRC_UART_RX) |
            IRQ_BIT(IRQ_SRC_ASTRAHOST_MONITOR)));
    assert(kernel_irq_pool_stats(&stats));
    assert(stats.internal_routes == 3u);
    assert(stats.internal_deliveries == 1u);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_ENTRY] == 0u);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_EXIT] == 0u);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_DELIVER] == 0u);
    assert(trace_worker_signals == 0u);

    select_interrupt(registers, IRQ_SRC_UART_RX);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    assert(woken == 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_UART_RX)) == 0u);
    assert(monitor_irq_services == 0u);
    service_deferred_irq();
    assert(monitor_irq_services == 1u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_UART_RX)) != 0u);

    select_interrupt(registers, IRQ_SRC_ASTRAHOST_MONITOR);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    assert(woken == 0u);
    assert(monitor_spi_irq_services == 0u);
    service_deferred_irq();
    assert(monitor_spi_irq_services == 1u);
    assert((registers->IRQ_ENABLE &
            IRQ_BIT(IRQ_SRC_ASTRAHOST_MONITOR)) != 0u);

    registers->IRQ_ENABLE |= IRQ_BIT(IRQ_SRC_USB);
    registers->IRQ_CURRENT = 0x80000000u |
        (KERNEL_IRQ_COMMON_VECTOR << 16) | (IRQ_SRC_USB << 8) | 3u;
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    assert(woken == 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_USB)) == 0u);
    service_deferred_irq();
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_TIMER0)) != 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_UART_RX)) != 0u);
    assert((registers->IRQ_ENABLE &
            IRQ_BIT(IRQ_SRC_ASTRAHOST_MONITOR)) != 0u);
    assert(registers->IRQ_ACK == IRQ_BIT(IRQ_SRC_USB));
    /*
     * A quarantine explains a machine that misbehaved and is kept by every
     * build. Deliveries are the per-interrupt stream, which only a debug build
     * keeps -- see KERNEL_TRACE_BUILD_LEVEL.
     */
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_QUARANTINE] == 1u);
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_DELIVER] ==
           (KERNEL_TRACE_KEEPS(KERNEL_TRACE_LEVEL_DEBUG) ? 2u : 0u));
    assert(trace_event_count[KERNEL_TRACE_EVENT_IRQ_EXIT] == 0u);
    assert(trace_worker_signals == 0u);

    registers->TIMER[0].STATUS = TMR_EXPIRED;
    registers->IRQ_CURRENT = 0x80000000u |
        (81u << 16) | (IRQ_SRC_TIMER0 << 8) | 4u;
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_FATAL);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_TIMER0)) == 0u);
    assert(kernel_interrupt_dispatch(NULL) == KERNEL_INTERRUPT_FATAL);
    assert(kernel_irq_pool_valid());
}

static void test_device_endpoints_use_common_dispatch(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    AstraeaRegs *astraea = kernel_platform_test_astraea_registers();
    VegaRegs *vega = kernel_platform_test_vega_registers();
    OhciRegs *ohci = kernel_platform_test_ohci_registers();
    KernelIrqEndpoint *endpoint;
    KernelIrqRecord record;
    KernelIrqBinding binding;
    KernelPerformanceStats performance;
    KernelThreadPoolStats thread_stats;
    KernelThreadWaitSpec wait;
    KernelThread *waiter;
    uint32_t event_flags;
    uint32_t woken;

    assert(registers != NULL && astraea != NULL && vega != NULL &&
           ohci != NULL);
    clear_registers(registers);
    advertise_monitor_spi(registers);
    clear_astraea(astraea);
    clear_vega(vega);
    clear_ohci(ohci);
    clear_trace();
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 100u);
    kernel_thread_pool_init();
    assert(kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));
    assert(!kernel_interrupt_device_binding(
        IRQ_SRC_ASTRAHOST_MONITOR, &binding));
    assert(!kernel_interrupt_device_binding(IRQ_SRC_INPUT, NULL));
    assert(kernel_interrupt_device_binding(IRQ_SRC_NETWORK, &binding));
    assert(binding.source == IRQ_SRC_NETWORK);
    assert(binding.trigger == KERNEL_IRQ_TRIGGER_LEVEL);
    assert(binding.ipl == 3u);
    assert(binding.vector == KERNEL_IRQ_COMMON_VECTOR);

    endpoint = bind_device(41u, IRQ_SRC_INPUT);
    waiter = allocate_running_thread();
    assert(kernel_irq_prepare_wait(endpoint, &wait) ==
           KERNEL_IRQ_WOULD_BLOCK);
    assert(kernel_thread_block(waiter, wait.queue, wait.sequence) ==
           KERNEL_THREAD_OK);
    assert(kernel_irq_commit_wait(endpoint) == KERNEL_IRQ_OK);
    registers->INPUT_STATUS = INPUT_EVENT_VALID | 3u;
    registers->INPUT_DEVICE_SEQ = UINT32_C(0x00010001);
    select_interrupt(registers, IRQ_SRC_INPUT);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    assert(woken == 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_INPUT)) == 0u);
    service_deferred_irq();
    assert(kernel_thread_take_next(&waiter) == KERNEL_THREAD_OK);
    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.irq_wake_to_run_samples == 1u);
    assert(thread_stats.irq_wake_to_run_max_cycles != 0u);
    assert(thread_stats.irq_wake_to_run_max_cycles <=
           KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_INPUT)) == 0u);
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status == UINT32_C(0x00010001));
    assert(event_flags == 0u);
    /* A different head is new work, not failure to drain the captured one. */
    registers->INPUT_STATUS = INPUT_EVENT_VALID | 1u;
    registers->INPUT_DEVICE_SEQ = UINT32_C(0x00010002);
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_INPUT)) != 0u);
    assert(registers->IRQ_ACK == IRQ_BIT(IRQ_SRC_INPUT));
    release_device(endpoint);

    endpoint = bind_device(42u, IRQ_SRC_STORAGE);
    registers->BLOCK_QUEUE = BLOCK_QUEUE_COMPLETION_VALID | (2u << 12);
    select_interrupt(registers, IRQ_SRC_STORAGE);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    service_deferred_irq();
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status == registers->BLOCK_QUEUE);
    registers->BLOCK_QUEUE = 0u;
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_device(endpoint);

    endpoint = bind_device(43u, IRQ_SRC_STORAGE);
    registers->BLOCK_STATE_ACK = BLOCK_STATE_ACK_BIT;
    select_interrupt(registers, IRQ_SRC_STORAGE);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    service_deferred_irq();
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status == KERNEL_PLATFORM_STORAGE_IRQ_STATE_CHANGE);
    registers->BLOCK_STATE_ACK = 0u;
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_device(endpoint);

    endpoint = bind_device(44u, IRQ_SRC_VEGA);
    vega->IRQ_EN = VEGA_IRQ_VBLANK;
    vega->IRQ_STAT = VEGA_IRQ_VBLANK;
    select_interrupt(registers, IRQ_SRC_VEGA);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    service_deferred_irq();
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status == VEGA_IRQ_VBLANK);
    vega->IRQ_STAT = 0u;
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_device(endpoint);

    endpoint = bind_device(45u, IRQ_SRC_USB);
    ohci->INTERRUPT_STATUS = OHCI_INT_WDH | OHCI_INT_RHSC;
    ohci->ASTRA_STATUS = OHCI_ASTRA_IRQ | OHCI_ASTRA_DMA_FAULT;
    select_interrupt(registers, IRQ_SRC_USB);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    service_deferred_irq();
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status ==
           (OHCI_INT_WDH | OHCI_INT_RHSC |
            KERNEL_PLATFORM_USB_IRQ_CONTROLLER |
            KERNEL_PLATFORM_USB_IRQ_DMA_FAULT));
    ohci->INTERRUPT_STATUS = 0u;
    ohci->ASTRA_STATUS = 0u;
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_device(endpoint);

    endpoint = bind_device(46u, IRQ_SRC_ASTRAEA);
    astraea->IRQ_EN = ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_DRAW_DONE;
    astraea->IRQ_STAT = ASTRAEA_IRQ_DRAW_DONE;
    select_interrupt(registers, IRQ_SRC_ASTRAEA);
    assert(kernel_interrupt_dispatch(&woken) == KERNEL_INTERRUPT_DEVICE);
    service_deferred_irq();
    assert(kernel_irq_read(endpoint, &record, &event_flags) == KERNEL_IRQ_OK);
    assert(record.status == ASTRAEA_IRQ_DRAW_DONE);
    astraea->IRQ_STAT = 0u;
    assert(kernel_irq_ack(endpoint, record.sequence) == KERNEL_IRQ_OK);
    release_device(endpoint);

    assert(kernel_performance_stats(&performance));
    assert(performance.metric[KERNEL_PERFORMANCE_HARD_IRQ].samples >= 5u);
    assert(performance.metric[KERNEL_PERFORMANCE_HARD_IRQ].maximum_cycles <=
           KERNEL_PERFORMANCE_BUDGET_HARD_IRQ);
    assert(performance.metric[KERNEL_PERFORMANCE_HARD_IRQ_WAKE].samples ==
           1u);
    assert(performance.metric[
        KERNEL_PERFORMANCE_HARD_IRQ_WAKE].maximum_cycles <=
           KERNEL_PERFORMANCE_BUDGET_HARD_IRQ_WAKE);
    assert(performance.metric[KERNEL_PERFORMANCE_IRQ_READ].samples >= 6u);
    assert(performance.metric[KERNEL_PERFORMANCE_IRQ_READ].maximum_cycles <=
           KERNEL_PERFORMANCE_BUDGET_IRQ_READ);
    assert(performance.metric[KERNEL_PERFORMANCE_IRQ_ACK].samples >= 6u);
    assert(performance.metric[KERNEL_PERFORMANCE_IRQ_ACK].maximum_cycles <=
           KERNEL_PERFORMANCE_BUDGET_IRQ_ACK);
}

static void test_device_service_death_quiesces_source(void)
{
    VestaRegs *registers = kernel_platform_test_registers();
    AstraeaRegs *astraea = kernel_platform_test_astraea_registers();
    VegaRegs *vega = kernel_platform_test_vega_registers();
    OhciRegs *ohci = kernel_platform_test_ohci_registers();
    KernelIrqEndpoint *endpoint;
    uint32_t revoked;
    uint32_t woken;

    assert(registers != NULL && astraea != NULL && vega != NULL &&
           ohci != NULL);
    clear_registers(registers);
    advertise_monitor_spi(registers);
    clear_astraea(astraea);
    clear_vega(vega);
    clear_ohci(ohci);
    clear_trace();
    kernel_allocation_test_clear_failure();
    kernel_performance_init();
    kernel_performance_test_set_cycles(100u, 100u);
    kernel_thread_pool_init();
    assert(kernel_interrupt_init(KERNEL_PLATFORM_CPU_HZ));

    endpoint = bind_device(51u, IRQ_SRC_VEGA);
    vega->IRQ_EN = VEGA_IRQ_VBLANK | VEGA_IRQ_RASTER;
    vega->IRQ_STAT = VEGA_IRQ_VBLANK;
    assert(kernel_irq_owner_died(51u, &revoked, &woken) == KERNEL_IRQ_OK);
    assert(revoked == 1u && woken == 0u);
    assert(vega->IRQ_EN == (VEGA_IRQ_VBLANK | VEGA_IRQ_RASTER));
    assert(vega->IRQ_STAT == VEGA_IRQ_VBLANK);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_VEGA)) == 0u);
    service_scheduled_device_reset();
    assert(vega->IRQ_EN == 0u);
    assert(vega->IRQ_STAT == VEGA_IRQ_VBLANK);
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_pool_valid());

    endpoint = bind_device(52u, IRQ_SRC_INPUT);
    registers->INPUT_STATUS = INPUT_EVENT_VALID | 1u;
    assert(kernel_irq_owner_died(52u, &revoked, &woken) == KERNEL_IRQ_OK);
    assert(revoked == 1u && woken == 0u);
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_INPUT)) == 0u);
    assert(registers->INPUT_STATUS == (INPUT_EVENT_VALID | 1u));
    service_scheduled_device_reset();
    assert(registers->INPUT_STATUS == (INPUT_EVENT_VALID | 1u));
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_pool_valid());

    endpoint = bind_device(53u, IRQ_SRC_USB);
    ohci->CONTROL = OHCI_CONTROL_HCFS_OPERATIONAL | OHCI_CONTROL_PLE |
                    OHCI_CONTROL_CLE | OHCI_CONTROL_BLE | 1u;
    ohci->HCCA = OHCI_DMA_POOL_BASE;
    ohci->INTERRUPT_STATUS = OHCI_INT_WDH | OHCI_INT_RHSC;
    ohci->ASTRA_STATUS = OHCI_ASTRA_IRQ | OHCI_ASTRA_DMA_FAULT;
    assert(kernel_irq_owner_died(53u, &revoked, &woken) == KERNEL_IRQ_OK);
    assert(revoked == 1u && woken == 0u);
    assert(ohci->INTERRUPT_DISABLE == 0u);
    assert(ohci->INTERRUPT_STATUS == (OHCI_INT_WDH | OHCI_INT_RHSC));
    assert(ohci->ASTRA_STATUS ==
           (OHCI_ASTRA_IRQ | OHCI_ASTRA_DMA_FAULT));
    assert(ohci->CONTROL ==
           (OHCI_CONTROL_HCFS_OPERATIONAL | OHCI_CONTROL_PLE |
            OHCI_CONTROL_CLE | OHCI_CONTROL_BLE | 1u));
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_USB)) == 0u);
    service_scheduled_device_reset();
    assert(ohci->INTERRUPT_DISABLE ==
           (ohci_interrupt_sources() | OHCI_INT_MIE));
    assert(ohci->INTERRUPT_STATUS == 0u);
    assert(ohci->INTERRUPT_ENABLE == 0u);
    assert(ohci->ASTRA_STATUS == 0u);
    assert(ohci->CONTROL == OHCI_CONTROL_HCFS_SUSPEND);
    assert(ohci->COMMAND_STATUS == 0u);
    assert(ohci->HCCA == 0u);
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_pool_valid());

    endpoint = bind_device(54u, IRQ_SRC_ASTRAEA);
    astraea->COP_CTRL = COP_ENABLE | COP_VBL_RESTART;
    astraea->IRQ_EN = ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_COPPER |
                      ASTRAEA_IRQ_DRAW_DONE;
    astraea->IRQ_STAT = ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_COPPER;
    assert(kernel_irq_owner_died(54u, &revoked, &woken) == KERNEL_IRQ_OK);
    assert(revoked == 1u && woken == 0u);
    assert(astraea->IRQ_EN ==
           (ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_COPPER |
            ASTRAEA_IRQ_DRAW_DONE));
    assert(astraea->IRQ_STAT ==
           (ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_COPPER));
    assert(astraea->COP_CTRL == (COP_ENABLE | COP_VBL_RESTART));
    assert((registers->IRQ_ENABLE & IRQ_BIT(IRQ_SRC_ASTRAEA)) == 0u);
    service_scheduled_device_reset();
    assert(astraea->IRQ_EN == 0u);
    assert(astraea->IRQ_STAT ==
           (ASTRAEA_IRQ_BLIT_DONE | ASTRAEA_IRQ_COPPER));
    assert(astraea->COP_CTRL == 0u);
    kernel_irq_handle_release(endpoint, NULL);
    assert(kernel_irq_pool_valid());
}

int main(void)
{
    test_init_profiles_optional_spi();
    test_init_without_monitor();
    test_timer_and_quarantine_use_common_dispatch();
    test_device_endpoints_use_common_dispatch();
    test_device_service_death_quiesces_source();
    puts("common interrupt tests passed");
    return 0;
}
