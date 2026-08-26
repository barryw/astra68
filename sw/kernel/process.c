#include "process.h"

#include <astra/block.h>
#include <astra/display.h>
#include <astra/divide.h>
#include <astra/endian.h>
#include <astra/syscall.h>
#include <astra/input.h>
#include <astra/integer.h>
#include <astra/library.h>
#include <astra/process.h>
#include <astra/render_batch.h>
#include <astra/event.h>
#include <astra/status.h>

#include "area.h"
#include "block.h"
#include "bytes.h"
#include "device.h"
#include "elf.h"
#include "exception.h"
#include "generation.h"
#include "interrupt.h"
#include "memory.h"
#include "object_cache.h"
#include "performance.h"
#include "platform.h"
#include "port.h"
#include "qualification.h"
#include "ring.h"
#include "sync.h"
#include "trace.h"
#include "user_copy.h"
#include "vm.h"

#include <stddef.h>

/* Object tables live above the frame metadata; see kernel.ld. */
#if defined(__m68k__)
#define KERNEL_TABLES __attribute__((section(".tables")))
#else
#define KERNEL_TABLES
#endif

#define PROCESS_OWNER_PREFIX 0x10000000u
#define PROCESS_QUALIFICATION_CLIENT_MAX 2u
#define M68K_SSW_READ 0x0040u
#define KERNEL_SIGNAL_ALARM 14u
#define KERNEL_SIGNAL_ALARM_BIT (1u << KERNEL_SIGNAL_ALARM)

_Static_assert(KERNEL_QUALIFICATION_SHARED_BASE == KERNEL_PROCESS_DATA_BASE,
               "the qualification image's shared block is the data page");
_Static_assert(KERNEL_QUALIFICATION_OFFENDER_EXIT_STATUS ==
                   (uint32_t)ASTRA_STATUS_FAULTED,
               "the qualification image reads the fault verdict");
_Static_assert(sizeof(KernelInputEvent) == sizeof(AstraInputEvent),
               "kernel and public input event layouts differ");

typedef struct KernelProcessQualificationClient {
    uint32_t process_id;
    uint32_t authorized_sources;
    uint32_t completed_sources;
} KernelProcessQualificationClient;

/*
 * One transfer-memory buffer a process owns. The engine already allocates
 * owner-charged, physically contiguous, generation-tracked frames and already
 * revokes them on owner death; what was missing was a user-visible handle for
 * one and a mapping of its frames into the owner. This record is that bridge,
 * not a second memory mechanism.
 */
typedef struct KernelProcessDmaBuffer {
    KernelDmaHandle dma;
    uint32_t virtual_base;
    uint16_t page_count;
    uint8_t slot;
    uint8_t active;
} KernelProcessDmaBuffer;

typedef struct KernelProcess {
    KernelAddressSpace address_space;
    KernelHandleTable handles;
    KernelThreadWaitQueue death_waiters;
    uint32_t id;
    uint32_t owner;
    uint32_t generation;
    uint32_t image_size;
    uint32_t entry_base;
    uint32_t progress;
    uint32_t fault_pc;
    uint32_t fault_address;
    uint32_t exit_status;
    uint32_t terminal_result;
    uint64_t runtime_cycles;
    uint64_t started_cycles;
    uint64_t interval_deadline;
    uint64_t interval_period;
    uint32_t signal_trampoline;
    uint32_t signal_stack_top;
    uint32_t signal_pending;
    uint32_t signal_blocked;
    uint32_t signal_target_thread;
    KernelHandle self_handle;
    uint16_t fault_vector;
    uint16_t fault_status;
    /*
     * One bit per thread stack slot. Sixteen bits was the ceiling on
     * KERNEL_PROCESS_THREAD_MAX; sixty-four costs six bytes a process.
     */
    uint64_t stack_slots;
    uint16_t handle_references;
    uint8_t process_state;
    uint8_t exit_reason;
    uint8_t default_priority;
    uint8_t priority_ceiling;
    uint8_t thread_count;
    uint8_t live_threads;
    uint8_t user_stack_pages;
    uint8_t user_guard_pages;
    uint8_t supervisor_stack_pages;
    uint8_t supervisor_guard_pages;
    uint8_t handles_closed;
    uint8_t address_space_destroyed;
    uint16_t dma_pages;
    KernelProcessDmaBuffer dma_buffers[KERNEL_VM_DMA_SLOT_COUNT];
} KernelProcess;

#define LIBRARY_OWNER_PREFIX 0x30000000u
#define LIBRARY_PAGE_MAX (ASTRA_LIBRARY_IMAGE_MAX / KERNEL_PAGE_SIZE)

typedef struct KernelLibraryCacheEntry {
    KernelElfImage plan;
    AstraLibraryReference reference;
    uint32_t physical_pages[LIBRARY_PAGE_MAX];
    uint32_t owner;
    uint32_t span;
    uint8_t used;
    uint8_t reserved[3];
} KernelLibraryCacheEntry;

#if defined(__m68k__)
/*
 * A ceiling rather than an exact size. The exact number had to be edited every
 * time a field changed and said nothing about whether the result still fit;
 * this catches a record that grows by a field nobody costed, and the pool
 * assert below catches the thing that actually matters.
 */
_Static_assert(sizeof(KernelProcess) <= 4608u,
               "process record grew past its memory budget");
_Static_assert(sizeof(KernelProcess) * KERNEL_PROCESS_MAX <= 256u * 1024u,
               "process pool exceeds its share of TABLES");
#endif


static KernelProcess processes[KERNEL_PROCESS_MAX] KERNEL_TABLES;
static KernelLibraryCacheEntry library_cache[ASTRA_LIBRARY_SLOT_COUNT];
static KernelObjectCache process_cache;
static uint32_t process_cache_bitmap[
    KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PROCESS_MAX)];
static KernelSchedulerStats scheduler_stats;
/*
 * Whether the console still narrates what programs say.
 *
 * The console sink exists because it works when nothing else does: before the
 * events service is running there is no other way to see an event, and the
 * layout spec's "the event sink starts first" rests on it. Once something has
 * drained the ring, that is no longer true -- and the sink is then a second
 * timeline, painted over the terminal's own plane by a writer the terminal
 * knows nothing about.
 *
 * So the first successful drain closes it. Not a setting, not a level: the
 * drain itself is the proof that a better reader exists, and proof is a better
 * trigger than configuration. It does not reopen if that reader stops -- a
 * service that dies is reported as a service that died, which is the honest
 * account of that, rather than the console quietly resuming and looking like
 * nothing happened.
 */
static uint8_t diagnostic_console_open = 1u;
static KernelProcessMaintenanceDiagnostics maintenance_diagnostics;
static KernelProcessQualificationClient
    qualification_clients[PROCESS_QUALIFICATION_CLIENT_MAX];
static KernelThread *current_thread;
static uint64_t quantum_deadline;
static uint32_t scheduler_quantum_cycles;
static uint32_t runtime_started_cycles;
static uint8_t runtime_active;
static uint8_t scheduler_initialized;
static uint8_t scheduler_started;
static uint8_t quantum_active;
static uint8_t quantum_preempt_pending;
static uint8_t deadline_preempt_pending;
static KernelCpuContext signal_saved_context[KERNEL_THREAD_MAX];
static uint8_t signal_context_active[KERNEL_THREAD_MAX];
static uint8_t worker_active;
static uint8_t milestone_progress_ready;
static uint8_t process_pool_corrupt;
/*
 * One counter for the whole machine, so an activity id is unique across every
 * process without anybody coordinating. Monotonic within a boot; a reader
 * distinguishes boots by the boot event rather than by the id.
 */
static uint32_t next_activity;
static uint32_t initial_image_process_id;
static uint32_t initial_image_progress;
static uint8_t initial_image_exited;
static KernelDmaToken display_dma_token;
static uint32_t display_dma_owner;
static uint8_t display_dma_active;

static bool display_dma_abort_owner(uint32_t owner);

/* Whole pages, and the floor of every slot must stay unmapped. */
_Static_assert(KERNEL_THREAD_STACK_SIZE % KERNEL_PAGE_SIZE == 0u &&
                   KERNEL_THREAD_STACK_SIZE != 0u,
               "a user stack must be a whole number of VM pages");
_Static_assert(ASTRA_PROCESS_PRIORITY_MIN == KERNEL_THREAD_PRIORITY_USER_MIN &&
                   ASTRA_PROCESS_PRIORITY_NORMAL ==
                       KERNEL_THREAD_PRIORITY_NORMAL &&
                   ASTRA_PROCESS_PRIORITY_MAX == KERNEL_THREAD_PRIORITY_USER_MAX,
               "public and kernel process priorities differ");
_Static_assert(KERNEL_THREAD_STACK_STRIDE % KERNEL_PAGE_SIZE == 0u,
               "a stack reservation must be a whole number of VM pages");
_Static_assert(KERNEL_THREAD_STACK_STRIDE >=
                   KERNEL_THREAD_STACK_SIZE + KERNEL_THREAD_STACK_GUARD_SIZE,
               "each stack slot needs an unmapped guard page below it");

#define KERNEL_THREAD_STACK_PAGES \
    (KERNEL_THREAD_STACK_SIZE / KERNEL_PAGE_SIZE)

/* What the reservation allows a stack to reach, guard page excluded. */
#define KERNEL_THREAD_STACK_PAGES_MAX \
    ((KERNEL_THREAD_STACK_STRIDE - KERNEL_THREAD_STACK_GUARD_SIZE) / \
     KERNEL_PAGE_SIZE)

/*
 * uint8_t counters hold the per-process totals, and a fully grown set of
 * stacks is the worst case they have to survive -- not the committed set,
 * which is what this bounded before growth existed.
 */
_Static_assert(
    KERNEL_PROCESS_THREAD_MAX * KERNEL_THREAD_STACK_PAGES_MAX <= 255u,
    "user_stack_pages cannot count this many stack pages");
_Static_assert(KERNEL_THREAD_STACK_PAGES_MAX <= 255u,
               "a thread's committed page count must fit its uint8_t");
_Static_assert(KERNEL_PROCESS_THREAD_MAX <= 64u,
               "stack slot bitmap exceeds its storage");
_Static_assert(KERNEL_PROCESS_MAX <= KERNEL_VM_SHARED_ALIAS_MAX,
               "shared-area VM alias accounting must cover every process");
_Static_assert(ASTRA_EVENT_MANUAL_RESET ==
                   KERNEL_SYNC_EVENT_MANUAL_RESET,
               "event flag ABI mismatch");
_Static_assert(ASTRA_EVENT_INITIALLY_SIGNALED ==
                   KERNEL_SYNC_EVENT_INITIALLY_SIGNALED,
               "event flag ABI mismatch");
_Static_assert(ASTRA_RIGHT_READ == KERNEL_SYNC_RIGHT_QUERY &&
                   ASTRA_RIGHT_SIGNAL == KERNEL_SYNC_RIGHT_SIGNAL &&
                   ASTRA_RIGHT_WAIT == KERNEL_SYNC_RIGHT_WAIT &&
                   ASTRA_RIGHT_TRANSFER == KERNEL_SYNC_RIGHT_TRANSFER &&
                   ASTRA_RIGHT_ADMINISTER ==
                       KERNEL_SYNC_RIGHT_ADMINISTER,
               "synchronization-right ABI mismatch");
_Static_assert(ASTRA_RIGHT_READ == KERNEL_IRQ_RIGHT_READ &&
                   ASTRA_RIGHT_SIGNAL == KERNEL_IRQ_RIGHT_SIGNAL &&
                   ASTRA_RIGHT_WAIT == KERNEL_IRQ_RIGHT_WAIT &&
                   ASTRA_RIGHT_TRANSFER == KERNEL_IRQ_RIGHT_TRANSFER &&
                   ASTRA_RIGHT_ADMINISTER == KERNEL_IRQ_RIGHT_ADMINISTER,
               "IRQ-right ABI mismatch");
_Static_assert(sizeof(AstraIrqRecord) == sizeof(KernelIrqRecord),
               "IRQ record kernel/ABI size mismatch");
_Static_assert(ASTRA_MESSAGE_HEADER_SIZE == KERNEL_PORT_MESSAGE_SIZE_MIN &&
                   ASTRA_MESSAGE_SIZE_MAX ==
                       KERNEL_PORT_MESSAGE_SIZE_MAX &&
                   ASTRA_MESSAGE_HANDLES_MAX ==
                       KERNEL_PORT_MESSAGE_HANDLE_MAX &&
                   ASTRA_PORT_MESSAGES_MAX ==
                       KERNEL_PORT_QUEUE_MESSAGES_MAX &&
                   ASTRA_PORT_BYTES_MAX == KERNEL_PORT_QUEUE_BYTES_MAX,
               "message-port ABI limits mismatch");
_Static_assert(KERNEL_PORT_SEND_RIGHTS ==
                   (ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL |
                    ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER) &&
                   KERNEL_PORT_RECEIVE_RIGHTS ==
                       (ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT |
                        ASTRA_RIGHT_ADMINISTER),
               "message-port rights ABI mismatch");
_Static_assert(ASTRA_RIGHT_READ == KERNEL_THREAD_RIGHT_QUERY &&
                   ASTRA_RIGHT_WAIT == KERNEL_THREAD_RIGHT_WAIT &&
                   ASTRA_RIGHT_ADMINISTER ==
                       KERNEL_THREAD_RIGHT_CANCEL_WAIT,
               "thread-right ABI mismatch");
_Static_assert(ASTRA_RIGHT_READ == KERNEL_PROCESS_RIGHT_QUERY &&
                   ASTRA_RIGHT_WAIT == KERNEL_PROCESS_RIGHT_WAIT &&
                   ASTRA_RIGHT_ADMINISTER == KERNEL_PROCESS_RIGHT_PRIORITY,
               "process-right ABI mismatch");
_Static_assert(KERNEL_AREA_RIGHTS ==
                   (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                    ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER),
               "shared-area rights ABI mismatch");
_Static_assert(KERNEL_RING_PRODUCER_RIGHTS ==
                   (ASTRA_RIGHT_WRITE | ASTRA_RIGHT_SIGNAL |
                    ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER) &&
                   KERNEL_RING_CONSUMER_RIGHTS ==
                       (ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL |
                        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER),
               "bulk-ring rights ABI mismatch");

#if ASTRA_KERNEL_SOAK_SELFTEST
typedef struct KernelProcessSoakState {
    const void *image;
    uint32_t image_size;
    uint32_t entry_offset;
    uint32_t baseline_free_frames;
    uint32_t survivor_owner;
    uint32_t baseline_survivor_frames;
    uint32_t report_interval;
    uint32_t last_completed_teardowns;
    uint8_t enabled;
    uint8_t milestone_reported;
    uint8_t relaunch_pending;
    uint8_t reserved;
} KernelProcessSoakState;

static KernelProcessSoakState soak_state;
#endif

#if defined(KERNEL_PROCESS_HOST_TEST)
static uint8_t *host_physical_memory;
static uint32_t host_physical_base;
static uint32_t host_physical_size;
static KernelProcessThreadCreateFault next_thread_create_fault;

void kernel_process_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                              uint32_t size)
{
    host_physical_memory = memory;
    host_physical_base = base;
    host_physical_size = size;
}

void kernel_process_test_fail_next_thread_create(
    KernelProcessThreadCreateFault fault)
{
    next_thread_create_fault =
        fault < KERNEL_PROCESS_THREAD_CREATE_FAULT_COUNT ?
            fault : KERNEL_PROCESS_THREAD_CREATE_FAULT_NONE;
}

static bool consume_thread_create_fault(
    KernelProcessThreadCreateFault fault)
{
    if (next_thread_create_fault != fault)
        return false;
    next_thread_create_fault = KERNEL_PROCESS_THREAD_CREATE_FAULT_NONE;
    return true;
}
#endif

static KernelProcessStatus maintenance_failed(
    KernelProcessMaintenanceFailure failure, KernelProcessStatus status,
    uint32_t observed, uint32_t expected)
{
    maintenance_diagnostics.failure = (uint32_t)failure;
    maintenance_diagnostics.status = (uint32_t)status;
    maintenance_diagnostics.observed = observed;
    maintenance_diagnostics.expected = expected;
    return status;
}

/*
 * A thread entry must be even and inside the process's executable span. The
 * span is recorded per process rather than assumed to start at a fixed code
 * base, because a loaded executable places its own segments.
 */
static bool entry_within_code(const KernelProcess *process, uint32_t entry)
{
    if (process == NULL || (entry & 1u) != 0u)
        return false;
    if (entry < process->entry_base)
        return false;
    return entry - process->entry_base < process->image_size;
}

static uint8_t *physical_bytes(uint32_t physical, uint32_t size)
{
#if defined(KERNEL_PROCESS_HOST_TEST)
    if (host_physical_memory == NULL || physical < host_physical_base ||
        size > host_physical_size ||
        physical - host_physical_base > host_physical_size - size)
        return NULL;
    return &host_physical_memory[physical - host_physical_base];
#else
    (void)size;
    return (uint8_t *)(uintptr_t)physical;
#endif
}

static KernelProcess *process_for_thread(const KernelThread *thread)
{
    KernelProcess *process;

    if (thread == NULL || thread->process_slot >= KERNEL_PROCESS_MAX)
        return NULL;
    process = &processes[thread->process_slot];
    if (process->id != thread->process_id ||
        (process->process_state != KERNEL_PROCESS_CREATED &&
         process->process_state != KERNEL_PROCESS_RUNNING))
        return NULL;
    return process;
}

static bool valid_process_pointer(const KernelProcess *process)
{
    uintptr_t address = (uintptr_t)process;
    uintptr_t first = (uintptr_t)&processes[0];
    uintptr_t limit = (uintptr_t)&processes[KERNEL_PROCESS_MAX];

    return process != NULL && address >= first && address < limit &&
           (address - first) % sizeof(processes[0]) == 0u;
}

static bool valid_process_handle_object(const KernelProcess *process)
{
    return valid_process_pointer(process) && process->id != 0u &&
           process->generation != 0u &&
           process->process_state >= KERNEL_PROCESS_CREATED &&
           process->process_state <= KERNEL_PROCESS_DEAD &&
           kernel_thread_wait_queue_count(&process->death_waiters) <=
               KERNEL_THREAD_MAX;
}

static KernelProcessStatus retain_process_handle(KernelProcess *process)
{
    if (!valid_process_handle_object(process))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (process->handle_references == UINT16_MAX)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    ++process->handle_references;
    return KERNEL_PROCESS_OK;
}

static void maybe_release_process_record(KernelProcess *process)
{
    if (!valid_process_pointer(process) ||
        process->process_state != KERNEL_PROCESS_DEAD ||
        process->handle_references != 0u ||
        kernel_thread_wait_queue_count(&process->death_waiters) != 0u ||
        !kernel_object_cache_is_claimed(&process_cache, process))
        return;
    if (kernel_object_cache_release(&process_cache, process) !=
        KERNEL_OBJECT_CACHE_OK)
        process_pool_corrupt = 1u;
}

static void process_handle_release(void *object, void *context)
{
    KernelProcess *process = object;
    uint32_t woken = 0u;

    (void)context;
    if (!valid_process_handle_object(process) ||
        process->handle_references == 0u) {
        process_pool_corrupt = 1u;
        return;
    }
    --process->handle_references;
    if (process->handle_references != 0u)
        return;
    if (kernel_thread_wait_queue_count(&process->death_waiters) != 0u &&
        kernel_thread_wake_all_detail(
            &process->death_waiters, ASTRA_SYSCALL_CLOSED, 0u, true,
            &woken) != KERNEL_THREAD_OK)
        process_pool_corrupt = 1u;
    scheduler_stats.process_death_wakeups += woken;
    maybe_release_process_record(process);
}

static bool process_pool_healthy(void)
{
    return process_pool_corrupt == 0u;
}

static bool process_pool_valid(void)
{
    if (!process_pool_healthy() ||
        !kernel_allocation_valid() || !kernel_dma_valid() ||
        !kernel_block_valid() ||
        !kernel_object_cache_valid(&process_cache) ||
        !kernel_handle_transfer_pool_valid() ||
        !kernel_port_pool_valid() || !kernel_area_pool_valid() ||
        !kernel_ring_pool_valid() || !kernel_irq_pool_valid())
        return false;
    for (uint32_t owner = 0u; owner < KERNEL_PROCESS_MAX; ++owner) {
        if (!kernel_handle_table_valid(&processes[owner].handles))
            return false;
    }
    for (uint32_t slot = 0u; slot < KERNEL_PROCESS_MAX; ++slot) {
        const KernelProcess *process = &processes[slot];
        uint32_t references = 0u;
        uint32_t waiters =
            kernel_thread_wait_queue_count(&process->death_waiters);
        bool claimed = kernel_object_cache_slot_claimed(
            &process_cache, (uint16_t)slot);

        if (waiters > KERNEL_THREAD_MAX)
            return false;
        for (uint32_t owner = 0u; owner < KERNEL_PROCESS_MAX; ++owner) {
            const KernelHandleTable *table = &processes[owner].handles;

            for (uint32_t entry = 0u;
                 entry < KERNEL_HANDLE_MAX_ENTRIES; ++entry) {
                if (table->entries[entry].occupied != 0u &&
                    table->entries[entry].type == KERNEL_OBJECT_PROCESS &&
                    table->entries[entry].object == process)
                    ++references;
            }
        }
        if (references != process->handle_references)
            return false;
        if (process->process_state == KERNEL_PROCESS_UNUSED) {
            if (claimed || process->id != 0u || references != 0u ||
                waiters != 0u)
                return false;
        } else if (process->process_state == KERNEL_PROCESS_DEAD) {
            if (process->id == 0u || waiters != 0u ||
                claimed != (references != 0u))
                return false;
        } else if (process->process_state < KERNEL_PROCESS_CREATED ||
                   process->process_state > KERNEL_PROCESS_EXITING ||
                   process->id == 0u || !claimed) {
            return false;
        }
    }
    return true;
}

static uint64_t scheduler_cycles(void)
{
    KernelPlatformCycleCount cycles;

    kernel_platform_cpu_cycles(&cycles);
    return ((uint64_t)cycles.high << 32) | cycles.low;
}

static void runtime_stop(KernelProcess *process)
{
    uint32_t now;

    if (runtime_active == 0u || process == NULL)
        return;
    now = kernel_platform_cpu_cycles_low();
    process->runtime_cycles += (uint32_t)(now - runtime_started_cycles);
    runtime_active = 0u;
}

static void signal_deliver(KernelThread *thread)
{
    KernelProcess *process = process_for_thread(thread);
    uint32_t pending;
    uint32_t signal;
    uint32_t frame[2];
    uint32_t stack;

    if (process == NULL || thread->slot >= KERNEL_THREAD_MAX ||
        signal_context_active[thread->slot] != 0u ||
        process->signal_trampoline == 0u ||
        process->signal_stack_top < 8u)
        return;
    pending = process->signal_pending & ~process->signal_blocked;
    if (pending == 0u)
        return;
    signal = (uint32_t)__builtin_ctz(pending);
    stack = process->signal_stack_top - sizeof(frame);
    frame[0] = 0u;
    frame[1] = signal;
    if (kernel_copy_to_user(stack, frame, sizeof(frame)) !=
        KERNEL_USER_COPY_OK)
        return;
    kernel_bytes_copy(&signal_saved_context[thread->slot], &thread->context,
                      sizeof(thread->context));
    signal_context_active[thread->slot] = 1u;
    process->signal_pending &= ~(1u << signal);
    thread->context.usp = stack;
    thread->context.program_counter = process->signal_trampoline;
}

static KernelCpuContext *runtime_resume(KernelThread *thread)
{
    signal_deliver(thread);
    runtime_started_cycles = kernel_platform_cpu_cycles_low();
    runtime_active = 1u;
    return &thread->context;
}

static void scheduler_timer_rearm_at(uint64_t now)
{
    uint64_t target = KERNEL_THREAD_DEADLINE_NEVER;
    uint64_t deadline;
    uint64_t delta;

    if (scheduler_initialized == 0u)
        return;
    if (quantum_active != 0u)
        target = quantum_deadline;
    if (kernel_thread_next_deadline(&deadline) && deadline < target)
        target = deadline;
    if (kernel_sync_next_timer_deadline(&deadline) && deadline < target)
        target = deadline;
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        const KernelProcess *process = &processes[index];

        if ((process->process_state == KERNEL_PROCESS_CREATED ||
             process->process_state == KERNEL_PROCESS_RUNNING) &&
            process->interval_deadline != 0u &&
            process->interval_deadline < target)
            target = process->interval_deadline;
    }
    if (target == KERNEL_THREAD_DEADLINE_NEVER) {
        delta = scheduler_quantum_cycles;
    } else if (target <= now) {
        delta = 1u;
    } else {
        delta = target - now;
    }
    if (delta > UINT32_MAX)
        delta = UINT32_MAX;
    kernel_platform_timer_arm((uint32_t)delta);
    ++scheduler_stats.timer_rearms;
}

static void scheduler_timer_rearm(void)
{
    scheduler_timer_rearm_at(scheduler_cycles());
}

static void scheduler_start_quantum(void)
{
    uint64_t now = scheduler_cycles();

    quantum_deadline = UINT64_MAX - now < scheduler_quantum_cycles ?
        UINT64_MAX : now + scheduler_quantum_cycles;
    quantum_active = 1u;
    quantum_preempt_pending = 0u;
    deadline_preempt_pending = 0u;
    scheduler_timer_rearm_at(now);
}

static void scheduler_mark_quantum_expired(KernelThread *thread)
{
    if (quantum_preempt_pending != 0u)
        return;
    quantum_active = 0u;
    quantum_preempt_pending = 1u;
    ++thread->timer_ticks;
    ++scheduler_stats.quantum_expirations;
}

static KernelProcessStatus scheduler_expire_due(
    uint64_t now, uint32_t *expired_threads, uint8_t *highest_priority)
{
    uint64_t deadline;
    uint32_t expired = 0u;
    uint32_t expired_timers = 0u;
    uint32_t timer_wakeups = 0u;
    uint8_t highest = 0u;

    if (kernel_thread_next_deadline(&deadline) && deadline <= now) {
        if (kernel_thread_expire_deadlines(now, &expired, &highest) !=
            KERNEL_THREAD_OK)
            return KERNEL_PROCESS_CORRUPT;
        scheduler_stats.deadline_expirations += expired;
    }
    if (kernel_sync_next_timer_deadline(&deadline) && deadline <= now) {
        if (kernel_sync_expire_timers(now, &expired_timers,
                                      &timer_wakeups) != KERNEL_SYNC_OK)
            return KERNEL_PROCESS_CORRUPT;
        if (timer_wakeups != 0u) {
            uint8_t ready_priority;

            if (!kernel_thread_highest_ready_priority(&ready_priority))
                return KERNEL_PROCESS_CORRUPT;
            if (expired == 0u || ready_priority > highest)
                highest = ready_priority;
        }
        scheduler_stats.timer_expirations += expired_timers;
        scheduler_stats.sync_wakeups += timer_wakeups;
        expired += timer_wakeups;
    }
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        KernelProcess *process = &processes[index];
        KernelThread *target = NULL;

        if ((process->process_state != KERNEL_PROCESS_CREATED &&
             process->process_state != KERNEL_PROCESS_RUNNING) ||
            process->interval_deadline == 0u ||
            process->interval_deadline > now)
            continue;
        process->signal_pending |= KERNEL_SIGNAL_ALARM_BIT;
        if (process->interval_period == 0u) {
            process->interval_deadline = 0u;
        } else {
            uint64_t remainder;
            uint64_t increment;

            (void)astra_divide_u64_u64(
                now - process->interval_deadline,
                process->interval_period, &remainder);
            increment = process->interval_period - remainder;
            if (UINT64_MAX - now < increment)
                process->interval_deadline = 0u;
            else
                process->interval_deadline = now + increment;
        }
        for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            KernelThread *candidate = kernel_thread_at(slot);

            if (candidate != NULL && candidate->process_id == process->id &&
                candidate->state != KERNEL_THREAD_DEAD &&
                (candidate->id == process->signal_target_thread ||
                 target == NULL)) {
                target = candidate;
                if (candidate->id == process->signal_target_thread)
                    break;
            }
        }
        if (target != NULL && target->state == KERNEL_THREAD_BLOCKED) {
            KernelThreadStatus cancel = kernel_thread_cancel_wait(
                target, ASTRA_SYSCALL_CANCELLED);

            if (cancel != KERNEL_THREAD_OK)
                return KERNEL_PROCESS_CORRUPT;
            ++expired;
            if (target->effective_priority > highest)
                highest = target->effective_priority;
        }
    }
    if (expired_threads != NULL)
        *expired_threads = expired;
    if (highest_priority != NULL)
        *highest_priority = highest;
    return KERNEL_PROCESS_OK;
}

static __attribute__((noinline))
KernelProcessStatus activate_fast(KernelThread *next,
                                  KernelCpuContext **next_context)
{
    KernelProcess *next_process;
    KernelThread *previous = current_thread;

    if (next == NULL || next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    next_process = process_for_thread(next);
    if (next_process == NULL || next->state != KERNEL_THREAD_RUNNING ||
        !kernel_context_valid(&next->context))
        return KERNEL_PROCESS_INVALID_STATE;
    if (previous != next) {
        if (kernel_vm_switch(&next_process->address_space) != KERNEL_VM_OK)
            return KERNEL_PROCESS_CORRUPT;
        ++scheduler_stats.context_switches;
        if (previous != NULL) {
            if (previous->process_slot == next->process_slot)
                ++scheduler_stats.same_address_space_switches;
            else
                ++scheduler_stats.cross_address_space_switches;
        }
    }
    next_process->process_state = KERNEL_PROCESS_RUNNING;
    ++next->run_count;
    current_thread = next;
    scheduler_stats.current_process_id = next_process->id;
    scheduler_stats.current_thread_id = next->id;
    if (worker_active == 0u)
        scheduler_start_quantum();
    else
        scheduler_timer_rearm();
    *next_context = runtime_resume(next);
    return KERNEL_PROCESS_OK;
}

static __attribute__((noinline))
KernelProcessStatus activate_profiled(KernelThread *next,
                                      KernelCpuContext **next_context)
{
    KernelPerformanceToken performance;
    KernelPerformanceMetric metric;
    KernelProcessStatus status;
    KernelThread *previous = current_thread;

    if (previous == NULL || previous == next || next == NULL)
        return activate_fast(next, next_context);
    metric = previous->process_slot == next->process_slot ?
        KERNEL_PERFORMANCE_SAME_CRP_SWITCH :
        KERNEL_PERFORMANCE_CROSS_CRP_SWITCH;
    performance = kernel_performance_begin_sampled(metric);
    status = activate_fast(next, next_context);
    kernel_performance_end(performance);
    return status;
}

static KernelProcessStatus activate(KernelThread *next,
                                    KernelCpuContext **next_context)
{
    if (kernel_performance_sampling_enabled == 0u)
        return activate_fast(next, next_context);
    return activate_profiled(next, next_context);
}

static KernelProcessStatus capture_current(const uint32_t *registers,
                                           uint32_t user_stack,
                                           const void *raw_frame,
                                           KernelProcess **process,
                                           KernelThread **thread)
{
    KernelProcess *current;

    if (current_thread == NULL || process == NULL || thread == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    current = process_for_thread(current_thread);
    if (current == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    if (current->process_state != KERNEL_PROCESS_RUNNING ||
        current_thread->state != KERNEL_THREAD_RUNNING)
        return KERNEL_PROCESS_INVALID_STATE;
#if defined(KERNEL_PROCESS_HOST_TEST)
    uint32_t kernel_stack_pointer =
        current_thread->kernel_stack_top - 64u;
#else
    uint32_t kernel_stack_pointer = (uint32_t)(uintptr_t)registers;
#endif
    if (kernel_thread_note_kernel_entry(current_thread,
                                        kernel_stack_pointer) !=
        KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_context_capture(&current_thread->context, registers, user_stack,
                               raw_frame) != KERNEL_CONTEXT_OK)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    runtime_stop(current);
    *process = current;
    *thread = current_thread;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus schedule_next(KernelCpuContext **next_context)
{
    KernelThread *next = NULL;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    switch (kernel_thread_take_next(&next)) {
    case KERNEL_THREAD_OK:
        return activate(next, next_context);
    case KERNEL_THREAD_NO_RUNNABLE:
        if (kernel_vm_switch_to_empty() != KERNEL_VM_OK)
            return KERNEL_PROCESS_CORRUPT;
        current_thread = NULL;
        runtime_active = 0u;
        quantum_active = 0u;
        quantum_preempt_pending = 0u;
        deadline_preempt_pending = 0u;
        scheduler_stats.current_process_id = 0u;
        scheduler_stats.current_thread_id = 0u;
        *next_context = NULL;
        scheduler_timer_rearm();
        return KERNEL_PROCESS_NO_RUNNABLE;
    default:
        return KERNEL_PROCESS_CORRUPT;
    }
}

static bool ready_thread_outranks(const KernelThread *thread)
{
    uint8_t priority;

    return thread != NULL &&
           kernel_thread_highest_ready_priority(&priority) &&
           priority > thread->effective_priority;
}

static KernelProcessStatus schedule_pending(KernelCpuContext **next_context)
{
    KernelThread *previous = current_thread;
    KernelThread *next = NULL;
    bool quantum_preemption;
    bool deadline_preemption;

    if (next_context == NULL || process_for_thread(previous) == NULL ||
        previous->state != KERNEL_THREAD_RUNNING)
        return KERNEL_PROCESS_INVALID_STATE;
    if (quantum_preempt_pending == 0u &&
        deadline_preempt_pending == 0u &&
        !ready_thread_outranks(previous)) {
        *next_context = runtime_resume(previous);
        return KERNEL_PROCESS_OK;
    }

    quantum_preemption = quantum_preempt_pending != 0u;
    deadline_preemption = deadline_preempt_pending != 0u;
    if (kernel_thread_make_ready(previous) != KERNEL_THREAD_OK ||
        kernel_thread_take_next(&next) != KERNEL_THREAD_OK || next == NULL)
        return KERNEL_PROCESS_CORRUPT;
    if (next != previous) {
        if (quantum_preemption)
            ++scheduler_stats.timer_preemptions;
        if (deadline_preemption)
            ++scheduler_stats.deadline_preemptions;
        if (next->effective_priority > previous->effective_priority)
            ++scheduler_stats.priority_preemptions;
    }
    return activate(next, next_context);
}

static KernelProcessStatus finish_reap(KernelProcess *process)
{
    uint32_t released_buffers = 0u;
    uint32_t deferred_buffers = 0u;
    uint32_t released_frames = 0u;

    if (process->handles_closed == 0u) {
        uint32_t revoked_leases = 0u;

        KernelDeviceStatus device_status =
            kernel_device_owner_died(process->id, &revoked_leases);

        if (device_status != KERNEL_DEVICE_OK &&
            device_status != KERNEL_DEVICE_QUIESCE_FAILED &&
            device_status != KERNEL_DEVICE_RESET_FAILED)
            return KERNEL_PROCESS_CORRUPT;
        if (!display_dma_abort_owner(process->owner))
            return KERNEL_PROCESS_CORRUPT;
        (void)revoked_leases;
        (void)kernel_handle_close_all(&process->handles);
        process->self_handle = KERNEL_HANDLE_INVALID;
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_sync_pool_valid() || !kernel_port_pool_valid() ||
            !kernel_area_pool_valid() || !kernel_ring_pool_valid() ||
            !kernel_handle_transfer_pool_valid() ||
            !kernel_irq_pool_valid() || !kernel_device_pool_valid() ||
            !process_pool_valid())
#else
        if (!kernel_sync_pool_healthy() || !kernel_port_pool_healthy() ||
            !kernel_area_pool_healthy() || !kernel_ring_pool_healthy() ||
            !kernel_irq_pool_healthy() ||
            !kernel_handle_transfer_pool_healthy() ||
            !kernel_device_pool_valid() ||
            !process_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        process->handles_closed = 1u;
    }
    if (kernel_block_revoke_owner(process->owner, &released_buffers,
                                  &deferred_buffers) != KERNEL_BLOCK_OK)
        return KERNEL_PROCESS_CORRUPT;
    (void)released_buffers;
    if (process->address_space_destroyed == 0u) {
        if (kernel_vm_destroy_address_space(&process->address_space) !=
            KERNEL_VM_OK)
            return KERNEL_PROCESS_CORRUPT;
        process->address_space_destroyed = 1u;
    }
    if (deferred_buffers != 0u)
        return KERNEL_PROCESS_DEFERRED;
    switch (kernel_memory_release_owner(process->owner, &released_frames)) {
    case KERNEL_MEMORY_OK:
        break;
    case KERNEL_MEMORY_BUSY:
        return KERNEL_PROCESS_DEFERRED;
    default:
        return KERNEL_PROCESS_CORRUPT;
    }
    if (!kernel_memory_unprotect_owner(process->owner))
        return KERNEL_PROCESS_CORRUPT;
    scheduler_stats.forced_frame_releases += released_frames;
    if (kernel_thread_release_process(
            (uint16_t)(process - processes)) != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    process->process_state = KERNEL_PROCESS_DEAD;
    process->live_threads = 0u;
    process->thread_count = 0u;
    process->stack_slots = 0u;
    process->user_stack_pages = 0u;
    process->user_guard_pages = 0u;
    process->supervisor_stack_pages = 0u;
    process->supervisor_guard_pages = 0u;
    if (kernel_thread_wait_queue_count(&process->death_waiters) != 0u)
        return KERNEL_PROCESS_CORRUPT;
    if (process->exit_reason == KERNEL_PROCESS_EXIT_USER_FAULT)
        ++scheduler_stats.completed_user_fault_teardowns;
    ++scheduler_stats.dead_processes;
    ++scheduler_stats.completed_teardowns;
    maybe_release_process_record(process);
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus finish_thread_reaps(void)
{
    uint64_t pending = kernel_thread_reap_slots();

    for (uint16_t slot = 0u; pending != 0u; ++slot, pending >>= 1u) {
        KernelThread *thread = kernel_thread_at(slot);
        KernelProcess *process;
        uint64_t stack_bit;
        bool released = false;
        KernelPerformanceToken performance;

        if ((pending & 1u) == 0u)
            continue;
        if (thread == NULL || thread->state != KERNEL_THREAD_DEAD ||
            thread->reap_pending == 0u)
            return KERNEL_PROCESS_CORRUPT;
        if (thread->process_slot >= KERNEL_PROCESS_MAX)
            return KERNEL_PROCESS_CORRUPT;
        process = &processes[thread->process_slot];
        if (process->process_state == KERNEL_PROCESS_EXITING)
            continue;
        if (process->process_state != KERNEL_PROCESS_CREATED &&
            process->process_state != KERNEL_PROCESS_RUNNING)
            return KERNEL_PROCESS_CORRUPT;
        if (thread->stack_slot >= KERNEL_PROCESS_THREAD_MAX)
            return KERNEL_PROCESS_CORRUPT;
        performance = kernel_performance_begin(
            KERNEL_PERFORMANCE_THREAD_REAP);
        stack_bit = astra_u64_bit(thread->stack_slot);
        if (thread->stack_released == 0u) {
            /*
             * What this thread grew to, not what every thread starts with.
             * The pages are contiguous and end at the top of the slot, so the
             * count and the base describe the same range from either end.
             */
            uint32_t committed = thread->stack_pages;

            if ((process->stack_slots & stack_bit) == 0u ||
                committed == 0u ||
                committed > KERNEL_THREAD_STACK_PAGES_MAX ||
                thread->user_stack_base +
                        (committed * KERNEL_PAGE_SIZE) !=
                    thread->user_stack_top ||
                process->user_stack_pages < committed ||
                process->user_guard_pages == 0u) {
                kernel_performance_end(performance);
                return KERNEL_PROCESS_CORRUPT;
            }
            for (uint32_t page = 0u; page < committed; ++page) {
                if (kernel_vm_unmap_page(
                        &process->address_space,
                        thread->user_stack_base +
                            (page * KERNEL_PAGE_SIZE)) != KERNEL_VM_OK) {
                    kernel_performance_end(performance);
                    return KERNEL_PROCESS_CORRUPT;
                }
            }
            process->stack_slots &= ~stack_bit;
            process->user_stack_pages =
                (uint8_t)(process->user_stack_pages - committed);
            --process->user_guard_pages;
        }
        if (kernel_thread_finish_reap(thread, &released) !=
            KERNEL_THREAD_OK) {
            kernel_performance_end(performance);
            return KERNEL_PROCESS_CORRUPT;
        }
        if (!released) {
            kernel_performance_end(performance);
            continue;
        }
        if (process->thread_count == 0u ||
            process->supervisor_stack_pages <
                KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE ||
            process->supervisor_guard_pages <
                KERNEL_THREAD_SUPERVISOR_GUARD_SIZE / KERNEL_PAGE_SIZE) {
            kernel_performance_end(performance);
            return KERNEL_PROCESS_CORRUPT;
        }
        --process->thread_count;
        process->supervisor_stack_pages -=
            KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE;
        process->supervisor_guard_pages -=
            KERNEL_THREAD_SUPERVISOR_GUARD_SIZE / KERNEL_PAGE_SIZE;
        ++scheduler_stats.completed_thread_reaps;
        kernel_performance_end(performance);
    }
    return KERNEL_PROCESS_OK;
}

static void check_milestone(void)
{
    KernelSchedulerStats stats;
    uint32_t measured_stack_use;
    bool survivor_ready = false;

    if (scheduler_stats.milestone_complete != 0u)
        return;
    if (milestone_progress_ready == 0u)
        return;
    if (!kernel_process_stats(&stats) ||
        stats.created_processes < 2u ||
        stats.created_threads < 3u ||
        stats.timer_preemptions == 0u ||
        stats.same_address_space_switches == 0u ||
        stats.cross_address_space_switches == 0u ||
        stats.wait_blocks < 2u ||
        stats.sync_wakeups == 0u ||
        stats.wake_preemptions == 0u ||
        stats.quantum_expirations == 0u ||
        stats.deadline_expirations == 0u ||
        stats.deadline_preemptions == 0u ||
        stats.blocked_threads == 0u ||
        stats.deadline_max_depth == 0u ||
        stats.sync_cancellations == 0u ||
#if !defined(KERNEL_PROCESS_HOST_TEST)
        stats.wait_set_calls < 4u ||
        stats.wait_set_blocks < 2u ||
        stats.wait_set_wakeups < 2u ||
        stats.wait_set_registration_max < 2u ||
        stats.wait_set_max_members < 2u ||
        stats.timer_created == 0u ||
        stats.timer_arms == 0u ||
        stats.timer_expirations == 0u ||
        stats.process_death_waits == 0u ||
        stats.port_created == 0u ||
        stats.port_sends < 3u ||
        stats.port_receives < 3u ||
        stats.port_send_would_block == 0u ||
        stats.port_receive_buffer_too_small == 0u ||
        stats.port_active != 0u ||
        stats.port_queued_messages != 0u ||
        stats.port_queued_bytes != 0u ||
        stats.port_queued_handles != 0u ||
        stats.handle_transfers < 2u ||
        stats.handle_transfer_imports < 2u ||
        stats.handle_transfer_import_rollbacks == 0u ||
        stats.handle_transfer_live_detached != 0u ||
        stats.area_created == 0u ||
        stats.area_active != 0u ||
        stats.area_committed_pages != 0u ||
        stats.area_mappings != 0u ||
        stats.area_map_operations == 0u ||
        stats.area_unmap_operations == 0u ||
        stats.ring_created == 0u ||
        stats.ring_active != 0u ||
        stats.ring_producer_notifications == 0u ||
        stats.ring_consumer_notifications == 0u ||
        stats.ring_wait_wakeups == 0u ||
        stats.ring_peer_closures == 0u ||
#endif
        stats.ring_copy_cycle_overruns != 0u ||
        stats.sync_created_events < 3u ||
        stats.sync_created_semaphores == 0u ||
        stats.sync_blocked_waits < 5u ||
        stats.sync_signal_calls < 2u ||
        stats.sync_close_wakeups == 0u ||
        stats.sync_owner_deaths == 0u ||
        stats.kernel_stack_entries == 0u ||
        stats.kernel_stack_max_used == 0u ||
        !kernel_thread_stacks_valid() ||
        stats.user_faults == 0u ||
        stats.completed_user_fault_teardowns == 0u)
        return;
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        const KernelProcess *process = &processes[index];
        bool all_started = true;
        uint32_t live_threads = 0u;

        for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            const KernelThread *thread = kernel_thread_at(slot);

            if (thread == NULL || thread->process_slot != index ||
                thread->state == KERNEL_THREAD_DEAD)
                continue;
            ++live_threads;
            if (thread->run_count == 0u)
                all_started = false;
        }

        if ((process->process_state == KERNEL_PROCESS_CREATED ||
             process->process_state == KERNEL_PROCESS_RUNNING) &&
            kernel_thread_process_runnable((uint16_t)index) &&
            live_threads >= 2u && all_started &&
            process->progress >= KERNEL_PROCESS_PROGRESS_GOAL &&
            kernel_thread_process_run_count((uint16_t)index) != 0u)
            survivor_ready = true;
    }
    if (!survivor_ready)
        return;
    if (!kernel_thread_measure_stacks(&measured_stack_use) ||
        measured_stack_use == 0u)
        return;
    scheduler_stats.milestone_complete = 1u;
    stats.milestone_complete = 1u;
    kernel_process_milestone_reached(&stats);
}

static KernelProcessStatus wake_process_death(KernelProcess *process)
{
    uint32_t woken = 0u;

    if (!valid_process_handle_object(process) ||
        process->process_state != KERNEL_PROCESS_EXITING ||
        process->exit_reason == KERNEL_PROCESS_EXIT_NONE)
        return KERNEL_PROCESS_INVALID_STATE;
    /*
     * The status is reported as it stands, never zeroed. A waiter that reads
     * only the status used to see success for a process that had crashed.
     */
    if (kernel_thread_wake_all_detail(
            &process->death_waiters, process->terminal_result,
            process->exit_status, true, &woken) != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    scheduler_stats.process_death_wakeups += woken;
    return KERNEL_PROCESS_OK;
}

/*
 * Unmapping and closing must both happen exactly once, whether the service
 * closed the handle or died holding it. The engine defers reclaim while a
 * transfer is still in flight, so a buffer closed under the device is
 * released when the device is done with it, not while it is writing.
 */
static void dma_buffer_release(void *object, void *context)
{
    KernelProcessDmaBuffer *buffer = object;
    KernelProcess *process = context;

    if (buffer == NULL || process == NULL || buffer->active == 0u)
        return;
    for (uint32_t page = 0u; page < buffer->page_count; ++page) {
        (void)kernel_vm_unmap_page(
            &process->address_space,
            buffer->virtual_base + (page * KERNEL_PAGE_SIZE));
    }
    (void)kernel_dma_close(buffer->dma, process->owner);
    if (process->dma_pages >= buffer->page_count)
        process->dma_pages -= buffer->page_count;
    else
        process->dma_pages = 0u;
    buffer->dma = KERNEL_DMA_HANDLE_INVALID;
    buffer->virtual_base = 0u;
    buffer->page_count = 0u;
    buffer->active = 0u;
}

static KernelProcessStatus create_dma_buffer(KernelProcess *process,
                                             uint32_t byte_size,
                                             AstraDmaBufferInfo *info)
{
    KernelProcessDmaBuffer *buffer = NULL;
    KernelDmaBufferInfo engine_info;
    KernelDmaHandle dma = KERNEL_DMA_HANDLE_INVALID;
    KernelHandle handle = KERNEL_HANDLE_INVALID;
    KernelVmStatus vm_status = KERNEL_VM_OK;
    uint32_t page_count;
    uint32_t slot;
    uint32_t mapped = 0u;

    if (process == NULL || info == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (byte_size == 0u || byte_size > KERNEL_VM_DMA_SLOT_SIZE)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    page_count = (byte_size + KERNEL_PAGE_SIZE - 1u) / KERNEL_PAGE_SIZE;
    /* Hard ceilings: exceeding one is a rejection, never a stall. */
    if (page_count == 0u ||
        (uint32_t)process->dma_pages + page_count >
            ASTRA_DMA_MAX_PAGES_PER_SERVICE)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    for (slot = 0u; slot < KERNEL_VM_DMA_SLOT_COUNT; ++slot) {
        if (process->dma_buffers[slot].active == 0u) {
            buffer = &process->dma_buffers[slot];
            break;
        }
    }
    if (buffer == NULL)
        return KERNEL_PROCESS_RESOURCE_LIMIT;

    {
        KernelDmaStatus dma_status = kernel_dma_create(
            process->owner, page_count * KERNEL_PAGE_SIZE, 1u, &dma);

        if (dma_status == KERNEL_DMA_NO_RESOURCES)
            return KERNEL_PROCESS_RESOURCE_LIMIT;
        if (dma_status == KERNEL_DMA_OUT_OF_MEMORY)
            return KERNEL_PROCESS_OUT_OF_MEMORY;
        if (dma_status == KERNEL_DMA_INVALID_ARGUMENT)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        if (dma_status != KERNEL_DMA_OK ||
            dma == KERNEL_DMA_HANDLE_INVALID)
            return KERNEL_PROCESS_CORRUPT;
    }
    if (kernel_dma_buffer_info(dma, process->owner, &engine_info) !=
            KERNEL_DMA_OK ||
        engine_info.frame_count != page_count) {
        (void)kernel_dma_close(dma, process->owner);
        return KERNEL_PROCESS_CORRUPT;
    }

    buffer->dma = dma;
    buffer->slot = (uint8_t)slot;
    buffer->page_count = (uint16_t)page_count;
    buffer->virtual_base = KERNEL_VM_DMA_BASE + (slot * KERNEL_VM_DMA_SLOT_SIZE);
    for (mapped = 0u; mapped < page_count; ++mapped) {
        vm_status = kernel_vm_map_transfer_page(
            &process->address_space,
            buffer->virtual_base + (mapped * KERNEL_PAGE_SIZE),
            engine_info.physical_base + (mapped * KERNEL_PAGE_SIZE),
            KERNEL_VM_READ | KERNEL_VM_WRITE);
        if (vm_status != KERNEL_VM_OK)
            break;
    }
    if (mapped != page_count) {
        while (mapped-- != 0u)
            (void)kernel_vm_unmap_page(
                &process->address_space,
                buffer->virtual_base + (mapped * KERNEL_PAGE_SIZE));
        (void)kernel_dma_close(dma, process->owner);
        buffer->dma = KERNEL_DMA_HANDLE_INVALID;
        buffer->virtual_base = 0u;
        buffer->page_count = 0u;
        return vm_status == KERNEL_VM_OUT_OF_MEMORY ?
            KERNEL_PROCESS_OUT_OF_MEMORY : KERNEL_PROCESS_INVALID_STATE;
    }

    buffer->active = 1u;
    process->dma_pages += (uint16_t)page_count;
    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_DMA,
                              ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, buffer,
                              dma_buffer_release, process, &handle) !=
        KERNEL_HANDLE_OK) {
        dma_buffer_release(buffer, process);
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    }

    kernel_bytes_clear(info, sizeof(*info));
    info->size = ASTRA_DMA_BUFFER_INFO_SIZE;
    info->handle = handle;
    info->virtual_base = buffer->virtual_base;
    info->byte_size = page_count * KERNEL_PAGE_SIZE;
    info->page_count = page_count;
    return KERNEL_PROCESS_OK;
}

/*
 * The block admission calls, once the lease has proved authority. The engine
 * owns every validation that depends on the device: presence, media, write
 * protection, capability, transfer ceiling, and range. This layer owns what
 * depends on the caller: which buffer it may name, how many requests it may
 * hold, and how a result is rendered back to it.
 */
static uint32_t block_lease_info(AstraBlockLeaseInfo *geometry)
{
    KernelPlatformBlockState state;

    kernel_bytes_clear(geometry, sizeof(*geometry));
    if (!kernel_platform_block_state(&state))
        return ASTRA_SYSCALL_IO_ERROR;
    geometry->size = ASTRA_BLOCK_LEASE_INFO_SIZE;
    geometry->sector_bytes = ASTRA_BLOCK_SECTOR_BYTES;
    geometry->max_transfer_sectors = state.max_sectors;
    geometry->capabilities = state.capabilities;
    geometry->state_flags = state.state_flags;
    geometry->media_generation = state.media_generation;
    geometry->host_generation = state.host_generation;
    geometry->queue_depth = 1u;
    geometry->sector_count = state.media_sectors;
    return ASTRA_SYSCALL_OK;
}

static uint32_t block_completion_status(uint16_t engine_status)
{
    if (engine_status == 0u)
        return ASTRA_BLOCK_COMPLETION_OK;
    if (engine_status == KERNEL_BLOCK_COMPLETION_MEDIA_CHANGED)
        return ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED;
    if (engine_status == KERNEL_BLOCK_COMPLETION_RESET)
        return ASTRA_BLOCK_COMPLETION_RESET;
    if (engine_status == KERNEL_BLOCK_COMPLETION_CANCELLED)
        return ASTRA_BLOCK_COMPLETION_CANCELLED;
    return ASTRA_BLOCK_COMPLETION_DEVICE_ERROR;
}

static uint32_t block_submit_status(KernelBlockStatus status)
{
    switch (status) {
    case KERNEL_BLOCK_OK:
        return ASTRA_SYSCALL_OK;
    case KERNEL_BLOCK_INVALID_ARGUMENT:
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    case KERNEL_BLOCK_INVALID_HANDLE:
    case KERNEL_BLOCK_NOT_OWNED:
        return ASTRA_SYSCALL_INVALID_HANDLE;
    case KERNEL_BLOCK_NOT_PRESENT:
    case KERNEL_BLOCK_NO_MEDIA:
        return ASTRA_SYSCALL_PEER_DEAD;
    case KERNEL_BLOCK_WRITE_PROTECTED:
        return ASTRA_SYSCALL_ACCESS_DENIED;
    case KERNEL_BLOCK_OUT_OF_RANGE:
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    case KERNEL_BLOCK_UNSUPPORTED:
        return ASTRA_SYSCALL_BAD_SYSCALL;
    case KERNEL_BLOCK_QUEUE_FULL:
    case KERNEL_BLOCK_BUSY:
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    default:
        return ASTRA_SYSCALL_IO_ERROR;
    }
}

static uint32_t block_syscall(KernelProcess *process, KernelThread *thread,
                              uint32_t syscall)
{
    uint32_t user_address = thread->context.data[2];
    int copy_status;

    if ((user_address & (sizeof(uint32_t) - 1u)) != 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    if (syscall == ASTRA_SYSCALL_BLOCK_QUERY) {
        AstraBlockLeaseInfo geometry;
        uint32_t status = block_lease_info(&geometry);

        if (status != ASTRA_SYSCALL_OK)
            return status;
        copy_status = kernel_copy_to_user(user_address, &geometry,
                                          sizeof(geometry));
        return copy_status == KERNEL_USER_COPY_OK ?
            ASTRA_SYSCALL_OK : ASTRA_SYSCALL_BAD_ADDRESS;
    }

    if (syscall == ASTRA_SYSCALL_BLOCK_SUBMIT) {
        AstraBlockRequest request;
        KernelProcessDmaBuffer *buffer = NULL;
        KernelBlockHandle handle = KERNEL_BLOCK_HANDLE_INVALID;
        KernelDmaHandle dma = KERNEL_DMA_HANDLE_INVALID;
        KernelHandleStatus handle_status;
        KernelBlockStatus block_status;
        uint32_t transfer_bytes;

        copy_status = kernel_copy_from_user(&request, user_address,
                                            sizeof(request));
        if (copy_status != KERNEL_USER_COPY_OK)
            return ASTRA_SYSCALL_BAD_ADDRESS;
        if (request.size != ASTRA_BLOCK_REQUEST_SIZE ||
            request.reserved != 0u ||
            request.sectors > UINT16_MAX)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        if (kernel_block_owner_requests(process->owner) >=
            ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE)
            return ASTRA_SYSCALL_RESOURCE_LIMIT;

        if (request.operation != ASTRA_BLOCK_OP_FLUSH) {
            /*
             * Transfer memory is named by handle. A service can only reach a
             * buffer it owns, and the kernel resolves it to physical pages the
             * caller never sees.
             */
            handle_status = kernel_handle_lookup(
                &process->handles, request.buffer, KERNEL_OBJECT_DMA,
                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, (void **)&buffer);
            if (handle_status != KERNEL_HANDLE_OK || buffer == NULL ||
                buffer->active == 0u)
                return ASTRA_SYSCALL_INVALID_HANDLE;
            transfer_bytes = request.sectors * ASTRA_BLOCK_SECTOR_BYTES;
            if (transfer_bytes == 0u ||
                request.buffer_offset > (uint32_t)buffer->page_count *
                    KERNEL_PAGE_SIZE ||
                transfer_bytes > ((uint32_t)buffer->page_count *
                                  KERNEL_PAGE_SIZE) - request.buffer_offset)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            dma = buffer->dma;
        }

        block_status = kernel_block_submit(
            process->owner, (uint8_t)request.operation, request.lba,
            (uint16_t)request.sectors, dma, request.buffer_offset, &handle);
        if (block_status != KERNEL_BLOCK_OK)
            return block_submit_status(block_status);
        thread->context.data[1] = handle;
        return ASTRA_SYSCALL_OK;
    }

    {
        AstraBlockCompletion completion;
        KernelBlockResult engine_result;
        KernelBlockStatus block_status;

        /*
         * Draining the transport here is what makes collection self
         * sufficient: a service that waited on its completion endpoint does
         * not also depend on maintenance having run.
         */
        if (kernel_block_service(NULL) != KERNEL_BLOCK_OK)
            return ASTRA_SYSCALL_IO_ERROR;
        block_status = kernel_block_collect(thread->context.data[3],
                                            process->owner, &engine_result);
        if (block_status == KERNEL_BLOCK_PENDING)
            return ASTRA_SYSCALL_WOULD_BLOCK;
        if (block_status != KERNEL_BLOCK_OK)
            return block_submit_status(block_status);

        kernel_bytes_clear(&completion, sizeof(completion));
        completion.size = ASTRA_BLOCK_COMPLETION_SIZE;
        completion.request = thread->context.data[3];
        completion.status = block_completion_status(engine_result.status);
        completion.detail = engine_result.detail;
        completion.sectors = engine_result.sectors;
        completion.media_generation = engine_result.media_generation;
        completion.host_generation = engine_result.host_generation;
        copy_status = kernel_copy_to_user(user_address, &completion,
                                          sizeof(completion));
        return copy_status == KERNEL_USER_COPY_OK ?
            ASTRA_SYSCALL_OK : ASTRA_SYSCALL_BAD_ADDRESS;
    }
}

static bool display_dma_abort_owner(uint32_t owner)
{
    if (display_dma_active == 0u || display_dma_owner != owner)
        return true;
    if (kernel_dma_abort(&display_dma_token) != KERNEL_DMA_OK)
        return false;
    kernel_bytes_clear(&display_dma_token, sizeof(display_dma_token));
    display_dma_owner = 0u;
    display_dma_active = 0u;
    return true;
}

static uint32_t display_syscall(KernelProcess *process, KernelThread *thread,
                                uint32_t syscall,
                                uint32_t device_generation)
{
    uint32_t user_address = thread->context.data[2];
    int copy_status;

    if ((user_address & (sizeof(uint32_t) - 1u)) != 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (syscall == ASTRA_SYSCALL_DISPLAY_SUBMIT) {
        AstraDisplayFrameRequest request;
        uint32_t platform_source;

        copy_status = kernel_copy_from_user(&request, user_address,
                                            sizeof(request));
        if (copy_status != KERNEL_USER_COPY_OK)
            return ASTRA_SYSCALL_BAD_ADDRESS;
        if (request.size != ASTRA_DISPLAY_FRAME_REQUEST_SIZE ||
            request.fence == 0u)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        if (request.operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID) {
            if ((request.source & UINT32_C(0xffff0000)) != 0u ||
                request.pitch != 0u || request.byte_size != 0u)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            platform_source = request.source;
        } else if (request.operation ==
                       ASTRA_DISPLAY_FRAME_PRESENT_RGB565) {
            KernelProcessDmaBuffer *buffer = NULL;
            KernelDmaBufferInfo info;
            KernelHandleStatus handle_status;
            const uint32_t pitch = ASTRA_DISPLAY_WIDTH * sizeof(uint16_t);
            const uint32_t bytes = pitch * ASTRA_DISPLAY_HEIGHT;

            if (display_dma_active != 0u || request.pitch != pitch ||
                request.byte_size != bytes)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            handle_status = kernel_handle_lookup(
                &process->handles, request.source, KERNEL_OBJECT_DMA,
                ASTRA_RIGHT_READ, (void **)&buffer);
            if (handle_status != KERNEL_HANDLE_OK || buffer == NULL ||
                buffer->active == 0u ||
                kernel_dma_buffer_info(buffer->dma, process->owner, &info) !=
                    KERNEL_DMA_OK || info.byte_size < bytes)
                return ASTRA_SYSCALL_INVALID_HANDLE;
            if (kernel_dma_begin(buffer->dma, process->owner, 0u, bytes,
                                 KERNEL_DMA_TO_DEVICE, device_generation,
                                 &display_dma_token) != KERNEL_DMA_OK)
                return ASTRA_SYSCALL_WOULD_BLOCK;
            display_dma_owner = process->owner;
            display_dma_active = 1u;
            platform_source = display_dma_token.physical_address;
        } else if (request.operation ==
                       ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH) {
            KernelProcessDmaBuffer *buffer = NULL;
            KernelDmaBufferInfo info;
            KernelHandleStatus handle_status;

            if (display_dma_active != 0u || request.pitch != 0u ||
                request.byte_size < ASTRA_RENDER_BATCH_MIN_BYTES ||
                request.byte_size > ASTRA_RENDER_BATCH_MAX_BYTES)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            handle_status = kernel_handle_lookup(
                &process->handles, request.source, KERNEL_OBJECT_DMA,
                ASTRA_RIGHT_READ, (void **)&buffer);
            if (handle_status != KERNEL_HANDLE_OK || buffer == NULL ||
                buffer->active == 0u ||
                kernel_dma_buffer_info(buffer->dma, process->owner, &info) !=
                    KERNEL_DMA_OK || info.byte_size < request.byte_size)
                return ASTRA_SYSCALL_INVALID_HANDLE;
            if (kernel_dma_begin(buffer->dma, process->owner, 0u,
                                 request.byte_size, KERNEL_DMA_TO_DEVICE,
                                 device_generation,
                                 &display_dma_token) != KERNEL_DMA_OK)
                return ASTRA_SYSCALL_WOULD_BLOCK;
            display_dma_owner = process->owner;
            display_dma_active = 1u;
            platform_source = display_dma_token.physical_address;
        } else if (request.operation == ASTRA_DISPLAY_CURSOR_UPDATE) {
            if (request.source >= ASTRA_DISPLAY_WIDTH ||
                request.pitch >= ASTRA_DISPLAY_HEIGHT ||
                (request.byte_size &
                 ~(ASTRA_DISPLAY_CURSOR_VISIBLE |
                   ASTRA_DISPLAY_CURSOR_DEFER_COMMIT)) != 0u)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            platform_source = ASTRA_DISPLAY_HOST_CURSOR_PACK(
                request.source, request.pitch,
                (request.byte_size & ASTRA_DISPLAY_CURSOR_VISIBLE) != 0u);
        } else {
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        }
        if (kernel_platform_display_submit(
                request.fence, request.operation, platform_source,
                request.operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ?
                    request.byte_size :
                request.operation == ASTRA_DISPLAY_CURSOR_UPDATE ?
                    request.byte_size : 0u))
            return ASTRA_SYSCALL_OK;
        if (!display_dma_abort_owner(process->owner))
            return ASTRA_SYSCALL_IO_ERROR;
        return ASTRA_SYSCALL_WOULD_BLOCK;
    }
    {
        AstraDisplayFrameCompletion completion;

        if (!kernel_platform_display_collect(&completion))
            return ASTRA_SYSCALL_WOULD_BLOCK;
        if (display_dma_active != 0u) {
            if (display_dma_owner != process->owner ||
                kernel_dma_complete(&display_dma_token) != KERNEL_DMA_OK)
                return ASTRA_SYSCALL_IO_ERROR;
            kernel_bytes_clear(&display_dma_token,
                               sizeof(display_dma_token));
            display_dma_owner = 0u;
            display_dma_active = 0u;
        }
        copy_status = kernel_copy_to_user(user_address, &completion,
                                          sizeof(completion));
        return copy_status == KERNEL_USER_COPY_OK ?
            ASTRA_SYSCALL_OK : ASTRA_SYSCALL_BAD_ADDRESS;
    }
}

static KernelProcessStatus retire_current(KernelProcessExitReason reason,
                                          uint32_t exit_status,
                                          KernelCpuContext **next_context)
{
    KernelProcess *retiring;
    KernelProcessStatus status;
    uint16_t retiring_slot;
    uint32_t closed_sync_objects;
    uint32_t closed_ports;
    uint32_t closed_rings;
    uint32_t closed_areas;
    uint32_t revoked_irqs;
    uint32_t revoked_area_mappings;
    uint32_t retired_threads;
    uint32_t woken_irq_waiters;
    uint32_t woken_ring_waiters;
    uint32_t woken_port_waiters;
    uint32_t woken_sync_waiters;
    KernelIrqStatus irq_status;

    if (next_context == NULL || current_thread == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    retiring_slot = current_thread->process_slot;
    if (retiring_slot >= KERNEL_PROCESS_MAX)
        return KERNEL_PROCESS_CORRUPT;
    retiring = &processes[retiring_slot];
    retiring->process_state = KERNEL_PROCESS_EXITING;
    retiring->exit_reason = (uint8_t)reason;
    /*
     * A process killed by its own fault never returned a status, and zero is
     * the one value that must not stand in for one: it is what a program says
     * when it succeeded. The verdict replaces it here, at the single point
     * every process passes through on its way out, so that the death wait, the
     * process info record, the snapshot and the kernel's own boot line all
     * report the same answer rather than three of them agreeing by accident.
     *
     * The verdict bit is the system's alone, so a status carrying one did not
     * come from the program whatever it claims. crt0 already refuses to exit
     * with one; this is the same substitution for anything that did not come
     * through crt0, and it is what makes the bit worth reading at all.
     */
    if (reason == KERNEL_PROCESS_EXIT_USER_FAULT)
        retiring->exit_status = (uint32_t)ASTRA_STATUS_FAULTED;
    else if ((exit_status & (uint32_t)ASTRA_STATUS_VERDICT) != 0u)
        retiring->exit_status = (uint32_t)ASTRA_STATUS_BAD_EXIT;
    else
        retiring->exit_status = exit_status;
    retiring->terminal_result = reason == KERNEL_PROCESS_EXIT_USER_FAULT ?
        ASTRA_SYSCALL_PEER_DEAD : ASTRA_SYSCALL_OK;
    /*
     * The initial image is the one process whose death the kernel itself must
     * notice: its record is reclaimed as soon as the last handle closes, and
     * after that there is nobody left to ask how boot went.
     */
    if (initial_image_process_id != 0u &&
        retiring->id == initial_image_process_id && !initial_image_exited) {
        initial_image_exited = 1u;
        kernel_process_initial_image_exited(exit_status, (uint32_t)reason);
    }
    if (kernel_thread_retire_process(retiring_slot, ASTRA_SYSCALL_PEER_DEAD,
                                     &retired_threads) !=
        KERNEL_THREAD_OK || retired_threads != retiring->live_threads)
        return KERNEL_PROCESS_CORRUPT;
    retiring->live_threads = 0u;
    if (retired_threads > scheduler_stats.live_threads)
        return KERNEL_PROCESS_CORRUPT;
    scheduler_stats.live_threads -= retired_threads;
    scheduler_stats.dead_threads += retired_threads;
    irq_status = kernel_irq_owner_died(retiring->id, &revoked_irqs,
                                       &woken_irq_waiters);
    if (irq_status != KERNEL_IRQ_OK &&
        irq_status != KERNEL_IRQ_DEVICE_ERROR)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_sync_owner_died(retiring->id, ASTRA_SYSCALL_PEER_DEAD,
                               &closed_sync_objects,
                               &woken_sync_waiters) != KERNEL_SYNC_OK)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_port_owner_died(retiring->id, &closed_ports,
                               &woken_port_waiters) != KERNEL_PORT_OK)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_ring_process_died(retiring->id, &closed_rings,
                                 &woken_ring_waiters) != KERNEL_RING_OK)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_area_process_died(retiring->id, &closed_areas,
                                 &revoked_area_mappings) != KERNEL_AREA_OK)
        return KERNEL_PROCESS_CORRUPT;
    (void)closed_sync_objects;
    (void)closed_ports;
    (void)closed_rings;
    (void)closed_areas;
    (void)revoked_irqs;
    (void)revoked_area_mappings;
    (void)woken_irq_waiters;
    (void)woken_port_waiters;
    (void)woken_ring_waiters;
    (void)woken_sync_waiters;
    if (wake_process_death(retiring) != KERNEL_PROCESS_OK)
        return KERNEL_PROCESS_CORRUPT;
    --scheduler_stats.live_processes;
    status = schedule_next(next_context);
    if (status != KERNEL_PROCESS_OK &&
        status != KERNEL_PROCESS_NO_RUNNABLE)
        return status;
    check_milestone();
    return status;
}

static KernelProcessStatus retire_current_thread(
    uint32_t exit_status, KernelCpuContext **next_context)
{
    KernelProcess *process;
    KernelProcessStatus status;
    uint32_t woken = 0u;

    if (next_context == NULL || current_thread == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    process = process_for_thread(current_thread);
    if (process == NULL || process->live_threads == 0u)
        return KERNEL_PROCESS_CORRUPT;
    ++scheduler_stats.thread_exits;
    if (process->live_threads == 1u)
        return retire_current(KERNEL_PROCESS_EXIT_LAST_THREAD, exit_status,
                              next_context);
    if (kernel_thread_complete(current_thread, exit_status,
                               ASTRA_SYSCALL_OK, &woken) !=
        KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    --process->live_threads;
    --scheduler_stats.live_threads;
    ++scheduler_stats.dead_threads;
    scheduler_stats.thread_death_wakeups += woken;
    status = schedule_next(next_context);
    if (status != KERNEL_PROCESS_OK &&
        status != KERNEL_PROCESS_NO_RUNNABLE)
        return status;
    return status;
}

void kernel_process_init(void)
{
    scheduler_initialized = 0u;
    scheduler_started = 0u;
    /*
     * A fresh boot narrates again. The sink closes when a reader appears, and
     * on this side of a reset there is not one yet.
     */
    diagnostic_console_open = 1u;
#if defined(KERNEL_PROCESS_HOST_TEST)
    next_thread_create_fault = KERNEL_PROCESS_THREAD_CREATE_FAULT_NONE;
#endif
    kernel_performance_init();
    if (!kernel_irq_pool_healthy()) {
        process_pool_corrupt = 1u;
        return;
    }
    if (!kernel_object_cache_init(
            &process_cache, processes, sizeof(processes[0]),
            KERNEL_PROCESS_MAX, process_cache_bitmap,
            KERNEL_OBJECT_CACHE_BITMAP_WORDS(KERNEL_PROCESS_MAX),
            KERNEL_ALLOCATION_SITE_PROCESS_RECORD)) {
        process_pool_corrupt = 1u;
        return;
    }
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        kernel_bytes_clear(&processes[index], sizeof(processes[index]));
        kernel_handle_table_init(&processes[index].handles);
        kernel_thread_wait_queue_init(&processes[index].death_waiters);
    }
    kernel_bytes_clear(&scheduler_stats, sizeof(scheduler_stats));
    kernel_bytes_clear(signal_saved_context, sizeof(signal_saved_context));
    kernel_bytes_clear(signal_context_active, sizeof(signal_context_active));
    kernel_bytes_clear(library_cache, sizeof(library_cache));
    kernel_bytes_clear(&maintenance_diagnostics,
                       sizeof(maintenance_diagnostics));
    kernel_bytes_clear(qualification_clients,
                       sizeof(qualification_clients));
    initial_image_process_id = 0u;
    initial_image_progress = 0u;
    initial_image_exited = 0u;
    kernel_bytes_clear(&display_dma_token, sizeof(display_dma_token));
    display_dma_owner = 0u;
    display_dma_active = 0u;
    kernel_thread_pool_init();
    kernel_sync_pool_init();
    kernel_handle_transfer_pool_init();
    kernel_port_pool_init();
    kernel_area_pool_init();
#if defined(KERNEL_PROCESS_HOST_TEST)
    kernel_area_test_bind_physical_memory(
        host_physical_memory, host_physical_base, host_physical_size);
#endif
    kernel_ring_pool_init();
#if ASTRA_KERNEL_SOAK_SELFTEST
    kernel_bytes_clear(&soak_state, sizeof(soak_state));
#endif
    current_thread = NULL;
    runtime_started_cycles = 0u;
    runtime_active = 0u;
    quantum_deadline = 0u;
    scheduler_quantum_cycles = kernel_platform_quantum_cycles();
    if (scheduler_quantum_cycles == 0u)
        scheduler_quantum_cycles = 1u;
    scheduler_stats.quantum_cycles = scheduler_quantum_cycles;
    quantum_active = 0u;
    quantum_preempt_pending = 0u;
    deadline_preempt_pending = 0u;
    worker_active = 0u;
    milestone_progress_ready = 0u;
    process_pool_corrupt = 0u;
    scheduler_initialized = 1u;
    scheduler_timer_rearm();
}

static KernelProcess *find_process_by_id(uint32_t process_id)
{
    for (uint32_t slot = 0u; slot < KERNEL_PROCESS_MAX; ++slot) {
        KernelProcess *process = &processes[slot];

        if (process->id == process_id &&
            (process->process_state == KERNEL_PROCESS_CREATED ||
             process->process_state == KERNEL_PROCESS_RUNNING))
            return process;
    }
    return NULL;
}

#if defined(KERNEL_PROCESS_HOST_TEST)
uint32_t kernel_process_test_handle_count(uint32_t process_id)
{
    KernelProcess *process = find_process_by_id(process_id);

    return process != NULL ? kernel_handle_count(&process->handles) : 0u;
}
#endif

#if defined(KERNEL_PROCESS_HOST_TEST)
/*
 * The host suites have to be able to stand in the shipped configuration as
 * well as the development one. The compile-time constant is still what a real
 * build is made from; this only lets a test ask what happens on the other side
 * of it, which is the side nobody exercises by accident and the side where a
 * mistake is a backdoor rather than a missing convenience.
 */
static int debug_surface_override = -1;

void kernel_process_set_debug_surface(int enabled)
{
    debug_surface_override = enabled;
}
#endif

bool kernel_process_debug_surface(void)
{
#if defined(KERNEL_PROCESS_HOST_TEST)
    if (debug_surface_override >= 0)
        return debug_surface_override != 0;
#endif
    return ASTRA_KERNEL_DEBUG_SURFACE != 0;
}

/*
 * What a process holds over itself. QUERY always; DEBUG only where the build
 * carries a diagnostic surface, which is what makes the log channel a
 * capability rather than a syscall anyone can reach.
 */
static uint32_t self_handle_rights(void)
{
    return KERNEL_PROCESS_RIGHT_QUERY | KERNEL_PROCESS_RIGHT_PRIORITY |
           (kernel_process_debug_surface() ? KERNEL_PROCESS_RIGHT_DEBUG : 0u);
}

static int32_t find_stack_slot(const KernelProcess *process)
{
    for (uint32_t slot = 0u; slot < KERNEL_PROCESS_THREAD_MAX; ++slot) {
        if ((process->stack_slots & astra_u64_bit(slot)) == 0u)
            return (int32_t)slot;
    }
    return -1;
}

/*
 * A slot is a reservation, and these three addresses are the whole of its
 * layout: the guard page sits at the base, the stack pointer starts at the
 * top, and everything between the floor and the top may be committed.
 */
static uint32_t stack_slot_base(uint32_t slot)
{
    return KERNEL_THREAD_STACK_BASE + slot * KERNEL_THREAD_STACK_STRIDE;
}

static uint32_t stack_slot_floor(uint32_t slot)
{
    return stack_slot_base(slot) + KERNEL_THREAD_STACK_GUARD_SIZE;
}

static uint32_t stack_slot_top(uint32_t slot)
{
    return stack_slot_base(slot) + KERNEL_THREAD_STACK_STRIDE;
}

/*
 * Commits the pages between an address a thread's stack has reached and the
 * lowest one it already holds, and answers whether it did.
 *
 * The whole span is mapped rather than the one page containing the address: a
 * frame larger than a page can touch its bottom first, and leaving a hole
 * would make user_stack_base stop describing what is mapped.
 *
 * Refusing is the important half. The floor page of the slot is never mapped,
 * so a stack that runs past its reservation still dies exactly where it used
 * to, and an address already inside the committed range is some other defect
 * -- a write to a read-only page, say -- which must not be answered by
 * committing memory to it.
 */
static bool grow_user_stack(KernelProcess *process, KernelThread *thread,
                            uint32_t address)
{
    uint32_t slot;
    uint32_t page;
    uint32_t pages;
    uint32_t mapped = 0u;
    bool failed = false;

    if (process == NULL || thread == NULL ||
        thread->stack_slot >= KERNEL_PROCESS_THREAD_MAX)
        return false;
    slot = thread->stack_slot;
    /* A stack that is not this slot's reservation is not one to grow. */
    if (thread->user_stack_top != stack_slot_top(slot) ||
        (thread->user_stack_base & (KERNEL_PAGE_SIZE - 1u)) != 0u)
        return false;
    if (address < stack_slot_floor(slot) ||
        address >= thread->user_stack_base)
        return false;
    page = address & ~(KERNEL_PAGE_SIZE - 1u);
    pages = (thread->user_stack_base - page) / KERNEL_PAGE_SIZE;
    if (pages == 0u ||
        (uint32_t)thread->stack_pages + pages > KERNEL_THREAD_STACK_PAGES_MAX ||
        (uint32_t)process->user_stack_pages + pages > 255u)
        return false;

    while (mapped < pages) {
        uint32_t page_address = page + (mapped * KERNEL_PAGE_SIZE);
        uint32_t physical = 0u;

        if (kernel_memory_alloc_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_THREAD_STACK_PAGE, 1u, 1u,
                KERNEL_FRAME_PROCESS, process->owner, &physical) !=
            KERNEL_MEMORY_OK) {
            failed = true;
            break;
        }
#if defined(KERNEL_PROCESS_HOST_TEST)
        {
            /* Host memory tests disable allocator writes to synthetic pages. */
            uint8_t *bytes = physical_bytes(physical, KERNEL_PAGE_SIZE);

            if (bytes != NULL)
                kernel_bytes_clear(bytes, KERNEL_PAGE_SIZE);
        }
#endif
        if (kernel_vm_map_page(&process->address_space, page_address, physical,
                               KERNEL_VM_READ | KERNEL_VM_WRITE) !=
            KERNEL_VM_OK) {
            (void)kernel_memory_release(physical, 1u, process->owner);
            failed = true;
            break;
        }
        /* The mapping owns the frame now; the reservation must not also. */
        if (kernel_memory_release(physical, 1u, process->owner) !=
            KERNEL_MEMORY_OK) {
            failed = true;
            ++mapped;
            break;
        }
        ++mapped;
    }
    if (failed) {
        /*
         * Unwound rather than kept: a half-grown stack would leave
         * user_stack_base describing pages that are not there, and the
         * process is about to be retired for the fault anyway.
         */
        while (mapped != 0u) {
            --mapped;
            (void)kernel_vm_unmap_page(
                &process->address_space,
                page + (mapped * KERNEL_PAGE_SIZE));
        }
        return false;
    }

    thread->user_stack_base = page;
    thread->stack_pages = (uint8_t)(thread->stack_pages + pages);
    process->user_stack_pages =
        (uint8_t)(process->user_stack_pages + pages);
    ++scheduler_stats.user_stack_growths;
    scheduler_stats.user_stack_pages_committed += pages;
    return true;
}

typedef struct KernelPreparedThread {
    KernelProcess *process;
    KernelThread *thread;
    KernelHandle handle;
    uint16_t stack_slot;
} KernelPreparedThread;

static KernelProcessStatus prepare_thread(KernelProcess *process,
                                          uint32_t entry,
                                          uint32_t initial_argument,
                                          uint8_t priority,
                                          uint32_t rights,
                                          KernelPreparedThread *prepared)
{
    KernelThread *thread = NULL;
    KernelThreadStatus thread_status;
    KernelHandle thread_handle = KERNEL_HANDLE_INVALID;
    uint32_t stack_physical = 0u;
    uint32_t stack_base;
    uint32_t stack_top;
    int32_t stack_slot;
    bool stack_held = false;
    uint32_t stack_pages_mapped = 0u;
    bool handle_installed = false;
    bool cleanup_failed = false;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;
    KernelVmStatus vm_status;
#if defined(KERNEL_PROCESS_HOST_TEST)
    /*
     * Declared here rather than at its first use so no `goto failed` jumps
     * over its initialisation. The cleanup path does not touch it today, which
     * is the only reason that was harmless; it stops being harmless the moment
     * someone adds a release that does.
     */
    uint8_t *stack = NULL;
#endif

    if (process == NULL || prepared == NULL ||
        !entry_within_code(process, entry) ||
        priority < KERNEL_THREAD_PRIORITY_USER_MIN ||
        priority > process->priority_ceiling || rights == 0u ||
        (rights & ~KERNEL_THREAD_RIGHTS) != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    prepared->process = NULL;
    prepared->thread = NULL;
    prepared->handle = KERNEL_HANDLE_INVALID;
    prepared->stack_slot = 0u;
    if (process->thread_count >= KERNEL_PROCESS_THREAD_MAX)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    stack_slot = find_stack_slot(process);
    if (stack_slot < 0)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    /*
     * The commitment is at the **top** of the slot, not the bottom: a stack
     * grows down, so the pages it starts with are the ones it uses first and
     * the room it may grow into is underneath them.
     */
    stack_top = stack_slot_top((uint32_t)stack_slot);
    stack_base = stack_top - KERNEL_THREAD_STACK_SIZE;

    thread_status = kernel_thread_allocate(
        (uint16_t)(process - processes), process->id,
        (uint16_t)stack_slot, entry,
        stack_top, initial_argument, priority, &thread);
    if (thread_status == KERNEL_THREAD_NO_SLOT)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    if (thread_status != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    signal_context_active[thread->slot] = 0u;

#if defined(KERNEL_PROCESS_HOST_TEST)
    if (consume_thread_create_fault(
            KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_ALLOC)) {
        result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
#endif

    /*
     * One page at a time rather than one contiguous run: the pages are mapped
     * separately anyway, so demanding physical contiguity would fail against a
     * fragmented pool for no benefit. `stack_pages_mapped` is what the failure
     * path unwinds, so a partial mapping leaves nothing behind.
     */
    for (stack_pages_mapped = 0u;
         stack_pages_mapped < KERNEL_THREAD_STACK_PAGES;) {
        uint32_t page_address =
            stack_base + (stack_pages_mapped * KERNEL_PAGE_SIZE);

        if (kernel_memory_alloc_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_THREAD_STACK_PAGE, 1u, 1u,
                KERNEL_FRAME_PROCESS, process->owner, &stack_physical) !=
            KERNEL_MEMORY_OK) {
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
            goto failed;
        }
        stack_held = true;
#if defined(KERNEL_PROCESS_HOST_TEST)
        stack = physical_bytes(stack_physical, KERNEL_PAGE_SIZE);
        if (stack == NULL)
            goto failed;
        /* Host memory tests disable allocator writes to synthetic addresses. */
        kernel_bytes_clear(stack, KERNEL_PAGE_SIZE);
        if (consume_thread_create_fault(
                KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_MAP)) {
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
            goto failed;
        }
#endif
        vm_status = kernel_vm_map_page(
            &process->address_space, page_address, stack_physical,
            KERNEL_VM_READ | KERNEL_VM_WRITE);
        if (vm_status != KERNEL_VM_OK) {
            if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
                result = KERNEL_PROCESS_OUT_OF_MEMORY;
            goto failed;
        }
        ++stack_pages_mapped;
        /* The mapping owns the frame now; the reservation must not also. */
        if (kernel_memory_release(stack_physical, 1u, process->owner) !=
            KERNEL_MEMORY_OK)
            goto failed;
        stack_held = false;
    }

#if defined(KERNEL_PROCESS_HOST_TEST)
    if (consume_thread_create_fault(
            KERNEL_PROCESS_THREAD_CREATE_FAULT_HANDLE_INSTALL)) {
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
#endif

    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_THREAD,
                              rights, thread,
                              kernel_thread_handle_release, NULL,
                              &thread_handle) !=
        KERNEL_HANDLE_OK) {
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
    handle_installed = true;
    if (kernel_thread_attach_handle(thread, thread_handle) !=
        KERNEL_THREAD_OK)
        goto failed;
    thread->context.data[4] = process->self_handle;
    thread->context.data[5] = thread_handle;
#if defined(KERNEL_PROCESS_HOST_TEST)
    if (consume_thread_create_fault(
            KERNEL_PROCESS_THREAD_CREATE_FAULT_PUBLISH)) {
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
#endif
    prepared->process = process;
    prepared->thread = thread;
    prepared->handle = thread_handle;
    prepared->stack_slot = (uint16_t)stack_slot;
    return KERNEL_PROCESS_OK;

failed:
    if (handle_installed &&
        kernel_handle_close(&process->handles, thread_handle) !=
            KERNEL_HANDLE_OK)
        cleanup_failed = true;
    while (stack_pages_mapped != 0u) {
        --stack_pages_mapped;
        if (kernel_vm_unmap_page(
                &process->address_space,
                stack_base + (stack_pages_mapped * KERNEL_PAGE_SIZE)) !=
            KERNEL_VM_OK)
            cleanup_failed = true;
    }
    if (stack_held &&
        kernel_memory_release(stack_physical, 1u, process->owner) !=
            KERNEL_MEMORY_OK)
        cleanup_failed = true;
    if (thread != NULL && kernel_thread_abort(thread) != KERNEL_THREAD_OK)
        cleanup_failed = true;
    if (cleanup_failed)
        return KERNEL_PROCESS_CORRUPT;
    return result;
}

static KernelProcessStatus abort_prepared_thread(
    KernelPreparedThread *prepared)
{
    uint32_t stack_base;
    bool cleanup_failed = false;

    if (prepared == NULL || prepared->process == NULL ||
        prepared->thread == NULL ||
        prepared->handle == KERNEL_HANDLE_INVALID ||
        prepared->stack_slot >= KERNEL_PROCESS_THREAD_MAX)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    stack_base = stack_slot_top((uint32_t)prepared->stack_slot) -
                 KERNEL_THREAD_STACK_SIZE;
    if (kernel_handle_close(&prepared->process->handles, prepared->handle) !=
        KERNEL_HANDLE_OK)
        cleanup_failed = true;
    /*
     * Every page committed at creation, not the first one. A thread that has
     * not been published cannot have grown, so the count is still the
     * creation-time one.
     */
    for (uint32_t page = 0u; page < KERNEL_THREAD_STACK_PAGES; ++page) {
        if (kernel_vm_unmap_page(&prepared->process->address_space,
                                 stack_base + (page * KERNEL_PAGE_SIZE)) !=
            KERNEL_VM_OK)
            cleanup_failed = true;
    }
    if (kernel_thread_abort(prepared->thread) != KERNEL_THREAD_OK)
        cleanup_failed = true;
    prepared->process = NULL;
    prepared->thread = NULL;
    prepared->handle = KERNEL_HANDLE_INVALID;
    return cleanup_failed ? KERNEL_PROCESS_CORRUPT : KERNEL_PROCESS_OK;
}

/* Caller serializes this no-allocation publication against timer IRQs. */
static KernelProcessStatus commit_thread(KernelPreparedThread *prepared,
                                         uint32_t *thread_id,
                                         KernelHandle *created_handle)
{
    KernelProcess *process;
    KernelThread *thread;

    if (prepared == NULL || thread_id == NULL || created_handle == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *thread_id = 0u;
    *created_handle = KERNEL_HANDLE_INVALID;
    process = prepared->process;
    thread = prepared->thread;
    if (process == NULL || thread == NULL ||
        prepared->handle == KERNEL_HANDLE_INVALID ||
        prepared->stack_slot >= KERNEL_PROCESS_THREAD_MAX ||
        process->thread_count >= KERNEL_PROCESS_THREAD_MAX ||
        (process->stack_slots & astra_u64_bit(prepared->stack_slot)) != 0u ||
        thread->process_id != process->id ||
        thread->process_slot != (uint16_t)(process - processes) ||
        thread->stack_slot != prepared->stack_slot ||
        thread->self_handle != prepared->handle)
        return KERNEL_PROCESS_CORRUPT;
    if (kernel_thread_publish(thread) != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;

    process->stack_slots |= astra_u64_bit(prepared->stack_slot);
    ++process->thread_count;
    ++process->live_threads;
    process->user_stack_pages =
        (uint8_t)(process->user_stack_pages + thread->stack_pages);
    ++process->user_guard_pages;
    process->supervisor_stack_pages +=
        KERNEL_THREAD_SUPERVISOR_STACK_SIZE / KERNEL_PAGE_SIZE;
    process->supervisor_guard_pages +=
        KERNEL_THREAD_SUPERVISOR_GUARD_SIZE / KERNEL_PAGE_SIZE;
    ++scheduler_stats.created_threads;
    ++scheduler_stats.live_threads;
    *thread_id = thread->id;
    *created_handle = prepared->handle;
    prepared->process = NULL;
    prepared->thread = NULL;
    prepared->handle = KERNEL_HANDLE_INVALID;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus prepare_cloned_thread(
    KernelProcess *process, const KernelThread *source,
    KernelPreparedThread *prepared)
{
    KernelThread *thread = NULL;
    KernelHandle handle = KERNEL_HANDLE_INVALID;
    KernelThreadStatus thread_status;

    if (process == NULL || source == NULL || prepared == NULL ||
        source->stack_slot >= KERNEL_PROCESS_THREAD_MAX ||
        source->stack_pages == 0u || source->stack_pages >
                                              KERNEL_THREAD_STACK_PAGES_MAX)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    prepared->process = NULL;
    prepared->thread = NULL;
    prepared->handle = KERNEL_HANDLE_INVALID;
    prepared->stack_slot = 0u;
    thread_status = kernel_thread_allocate(
        (uint16_t)(process - processes), process->id, source->stack_slot,
        source->context.program_counter, source->user_stack_top, 0u,
        source->base_priority, &thread);
    if (thread_status == KERNEL_THREAD_NO_SLOT)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    if (thread_status != KERNEL_THREAD_OK || thread == NULL)
        return KERNEL_PROCESS_CORRUPT;
    if (signal_context_active[source->slot] != 0u) {
        kernel_bytes_copy(&signal_saved_context[thread->slot],
                          &signal_saved_context[source->slot],
                          sizeof(signal_saved_context[thread->slot]));
        signal_context_active[thread->slot] = 1u;
    } else {
        signal_context_active[thread->slot] = 0u;
    }
    thread->user_stack_base = source->user_stack_base;
    thread->stack_pages = source->stack_pages;
    kernel_bytes_copy(&thread->context, &source->context,
                      sizeof(thread->context));
    thread->context.data[0] = ASTRA_SYSCALL_OK;
    thread->context.data[1] = 0u;
    thread->context.data[2] = 0u;
    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_THREAD,
                              KERNEL_THREAD_RIGHTS, thread,
                              kernel_thread_handle_release, NULL,
                              &handle) != KERNEL_HANDLE_OK ||
        kernel_thread_attach_handle(thread, handle) != KERNEL_THREAD_OK) {
        if (handle != KERNEL_HANDLE_INVALID)
            (void)kernel_handle_close(&process->handles, handle);
        (void)kernel_thread_abort(thread);
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    }
    thread->context.data[4] = process->self_handle;
    thread->context.data[5] = handle;
    prepared->process = process;
    prepared->thread = thread;
    prepared->handle = handle;
    prepared->stack_slot = source->stack_slot;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus abort_cloned_thread(KernelPreparedThread *prepared)
{
    bool failed = false;

    if (prepared == NULL || prepared->process == NULL ||
        prepared->thread == NULL ||
        prepared->handle == KERNEL_HANDLE_INVALID)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (kernel_handle_close(&prepared->process->handles,
                            prepared->handle) != KERNEL_HANDLE_OK)
        failed = true;
    if (kernel_thread_abort(prepared->thread) != KERNEL_THREAD_OK)
        failed = true;
    signal_context_active[prepared->thread->slot] = 0u;
    prepared->process = NULL;
    prepared->thread = NULL;
    prepared->handle = KERNEL_HANDLE_INVALID;
    return failed ? KERNEL_PROCESS_CORRUPT : KERNEL_PROCESS_OK;
}

static KernelProcessStatus clone_current_process(
    KernelProcess *source, KernelThread *source_thread,
    uint32_t *process_id, KernelHandle *process_handle)
{
    KernelProcess *child;
    KernelPreparedThread prepared;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;
    KernelHandle self_handle = KERNEL_HANDLE_INVALID;
    KernelHandle parent_handle = KERNEL_HANDLE_INVALID;
    KernelHandle child_thread_handle;
    KernelHandleStatus handle_status;
    KernelObjectCacheStatus cache_status;
    KernelVmStatus vm_status;
    void *raw_child;
    uint32_t generation;
    uint32_t child_thread_id;
    uint16_t slot;
    uint16_t saved_status;
    bool owner_protected = false;

    if (source == NULL || source_thread == NULL || process_id == NULL ||
        process_handle == NULL || process_for_thread(source_thread) != source)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    kernel_bytes_clear(&prepared, sizeof(prepared));
    *process_id = 0u;
    *process_handle = KERNEL_HANDLE_INVALID;
    cache_status = kernel_object_cache_claim(&process_cache, 0u, &raw_child,
                                             &slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
        return KERNEL_PROCESS_NO_SLOT;
    if (cache_status != KERNEL_OBJECT_CACHE_OK || slot >= KERNEL_PROCESS_MAX)
        return KERNEL_PROCESS_CORRUPT;
    child = raw_child;
    if (child->process_state != KERNEL_PROCESS_UNUSED &&
        child->process_state != KERNEL_PROCESS_DEAD)
        return KERNEL_PROCESS_CORRUPT;

    generation = kernel_generation_next(child->generation);
    kernel_bytes_clear(child, sizeof(*child));
    kernel_thread_wait_queue_init(&child->death_waiters);
    child->generation = generation;
    child->id = PROCESS_OWNER_PREFIX |
                ((generation & 0x000fffffu) << 4) | ((uint32_t)slot + 1u);
    child->owner = child->id;
    child->image_size = source->image_size;
    child->entry_base = source->entry_base;
    child->started_cycles = scheduler_cycles();
    child->default_priority = source->default_priority;
    child->priority_ceiling = source->priority_ceiling;
    child->signal_trampoline = source->signal_trampoline;
    child->signal_stack_top = source->signal_stack_top;
    child->signal_blocked = source->signal_blocked;
    kernel_handle_table_init(&child->handles);
    if (!kernel_handle_table_set_owner(&child->handles, child->owner))
        goto failed;
    if (child->priority_ceiling == KERNEL_THREAD_PRIORITY_USER_MAX) {
        if (!kernel_memory_protect_owner(child->owner))
            goto failed;
        owner_protected = true;
    }
    vm_status = kernel_vm_clone_address_space(
        &source->address_space, child->owner, &child->address_space);
    if (vm_status != KERNEL_VM_OK) {
        result = vm_status == KERNEL_VM_OUT_OF_MEMORY ?
            KERNEL_PROCESS_OUT_OF_MEMORY : KERNEL_PROCESS_CORRUPT;
        goto failed;
    }
    handle_status = kernel_handle_clone_table(&source->handles,
                                              &child->handles);
    if (handle_status != KERNEL_HANDLE_OK) {
        result = handle_status == KERNEL_HANDLE_TABLE_FULL ||
                         handle_status == KERNEL_HANDLE_INVALID_STATE ?
            KERNEL_PROCESS_RESOURCE_LIMIT : KERNEL_PROCESS_CORRUPT;
        goto failed;
    }
    child->process_state = KERNEL_PROCESS_CREATED;
    child->handle_references = 1u;
    if (kernel_handle_install(&child->handles, KERNEL_OBJECT_PROCESS,
                              self_handle_rights(), child,
                              process_handle_release, NULL,
                              &self_handle) != KERNEL_HANDLE_OK) {
        child->handle_references = 0u;
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
    child->self_handle = self_handle;
    result = prepare_cloned_thread(child, source_thread, &prepared);
    if (result != KERNEL_PROCESS_OK)
        goto failed;
    result = retain_process_handle(child);
    if (result != KERNEL_PROCESS_OK)
        goto failed;
    handle_status = kernel_handle_install(
        &source->handles, KERNEL_OBJECT_PROCESS,
        KERNEL_PROCESS_RIGHT_QUERY | KERNEL_PROCESS_RIGHT_WAIT, child,
        process_handle_release, NULL, &parent_handle);
    if (handle_status != KERNEL_HANDLE_OK) {
        process_handle_release(child, NULL);
        result = handle_status == KERNEL_HANDLE_TABLE_FULL ?
            KERNEL_PROCESS_RESOURCE_LIMIT : KERNEL_PROCESS_CORRUPT;
        goto failed;
    }

    saved_status = kernel_interrupt_save_disable();
    result = commit_thread(&prepared, &child_thread_id,
                           &child_thread_handle);
    if (result == KERNEL_PROCESS_OK) {
        ++scheduler_stats.created_processes;
        ++scheduler_stats.live_processes;
        *process_id = child->id;
        *process_handle = parent_handle;
    }
    kernel_interrupt_restore(saved_status);
    if (result != KERNEL_PROCESS_OK)
        goto failed;
    (void)child_thread_id;
    (void)child_thread_handle;
    return KERNEL_PROCESS_OK;

failed:
    if (parent_handle != KERNEL_HANDLE_INVALID)
        (void)kernel_handle_close(&source->handles, parent_handle);
    if (prepared.thread != NULL)
        (void)abort_cloned_thread(&prepared);
    (void)kernel_handle_close_all(&child->handles);
    if (child->address_space.initialized != 0u)
        (void)kernel_vm_destroy_address_space(&child->address_space);
    (void)kernel_memory_release_owner(child->owner, NULL);
    if (owner_protected)
        (void)kernel_memory_unprotect_owner(child->owner);
    child->process_state = KERNEL_PROCESS_DEAD;
    maybe_release_process_record(child);
    return result;
}

static KernelProcessStatus create_process(const void *image,
                                          uint32_t image_size,
                                          uint32_t entry_offset,
                                          uint32_t initial_argument,
                                          uint32_t *process_id)
{
    KernelProcess *process;
    KernelPreparedThread prepared_thread;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;
    KernelHandle self_handle;
    KernelVmStatus vm_status;
    uint32_t generation;
    uint32_t code_physical = 0u;
    uint32_t data_physical = 0u;
    uint32_t initial_thread_id;
    KernelHandle initial_thread_handle;
    uint16_t saved_status;
    bool code_held = false;
    bool data_held = false;
    void *raw_process;
    uint16_t slot;
    KernelObjectCacheStatus cache_status;
    /* See prepare_thread: hoisted so no `goto failed` jumps over it. */
    uint8_t *code = NULL;

    if (image == NULL || image_size == 0u ||
        image_size > KERNEL_PROCESS_RAW_IMAGE_MAX ||
        entry_offset >= image_size || process_id == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *process_id = 0u;
    cache_status = kernel_object_cache_claim(
        &process_cache, 0u, &raw_process, &slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
        return KERNEL_PROCESS_NO_SLOT;
    if (cache_status != KERNEL_OBJECT_CACHE_OK ||
        slot >= KERNEL_PROCESS_MAX) {
        process_pool_corrupt = 1u;
        return KERNEL_PROCESS_CORRUPT;
    }
    process = raw_process;
    if (process->process_state != KERNEL_PROCESS_UNUSED &&
        process->process_state != KERNEL_PROCESS_DEAD) {
        process_pool_corrupt = 1u;
        return KERNEL_PROCESS_CORRUPT;
    }
    generation = kernel_generation_next(process->generation);
    kernel_bytes_clear(process, sizeof(*process));
    kernel_thread_wait_queue_init(&process->death_waiters);
    process->generation = generation;
    process->id = PROCESS_OWNER_PREFIX |
                  ((generation & 0x000fffffu) << 4) |
                  ((uint32_t)slot + 1u);
    process->owner = process->id;
    process->started_cycles = scheduler_cycles();
    process->image_size = image_size;
    process->entry_base = KERNEL_PROCESS_CODE_BASE;
    process->default_priority = KERNEL_THREAD_PRIORITY_NORMAL;
    process->priority_ceiling = KERNEL_THREAD_PRIORITY_USER_MAX;
    kernel_handle_table_init(&process->handles);
    if (!kernel_handle_table_set_owner(&process->handles, process->owner))
        goto failed;
    if (!kernel_memory_protect_owner(process->owner))
        goto failed;

    vm_status = kernel_vm_create_address_space(process->owner,
                                               &process->address_space);
    if (vm_status != KERNEL_VM_OK) {
        if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    /*
     * One page at a time: the blob is copied into the frame that will hold it
     * and mapped read-execute, so the image may be larger than a page without
     * anything else in here knowing how large.
     */
    for (uint32_t offset = 0u; offset < image_size;
         offset += KERNEL_PAGE_SIZE) {
        uint32_t chunk = image_size - offset;

        if (chunk > KERNEL_PAGE_SIZE)
            chunk = KERNEL_PAGE_SIZE;
        if (kernel_memory_alloc_zeroed_tagged(
                KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE, 1u, 1u,
                KERNEL_FRAME_PROCESS, process->owner, &code_physical) !=
            KERNEL_MEMORY_OK) {
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
            goto failed;
        }
        code_held = true;
        code = physical_bytes(code_physical, KERNEL_PAGE_SIZE);
        if (code == NULL)
            goto failed;
#if defined(KERNEL_PROCESS_HOST_TEST)
        /* Host memory tests disable allocator writes to synthetic addresses. */
        kernel_bytes_clear(code, KERNEL_PAGE_SIZE);
#endif
        kernel_bytes_copy(code, (const uint8_t *)image + offset, chunk);
        vm_status = kernel_vm_map_page(
            &process->address_space, KERNEL_PROCESS_CODE_BASE + offset,
            code_physical, KERNEL_VM_READ | KERNEL_VM_EXEC);
        if (vm_status != KERNEL_VM_OK) {
            if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
                result = KERNEL_PROCESS_OUT_OF_MEMORY;
            goto failed;
        }
        if (kernel_memory_release(code_physical, 1u, process->owner) !=
            KERNEL_MEMORY_OK)
            goto failed;
        code_held = false;
    }
    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE, 1u, 1u,
            KERNEL_FRAME_PROCESS, process->owner, &data_physical) !=
        KERNEL_MEMORY_OK) {
        result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    data_held = true;
    vm_status = kernel_vm_map_page(
        &process->address_space, KERNEL_PROCESS_DATA_BASE, data_physical,
        KERNEL_VM_READ | KERNEL_VM_WRITE);
    if (vm_status != KERNEL_VM_OK) {
        if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    if (kernel_memory_release(data_physical, 1u, process->owner) !=
        KERNEL_MEMORY_OK)
        goto failed;
    data_held = false;

    process->process_state = KERNEL_PROCESS_CREATED;
    process->handle_references = 1u;
    if (kernel_handle_install(
            &process->handles, KERNEL_OBJECT_PROCESS,
            self_handle_rights(), process, process_handle_release,
            NULL, &self_handle) != KERNEL_HANDLE_OK) {
        process->handle_references = 0u;
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
    process->self_handle = self_handle;
    result = prepare_thread(process, KERNEL_PROCESS_CODE_BASE + entry_offset,
                            initial_argument,
                            process->default_priority, KERNEL_THREAD_RIGHTS,
                            &prepared_thread);
    if (result != KERNEL_PROCESS_OK)
        goto failed;
    saved_status = kernel_interrupt_save_disable();
    result = commit_thread(&prepared_thread, &initial_thread_id,
                           &initial_thread_handle);
    if (result == KERNEL_PROCESS_OK) {
        ++scheduler_stats.created_processes;
        ++scheduler_stats.live_processes;
        *process_id = process->id;
    }
    kernel_interrupt_restore(saved_status);
    if (result != KERNEL_PROCESS_OK) {
        (void)abort_prepared_thread(&prepared_thread);
        goto failed;
    }
    (void)initial_thread_handle;
    return KERNEL_PROCESS_OK;

failed:
    if (code_held)
        (void)kernel_memory_release(code_physical, 1u, process->owner);
    if (data_held)
        (void)kernel_memory_release(data_physical, 1u, process->owner);
    (void)kernel_handle_close_all(&process->handles);
    if (process->address_space.initialized != 0u)
        (void)kernel_vm_destroy_address_space(&process->address_space);
    (void)kernel_memory_release_owner(process->owner, NULL);
    (void)kernel_memory_unprotect_owner(process->owner);
    process->process_state = KERNEL_PROCESS_DEAD;
    maybe_release_process_record(process);
    return result;
}

#define KERNEL_PROCESS_STARTUP_BASE KERNEL_VM_USER_MIN
#define KERNEL_PROCESS_LAUNCH_HEADER_BYTES 1024u
static uint8_t launch_page[KERNEL_PAGE_SIZE];
static uint8_t launch_header[KERNEL_PROCESS_LAUNCH_HEADER_BYTES];
static char syscall_data[ASTRA_STARTUP_BLOCK_SIZE];
static AstraStartupCapability
    exec_capabilities[ASTRA_STARTUP_CAPABILITY_MAX];

static KernelProcessStatus publish_page(KernelAddressSpace *space,
                                        uint32_t owner,
                                        uint32_t virtual_address,
                                        const uint8_t *source,
                                        uint32_t source_size,
                                        uint32_t rights);
static KernelProcessStatus map_segments(KernelAddressSpace *space,
                                        uint32_t owner,
                                        const KernelElfImage *plan,
                                        const uint8_t *image,
                                        uint32_t user_image,
                                        uint32_t virtual_base,
                                        bool rollback);
static KernelProcessStatus publish_startup_block(
    KernelAddressSpace *space, uint32_t owner, KernelHandle process_handle,
    KernelHandle thread_handle, const AstraStartupCapability *bootstrap,
    uint32_t bootstrap_count, const AstraLaunchArguments *arguments,
    const char *argument_bytes, const char *environment,
    uint32_t handoff_address, uint32_t handoff_size);
static KernelProcessStatus accept_executable(const void *image,
                                             uint32_t image_size,
                                             uint32_t user_image,
                                             KernelElfImage *plan);
static bool executable_span(const KernelElfImage *plan, uint32_t *base,
                            uint32_t *size);

static KernelProcessStatus read_exec_startup(
    const KernelProcess *process, AstraStartupInfo *info,
    uint32_t *bootstrap_count)
{
    uint32_t capability_bytes;

    if (process == NULL || info == NULL || bootstrap_count == NULL ||
        kernel_copy_from_user(info, KERNEL_PROCESS_STARTUP_BASE,
                              sizeof(*info)) != KERNEL_USER_COPY_OK ||
        info->magic != ASTRA_STARTUP_MAGIC ||
        info->abi_version != ASTRA_STARTUP_ABI_VERSION ||
        info->header_size != ASTRA_STARTUP_INFO_SIZE ||
        info->syscall_abi_version != ASTRA_SYSCALL_ABI_VERSION ||
        info->process_handle != process->self_handle ||
        info->capability_count < 2u ||
        info->capability_count > ASTRA_STARTUP_CAPABILITY_MAX ||
        info->capabilities_address !=
            KERNEL_PROCESS_STARTUP_BASE + ASTRA_STARTUP_INFO_SIZE ||
        info->launch_source > ASTRA_LAUNCH_SOURCE_DESKTOP)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    capability_bytes =
        info->capability_count * ASTRA_STARTUP_CAPABILITY_SIZE;
    if (ASTRA_STARTUP_INFO_SIZE + capability_bytes >
            ASTRA_STARTUP_BLOCK_SIZE ||
        kernel_copy_from_user(exec_capabilities,
                              info->capabilities_address,
                              capability_bytes) != KERNEL_USER_COPY_OK)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *bootstrap_count = info->capability_count - 2u;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus publish_exec_handoff(
    KernelAddressSpace *space, uint32_t owner, uint32_t source,
    uint32_t size, const KernelElfImage *plan, uint32_t *address)
{
    const uint32_t base = KERNEL_PROCESS_STARTUP_BASE + KERNEL_PAGE_SIZE;
    uint32_t first_segment = UINT32_MAX;
    uint32_t rounded;

    if (space == NULL || plan == NULL || address == NULL ||
        (source == 0u) != (size == 0u))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *address = 0u;
    if (size == 0u)
        return KERNEL_PROCESS_OK;
    if (source < KERNEL_VM_USER_MIN || source > KERNEL_VM_USER_MAX ||
        size - 1u > KERNEL_VM_USER_MAX - source)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < plan->segment_count; ++index)
        if (plan->segment[index].virtual_address < first_segment)
            first_segment = plan->segment[index].virtual_address;
    if (size > UINT32_MAX - (KERNEL_PAGE_SIZE - 1u))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    rounded = (size + KERNEL_PAGE_SIZE - 1u) & ~(KERNEL_PAGE_SIZE - 1u);
    if (first_segment <= base || rounded > first_segment - base)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    for (uint32_t offset = 0u; offset < rounded; offset += KERNEL_PAGE_SIZE) {
        uint32_t copied = size - offset;
        KernelProcessStatus status;

        if (copied > KERNEL_PAGE_SIZE)
            copied = KERNEL_PAGE_SIZE;
        kernel_bytes_clear(launch_page, sizeof(launch_page));
        if (kernel_copy_from_user(launch_page, source + offset, copied) !=
            KERNEL_USER_COPY_OK)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        status = publish_page(space, owner, base + offset, launch_page,
                              copied, KERNEL_VM_READ);
        if (status != KERNEL_PROCESS_OK)
            return status;
    }
    *address = base;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus replace_process_image(
    KernelProcess *process, KernelThread *thread, uint32_t user_image,
    uint32_t image_size, AstraExecRequest *request,
    KernelCpuContext **next_context)
{
    KernelAddressSpace replacement;
    KernelElfImage plan;
    AstraStartupInfo prior_startup;
    KernelProcessStatus status;
    uint32_t bootstrap_count = 0u;
    uint32_t handoff_address = 0u;
    uint32_t entry_base;
    uint32_t entry_size;
    uint32_t retired = 0u;
    uint32_t released_buffers = 0u;
    uint32_t deferred_buffers = 0u;
    uint32_t unmapped_areas = 0u;

    if (process == NULL || thread == NULL || request == NULL ||
        next_context == NULL || user_image == 0u || image_size == 0u ||
        request->size != ASTRA_EXEC_REQUEST_SIZE ||
        request->arguments.flags != 0u ||
        (request->handoff_address == 0u) !=
            (request->handoff_size == 0u))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    status = accept_executable(NULL, image_size, user_image, &plan);
    if (status != KERNEL_PROCESS_OK ||
        !executable_span(&plan, &entry_base, &entry_size))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    status = read_exec_startup(process, &prior_startup, &bootstrap_count);
    if (status != KERNEL_PROCESS_OK)
        return status;
    request->arguments.source = (uint16_t)prior_startup.launch_source;

    kernel_bytes_clear(&replacement, sizeof(replacement));
    if (kernel_vm_create_address_space(process->owner, &replacement) !=
        KERNEL_VM_OK)
        return KERNEL_PROCESS_OUT_OF_MEMORY;
    status = map_segments(&replacement, process->owner, &plan, NULL,
                          user_image, 0u, false);
    if (status != KERNEL_PROCESS_OK)
        goto failed;
    status = publish_exec_handoff(
        &replacement, process->owner, request->handoff_address,
        request->handoff_size, &plan, &handoff_address);
    if (status != KERNEL_PROCESS_OK)
        goto failed;
    status = publish_startup_block(
        &replacement, process->owner, process->self_handle,
        thread->self_handle, &exec_capabilities[2], bootstrap_count,
        &request->arguments,
        request->arguments.count != 0u ? syscall_data : NULL,
        request->arguments.environment_count != 0u ?
            syscall_data + request->arguments.length : NULL,
        handoff_address, request->handoff_size);
    if (status != KERNEL_PROCESS_OK)
        goto failed;
    status = publish_page(
        &replacement, process->owner,
        thread->user_stack_top - KERNEL_THREAD_STACK_SIZE, NULL, 0u,
        KERNEL_VM_READ | KERNEL_VM_WRITE);
    if (status != KERNEL_PROCESS_OK)
        goto failed;

    /* Everything below is the no-allocation commit. */
    if (!display_dma_abort_owner(process->owner) ||
        kernel_block_revoke_owner(process->owner, &released_buffers,
                                  &deferred_buffers) != KERNEL_BLOCK_OK ||
        deferred_buffers != 0u) {
        status = KERNEL_PROCESS_CORRUPT;
        goto failed;
    }
    (void)kernel_handle_close_type(&process->handles, KERNEL_OBJECT_DMA);
    if (process->dma_pages != 0u ||
        kernel_area_unmap_process(process->id, &unmapped_areas) !=
            KERNEL_AREA_OK ||
        kernel_thread_exec_retire_others(
            (uint16_t)(process - processes), thread,
            ASTRA_SYSCALL_PEER_DEAD, &retired) != KERNEL_THREAD_OK) {
        status = KERNEL_PROCESS_CORRUPT;
        goto failed;
    }
    (void)released_buffers;
    (void)unmapped_areas;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *other = kernel_thread_at(slot);

        if (other == NULL || other == thread ||
            other->process_slot != (uint16_t)(process - processes))
            continue;
        signal_context_active[slot] = 0u;
        if (other->self_handle != KERNEL_HANDLE_INVALID &&
            kernel_handle_close(&process->handles, other->self_handle) !=
                KERNEL_HANDLE_OK) {
            status = KERNEL_PROCESS_CORRUPT;
            goto failed;
        }
    }
    if (retired > process->live_threads ||
        retired > scheduler_stats.live_threads ||
        kernel_vm_exchange_address_spaces(&process->address_space,
                                           &replacement) != KERNEL_VM_OK ||
        kernel_vm_switch(&process->address_space) != KERNEL_VM_OK ||
        kernel_vm_destroy_address_space(&replacement) != KERNEL_VM_OK)
        return KERNEL_PROCESS_CORRUPT;

    process->live_threads = (uint8_t)(process->live_threads - retired);
    scheduler_stats.live_threads -= retired;
    scheduler_stats.dead_threads += retired;
    process->stack_slots = astra_u64_bit(thread->stack_slot);
    process->user_stack_pages = 1u;
    process->user_guard_pages = 1u;
    thread->user_stack_base = thread->user_stack_top - KERNEL_THREAD_STACK_SIZE;
    thread->stack_pages = 1u;
    thread->activity = 0u;
    kernel_context_initialize(&thread->context, plan.entry,
                              thread->user_stack_top);
    thread->context.data[2] = KERNEL_PROCESS_STARTUP_BASE;
    thread->context.data[4] = process->self_handle;
    thread->context.data[5] = thread->self_handle;
    process->entry_base = entry_base;
    process->image_size = entry_size;
    process->progress = 0u;
    process->fault_pc = 0u;
    process->fault_address = 0u;
    process->fault_vector = 0u;
    process->fault_status = 0u;
    process->signal_trampoline = 0u;
    process->signal_stack_top = 0u;
    process->signal_pending = 0u;
    process->signal_target_thread = 0u;
    process->interval_deadline = 0u;
    process->interval_period = 0u;
    signal_context_active[thread->slot] = 0u;
    scheduler_timer_rearm();
    *next_context = runtime_resume(thread);
    return KERNEL_PROCESS_OK;

failed:
    if (replacement.initialized != 0u &&
        kernel_vm_destroy_address_space(&replacement) != KERNEL_VM_OK)
        return KERNEL_PROCESS_CORRUPT;
    return status;
}

/*
 * Executable loading.
 *
 * The startup block sits at the bottom of the user range, below where the link
 * script places an image, and is mapped read-only: a process may read what it
 * was launched with but cannot rewrite its own provenance.
 */
/*
 * The ceiling on text, data and BSS a single process image may map is
 * KERNEL_PROCESS_IMAGE_PAGES_MAX, declared in process.h beside the rest of the
 * acceptance contract so the loader and its test cannot hold different numbers.
 */

static uint32_t segment_vm_rights(uint32_t elf_rights)
{
    uint32_t rights = KERNEL_VM_READ;

    if ((elf_rights & KERNEL_ELF_SEGMENT_WRITE) != 0u)
        rights |= KERNEL_VM_WRITE;
    if ((elf_rights & KERNEL_ELF_SEGMENT_EXEC) != 0u)
        rights |= KERNEL_VM_EXEC;
    return rights;
}

/*
 * Populate one page and publish it. The frame is allocated held, mapped, then
 * released: the mapping owns the reference from that point, and every frame
 * carries the process owner tag so a failure anywhere unwinds through
 * kernel_memory_release_owner() with the rest of the address space.
 */
static KernelProcessStatus publish_page(KernelAddressSpace *space,
                                        uint32_t owner,
                                        uint32_t virtual_address,
                                        const uint8_t *source,
                                        uint32_t source_size,
                                        uint32_t rights)
{
    uint32_t physical = 0u;
    uint8_t *bytes;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE, 1u, 1u,
            KERNEL_FRAME_PROCESS, owner, &physical) !=
        KERNEL_MEMORY_OK)
        return KERNEL_PROCESS_OUT_OF_MEMORY;

    bytes = physical_bytes(physical, KERNEL_PAGE_SIZE);
    if (bytes == NULL) {
        (void)kernel_memory_release(physical, 1u, owner);
        return KERNEL_PROCESS_CORRUPT;
    }
#if defined(KERNEL_PROCESS_HOST_TEST)
    kernel_bytes_clear(bytes, KERNEL_PAGE_SIZE);
#endif
    if (source != NULL && source_size != 0u)
        kernel_bytes_copy(bytes, source, source_size);

    if (kernel_vm_map_page(space, virtual_address, physical, rights) !=
        KERNEL_VM_OK) {
        (void)kernel_memory_release(physical, 1u, owner);
        return KERNEL_PROCESS_OUT_OF_MEMORY;
    }
    if (kernel_memory_release(physical, 1u, owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_PROCESS_CORRUPT;
    return KERNEL_PROCESS_OK;
}

/*
 * One page of an image on its way into a process, when the image is in another
 * process's memory rather than in the kernel's.
 *
 * A launcher hands over an address in its own address space, and the kernel
 * cannot map a segment straight out of it: reading it directly would be the
 * kernel dereferencing a user pointer, which is the one thing user_copy.c
 * exists to stop. So each page bounces, through a buffer bounded at exactly one
 * page. This is the third copy of a launched image -- the launcher's buffer,
 * this, and the frame -- and it is the one that is bounded rather than
 * proportional to the program.
 */
/*
 * The window the acceptance profile is allowed to read of a launched image. It
 * holds the ELF header and a program header table that follows it, which is
 * what every image this machine links produces.
 */
/* One syscall scratch page. Launch data and byte-ring transfers are serialized
 * on one CPU and each is bounded by this physical copy window. */

static uint32_t copy_launch_arguments(uint32_t user_address,
                                      AstraLaunchArguments *arguments)
{
    int copy_status;

    if (arguments == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    kernel_bytes_clear(arguments, sizeof(*arguments));
    kernel_bytes_clear(syscall_data, sizeof(syscall_data));
    if (user_address == 0u)
        return ASTRA_SYSCALL_OK;
    copy_status = kernel_copy_from_user(arguments, user_address,
                                        (uint32_t)sizeof(*arguments));
    if (copy_status != KERNEL_USER_COPY_OK)
        return ASTRA_SYSCALL_BAD_ADDRESS;
    if (arguments->count > ASTRA_LAUNCH_ARGUMENT_MAX ||
        arguments->length > ASTRA_LAUNCH_ARGUMENT_BYTES ||
        (arguments->count != 0u) != (arguments->length != 0u) ||
        (arguments->count != 0u) !=
            (arguments->argument_address != 0u) ||
        arguments->environment_count > ASTRA_LAUNCH_ENVIRONMENT_MAX ||
        arguments->environment_length > ASTRA_LAUNCH_ENVIRONMENT_BYTES ||
        (arguments->environment_count != 0u) !=
            (arguments->environment_length != 0u) ||
        (arguments->environment_count != 0u) !=
            (arguments->environment_address != 0u) ||
        arguments->source > ASTRA_LAUNCH_SOURCE_DESKTOP ||
        (arguments->flags & ~ASTRA_LAUNCH_FLAG_MASK) != 0u ||
        arguments->length + arguments->environment_length >
            (uint32_t)sizeof(syscall_data) ||
        (arguments->count == 0u &&
         arguments->environment_count == 0u &&
         arguments->source != ASTRA_LAUNCH_SOURCE_SYSTEM))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (arguments->count != 0u) {
        copy_status = kernel_copy_from_user(
            syscall_data, arguments->argument_address, arguments->length);
        if (copy_status != KERNEL_USER_COPY_OK)
            return ASTRA_SYSCALL_BAD_ADDRESS;
    }
    if (arguments->environment_count != 0u) {
        copy_status = kernel_copy_from_user(
            syscall_data + arguments->length,
            arguments->environment_address,
            arguments->environment_length);
        if (copy_status != KERNEL_USER_COPY_OK)
            return ASTRA_SYSCALL_BAD_ADDRESS;
    }
    return ASTRA_SYSCALL_OK;
}

static KernelProcessStatus map_segments(KernelAddressSpace *space,
                                        uint32_t owner,
                                        const KernelElfImage *plan,
                                        const uint8_t *image,
                                        uint32_t user_image,
                                        uint32_t virtual_base,
                                        bool rollback)
{
    uint32_t mapped = 0u;
    KernelProcessStatus failure = KERNEL_PROCESS_OK;

    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];
        uint32_t rights = segment_vm_rights(segment->rights);

        for (uint32_t page = 0u; page < segment->page_count; ++page) {
            uint32_t offset = page * KERNEL_PAGE_SIZE;
            uint32_t copy = 0u;
            const uint8_t *source = NULL;
            KernelProcessStatus status;

            /* Bytes past the file size are the segment's zero-filled tail. */
            if (offset < segment->file_size) {
                copy = segment->file_size - offset;
                if (copy > KERNEL_PAGE_SIZE)
                    copy = KERNEL_PAGE_SIZE;
            }
            if (copy != 0u) {
                if (user_image != 0u) {
                    int copy_status = kernel_copy_from_user(
                        launch_page,
                        user_image + segment->file_offset + offset, copy);

                    if (copy_status != KERNEL_USER_COPY_OK) {
                        failure = KERNEL_PROCESS_INVALID_ARGUMENT;
                        goto failed;
                    }
                    source = launch_page;
                } else {
                    source = image + segment->file_offset + offset;
                }
            }
            status = publish_page(space, owner,
                                  virtual_base + segment->virtual_address +
                                      offset,
                                  source, copy, rights);
            if (status != KERNEL_PROCESS_OK) {
                failure = status;
                goto failed;
            }
            ++mapped;
        }
    }
    return KERNEL_PROCESS_OK;

failed:
    if (!rollback)
        return failure;
    for (uint32_t index = 0u; index < plan->segment_count && mapped != 0u;
         ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        for (uint32_t page = 0u; page < segment->page_count && mapped != 0u;
             ++page) {
            uint32_t address = virtual_base + segment->virtual_address +
                               (page * KERNEL_PAGE_SIZE);

            if (kernel_vm_unmap_page(space, address) !=
                KERNEL_VM_OK)
                return KERNEL_PROCESS_CORRUPT;
            --mapped;
        }
    }
    return failure;
}

static bool library_name_valid(const char name[ASTRA_LIBRARY_NAME_MAX])
{
    for (uint32_t index = 0u; index < ASTRA_LIBRARY_NAME_MAX; ++index) {
        char value = name[index];

        if (value == '\0')
            return index != 0u;
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-'))
            return false;
    }
    return false;
}

static bool library_reference_valid(const AstraLibraryReference *reference)
{
    bool has_version;

    if (reference == NULL ||
        reference->size != ASTRA_LIBRARY_REFERENCE_SIZE ||
        reference->abi_major == 0u || !library_name_valid(reference->name))
        return false;
    has_version = reference->major != 0u || reference->minor != 0u ||
                  reference->patch != 0u;
    if (reference->flags == ASTRA_LIBRARY_REFERENCE_EXACT)
        return has_version;
    return reference->flags == ASTRA_LIBRARY_REFERENCE_LATEST &&
           !has_version && reference->build_id == 0u;
}

static bool library_reference_equal(const AstraLibraryReference *left,
                                    const AstraLibraryReference *right)
{
    return left->build_id == right->build_id &&
           left->major == right->major && left->minor == right->minor &&
           left->patch == right->patch &&
           left->abi_major == right->abi_major &&
           left->abi_minor == right->abi_minor &&
           kernel_bytes_equal(left->name, right->name,
                              ASTRA_LIBRARY_NAME_MAX);
}

static bool library_reference_newer(const AstraLibraryReference *left,
                                    const AstraLibraryReference *right)
{
    if (left->major != right->major)
        return left->major > right->major;
    if (left->minor != right->minor)
        return left->minor > right->minor;
    if (left->patch != right->patch)
        return left->patch > right->patch;
    return left->abi_minor > right->abi_minor;
}

static bool library_reference_matches_request(
    const AstraLibraryReference *cached, const AstraLibraryReference *request)
{
    /*
     * ponytail: installed Kits are immutable for one boot. Add an installer
     * generation/invalidation message when live Kit replacement exists.
     */
    if (request->flags == ASTRA_LIBRARY_REFERENCE_EXACT)
        return library_reference_equal(cached, request);
    return cached->abi_major == request->abi_major &&
           cached->abi_minor >= request->abi_minor &&
           kernel_bytes_equal(cached->name, request->name,
                              ASTRA_LIBRARY_NAME_MAX);
}

#if defined(KERNEL_PROCESS_HOST_TEST)
bool kernel_process_test_library_reference_selection(void)
{
    AstraLibraryReference older = {
        .size = ASTRA_LIBRARY_REFERENCE_SIZE,
        .build_id = 1u,
        .name = "filesystem.library",
        .major = 1u,
        .abi_major = 1u,
    };
    AstraLibraryReference newer = older;
    AstraLibraryReference request = {
        .size = ASTRA_LIBRARY_REFERENCE_SIZE,
        .name = "filesystem.library",
        .abi_major = 1u,
        .flags = ASTRA_LIBRARY_REFERENCE_LATEST,
    };

    newer.minor = 1u;
    newer.build_id = 2u;
    return library_reference_valid(&request) &&
           library_reference_matches_request(&older, &request) &&
           library_reference_matches_request(&newer, &request) &&
           library_reference_newer(&newer, &older) &&
           !library_reference_newer(&older, &newer) &&
           library_reference_valid(&older);
}
#endif

static bool library_reference_from_image(const uint8_t *image,
                                         uint32_t readable,
                                         AstraLibraryReference *reference)
{
    const uint8_t *record;

    if (image == NULL || reference == NULL ||
        readable < ASTRA_LIBRARY_FILE_OFFSET + ASTRA_LIBRARY_SIZE)
        return false;
    record = image + ASTRA_LIBRARY_FILE_OFFSET;
    if (astra_load_be32(record) != ASTRA_LIBRARY_MAGIC ||
        astra_load_be16(record + 4u) != ASTRA_LIBRARY_RECORD_VERSION ||
        astra_load_be16(record + 6u) != ASTRA_LIBRARY_SIZE ||
        astra_load_be16(record + 18u) != 0u ||
        astra_load_be32(record + 20u) != ASTRA_LIBRARY_TARGET_M68030 ||
        astra_load_be32(record + 28u) != ASTRA_LIBRARY_EXPORTS_OFFSET)
        return false;
    kernel_bytes_clear(reference, sizeof(*reference));
    reference->size = ASTRA_LIBRARY_REFERENCE_SIZE;
    reference->major = astra_load_be16(record + 8u);
    reference->minor = astra_load_be16(record + 10u);
    reference->patch = astra_load_be16(record + 12u);
    reference->abi_major = astra_load_be16(record + 14u);
    reference->abi_minor = astra_load_be16(record + 16u);
    reference->build_id = astra_load_be32(record + 24u);
    kernel_bytes_copy(reference->name, record + 32u,
                      ASTRA_LIBRARY_NAME_MAX);
    return library_reference_valid(reference);
}

static KernelProcessStatus read_library_page(
    uint32_t user_image, const KernelElfSegment *segment, uint32_t page,
    uint32_t *copied)
{
    uint32_t offset = page * KERNEL_PAGE_SIZE;

    kernel_bytes_clear(launch_page, sizeof(launch_page));
    *copied = 0u;
    if (offset >= segment->file_size)
        return KERNEL_PROCESS_OK;
    *copied = segment->file_size - offset;
    if (*copied > KERNEL_PAGE_SIZE)
        *copied = KERNEL_PAGE_SIZE;
    if (kernel_copy_from_user(launch_page,
                              user_image + segment->file_offset + offset,
                              *copied) != KERNEL_USER_COPY_OK)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus library_cache_match(
    const KernelLibraryCacheEntry *entry, const KernelElfImage *plan,
    const AstraLibraryReference *reference, uint32_t user_image, bool *matches)
{
    uint32_t flattened = 0u;

    *matches = false;
    if (entry->used == 0u || !library_reference_equal(&entry->reference,
                                                       reference) ||
        !kernel_bytes_equal(&entry->plan, plan, sizeof(*plan)))
        return KERNEL_PROCESS_OK;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        for (uint32_t page = 0u; page < segment->page_count;
             ++page, ++flattened) {
            uint32_t copied;
            uint8_t *cached;
            KernelProcessStatus status;

            status = read_library_page(user_image, segment, page, &copied);
            if (status != KERNEL_PROCESS_OK)
                return status;
            (void)copied;
            cached = physical_bytes(entry->physical_pages[flattened],
                                    KERNEL_PAGE_SIZE);
            if (cached == NULL)
                return KERNEL_PROCESS_CORRUPT;
            if (!kernel_bytes_equal(cached, launch_page, KERNEL_PAGE_SIZE))
                return KERNEL_PROCESS_OK;
        }
    }
    *matches = true;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus library_cache_create(
    KernelLibraryCacheEntry *entry, uint32_t slot,
    const KernelElfImage *plan, const AstraLibraryReference *reference,
    uint32_t user_image, uint32_t span)
{
    uint32_t flattened = 0u;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;

    kernel_bytes_clear(entry, sizeof(*entry));
    entry->owner = LIBRARY_OWNER_PREFIX | (slot + 1u);
    entry->span = span;
    kernel_bytes_copy(&entry->plan, plan, sizeof(*plan));
    kernel_bytes_copy(&entry->reference, reference, sizeof(*reference));
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        for (uint32_t page = 0u; page < segment->page_count;
             ++page, ++flattened) {
            uint32_t copied;
            uint32_t physical;
            uint8_t *target;

            result = read_library_page(user_image, segment, page, &copied);
            if (result != KERNEL_PROCESS_OK)
                goto failed;
            (void)copied;
            if (kernel_memory_alloc_zeroed_tagged(
                    KERNEL_ALLOCATION_SITE_LIBRARY_PAGE, 1u, 1u,
                    KERNEL_FRAME_SHARED, entry->owner, &physical) !=
                KERNEL_MEMORY_OK) {
                result = KERNEL_PROCESS_OUT_OF_MEMORY;
                goto failed;
            }
            entry->physical_pages[flattened] = physical;
            target = physical_bytes(physical, KERNEL_PAGE_SIZE);
            if (target == NULL) {
                result = KERNEL_PROCESS_CORRUPT;
                goto failed;
            }
#if defined(KERNEL_PROCESS_HOST_TEST)
            kernel_bytes_clear(target, KERNEL_PAGE_SIZE);
#endif
            kernel_bytes_copy(target, launch_page, KERNEL_PAGE_SIZE);
        }
    }
    entry->used = 1u;
    return KERNEL_PROCESS_OK;

failed:
    for (uint32_t page = 0u; page < LIBRARY_PAGE_MAX; ++page) {
        if (entry->physical_pages[page] != 0u &&
            kernel_memory_release(entry->physical_pages[page], 1u,
                                  entry->owner) != KERNEL_MEMORY_OK)
            result = KERNEL_PROCESS_CORRUPT;
    }
    kernel_bytes_clear(entry, sizeof(*entry));
    return result;
}

static KernelProcessStatus library_cache_reclaim(uint32_t *free_slot)
{
    for (uint32_t index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        bool idle = library_cache[index].used != 0u;

        if (!idle)
            continue;
        for (uint32_t page = 0u; page < LIBRARY_PAGE_MAX; ++page) {
            KernelFrameInfo info;
            uint32_t physical = library_cache[index].physical_pages[page];

            if (physical == 0u)
                continue;
            if (!kernel_memory_frame_info(physical, &info))
                return KERNEL_PROCESS_CORRUPT;
            if (info.references != 1u) {
                idle = false;
                break;
            }
        }
        if (!idle)
            continue;
        for (uint32_t page = 0u; page < LIBRARY_PAGE_MAX; ++page) {
            uint32_t physical = library_cache[index].physical_pages[page];

            if (physical != 0u &&
                kernel_memory_release(physical, 1u,
                                      library_cache[index].owner) !=
                    KERNEL_MEMORY_OK)
                return KERNEL_PROCESS_CORRUPT;
        }
        kernel_bytes_clear(&library_cache[index],
                           sizeof(library_cache[index]));
        *free_slot = index;
        return KERNEL_PROCESS_OK;
    }
    return KERNEL_PROCESS_RESOURCE_LIMIT;
}

#if defined(KERNEL_PROCESS_HOST_TEST)
bool kernel_process_test_library_cache_reclaims_after_last_mapping(void)
{
    KernelFrameInfo info;
    uint32_t physical;
    uint32_t slot = ASTRA_LIBRARY_SLOT_COUNT;
    uint32_t owner = LIBRARY_OWNER_PREFIX | 1u;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_LIBRARY_PAGE, 1u, 1u,
            KERNEL_FRAME_SHARED, owner, &physical) != KERNEL_MEMORY_OK)
        return false;
    library_cache[0].used = 1u;
    library_cache[0].owner = owner;
    library_cache[0].physical_pages[0] = physical;
    if (kernel_memory_retain(physical, 1u, owner) != KERNEL_MEMORY_OK ||
        library_cache_reclaim(&slot) != KERNEL_PROCESS_RESOURCE_LIMIT ||
        slot != ASTRA_LIBRARY_SLOT_COUNT ||
        kernel_memory_release(physical, 1u, owner) != KERNEL_MEMORY_OK ||
        library_cache_reclaim(&slot) != KERNEL_PROCESS_OK || slot != 0u ||
        library_cache[0].used != 0u ||
        !kernel_memory_frame_info(physical, &info))
        return false;
    return info.references == 0u && info.state == KERNEL_FRAME_FREE;
}
#endif

static KernelProcessStatus library_cache_get(
    const KernelElfImage *plan, const AstraLibraryReference *reference,
    uint32_t user_image, uint32_t span, KernelLibraryCacheEntry **cached,
    uint32_t *slot)
{
    uint32_t free_slot = ASTRA_LIBRARY_SLOT_COUNT;

    for (uint32_t index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        bool matches;
        KernelProcessStatus status;

        if (library_cache[index].used == 0u) {
            if (free_slot == ASTRA_LIBRARY_SLOT_COUNT)
                free_slot = index;
            continue;
        }
        status = library_cache_match(&library_cache[index], plan, reference,
                                     user_image, &matches);
        if (status != KERNEL_PROCESS_OK)
            return status;
        if (matches) {
            *cached = &library_cache[index];
            *slot = index;
            return KERNEL_PROCESS_OK;
        }
    }
    if (free_slot == ASTRA_LIBRARY_SLOT_COUNT) {
        /*
         * A dead process cannot pin a library forever: its VM teardown drops
         * every mapping reference, leaving only this cache's reference. First
         * eligible is enough for fifteen slots; add LRU only if measured churn
         * makes the choice matter.
         */
        KernelProcessStatus reclaim = library_cache_reclaim(&free_slot);

        if (reclaim != KERNEL_PROCESS_OK &&
            reclaim != KERNEL_PROCESS_RESOURCE_LIMIT)
            return reclaim;
    }
    if (free_slot == ASTRA_LIBRARY_SLOT_COUNT)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    {
        KernelProcessStatus status = library_cache_create(
            &library_cache[free_slot], free_slot, plan, reference, user_image,
            span);

        if (status != KERNEL_PROCESS_OK)
            return status;
    }
    *cached = &library_cache[free_slot];
    *slot = free_slot;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus map_cached_library(
    KernelProcess *process, KernelLibraryCacheEntry *cached, uint32_t slot,
    uint32_t *mapped_base, uint32_t *mapped_span)
{
    const KernelElfImage *plan = &cached->plan;
    uint32_t virtual_base = ASTRA_LIBRARY_BASE +
                            (slot * ASTRA_LIBRARY_SLOT_SIZE);
    uint32_t flattened = 0u;
    uint32_t mapped = 0u;
    KernelProcessStatus failure = KERNEL_PROCESS_CORRUPT;

    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];
        uint32_t rights = segment_vm_rights(segment->rights);

        if ((segment->rights & KERNEL_ELF_SEGMENT_WRITE) == 0u) {
            KernelVmStatus vm_status = kernel_vm_map_shared_range(
                &process->address_space,
                virtual_base + segment->virtual_address,
                &cached->physical_pages[flattened], segment->page_count,
                cached->owner, rights);

            if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
                failure = KERNEL_PROCESS_OUT_OF_MEMORY;
            else if (vm_status == KERNEL_VM_ALREADY_MAPPED)
                failure = KERNEL_PROCESS_INVALID_ARGUMENT;
            else if (vm_status != KERNEL_VM_OK)
                failure = KERNEL_PROCESS_CORRUPT;
            if (vm_status != KERNEL_VM_OK)
                goto failed;
            flattened += segment->page_count;
            mapped += segment->page_count;
            continue;
        }
        for (uint32_t page = 0u; page < segment->page_count;
             ++page, ++flattened) {
            uint32_t address = virtual_base + segment->virtual_address +
                               (page * KERNEL_PAGE_SIZE);

            const uint8_t *source = physical_bytes(
                cached->physical_pages[flattened], KERNEL_PAGE_SIZE);

            if (source == NULL)
                goto failed;
            failure = publish_page(&process->address_space, process->owner,
                                   address, source,
                                   KERNEL_PAGE_SIZE, rights);
            if (failure != KERNEL_PROCESS_OK)
                goto failed;
            ++mapped;
        }
    }
    *mapped_base = virtual_base;
    *mapped_span = cached->span;
    return KERNEL_PROCESS_OK;

failed:
    for (uint32_t index = 0u; index < plan->segment_count && mapped != 0u;
         ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        for (uint32_t page = 0u; page < segment->page_count && mapped != 0u;
             ++page) {
            uint32_t address = virtual_base + segment->virtual_address +
                               (page * KERNEL_PAGE_SIZE);

            if (kernel_vm_unmap_page(&process->address_space, address) !=
                KERNEL_VM_OK)
                return KERNEL_PROCESS_CORRUPT;
            --mapped;
        }
    }
    return failure;
}

static KernelProcessStatus map_library(KernelProcess *process,
                                       uint32_t user_image,
                                       uint32_t image_size,
                                       uint32_t *mapped_base,
                                       uint32_t *mapped_span)
{
    static const KernelElfLimits limits = {
        .minimum_address = 0u,
        .maximum_address = ASTRA_LIBRARY_SLOT_SIZE - 1u,
        .maximum_pages = ASTRA_LIBRARY_IMAGE_MAX / KERNEL_PAGE_SIZE,
        .page_size = KERNEL_PAGE_SIZE,
    };
    KernelElfImage plan;
    AstraLibraryReference reference;
    KernelLibraryCacheEntry *cached;
    uint32_t window;
    uint32_t span = 0u;
    uint32_t slot;
    KernelProcessStatus failure;

    if (process == NULL || user_image == 0u || image_size == 0u ||
        image_size > ASTRA_LIBRARY_IMAGE_MAX ||
        user_image > 0xffffffffu - image_size || mapped_base == NULL ||
        mapped_span == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;

    window = image_size < KERNEL_PROCESS_LAUNCH_HEADER_BYTES ?
        image_size : KERNEL_PROCESS_LAUNCH_HEADER_BYTES;
    kernel_bytes_clear(launch_header, sizeof(launch_header));
    if (kernel_copy_from_user(launch_header, user_image, window) !=
        KERNEL_USER_COPY_OK)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (kernel_elf_accept_library_windowed(launch_header, image_size, window,
                                           &limits, &plan) != KERNEL_ELF_OK)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (!library_reference_from_image(launch_header, window, &reference))
        return KERNEL_PROCESS_INVALID_ARGUMENT;

    for (uint32_t index = 0u; index < plan.segment_count; ++index) {
        uint32_t end = plan.segment[index].virtual_address +
                       (plan.segment[index].page_count * KERNEL_PAGE_SIZE);

        if (end > span)
            span = end;
    }
    if (span == 0u || span > ASTRA_LIBRARY_SLOT_SIZE)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    failure = library_cache_get(&plan, &reference, user_image, span, &cached,
                                &slot);
    if (failure != KERNEL_PROCESS_OK)
        return failure;
    return map_cached_library(process, cached, slot, mapped_base, mapped_span);
}

/*
 * The startup block is the only thing a new process is told about itself. Its
 * capability table publishes handles that were installed into the process
 * before it ran; destroying the block does not close them, and normal handle
 * lifetime rules release them.
 */
#define STARTUP_CAPABILITY_TOTAL_MAX \
    (2u + KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX)

/*
 * Grants the objects the launcher named, in the process that is being built.
 * Nothing is published yet: on failure the caller's unwind closes the handle
 * table, which releases every lease and endpoint installed here.
 */
static KernelProcessStatus grant_bootstrap_capabilities(
    KernelProcess *process, const KernelHandleTable *source_table,
    const KernelProcessBootstrapCapability *requested, uint32_t count,
    AstraStartupCapability *granted)
{
    for (uint32_t index = 0u; index < count; ++index) {
        const KernelProcessBootstrapCapability *entry = &requested[index];
        KernelHandle handle = KERNEL_HANDLE_INVALID;
        KernelProcessStatus status;

        if (entry->name == NULL || entry->name[0] == '\0' ||
            entry->rights == 0u)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        switch (entry->kind) {
        case KERNEL_PROCESS_BOOTSTRAP_DEVICE:
            status = kernel_process_grant_device(process->id, entry->device_id,
                                                 entry->rights, &handle);
            break;
        case KERNEL_PROCESS_BOOTSTRAP_IRQ: {
            KernelIrqBinding binding;

            if (!kernel_interrupt_device_binding(entry->irq_source, &binding))
                return KERNEL_PROCESS_INVALID_ARGUMENT;
            status = kernel_process_grant_irq(process->id, &binding,
                                              entry->rights, &handle);
            break;
        }
        case KERNEL_PROCESS_BOOTSTRAP_HANDLE: {
            /*
             * A launch creates no authority. The copy refuses a source that
             * does not carry TRANSFER and refuses rights that are not a subset
             * of it, so a caller can only hand over what it already holds, and
             * only narrowed. A handle it does not hold does not resolve at all.
             */
            KernelHandleStatus handle_status;
            uint32_t prior;

            if (source_table == NULL)
                return KERNEL_PROCESS_INVALID_ARGUMENT;
            for (prior = 0u; prior < index; ++prior) {
                if (requested[prior].kind ==
                        KERNEL_PROCESS_BOOTSTRAP_HANDLE &&
                    requested[prior].source_handle == entry->source_handle &&
                    requested[prior].rights == entry->rights) {
                    handle = granted[prior].handle;
                    break;
                }
            }
            if (prior != index) {
                status = KERNEL_PROCESS_OK;
                break;
            }
            handle_status = kernel_handle_duplicate_into(
                source_table, entry->source_handle, entry->rights,
                &process->handles, &handle);
            /*
             * Two different mistakes, told apart: a handle the caller does not
             * hold at all, and rights it holds less of than it asked to give.
             * A person reading a refused launch needs to know which, because
             * one is a bug in what was passed and the other is a bug in what
             * was expected.
             */
            if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
                handle_status == KERNEL_HANDLE_TYPE_MISMATCH)
                return KERNEL_PROCESS_INVALID_HANDLE;
            if (handle_status == KERNEL_HANDLE_INVALID_ARGUMENT)
                return KERNEL_PROCESS_INVALID_ARGUMENT;
            if (handle_status == KERNEL_HANDLE_ACCESS_DENIED)
                return KERNEL_PROCESS_ACCESS_DENIED;
            if (handle_status != KERNEL_HANDLE_OK)
                return KERNEL_PROCESS_NO_SLOT;
            status = KERNEL_PROCESS_OK;
            break;
        }
        default:
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        }
        if (status != KERNEL_PROCESS_OK)
            return status;
        if (handle == KERNEL_HANDLE_INVALID)
            return KERNEL_PROCESS_CORRUPT;
        astra_capability_name_set(granted[index].name, entry->name);
        granted[index].handle = handle;
        granted[index].rights = entry->rights;
        /*
         * Carried across, never interpreted. What a grant is for -- a name in
         * the child's namespace, a place to write -- is the launcher's
         * statement to the child, and a kernel that read it would be deciding
         * what a name means.
         */
        granted[index].flags = entry->flags;
        astra_capability_root_set(granted[index].root, entry->root);
    }
    return KERNEL_PROCESS_OK;
}

/*
 * The argument vector, after the capability table in the same page.
 *
 * A vector of addresses and then the bytes they point at, both inside the one
 * page the child already maps read-only. Nothing is copied a second time and
 * nothing needs an allocator before `main`, which is what makes an argument
 * cheap enough that a program can have several.
 *
 * Returns the bytes appended, or 0 when there is nothing to append. `*vector`
 * is where the child will find the addresses.
 */
static uint32_t append_vector(uint8_t *page, uint32_t at, uint32_t capacity,
                              const char *bytes, uint32_t byte_count,
                              uint32_t word_count, int environment,
                              uint32_t *vector, uint32_t *count)
{
    uint32_t vector_at = at;
    uint32_t text_at;
    uint32_t consumed = 0u;
    uint32_t written = 0u;

    *vector = 0u;
    *count = 0u;
    if (word_count == 0u)
        return 0u;
    text_at = vector_at + ((word_count + 1u) * 4u);
    if (text_at + byte_count > capacity)
        return 0u;
    for (uint32_t index = 0u; index < word_count; ++index) {
        uint32_t address = KERNEL_PROCESS_STARTUP_BASE + text_at + written;
        uint32_t length = 0u;
        uint32_t equals = UINT32_MAX;

        while (consumed + length < byte_count &&
               bytes[consumed + length] != '\0') {
            if (bytes[consumed + length] == '=' && equals == UINT32_MAX)
                equals = length;
            ++length;
        }
        if (consumed + length >= byte_count ||
            (environment && (equals == UINT32_MAX || equals == 0u))) {
            /* A count that promises more words than the bytes hold. */
            return 0u;
        }
        page[vector_at + (index * 4u)] = (uint8_t)(address >> 24);
        page[vector_at + (index * 4u) + 1u] = (uint8_t)(address >> 16);
        page[vector_at + (index * 4u) + 2u] = (uint8_t)(address >> 8);
        page[vector_at + (index * 4u) + 3u] = (uint8_t)address;
        for (uint32_t byte = 0u; byte <= length; ++byte)
            page[text_at + written + byte] = (uint8_t)bytes[consumed + byte];
        written += length + 1u;
        consumed += length + 1u;
    }
    if (consumed != byte_count)
        return 0u;
    *vector = KERNEL_PROCESS_STARTUP_BASE + vector_at;
    *count = word_count;
    return ((word_count + 1u) * 4u) + written;
}

static KernelProcessStatus publish_startup_block(
    KernelAddressSpace *space, uint32_t owner, KernelHandle process_handle,
    KernelHandle thread_handle, const AstraStartupCapability *bootstrap,
    uint32_t bootstrap_count, const AstraLaunchArguments *arguments,
    const char *argument_bytes, const char *environment,
    uint32_t handoff_address, uint32_t handoff_size)
{
    /*
     * _Alignas(4): AstraStartupCapability's widest member is a uint32_t, and
     * ASTRA_STARTUP_INFO_SIZE is itself a multiple of 4, so an aligned page
     * keeps the cast below aligned too. Without this the array is a plain
     * uint8_t[] with no alignment guarantee, and the cast through (void *)
     * would silence -Wcast-align without making the access safe -- on a
     * 68030 a misaligned access faults rather than merely costing cycles.
     */
    _Alignas(4) uint8_t page[ASTRA_STARTUP_BLOCK_SIZE];
    AstraStartupInfo info;
    /*
     * Written straight into the page. A second array of these used to sit on
     * this frame and be copied in whole; at 92 bytes a record that is 920
     * bytes of an 8 KiB supervisor stack, under a syscall frame that is now
     * carrying ten grants of its own.
     */
    AstraStartupCapability *capability =
        (AstraStartupCapability *)(void *)(page + ASTRA_STARTUP_INFO_SIZE);
    uint32_t count = 2u + bootstrap_count;
    uint32_t published_bytes;

    if (bootstrap_count > KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    kernel_bytes_clear(&info, sizeof(info));
    kernel_bytes_clear(page, sizeof(page));
    published_bytes = ASTRA_STARTUP_INFO_SIZE +
                      (count * ASTRA_STARTUP_CAPABILITY_SIZE);

    info.magic = ASTRA_STARTUP_MAGIC;
    info.abi_version = ASTRA_STARTUP_ABI_VERSION;
    info.header_size = ASTRA_STARTUP_INFO_SIZE;
    info.total_size = published_bytes;
    info.syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION;
    info.process_handle = process_handle;
    info.thread_handle = thread_handle;
    info.launch_source = arguments != NULL ? arguments->source :
                                             ASTRA_LAUNCH_SOURCE_SYSTEM;
    info.capability_count = count;
    info.capabilities_address =
        KERNEL_PROCESS_STARTUP_BASE + ASTRA_STARTUP_INFO_SIZE;
    info.handoff_address = handoff_address;
    info.handoff_size = handoff_size;

    astra_capability_name_set(capability[0].name,
                              ASTRA_CAPABILITY_PROCESS);
    capability[0].handle = process_handle;
    capability[0].rights = self_handle_rights();
    astra_capability_name_set(capability[1].name,
                              ASTRA_CAPABILITY_THREAD);
    capability[1].handle = thread_handle;
    capability[1].rights = KERNEL_THREAD_RIGHTS;
    /* Freestanding: a struct assignment here would call libc memcpy. */
    if (bootstrap_count != 0u)
        kernel_bytes_copy(&capability[2], bootstrap,
                          bootstrap_count * sizeof(capability[0]));

    if (arguments != NULL && arguments->count != 0u) {
        uint32_t appended = append_vector(
            page, published_bytes, (uint32_t)sizeof(page), argument_bytes,
            arguments->length, arguments->count, 0, &info.argv_address,
            &info.argc);

        if (appended == 0u)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        published_bytes += appended;
        info.total_size = published_bytes;
    }
    if (arguments != NULL && arguments->environment_count != 0u) {
        uint32_t appended = append_vector(
            page, published_bytes, (uint32_t)sizeof(page), environment,
            arguments->environment_length, arguments->environment_count, 1,
            &info.environment_address, &info.environment_count);

        if (appended == 0u)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        published_bytes += appended;
        info.total_size = published_bytes;
    }
    kernel_bytes_copy(page, &info, sizeof(info));

    return publish_page(space, owner, KERNEL_PROCESS_STARTUP_BASE, page,
                        published_bytes, KERNEL_VM_READ);
}

static const KernelElfLimits executable_limits = {
    .minimum_address = KERNEL_VM_USER_MIN + KERNEL_PAGE_SIZE,
    .maximum_address = KERNEL_PROCESS_STACK_BASE - 1u,
    .maximum_pages = KERNEL_PROCESS_IMAGE_PAGES_MAX,
    .page_size = KERNEL_PAGE_SIZE,
};

static KernelProcessStatus accept_executable(const void *image,
                                             uint32_t image_size,
                                             uint32_t user_image,
                                             KernelElfImage *plan)
{
    if (plan == NULL || (image == NULL) == (user_image == 0u) ||
        image_size == 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (user_image != 0u) {
        uint32_t window = image_size < KERNEL_PROCESS_LAUNCH_HEADER_BYTES ?
            image_size : KERNEL_PROCESS_LAUNCH_HEADER_BYTES;

        kernel_bytes_clear(launch_header, sizeof(launch_header));
        if (kernel_copy_from_user(launch_header, user_image, window) !=
                KERNEL_USER_COPY_OK ||
            kernel_elf_accept_windowed(launch_header, image_size, window,
                                       &executable_limits, plan) !=
                KERNEL_ELF_OK)
            return KERNEL_PROCESS_INVALID_ARGUMENT;
        return KERNEL_PROCESS_OK;
    }
    return kernel_elf_accept(image, image_size, &executable_limits, plan) ==
               KERNEL_ELF_OK ?
        KERNEL_PROCESS_OK : KERNEL_PROCESS_INVALID_ARGUMENT;
}

static bool executable_span(const KernelElfImage *plan, uint32_t *base,
                            uint32_t *size)
{
    if (plan == NULL || base == NULL || size == NULL)
        return false;
    *base = 0u;
    *size = 0u;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        if ((segment->rights & KERNEL_ELF_SEGMENT_EXEC) != 0u &&
            plan->entry >= segment->virtual_address &&
            plan->entry < segment->virtual_address + segment->memory_size) {
            *base = segment->virtual_address;
            *size = segment->memory_size;
            return true;
        }
    }
    return false;
}

KernelProcessStatus kernel_process_create_executable(
    const void *image, uint32_t image_size,
    const KernelProcessBootstrapCapability *capabilities,
    uint32_t capability_count, uint32_t *process_id)
{
    /* The firmware's case: no launcher, so no handle to copy and no argv. */
    return kernel_process_launch(image, image_size, 0u, NULL, capabilities,
                                 capability_count, NULL, NULL, NULL,
                                 process_id);
}

KernelProcessStatus kernel_process_launch(
    const void *image, uint32_t image_size, uint32_t user_image,
    const KernelHandleTable *source_table,
    const KernelProcessBootstrapCapability *capabilities,
    uint32_t capability_count, const AstraLaunchArguments *arguments,
    const char *argument_bytes, const char *environment,
    uint32_t *process_id)
{
    AstraStartupCapability granted[KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX];
    KernelElfImage plan;
    KernelProcess *process;
    KernelPreparedThread prepared_thread;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;
    KernelHandle self_handle;
    KernelVmStatus vm_status;
    uint32_t generation;
    uint32_t initial_thread_id;
    KernelHandle initial_thread_handle;
    uint16_t saved_status;
    void *raw_process;
    uint16_t slot;
    KernelObjectCacheStatus cache_status;

    if ((image == NULL) == (user_image == 0u) || process_id == NULL ||
        capability_count > KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX ||
        (capability_count != 0u && capabilities == NULL) ||
        (arguments != NULL &&
         ((arguments->count != 0u && argument_bytes == NULL) ||
          (arguments->environment_count != 0u && environment == NULL))))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    kernel_bytes_clear(granted, sizeof(granted));
    *process_id = 0u;
    if (accept_executable(image, image_size, user_image, &plan) !=
        KERNEL_PROCESS_OK) {
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    }

    cache_status = kernel_object_cache_claim(&process_cache, 0u, &raw_process,
                                             &slot);
    if (cache_status == KERNEL_OBJECT_CACHE_UNAVAILABLE)
        return KERNEL_PROCESS_NO_SLOT;
    if (cache_status != KERNEL_OBJECT_CACHE_OK || slot >= KERNEL_PROCESS_MAX) {
        process_pool_corrupt = 1u;
        return KERNEL_PROCESS_CORRUPT;
    }
    process = raw_process;
    if (process->process_state != KERNEL_PROCESS_UNUSED &&
        process->process_state != KERNEL_PROCESS_DEAD) {
        process_pool_corrupt = 1u;
        return KERNEL_PROCESS_CORRUPT;
    }

    generation = kernel_generation_next(process->generation);
    kernel_bytes_clear(process, sizeof(*process));
    kernel_thread_wait_queue_init(&process->death_waiters);
    process->generation = generation;
    process->id = PROCESS_OWNER_PREFIX |
                  ((generation & 0x000fffffu) << 4) | ((uint32_t)slot + 1u);
    process->owner = process->id;
    process->started_cycles = scheduler_cycles();
    process->default_priority = KERNEL_THREAD_PRIORITY_NORMAL;
    process->priority_ceiling =
        source_table == NULL ||
                (arguments != NULL &&
                 (arguments->flags & ASTRA_LAUNCH_FLAG_ESSENTIAL) != 0u) ?
            KERNEL_THREAD_PRIORITY_USER_MAX :
            KERNEL_THREAD_PRIORITY_NORMAL;

    if (!executable_span(&plan, &process->entry_base, &process->image_size)) {
        result = KERNEL_PROCESS_INVALID_ARGUMENT;
        goto failed;
    }

    kernel_handle_table_init(&process->handles);
    if (!kernel_handle_table_set_owner(&process->handles, process->owner))
        goto failed;
    if (process->priority_ceiling == KERNEL_THREAD_PRIORITY_USER_MAX &&
        !kernel_memory_protect_owner(process->owner))
        goto failed;

    vm_status = kernel_vm_create_address_space(process->owner,
                                               &process->address_space);
    if (vm_status != KERNEL_VM_OK) {
        if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }

    result = map_segments(&process->address_space, process->owner, &plan,
                          image, user_image, 0u, false);
    if (result != KERNEL_PROCESS_OK)
        goto failed;

    process->process_state = KERNEL_PROCESS_CREATED;
    process->handle_references = 1u;
    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_PROCESS,
                              self_handle_rights(), process,
                              process_handle_release, NULL,
                              &self_handle) != KERNEL_HANDLE_OK) {
        process->handle_references = 0u;
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
    process->self_handle = self_handle;

    result = prepare_thread(process, plan.entry, KERNEL_PROCESS_STARTUP_BASE,
                            process->default_priority, KERNEL_THREAD_RIGHTS,
                            &prepared_thread);
    if (result != KERNEL_PROCESS_OK)
        goto failed;

    /*
     * The entry contract from USERSPACE_RUNTIME.md: D2 carries the startup
     * block, D4 the process handle, D5 the initial thread handle.
     */
    prepared_thread.thread->context.data[4] = self_handle;
    prepared_thread.thread->context.data[5] = prepared_thread.handle;

    result = grant_bootstrap_capabilities(process, source_table, capabilities,
                                          capability_count, granted);
    if (result != KERNEL_PROCESS_OK) {
        (void)abort_prepared_thread(&prepared_thread);
        goto failed;
    }

    result = publish_startup_block(&process->address_space, process->owner,
                                   self_handle,
                                   prepared_thread.handle, granted,
                                   capability_count, arguments, argument_bytes,
                                   environment, 0u, 0u);
    if (result != KERNEL_PROCESS_OK) {
        (void)abort_prepared_thread(&prepared_thread);
        goto failed;
    }

    saved_status = kernel_interrupt_save_disable();
    result = commit_thread(&prepared_thread, &initial_thread_id,
                           &initial_thread_handle);
    if (result == KERNEL_PROCESS_OK) {
        ++scheduler_stats.created_processes;
        ++scheduler_stats.live_processes;
        *process_id = process->id;
    }
    kernel_interrupt_restore(saved_status);
    if (result != KERNEL_PROCESS_OK) {
        (void)abort_prepared_thread(&prepared_thread);
        goto failed;
    }
    (void)initial_thread_handle;
    return KERNEL_PROCESS_OK;

failed:
    (void)kernel_handle_close_all(&process->handles);
    if (process->address_space.initialized != 0u)
        (void)kernel_vm_destroy_address_space(&process->address_space);
    (void)kernel_memory_release_owner(process->owner, NULL);
    (void)kernel_memory_unprotect_owner(process->owner);
    process->process_state = KERNEL_PROCESS_DEAD;
    maybe_release_process_record(process);
    return result;
}

void kernel_process_register_initial_image(uint32_t process_id)
{
    initial_image_process_id = process_id;
    initial_image_progress = 0u;
    initial_image_exited = 0u;
}

KernelProcessStatus kernel_process_create(const void *image,
                                          uint32_t image_size,
                                          uint32_t entry_offset,
                                          uint32_t initial_argument,
                                          uint32_t *process_id)
{
    return create_process(image, image_size, entry_offset, initial_argument,
                          process_id);
}

KernelProcessStatus kernel_process_create_thread(uint32_t process_id,
                                                 uint32_t entry_offset,
                                                 uint32_t initial_argument,
                                                 uint8_t priority,
                                                 uint32_t *thread_id)
{
    KernelProcess *process = find_process_by_id(process_id);
    KernelPreparedThread prepared_thread;
    KernelHandle thread_handle;
    KernelProcessStatus status;
    uint16_t saved_status;

    if (process == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    status = prepare_thread(process, KERNEL_PROCESS_CODE_BASE + entry_offset,
                            initial_argument, priority,
                            KERNEL_THREAD_RIGHTS, &prepared_thread);
    if (status != KERNEL_PROCESS_OK)
        return status;
    saved_status = kernel_interrupt_save_disable();
    status = commit_thread(&prepared_thread, thread_id, &thread_handle);
    kernel_interrupt_restore(saved_status);
    if (status != KERNEL_PROCESS_OK)
        (void)abort_prepared_thread(&prepared_thread);
    return status;
}

KernelProcessStatus kernel_process_set_thread_bootstrap_argument(
    uint32_t process_id, uint32_t thread_id, uint32_t argument)
{
    KernelProcess *process = find_process_by_id(process_id);

    if (process == NULL || thread_id == 0u || current_thread != NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    for (uint16_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
        KernelThread *thread = kernel_thread_at(slot);

        if (thread == NULL || thread->id != thread_id ||
            thread->process_id != process_id)
            continue;
        if (thread->state != KERNEL_THREAD_READY || thread->run_count != 0u)
            return KERNEL_PROCESS_INVALID_STATE;
        thread->context.data[2] = argument;
        return KERNEL_PROCESS_OK;
    }
    return KERNEL_PROCESS_INVALID_ARGUMENT;
}

KernelProcessStatus kernel_process_grant_handle(
    uint32_t recipient_process_id, uint32_t target_process_id,
    uint32_t rights, KernelHandle *handle)
{
    KernelProcess *recipient = find_process_by_id(recipient_process_id);
    KernelProcess *target = find_process_by_id(target_process_id);
    KernelHandleStatus handle_status;
    KernelProcessStatus status;

    /*
     * DEBUG is grantable over another process on purpose: it is the authority
     * a debugger would hold, and it is deliberately not the authority to write
     * diagnostic lines in that process's name -- the log syscall refuses a
     * handle that names anyone but the caller.
     */
    if (recipient == NULL || target == NULL || handle == NULL ||
        rights == 0u ||
        (rights & ~(KERNEL_PROCESS_RIGHTS | KERNEL_PROCESS_RIGHT_DEBUG)) != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *handle = KERNEL_HANDLE_INVALID;
    status = retain_process_handle(target);
    if (status != KERNEL_PROCESS_OK)
        return status;
    handle_status = kernel_handle_install(
        &recipient->handles, KERNEL_OBJECT_PROCESS, rights, target,
        process_handle_release, NULL, handle);
    if (handle_status == KERNEL_HANDLE_OK)
        return process_pool_valid() ? KERNEL_PROCESS_OK :
                                      KERNEL_PROCESS_CORRUPT;
    process_handle_release(target, NULL);
    return handle_status == KERNEL_HANDLE_TABLE_FULL ?
        KERNEL_PROCESS_RESOURCE_LIMIT : KERNEL_PROCESS_CORRUPT;
}

KernelProcessStatus kernel_process_grant_irq(
    uint32_t recipient_process_id, const KernelIrqBinding *binding,
    uint32_t rights, KernelHandle *handle)
{
    KernelProcess *recipient = find_process_by_id(recipient_process_id);
    KernelIrqEndpoint *endpoint = NULL;
    KernelIrqStatus irq_status;
    KernelHandleStatus handle_status;

    if (recipient == NULL || binding == NULL || handle == NULL ||
        rights == 0u || (rights & ~KERNEL_IRQ_RIGHTS) != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *handle = KERNEL_HANDLE_INVALID;
    irq_status = kernel_irq_bind(recipient->id, binding, &endpoint);
    if (irq_status == KERNEL_IRQ_NO_SLOT ||
        irq_status == KERNEL_IRQ_QUOTA_EXCEEDED ||
        irq_status == KERNEL_IRQ_SOURCE_BUSY)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    if (irq_status == KERNEL_IRQ_INVALID_ARGUMENT)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (irq_status != KERNEL_IRQ_OK || endpoint == NULL)
        return irq_status == KERNEL_IRQ_DEVICE_ERROR ?
            KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
    handle_status = kernel_handle_install_cloneable(
        &recipient->handles, KERNEL_OBJECT_IRQ, rights, endpoint,
        kernel_irq_handle_retain, kernel_irq_handle_release, NULL, handle);
    if (handle_status == KERNEL_HANDLE_OK) {
#if defined(KERNEL_PROCESS_HOST_TEST)
        return process_pool_valid() ? KERNEL_PROCESS_OK :
                                      KERNEL_PROCESS_CORRUPT;
#else
        return process_pool_healthy() && kernel_irq_pool_healthy() ?
            KERNEL_PROCESS_OK : KERNEL_PROCESS_CORRUPT;
#endif
    }
    kernel_irq_abandon_unpublished(endpoint);
    return handle_status == KERNEL_HANDLE_TABLE_FULL ?
        KERNEL_PROCESS_RESOURCE_LIMIT : KERNEL_PROCESS_CORRUPT;
}

KernelProcessStatus kernel_process_grant_device(
    uint32_t recipient_process_id, uint32_t device_id, uint32_t rights,
    KernelHandle *handle)
{
    KernelProcess *recipient = find_process_by_id(recipient_process_id);
    KernelDeviceLease *lease = NULL;
    KernelDeviceStatus device_status;
    KernelHandleStatus handle_status;

    if (recipient == NULL || handle == NULL || rights == 0u ||
        (rights & ~KERNEL_DEVICE_RIGHTS) != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *handle = KERNEL_HANDLE_INVALID;
    device_status = kernel_device_acquire(recipient->id, device_id, &lease);
    if (device_status == KERNEL_DEVICE_NO_SLOT ||
        device_status == KERNEL_DEVICE_QUOTA_EXCEEDED ||
        device_status == KERNEL_DEVICE_BUSY)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    if (device_status == KERNEL_DEVICE_INVALID_ARGUMENT ||
        device_status == KERNEL_DEVICE_NOT_FOUND)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (device_status != KERNEL_DEVICE_OK || lease == NULL)
        return device_status == KERNEL_DEVICE_REVOKED ?
            KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
    handle_status = kernel_handle_install_cloneable(
        &recipient->handles, KERNEL_OBJECT_DEVICE, rights, lease,
        kernel_device_handle_retain, kernel_device_handle_release, NULL,
        handle);
    if (handle_status == KERNEL_HANDLE_OK)
        return process_pool_valid() && kernel_device_pool_valid() ?
            KERNEL_PROCESS_OK : KERNEL_PROCESS_CORRUPT;
    kernel_device_abandon_unpublished(lease);
    return handle_status == KERNEL_HANDLE_TABLE_FULL ?
        KERNEL_PROCESS_RESOURCE_LIMIT : KERNEL_PROCESS_CORRUPT;
}

KernelProcessStatus kernel_process_qualification_authorize(
    uint32_t process_id, uint32_t irq_source_mask)
{
    KernelProcessQualificationClient *available = NULL;

    if (find_process_by_id(process_id) == NULL ||
        (irq_source_mask & ~KERNEL_QUALIFICATION_IRQ_SOURCE_MASK) != 0u ||
        scheduler_started != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    for (uint32_t index = 0u;
         index < PROCESS_QUALIFICATION_CLIENT_MAX; ++index) {
        KernelProcessQualificationClient *client =
            &qualification_clients[index];

        if (client->process_id == process_id) {
            client->authorized_sources = irq_source_mask;
            client->completed_sources = 0u;
            return KERNEL_PROCESS_OK;
        }
        if (client->process_id == 0u && available == NULL)
            available = client;
    }
    if (available == NULL)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    available->process_id = process_id;
    available->authorized_sources = irq_source_mask;
    available->completed_sources = 0u;
    return KERNEL_PROCESS_OK;
}

bool kernel_process_qualification_status(uint32_t process_id,
                                         uint32_t *authorized_sources,
                                         uint32_t *completed_sources)
{
    if (authorized_sources == NULL || completed_sources == NULL)
        return false;
    for (uint32_t index = 0u;
         index < PROCESS_QUALIFICATION_CLIENT_MAX; ++index) {
        const KernelProcessQualificationClient *client =
            &qualification_clients[index];

        if (client->process_id != process_id)
            continue;
        *authorized_sources = client->authorized_sources;
        *completed_sources = client->completed_sources;
        return true;
    }
    return false;
}

static KernelProcessQualificationClient *qualification_client(
    uint32_t process_id)
{
    for (uint32_t index = 0u;
         index < PROCESS_QUALIFICATION_CLIENT_MAX; ++index) {
        if (qualification_clients[index].process_id == process_id)
            return &qualification_clients[index];
    }
    return NULL;
}

static bool qualification_source_allowed(
    const KernelProcessQualificationClient *client, uint32_t source)
{
    return client != NULL && source < KERNEL_IRQ_SOURCE_COUNT &&
           (client->authorized_sources & (1u << source)) != 0u;
}

static KernelProcessStatus qualification_command(
    KernelProcess *process, KernelThread *thread, uint32_t *syscall_result,
    bool *handled)
{
    KernelProcessQualificationClient *client;
    uint32_t command;
    uint32_t source;

    if (process == NULL || thread == NULL || syscall_result == NULL ||
        handled == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    command = thread->context.data[1];
    *handled = (command & KERNEL_QUALIFICATION_COMMAND_MASK) ==
        KERNEL_QUALIFICATION_COMMAND_PREFIX;
    if (!*handled)
        return KERNEL_PROCESS_OK;
    client = qualification_client(process->id);
    if (client == NULL) {
        *syscall_result = ASTRA_SYSCALL_ACCESS_DENIED;
        return KERNEL_PROCESS_OK;
    }
    source = thread->context.data[2];
    switch (command) {
    case KERNEL_QUALIFICATION_COMMAND_QUERY_IRQS:
        thread->context.data[1] = client->authorized_sources;
        return KERNEL_PROCESS_OK;
    case KERNEL_QUALIFICATION_COMMAND_BIND_IRQ: {
        KernelIrqBinding binding;
        KernelHandle handle;
        KernelProcessStatus status;

        if (!qualification_source_allowed(client, source) ||
            !kernel_interrupt_device_binding((uint8_t)source, &binding)) {
            *syscall_result = ASTRA_SYSCALL_ACCESS_DENIED;
            return KERNEL_PROCESS_OK;
        }
        status = kernel_process_grant_irq(process->id, &binding,
                                          KERNEL_IRQ_RIGHTS, &handle);
        if (status == KERNEL_PROCESS_OK) {
            thread->context.data[1] = handle;
            return KERNEL_PROCESS_OK;
        }
        if (status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        if (status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            *syscall_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            return KERNEL_PROCESS_OK;
        }
        if (status == KERNEL_PROCESS_INVALID_STATE) {
            *syscall_result = ASTRA_SYSCALL_IO_ERROR;
            return KERNEL_PROCESS_OK;
        }
        return status;
    }
    case KERNEL_QUALIFICATION_COMMAND_PREPARE_IRQ:
        if (!qualification_source_allowed(client, source))
            *syscall_result = ASTRA_SYSCALL_ACCESS_DENIED;
        else if (!kernel_platform_qualification_irq_prepare(
                     (uint8_t)source))
            *syscall_result = ASTRA_SYSCALL_IO_ERROR;
        return KERNEL_PROCESS_OK;
    case KERNEL_QUALIFICATION_COMMAND_CONSUME_IRQ:
        if (!qualification_source_allowed(client, source))
            *syscall_result = ASTRA_SYSCALL_ACCESS_DENIED;
        else if (!kernel_platform_qualification_irq_consume(
                     (uint8_t)source, thread->context.data[3]))
            *syscall_result = ASTRA_SYSCALL_IO_ERROR;
        return KERNEL_PROCESS_OK;
    case KERNEL_QUALIFICATION_COMMAND_COMPLETE_IRQS:
        /*
         * A subset, not the whole set. A source that is present but cannot be
         * provoked on this machine -- storage and input under the emulator,
         * where nothing plays the part the AstraHost link played -- is
         * reported as not qualified rather than claimed. What was proved is
         * the mask, and the kernel prints it.
         */
        if (source == 0u || (source & ~client->authorized_sources) != 0u) {
            *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        client->completed_sources = source;
        return KERNEL_PROCESS_OK;
    default:
        *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return KERNEL_PROCESS_OK;
    }
}

KernelProcessStatus kernel_process_start(KernelCpuContext **next_context)
{
    KernelThread *next = NULL;
    KernelProcessStatus activate_status;
    KernelThreadStatus status;

    if (next_context == NULL || current_thread != NULL ||
        scheduler_initialized == 0u || scheduler_started != 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    scheduler_started = 1u;
    status = kernel_thread_take_next(&next);
    if (status == KERNEL_THREAD_NO_RUNNABLE) {
        scheduler_started = 0u;
        return KERNEL_PROCESS_NO_RUNNABLE;
    }
    if (status != KERNEL_THREAD_OK || next == NULL) {
        scheduler_started = 0u;
        return KERNEL_PROCESS_CORRUPT;
    }
    activate_status = activate(next, next_context);

    if (activate_status != KERNEL_PROCESS_OK)
        scheduler_started = 0u;
    return activate_status;
}

bool kernel_process_active(void)
{
    return current_thread != NULL;
}

KernelCpuContext *kernel_process_current_context(void)
{
    KernelCpuContext *next;

    if (process_for_thread(current_thread) == NULL ||
        current_thread->state != KERNEL_THREAD_RUNNING ||
        !kernel_context_valid(&current_thread->context))
        return NULL;
    if ((quantum_preempt_pending != 0u ||
         deadline_preempt_pending != 0u ||
         ready_thread_outranks(current_thread)) &&
        schedule_pending(&next) != KERNEL_PROCESS_OK)
        return NULL;
    return runtime_resume(current_thread);
}

bool kernel_process_worker_enter(void)
{
    if (scheduler_initialized == 0u || worker_active != 0u)
        return false;

    worker_active = 1u;
    runtime_stop(process_for_thread(current_thread));
    quantum_active = 0u;
    scheduler_timer_rearm();
    return true;
}

KernelCpuContext *kernel_process_worker_resume(void)
{
    KernelCpuContext *next;

    if (worker_active == 0u)
        return NULL;
    /*
     * Nothing was running when the worker went idle, so a thread woken since
     * then has to be selected here.
     *
     * Device interrupts are always deferred: the handler queues the event and
     * signals the worker, and the wake happens later inside
     * service_deferred_interrupts(). By then there is no interrupt left to
     * carry a scheduling decision, and the interrupt path could not have made
     * one anyway -- it returns to a supervisor frame, because an idle kernel
     * is halted in kernel_worker_arch_wait().
     *
     * Leaving the selection out made every transfer that had to block wait for
     * the timer instead, and the timer is armed to the sleeping thread's own
     * deadline. One full lease timeout per blocking transfer, which is what
     * made an interrupt-driven block path behave like a polled one.
     */
    if (current_thread == NULL) {
        uint8_t ready_priority;

        if (scheduler_initialized == 0u || scheduler_started == 0u ||
            !kernel_thread_highest_ready_priority(&ready_priority))
            return NULL;
        (void)ready_priority;
        if (schedule_next(&next) != KERNEL_PROCESS_OK ||
            current_thread == NULL)
            return NULL;
        worker_active = 0u;
        scheduler_start_quantum();
        return runtime_resume(current_thread);
    }
    if (process_for_thread(current_thread) == NULL ||
        current_thread->state != KERNEL_THREAD_RUNNING ||
        !kernel_context_valid(&current_thread->context))
        return NULL;
    if ((quantum_preempt_pending != 0u ||
         deadline_preempt_pending != 0u ||
         ready_thread_outranks(current_thread)) &&
        schedule_pending(&next) != KERNEL_PROCESS_OK)
        return NULL;

    worker_active = 0u;
    scheduler_start_quantum();
    return runtime_resume(current_thread);
}

KernelProcessStatus kernel_process_on_timer(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelThread *previous;
    KernelProcessStatus status;
    uint64_t now;
    uint32_t expired;
    uint8_t highest;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current,
                             &previous);
    if (status != KERNEL_PROCESS_OK)
        return status;
    (void)current;
    now = scheduler_cycles();
    status = scheduler_expire_due(now, &expired, &highest);
    if (status != KERNEL_PROCESS_OK)
        return status;
    if (quantum_active != 0u && now >= quantum_deadline)
        scheduler_mark_quantum_expired(previous);
    if (expired != 0u && highest > previous->effective_priority)
        deadline_preempt_pending = 1u;
    if (quantum_preempt_pending != 0u ||
        deadline_preempt_pending != 0u ||
        ready_thread_outranks(previous))
        return schedule_pending(next_context);

    *next_context = runtime_resume(previous);
    scheduler_timer_rearm();
    return KERNEL_PROCESS_OK;
}

KernelProcessStatus kernel_process_on_interrupt_wakeup(
    const uint32_t *registers, uint32_t user_stack, const void *raw_frame,
    KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelThread *previous;
    KernelProcessStatus status;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current,
                             &previous);
    if (status != KERNEL_PROCESS_OK)
        return status;
    (void)current;
    if (ready_thread_outranks(previous)) {
        status = schedule_pending(next_context);
        if (status == KERNEL_PROCESS_OK &&
            *next_context != &previous->context)
            ++scheduler_stats.wake_preemptions;
        return status;
    }
    *next_context = runtime_resume(previous);
    return KERNEL_PROCESS_OK;
}

KernelProcessStatus kernel_process_on_supervisor_timer(void)
{
    KernelCpuContext *next = NULL;
    KernelProcessStatus status;
    uint64_t now;
    uint32_t expired;
    uint8_t highest;
    bool deferred = false;

    if (scheduler_initialized == 0u)
        return KERNEL_PROCESS_OK;
    now = scheduler_cycles();
    status = scheduler_expire_due(now, &expired, &highest);
    if (status != KERNEL_PROCESS_OK)
        return status;

    if (current_thread == NULL) {
        uint8_t ready_priority;

        if (scheduler_started == 0u) {
            scheduler_timer_rearm_at(now);
            return KERNEL_PROCESS_OK;
        }
        if (kernel_thread_highest_ready_priority(&ready_priority)) {
            (void)ready_priority;
            status = schedule_next(&next);
            return status == KERNEL_PROCESS_OK ? KERNEL_PROCESS_OK : status;
        }
        scheduler_timer_rearm_at(now);
        return KERNEL_PROCESS_OK;
    }
    if (process_for_thread(current_thread) == NULL ||
        current_thread->state != KERNEL_THREAD_RUNNING)
        return KERNEL_PROCESS_CORRUPT;
    if (quantum_active != 0u && now >= quantum_deadline) {
        scheduler_mark_quantum_expired(current_thread);
        deferred = true;
    }
    if (expired != 0u && highest > current_thread->effective_priority) {
        if (deadline_preempt_pending == 0u)
            deferred = true;
        deadline_preempt_pending = 1u;
    }
    if (deferred)
        ++scheduler_stats.supervisor_timer_deferrals;
    scheduler_timer_rearm_at(now);
    return KERNEL_PROCESS_OK;
}

static bool decode_wait_deadline(uint32_t high, uint32_t low,
                                 uint64_t *deadline_cycles)
{
    uint64_t bits;

    if (deadline_cycles == NULL || (high & 0x80000000u) != 0u)
        return false;
    bits = ((uint64_t)high << 32) | low;
    return kernel_platform_deadline_to_cycles((int64_t)bits,
                                               deadline_cycles);
}

static void set_wait_outputs(KernelThread *thread, bool multiple,
                             uint32_t index, uint32_t detail)
{
    if (multiple) {
        thread->context.data[1] = index;
        thread->context.data[2] = detail;
    } else {
        thread->context.data[1] = detail;
    }
}

static KernelProcessStatus prepare_process_death_wait(
    KernelProcess *target, KernelProcess *waiter_process,
    KernelThreadWaitSpec *spec, bool *ready, uint32_t *wait_result,
    uint32_t *exit_status)
{
    if (!valid_process_handle_object(target) || waiter_process == NULL ||
        target == waiter_process || target->handle_references == 0u ||
        spec == NULL || ready == NULL || wait_result == NULL ||
        exit_status == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    spec->queue = NULL;
    spec->sequence = 0u;
    *ready = false;
    *wait_result = ASTRA_SYSCALL_OK;
    *exit_status = 0u;
    ++scheduler_stats.process_death_waits;
    if (target->process_state == KERNEL_PROCESS_EXITING ||
        target->process_state == KERNEL_PROCESS_DEAD) {
        *ready = true;
        *wait_result = target->terminal_result;
        *exit_status = target->exit_status;
        return KERNEL_PROCESS_OK;
    }
    if (target->process_state != KERNEL_PROCESS_CREATED &&
        target->process_state != KERNEL_PROCESS_RUNNING)
        return KERNEL_PROCESS_INVALID_STATE;
    spec->queue = &target->death_waiters;
    spec->sequence = kernel_thread_wait_queue_sequence(spec->queue);
    return spec->sequence == 0u ? KERNEL_PROCESS_CORRUPT :
                                 KERNEL_PROCESS_OK;
}

static KernelProcessStatus commit_process_death_wait(KernelProcess *target)
{
    uint32_t waiters;

    if (!valid_process_handle_object(target))
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    waiters = kernel_thread_wait_queue_count(&target->death_waiters);
    return waiters != 0u && waiters <= KERNEL_THREAD_MAX ?
        KERNEL_PROCESS_OK : KERNEL_PROCESS_INVALID_STATE;
}

static bool ring_status_to_syscall(KernelRingStatus status,
                                   uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_RING_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_RING_WOULD_BLOCK:
        *result = ASTRA_SYSCALL_WOULD_BLOCK;
        return true;
    case KERNEL_RING_PEER_DEAD:
        *result = ASTRA_SYSCALL_PEER_DEAD;
        return true;
    case KERNEL_RING_CLOSED:
        *result = ASTRA_SYSCALL_CLOSED;
        return true;
    case KERNEL_RING_IO_ERROR:
        *result = ASTRA_SYSCALL_IO_ERROR;
        return true;
    case KERNEL_RING_INVALID_ARGUMENT:
    case KERNEL_RING_INVALID_STATE:
    case KERNEL_RING_OVERLAP:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_RING_NO_SLOT:
    case KERNEL_RING_QUOTA_EXCEEDED:
        *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return true;
    case KERNEL_RING_CORRUPT:
    default:
        return false;
    }
}

static bool area_status_to_syscall(KernelAreaStatus status,
                                   uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_AREA_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_AREA_INVALID_ARGUMENT:
    case KERNEL_AREA_INVALID_STATE:
    case KERNEL_AREA_ALREADY_MAPPED:
    case KERNEL_AREA_NOT_MAPPED:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_AREA_NO_SLOT:
    case KERNEL_AREA_QUOTA_EXCEEDED:
        *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return true;
    case KERNEL_AREA_OUT_OF_MEMORY:
        *result = ASTRA_SYSCALL_OUT_OF_MEMORY;
        return true;
    case KERNEL_AREA_ACCESS_DENIED:
        *result = ASTRA_SYSCALL_ACCESS_DENIED;
        return true;
    case KERNEL_AREA_PEER_DEAD:
        *result = ASTRA_SYSCALL_PEER_DEAD;
        return true;
    case KERNEL_AREA_CORRUPT:
    default:
        return false;
    }
}

static bool vm_status_to_syscall(KernelVmStatus status, uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_VM_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_VM_INVALID_ARGUMENT:
    case KERNEL_VM_ALREADY_MAPPED:
    case KERNEL_VM_NOT_MAPPED:
    case KERNEL_VM_CACHE_ALIAS:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_VM_NOT_OWNED:
        *result = ASTRA_SYSCALL_ACCESS_DENIED;
        return true;
    case KERNEL_VM_OUT_OF_MEMORY:
        *result = ASTRA_SYSCALL_OUT_OF_MEMORY;
        return true;
    case KERNEL_VM_BUSY:
        *result = ASTRA_SYSCALL_WOULD_BLOCK;
        return true;
    case KERNEL_VM_CORRUPT:
    default:
        return false;
    }
}

static bool irq_status_to_syscall(KernelIrqStatus status, uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_IRQ_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_IRQ_WOULD_BLOCK:
        *result = ASTRA_SYSCALL_WOULD_BLOCK;
        return true;
    case KERNEL_IRQ_CLOSED:
        *result = ASTRA_SYSCALL_PEER_DEAD;
        return true;
    case KERNEL_IRQ_INVALID_ARGUMENT:
    case KERNEL_IRQ_INVALID_STATE:
    case KERNEL_IRQ_SEQUENCE_MISMATCH:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_IRQ_NO_SLOT:
    case KERNEL_IRQ_QUOTA_EXCEEDED:
    case KERNEL_IRQ_SOURCE_BUSY:
        *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return true;
    case KERNEL_IRQ_OVERFLOW:
    case KERNEL_IRQ_STORM:
    case KERNEL_IRQ_DEVICE_ERROR:
        *result = ASTRA_SYSCALL_IO_ERROR;
        return true;
    case KERNEL_IRQ_UNCLAIMED:
    case KERNEL_IRQ_CORRUPT:
    default:
        return false;
    }
}

static bool device_status_to_syscall(KernelDeviceStatus status,
                                     uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_DEVICE_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_DEVICE_INVALID_ARGUMENT:
    case KERNEL_DEVICE_NOT_FOUND:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_DEVICE_BUSY:
    case KERNEL_DEVICE_NO_SLOT:
    case KERNEL_DEVICE_QUOTA_EXCEEDED:
        *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return true;
    case KERNEL_DEVICE_REVOKED:
        *result = ASTRA_SYSCALL_PEER_DEAD;
        return true;
    case KERNEL_DEVICE_QUIESCE_FAILED:
    case KERNEL_DEVICE_RESET_FAILED:
        *result = ASTRA_SYSCALL_IO_ERROR;
        return true;
    case KERNEL_DEVICE_CORRUPT:
    default:
        return false;
    }
}

static KernelRingEndpoint ring_endpoint_for_type(KernelObjectType type)
{
    return type == KERNEL_OBJECT_RING_PRODUCER ?
        KERNEL_RING_ENDPOINT_PRODUCER : KERNEL_RING_ENDPOINT_CONSUMER;
}

static KernelProcessStatus wait_handle_set(
    KernelProcess *process, KernelThread *thread,
    const KernelHandle *handles, uint32_t count, uint64_t now,
    uint64_t deadline, bool multiple, KernelHandle port_probe_handle,
    uint32_t port_probe_sequence, bool *blocked,
    uint32_t *syscall_result)
{
    void *objects[ASTRA_WAIT_MULTIPLE_MAX];
    KernelThreadWaitSpec specs[ASTRA_WAIT_MULTIPLE_MAX];
    KernelObjectType types[ASTRA_WAIT_MULTIPLE_MAX];

    if (process == NULL || thread == NULL || handles == NULL ||
        count == 0u || count > ASTRA_WAIT_MULTIPLE_MAX || blocked == NULL ||
        syscall_result == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *blocked = false;

    /* Resolve the complete set before probing any consumable state. */
    for (uint32_t index = 0u; index < count; ++index) {
        KernelHandleStatus handle_status = kernel_handle_lookup_any(
            &process->handles, handles[index], ASTRA_RIGHT_WAIT,
            &types[index], &objects[index]);

        specs[index].queue = NULL;
        specs[index].sequence = 0u;
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE) {
            *syscall_result = ASTRA_SYSCALL_INVALID_HANDLE;
            return KERNEL_PROCESS_OK;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            *syscall_result = ASTRA_SYSCALL_ACCESS_DENIED;
            return KERNEL_PROCESS_OK;
        }
        if (handle_status != KERNEL_HANDLE_OK || objects[index] == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (types[index] != KERNEL_OBJECT_SYNC &&
            types[index] != KERNEL_OBJECT_TIMER &&
            types[index] != KERNEL_OBJECT_THREAD &&
            types[index] != KERNEL_OBJECT_PROCESS &&
            types[index] != KERNEL_OBJECT_PORT_SEND &&
            types[index] != KERNEL_OBJECT_PORT_RECEIVE &&
            types[index] != KERNEL_OBJECT_RING_PRODUCER &&
            types[index] != KERNEL_OBJECT_RING_CONSUMER &&
            types[index] != KERNEL_OBJECT_IRQ) {
            *syscall_result = ASTRA_SYSCALL_INVALID_HANDLE;
            return KERNEL_PROCESS_OK;
        }
        if (types[index] == KERNEL_OBJECT_THREAD &&
            objects[index] == thread) {
            *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        if (types[index] == KERNEL_OBJECT_PROCESS &&
            objects[index] == process) {
            *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
    }

    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t detail = 0u;
        uint32_t wait_result = ASTRA_SYSCALL_OK;

        if (types[index] == KERNEL_OBJECT_SYNC ||
            types[index] == KERNEL_OBJECT_TIMER) {
            KernelSyncObject *object = objects[index];
            KernelSyncStatus sync_status =
                kernel_sync_prepare_wait(object, &specs[index]);

            if (sync_status == KERNEL_SYNC_OK) {
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (sync_status == KERNEL_SYNC_CLOSED) {
                wait_result = kernel_sync_terminal_result(object);
                if (wait_result == ASTRA_SYSCALL_OK)
                    return KERNEL_PROCESS_CORRUPT;
                *syscall_result = wait_result;
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (sync_status == KERNEL_SYNC_WAITER_LIMIT) {
                *syscall_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                return KERNEL_PROCESS_OK;
            }
            if (sync_status != KERNEL_SYNC_BLOCKED)
                return sync_status == KERNEL_SYNC_INVALID_ARGUMENT ||
                               sync_status == KERNEL_SYNC_INVALID_STATE ?
                    KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_PORT_SEND ||
                   types[index] == KERNEL_OBJECT_PORT_RECEIVE) {
            KernelPortEndpoint endpoint =
                types[index] == KERNEL_OBJECT_PORT_SEND ?
                    KERNEL_PORT_ENDPOINT_SEND :
                    KERNEL_PORT_ENDPOINT_RECEIVE;
            bool after_failed_send =
                types[index] == KERNEL_OBJECT_PORT_SEND &&
                handles[index] == port_probe_handle &&
                port_probe_sequence != 0u;
            KernelPortStatus port_status = after_failed_send ?
                kernel_port_prepare_wait_after(
                    objects[index], endpoint, port_probe_sequence,
                    &specs[index]) :
                kernel_port_prepare_wait(
                    objects[index], endpoint, &specs[index]);

            if (port_status == KERNEL_PORT_OK) {
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (port_status == KERNEL_PORT_PEER_DEAD ||
                port_status == KERNEL_PORT_CLOSED) {
                *syscall_result = port_status == KERNEL_PORT_PEER_DEAD ?
                    ASTRA_SYSCALL_PEER_DEAD : ASTRA_SYSCALL_CLOSED;
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (port_status == KERNEL_PORT_QUOTA_EXCEEDED) {
                *syscall_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                return KERNEL_PROCESS_OK;
            }
            if (port_status != KERNEL_PORT_WOULD_BLOCK)
                return port_status == KERNEL_PORT_INVALID_ARGUMENT ||
                               port_status == KERNEL_PORT_INVALID_STATE ?
                    KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_RING_PRODUCER ||
                   types[index] == KERNEL_OBJECT_RING_CONSUMER) {
            KernelRingStatus ring_status = kernel_ring_prepare_wait(
                objects[index], ring_endpoint_for_type(types[index]),
                &specs[index]);

            if (ring_status == KERNEL_RING_OK) {
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (ring_status != KERNEL_RING_WOULD_BLOCK) {
                if (!ring_status_to_syscall(ring_status, syscall_result))
                    return KERNEL_PROCESS_CORRUPT;
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
        } else if (types[index] == KERNEL_OBJECT_IRQ) {
            KernelIrqStatus irq_status = kernel_irq_prepare_wait(
                objects[index], &specs[index]);

            if (irq_status == KERNEL_IRQ_OK) {
                KernelIrqRecord record;
                uint32_t event_flags;
                KernelIrqStatus read_status = kernel_irq_read(
                    objects[index], &record, &event_flags);

                if (read_status != KERNEL_IRQ_OK &&
                    read_status != KERNEL_IRQ_OVERFLOW &&
                    read_status != KERNEL_IRQ_STORM &&
                    read_status != KERNEL_IRQ_DEVICE_ERROR)
                    return KERNEL_PROCESS_CORRUPT;
                set_wait_outputs(thread, multiple, index, event_flags);
                return KERNEL_PROCESS_OK;
            }
            if (irq_status == KERNEL_IRQ_CLOSED) {
                *syscall_result = ASTRA_SYSCALL_PEER_DEAD;
                if (multiple)
                    set_wait_outputs(thread, true, index, 0u);
                return KERNEL_PROCESS_OK;
            }
            if (irq_status == KERNEL_IRQ_QUOTA_EXCEEDED) {
                *syscall_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                return KERNEL_PROCESS_OK;
            }
            if (irq_status != KERNEL_IRQ_WOULD_BLOCK)
                return irq_status == KERNEL_IRQ_INVALID_ARGUMENT ||
                               irq_status == KERNEL_IRQ_INVALID_STATE ?
                    KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_THREAD) {
            bool ready;
            KernelThreadStatus thread_status =
                kernel_thread_prepare_death_wait(
                    objects[index], thread, &specs[index], &ready,
                    &wait_result, &detail);

            if (thread_status == KERNEL_THREAD_INVALID_ARGUMENT ||
                thread_status == KERNEL_THREAD_INVALID_STATE) {
                *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                return KERNEL_PROCESS_OK;
            }
            if (thread_status != KERNEL_THREAD_OK)
                return KERNEL_PROCESS_CORRUPT;
            if (ready) {
                *syscall_result = wait_result;
                set_wait_outputs(thread, multiple, index, detail);
                return KERNEL_PROCESS_OK;
            }
        } else {
            bool ready;
            KernelProcessStatus process_status =
                prepare_process_death_wait(
                    objects[index], process, &specs[index], &ready,
                    &wait_result, &detail);

            if (process_status == KERNEL_PROCESS_INVALID_ARGUMENT ||
                process_status == KERNEL_PROCESS_INVALID_STATE) {
                *syscall_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                return KERNEL_PROCESS_OK;
            }
            if (process_status != KERNEL_PROCESS_OK)
                return KERNEL_PROCESS_CORRUPT;
            if (ready) {
                *syscall_result = wait_result;
                set_wait_outputs(thread, multiple, index, detail);
                return KERNEL_PROCESS_OK;
            }
        }
    }

    if (multiple) {
        thread->context.data[1] = ASTRA_WAIT_INDEX_NONE;
        thread->context.data[2] = 0u;
    } else if (types[0] == KERNEL_OBJECT_THREAD ||
               types[0] == KERNEL_OBJECT_PROCESS) {
        thread->context.data[1] = 0u;
    }
    KernelThreadStatus thread_status = multiple ?
        kernel_thread_block_wait_set(
            thread, specs, count, now, deadline, ASTRA_SYSCALL_TIMED_OUT) :
        kernel_thread_block_until(
            thread, specs[0].queue, specs[0].sequence, now, deadline,
            ASTRA_SYSCALL_TIMED_OUT);

    if (thread_status == KERNEL_THREAD_DEADLINE_EXPIRED) {
        *syscall_result = ASTRA_SYSCALL_TIMED_OUT;
        return KERNEL_PROCESS_OK;
    }
    if (thread_status == KERNEL_THREAD_NO_SLOT) {
        *syscall_result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return KERNEL_PROCESS_OK;
    }
    if (thread_status != KERNEL_THREAD_OK)
        return thread_status == KERNEL_THREAD_INVALID_ARGUMENT ||
                       thread_status == KERNEL_THREAD_INVALID_STATE ||
                       thread_status == KERNEL_THREAD_CONDITION_CHANGED ?
            KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;

    for (uint32_t index = 0u; index < count; ++index) {
        if (types[index] == KERNEL_OBJECT_SYNC ||
            types[index] == KERNEL_OBJECT_TIMER) {
            if (kernel_sync_commit_wait(objects[index]) != KERNEL_SYNC_OK)
                return KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_PORT_SEND ||
                   types[index] == KERNEL_OBJECT_PORT_RECEIVE) {
            KernelPortEndpoint endpoint =
                types[index] == KERNEL_OBJECT_PORT_SEND ?
                    KERNEL_PORT_ENDPOINT_SEND :
                    KERNEL_PORT_ENDPOINT_RECEIVE;

            if (kernel_port_commit_wait(objects[index], endpoint) !=
                KERNEL_PORT_OK)
                return KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_RING_PRODUCER ||
                   types[index] == KERNEL_OBJECT_RING_CONSUMER) {
            if (kernel_ring_commit_wait(
                    objects[index], ring_endpoint_for_type(types[index])) !=
                KERNEL_RING_OK)
                return KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_IRQ) {
            if (kernel_irq_commit_wait(objects[index]) != KERNEL_IRQ_OK)
                return KERNEL_PROCESS_CORRUPT;
        } else if (types[index] == KERNEL_OBJECT_THREAD) {
            if (kernel_thread_commit_death_wait(objects[index]) !=
                KERNEL_THREAD_OK)
                return KERNEL_PROCESS_CORRUPT;
            ++scheduler_stats.thread_death_waits;
        } else {
            if (commit_process_death_wait(objects[index]) !=
                KERNEL_PROCESS_OK)
                return KERNEL_PROCESS_CORRUPT;
        }
    }
    *blocked = true;
    return KERNEL_PROCESS_OK;
}

static bool port_status_to_syscall(KernelPortStatus status,
                                   uint32_t *result)
{
    if (result == NULL)
        return false;
    switch (status) {
    case KERNEL_PORT_OK:
        *result = ASTRA_SYSCALL_OK;
        return true;
    case KERNEL_PORT_WOULD_BLOCK:
        *result = ASTRA_SYSCALL_WOULD_BLOCK;
        return true;
    case KERNEL_PORT_PEER_DEAD:
        *result = ASTRA_SYSCALL_PEER_DEAD;
        return true;
    case KERNEL_PORT_CLOSED:
        *result = ASTRA_SYSCALL_CLOSED;
        return true;
    case KERNEL_PORT_BUFFER_TOO_SMALL:
        *result = ASTRA_SYSCALL_BUFFER_TOO_SMALL;
        return true;
    case KERNEL_PORT_INVALID_ARGUMENT:
    case KERNEL_PORT_INVALID_STATE:
    case KERNEL_PORT_DUPLICATE_HANDLE:
        *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        return true;
    case KERNEL_PORT_INVALID_HANDLE:
        *result = ASTRA_SYSCALL_INVALID_HANDLE;
        return true;
    case KERNEL_PORT_ACCESS_DENIED:
        *result = ASTRA_SYSCALL_ACCESS_DENIED;
        return true;
    case KERNEL_PORT_NO_SLOT:
    case KERNEL_PORT_QUOTA_EXCEEDED:
    case KERNEL_PORT_TRANSFER_POOL_FULL:
    case KERNEL_PORT_HANDLE_TABLE_FULL:
        *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
        return true;
    case KERNEL_PORT_CORRUPT:
    default:
        return false;
    }
}

static bool valid_message_header(const uint8_t *message,
                                 uint32_t message_size)
{
    AstraMessageHeader header;

    if (message == NULL || message_size < sizeof(header))
        return false;
    kernel_bytes_copy(&header, message, sizeof(header));
    return header.total_size == message_size &&
           header.header_size == ASTRA_MESSAGE_HEADER_SIZE &&
           header.flags == 0u && header.reserved == 0u;
}

/*
 * The three port syscalls, lifted out of the dispatch switch.
 *
 * block_syscall already established this shape and returns a syscall result
 * directly, which works because nothing in the block path can detect a broken
 * kernel invariant. The port path can, repeatedly -- a handle that looks up OK
 * but yields a null object, a send that cannot read back its own wait sequence
 * -- and those are not syscall failures the caller should see, they are faults.
 * So the status and the result are separate here: KERNEL_PROCESS_OK with
 * *result set is an answer for the caller, anything else is a fault for the
 * dispatcher to act on.
 *
 * This buys readability and nothing else, which is worth saying because the
 * obvious other reason to do it does not hold: the dispatcher's stack frame is
 * 464 bytes measured by -fstack-usage both before and after, since the case
 * scopes are disjoint and GCC already shared the slots. The gain is that the
 * dispatcher drops from 1,563 lines to 1,341 and these 224 can be read on
 * their own.
 */
static KernelProcessStatus port_syscall(KernelProcess *current,
                                        KernelThread *thread, uint32_t syscall,
                                        uint32_t *result)
{
    KernelPort *port = NULL;
    KernelHandleStatus handle_status;
    KernelPortStatus port_status;

    if (syscall == ASTRA_SYSCALL_PORT_CREATE) {
        KernelHandle receive_handle = KERNEL_HANDLE_INVALID;
        KernelHandle send_handle = KERNEL_HANDLE_INVALID;

        if (kernel_handle_available(&current->handles) < 2u) {
            *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            return KERNEL_PROCESS_OK;
        }
        port_status = kernel_port_create(
            current->id, thread->context.data[1],
            thread->context.data[2], &port);
        if (port_status != KERNEL_PORT_OK) {
            if (!port_status_to_syscall(port_status, result))
                return KERNEL_PROCESS_CORRUPT;
            return KERNEL_PROCESS_OK;
        }
        handle_status = kernel_handle_install(
            &current->handles, KERNEL_OBJECT_PORT_RECEIVE,
            KERNEL_PORT_RECEIVE_RIGHTS, port,
            kernel_port_handle_release,
            (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_RECEIVE,
            &receive_handle);
        if (handle_status != KERNEL_HANDLE_OK) {
            kernel_port_abandon_unpublished(port);
            if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
                *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                return KERNEL_PROCESS_OK;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        /*
         * The send endpoint is cloneable and the receive one is not. A service
         * is published by handing out send handles -- at launch, to a child --
         * and that is a copy; a second receive handle would be a second
         * service on one port. See kernel_port_handle_retain.
         */
        handle_status = kernel_handle_install_cloneable(
            &current->handles, KERNEL_OBJECT_PORT_SEND,
            KERNEL_PORT_SEND_RIGHTS, port,
            kernel_port_handle_retain, kernel_port_handle_release,
            (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND,
            &send_handle);
        if (handle_status != KERNEL_HANDLE_OK) {
            if (kernel_handle_close(&current->handles, receive_handle) !=
                KERNEL_HANDLE_OK)
                return KERNEL_PROCESS_CORRUPT;
            kernel_port_handle_release(
                port, (void *)(uintptr_t)KERNEL_PORT_ENDPOINT_SEND);
            if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
                *result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                return KERNEL_PROCESS_OK;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        thread->context.data[1] = receive_handle;
        thread->context.data[2] = send_handle;
        if (!kernel_port_pool_healthy() ||
            !kernel_handle_transfer_pool_healthy())
            return KERNEL_PROCESS_CORRUPT;
        return KERNEL_PROCESS_OK;
    }

    /*
     * Both remaining calls resolve the same handle argument against a different
     * object type and right, so the lookup is shared and only the pair differs.
     */
    handle_status = kernel_handle_lookup(
        &current->handles, thread->context.data[1],
        syscall == ASTRA_SYSCALL_PORT_SEND_TRY ? KERNEL_OBJECT_PORT_SEND :
                                                 KERNEL_OBJECT_PORT_RECEIVE,
        syscall == ASTRA_SYSCALL_PORT_SEND_TRY ? ASTRA_RIGHT_SIGNAL :
                                                 ASTRA_RIGHT_READ,
        (void **)&port);
    if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
        handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
        *result = ASTRA_SYSCALL_INVALID_HANDLE;
        return KERNEL_PROCESS_OK;
    }
    if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
        *result = ASTRA_SYSCALL_ACCESS_DENIED;
        return KERNEL_PROCESS_OK;
    }
    if (handle_status != KERNEL_HANDLE_OK || port == NULL)
        return KERNEL_PROCESS_CORRUPT;

    if (syscall == ASTRA_SYSCALL_PORT_SEND_TRY) {
        KernelHandle attached[ASTRA_MESSAGE_HANDLES_MAX];
        uint8_t message[ASTRA_MESSAGE_SIZE_MAX];
        KernelPerformanceToken performance;
        uint32_t user_message = thread->context.data[2];
        uint32_t message_size = thread->context.data[3];
        uint32_t user_handles = thread->context.data[4];
        uint32_t handle_count = thread->context.data[5];
        uint32_t woken = 0u;
        int copy_status;

        if (message_size < ASTRA_MESSAGE_HEADER_SIZE ||
            message_size > ASTRA_MESSAGE_SIZE_MAX ||
            handle_count > ASTRA_MESSAGE_HANDLES_MAX ||
            (handle_count != 0u &&
             (user_handles & (sizeof(KernelHandle) - 1u)) != 0u)) {
            *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        copy_status = kernel_copy_from_user(
            message, user_message, message_size);
        if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
            copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
            *result = ASTRA_SYSCALL_BAD_ADDRESS;
            return KERNEL_PROCESS_OK;
        }
        if (copy_status != KERNEL_USER_COPY_OK)
            return KERNEL_PROCESS_CORRUPT;
        if (!valid_message_header(message, message_size)) {
            *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        if (handle_count != 0u) {
            copy_status = kernel_copy_from_user(
                attached, user_handles,
                handle_count * sizeof(attached[0]));
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                *result = ASTRA_SYSCALL_BAD_ADDRESS;
                return KERNEL_PROCESS_OK;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
        }
        performance = kernel_performance_begin(
            KERNEL_PERFORMANCE_PORT_SEND);
        port_status = kernel_port_send(
            port, &current->handles, message, message_size,
            handle_count != 0u ? attached : NULL, handle_count, &woken);
        kernel_performance_end(performance);
        if (!port_status_to_syscall(port_status, result))
            return KERNEL_PROCESS_CORRUPT;
        if (port_status == KERNEL_PORT_WOULD_BLOCK) {
            uint32_t sequence;

            if (kernel_port_wait_sequence(
                    port, KERNEL_PORT_ENDPOINT_SEND, &sequence) !=
                KERNEL_PORT_OK)
                return KERNEL_PROCESS_CORRUPT;
            thread->port_probe_handle = thread->context.data[1];
            thread->port_probe_sequence = sequence;
        }
        if (port_status == KERNEL_PORT_OK && woken != 0u &&
            ready_thread_outranks(thread))
            ++scheduler_stats.wake_preemptions;
        if (!kernel_port_pool_healthy() ||
            !kernel_handle_transfer_pool_healthy())
            return KERNEL_PROCESS_CORRUPT;
        return KERNEL_PROCESS_OK;
    }

    {
        KernelPortReceipt receipt;
        KernelPerformanceToken performance;
        uint32_t user_message = thread->context.data[2];
        uint32_t message_capacity = thread->context.data[3];
        uint32_t user_handles = thread->context.data[4];
        uint32_t handle_capacity = thread->context.data[5];
        uint32_t required_size = 0u;
        uint32_t required_handles = 0u;
        uint32_t woken = 0u;
        int copy_status;

        thread->context.data[3] = 0u;

        if (message_capacity > ASTRA_MESSAGE_SIZE_MAX ||
            handle_capacity > ASTRA_MESSAGE_HANDLES_MAX ||
            (handle_capacity != 0u &&
             (user_handles & (sizeof(KernelHandle) - 1u)) != 0u)) {
            *result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            return KERNEL_PROCESS_OK;
        }
        performance = kernel_performance_begin(
            KERNEL_PERFORMANCE_PORT_RECEIVE);
        port_status = kernel_port_receive_prepare(
            port, &current->handles, message_capacity, handle_capacity,
            &receipt, &required_size, &required_handles);
        thread->context.data[1] = required_size;
        thread->context.data[2] = required_handles;
        if (port_status != KERNEL_PORT_OK) {
            kernel_performance_end(performance);
            if (!port_status_to_syscall(port_status, result))
                return KERNEL_PROCESS_CORRUPT;
            return KERNEL_PROCESS_OK;
        }
        thread->context.data[3] = receipt.sender;
        copy_status = kernel_copy_to_user(
            user_message, receipt.message, receipt.message_size);
        if (copy_status == KERNEL_USER_COPY_OK &&
            receipt.handle_count != 0u) {
            copy_status = kernel_copy_to_user(
                user_handles, receipt.import.handles,
                receipt.handle_count * sizeof(receipt.import.handles[0]));
        }
        if (copy_status != KERNEL_USER_COPY_OK) {
            if (kernel_port_receive_cancel(&receipt, &woken) !=
                KERNEL_PORT_OK) {
                kernel_performance_end(performance);
                return KERNEL_PROCESS_CORRUPT;
            }
            kernel_performance_end(performance);
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                *result = ASTRA_SYSCALL_BAD_ADDRESS;
                return KERNEL_PROCESS_OK;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        port_status = kernel_port_receive_commit(&receipt, &woken);
        kernel_performance_end(performance);
        if (port_status != KERNEL_PORT_OK)
            return KERNEL_PROCESS_CORRUPT;
        if (woken != 0u && ready_thread_outranks(thread))
            ++scheduler_stats.wake_preemptions;
        if (!kernel_port_pool_healthy() ||
            !kernel_handle_transfer_pool_healthy())
            return KERNEL_PROCESS_CORRUPT;
    }
    return KERNEL_PROCESS_OK;
}

KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelThread *thread;
    KernelProcessStatus status;
    KernelHandle port_probe_handle;
    uint32_t port_probe_sequence;
    uint32_t syscall;
    uint32_t result = ASTRA_SYSCALL_OK;
    bool wake_preemption_pending = false;
    bool qualification_progress = false;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current,
                             &thread);
    if (status != KERNEL_PROCESS_OK)
        return status;
    if (thread->context.vector != ASTRA_SYSCALL_VECTOR ||
        thread->context.frame_format != 0u)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    ++thread->syscall_count;
    if (++scheduler_stats.total_syscalls_low == 0u)
        ++scheduler_stats.total_syscalls_high;
    syscall = thread->context.data[0];
    port_probe_handle = thread->port_probe_handle;
    port_probe_sequence = thread->port_probe_sequence;
    thread->port_probe_handle = KERNEL_HANDLE_INVALID;
    thread->port_probe_sequence = 0u;
    switch (syscall) {
    case ASTRA_SYSCALL_QUERY_ABI:
        thread->context.data[1] = ASTRA_SYSCALL_ABI_VERSION;
        thread->context.data[2] = current->self_handle;
        thread->context.data[3] = thread->self_handle;
        break;
    case ASTRA_SYSCALL_LIBRARY_MAP: {
        uint32_t base = 0u;
        uint32_t span = 0u;
        KernelProcessStatus library_status = map_library(
            current, thread->context.data[1], thread->context.data[2],
            &base, &span);

        if (library_status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (library_status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            break;
        }
        if (library_status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (library_status != KERNEL_PROCESS_OK)
            return library_status;
        thread->context.data[1] = base;
        thread->context.data[2] = span;
        break;
    }
    case ASTRA_SYSCALL_LIBRARY_ATTACH: {
        AstraLibraryReference reference;
        KernelLibraryCacheEntry *cached = NULL;
        uint32_t slot = 0u;
        uint32_t base = 0u;
        uint32_t span = 0u;
        KernelProcessStatus library_status;

        if (kernel_copy_from_user(&reference, thread->context.data[1],
                                  sizeof(reference)) != KERNEL_USER_COPY_OK ||
            !library_reference_valid(&reference)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        for (slot = 0u; slot < ASTRA_LIBRARY_SLOT_COUNT; ++slot) {
            KernelLibraryCacheEntry *candidate = &library_cache[slot];

            if (candidate->used == 0u ||
                !library_reference_matches_request(&candidate->reference,
                                                   &reference))
                continue;
            if (cached == NULL ||
                library_reference_newer(&candidate->reference,
                                        &cached->reference))
                cached = candidate;
        }
        if (cached == NULL) {
            result = ASTRA_SYSCALL_WOULD_BLOCK;
            break;
        }
        slot = (uint32_t)(cached - library_cache);
        library_status = map_cached_library(current, cached, slot, &base,
                                            &span);
        if (library_status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (library_status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            break;
        }
        if (library_status != KERNEL_PROCESS_OK)
            return library_status;
        thread->context.data[1] = base;
        thread->context.data[2] = span;
        break;
    }
    /*
     * The character plane of the display device. These calls are gated by a
     * lease on that device with the rights the operation needs, the same way
     * block and input are: the screen has one owner, and saying which process
     * that is belongs in a capability rather than in this switch.
     */
    case ASTRA_SYSCALL_CONSOLE_INFO:
    case ASTRA_SYSCALL_CONSOLE_WRITE:
    case ASTRA_SYSCALL_CONSOLE_CURSOR: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceSnapshot snapshot;
        KernelHandleStatus handle_status;
        KernelDeviceStatus device_status;
        /*
         * Device rights are not generic rights: a lease carries QUERY,
         * TRANSFER and ADMINISTER only. Drawing is a transfer to the device
         * for the same reason submitting a block request is, and asking for
         * ASTRA_RIGHT_WRITE here would name a right no lease can hold.
         */
        uint32_t required_rights =
            syscall == ASTRA_SYSCALL_CONSOLE_INFO ? ASTRA_RIGHT_READ :
                                                    ASTRA_RIGHT_TRANSFER;
        uint32_t columns = 0u;
        uint32_t rows = 0u;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            required_rights, (void **)&lease);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;
        device_status = kernel_device_query(lease, &snapshot);
        if (!device_status_to_syscall(device_status, &result))
            return KERNEL_PROCESS_CORRUPT;
        if (device_status != KERNEL_DEVICE_OK)
            break;
        if (snapshot.class_id != ASTRA_DEVICE_CLASS_DISPLAY ||
            snapshot.device_id != ASTRA_DEVICE_ID_DISPLAY0) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (!kernel_platform_post_text_present()) {
            result = ASTRA_SYSCALL_IO_ERROR;
            break;
        }
        kernel_platform_post_text_geometry(&columns, &rows);
        if (syscall == ASTRA_SYSCALL_CONSOLE_INFO) {
            thread->context.data[1] = columns;
            thread->context.data[2] = rows;
            break;
        }
        if (syscall == ASTRA_SYSCALL_CONSOLE_CURSOR) {
            uint32_t row = thread->context.data[2];
            uint32_t column = thread->context.data[3];
            uint32_t visible = thread->context.data[4];

            /* A column at the edge is the terminal's pending wrap state. */
            if (row >= rows || column > columns || visible > 1u) {
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                break;
            }
            if (!kernel_platform_post_text_cursor(row, column,
                                                   visible != 0u))
                result = ASTRA_SYSCALL_IO_ERROR;
            break;
        }
        {
            uint8_t cells[ASTRA_CONSOLE_WRITE_MAX];
            uint32_t cell = thread->context.data[2];
            uint32_t user_cells = thread->context.data[3];
            uint32_t count = thread->context.data[4];
            uint32_t index;
            int copy_status;

            if (count == 0u || count > ASTRA_CONSOLE_WRITE_MAX ||
                cell > columns * rows || columns * rows - cell < count) {
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                break;
            }
            copy_status = kernel_copy_from_user(cells, user_cells, count);
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
            for (index = 0u; index < count; ++index) {
                if (!kernel_platform_post_text_write(cell + index,
                                                     cells[index])) {
                    result = ASTRA_SYSCALL_IO_ERROR;
                    break;
                }
            }
        }
        break;
    }
    case ASTRA_SYSCALL_DISPLAY_SUBMIT:
    case ASTRA_SYSCALL_DISPLAY_COLLECT: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceSnapshot snapshot;
        KernelHandleStatus handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            ASTRA_RIGHT_TRANSFER, (void **)&lease);

        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (kernel_device_query(lease, &snapshot) != KERNEL_DEVICE_OK) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        if (snapshot.class_id != ASTRA_DEVICE_CLASS_DISPLAY ||
            snapshot.device_id != ASTRA_DEVICE_ID_DISPLAY0) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if ((snapshot.capabilities &
             (ASTRA_DISPLAY_CAP_SOLID_FRAME |
              ASTRA_DISPLAY_CAP_FENCED_PRESENT)) !=
            (ASTRA_DISPLAY_CAP_SOLID_FRAME |
             ASTRA_DISPLAY_CAP_FENCED_PRESENT)) {
            result = ASTRA_SYSCALL_BAD_SYSCALL;
            break;
        }
        result = display_syscall(current, thread, syscall,
                                 snapshot.generation);
        break;
    }
    /*
     * The diagnostic channel: a bounded line of text from a process that holds
     * ASTRA_RIGHT_DEBUG over itself. The right is the whole of the policy --
     * a build with no debug surface grants it to nobody and this answers
     * ACCESS_DENIED to everyone.
     */
    case ASTRA_SYSCALL_ACTIVITY: {
        /*
         * Ids come from one kernel counter, so they are unique across every
         * process without anybody coordinating. Zero is never issued: it is
         * what "no activity" means, and an allocator that could return it
         * would make the two indistinguishable.
         */
        uint32_t requested = thread->context.data[1];
        uint32_t previous = current_thread->activity;

        if (requested == ASTRA_ACTIVITY_CURRENT) {
            /* Query only. */
        } else if (requested == ASTRA_ACTIVITY_NONE) {
            current_thread->activity = 0u;
        } else if (requested == 0u) {
            ++next_activity;
            if (next_activity == 0u ||
                next_activity == ASTRA_ACTIVITY_CURRENT ||
                next_activity == ASTRA_ACTIVITY_NONE)
                next_activity = 1u;
            current_thread->activity = next_activity;
        } else {
            current_thread->activity = requested;
        }
        thread->context.data[1] = current_thread->activity;
        thread->context.data[2] = previous;
        break;
    }
    case ASTRA_SYSCALL_LOG_WRITE: {
        /*
         * An event append, and nothing gates it.
         *
         * This used to demand a process handle carrying ASTRA_RIGHT_DEBUG and
         * then check the handle named the caller. Both are gone: a process may
         * only speak for itself, and with no handle there is nothing to check
         * and nothing to get wrong -- the kernel already knows who is calling.
         * A machine whose account of itself depends on a capability has holes
         * exactly where something went wrong, so the right moved to reading.
         */
        uint8_t payload[ASTRA_EVENT_ARGUMENT_MAX];
        KernelTraceUserRecord record;
        uint32_t message = thread->context.data[1];
        uint32_t flags = thread->context.data[2];
        uint32_t user_payload = thread->context.data[3];
        uint32_t length = thread->context.data[4];
        int copy_status;

        if (message == 0u || flags > UINT16_MAX ||
            (flags & ~(uint32_t)ASTRA_EVENT_FLAG_MASK) != 0u ||
            (flags & ASTRA_EVENT_LEVEL_MASK) > ASTRA_EVENT_LEVEL_ERROR ||
            length > ASTRA_EVENT_ARGUMENT_MAX ||
            (user_payload == 0u) != (length == 0u)) {
            ++scheduler_stats.diagnostic_log_refusals;
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (length != 0u) {
            copy_status = kernel_copy_from_user(payload, user_payload, length);
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                ++scheduler_stats.diagnostic_log_refusals;
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
        }
        /*
         * The ring refuses only what this handler has already checked, so a
         * refusal here means the ring itself is unusable. The event is lost
         * and counted, and the call still succeeds: a program is never made to
         * fail because the machine could not write down what it said.
         */
        if (kernel_trace_write_user(message, current->id,
                                    (uint16_t)current_thread->id,
                                    current_thread->activity, (uint16_t)flags,
                                    length != 0u ? payload : NULL, length)) {
            ++scheduler_stats.diagnostic_logs;
            scheduler_stats.diagnostic_log_bytes += length;
        } else {
            ++scheduler_stats.diagnostic_log_refusals;
        }
        /*
         * The console is a sink on the stream, not a second destination: the
         * record is already in the ring by the time it renders, so the two
         * cannot disagree about order or about what was said. It renders even
         * when the ring refused, because a machine that cannot write to its
         * own ring is exactly when someone needs to be told something -- and
         * the counter above is what says the record itself was lost.
         */
        record.event = KERNEL_TRACE_EVENT_USER;
        record.flags = (uint16_t)flags;
        record.process = current->id;
        record.message = message;
        record.activity = current_thread->activity;
        record.thread = (uint16_t)current_thread->id;
        record.payload_length = (uint16_t)length;
        if (diagnostic_console_open) {
            kernel_process_diagnostic_log(&record, payload, length);
        }
        break;
    }
    case ASTRA_SYSCALL_PROCESS_CREATE: {
        /*
         * A launch, and the rule that makes it safe to expose: a parent hands a
         * child a subset of what the parent already holds. Nothing here can
         * produce a capability that did not exist a moment earlier, so what a
         * program may touch is always something somebody wrote down.
         *
         * There is no fork, so there is nothing to inherit implicitly and no
         * second path that answers the same question differently.
         */
        AstraLaunchGrant grants[ASTRA_LAUNCH_GRANT_MAX];
        KernelProcessBootstrapCapability requested[ASTRA_LAUNCH_GRANT_MAX];
        char names[ASTRA_LAUNCH_GRANT_MAX][ASTRA_CAPABILITY_NAME_MAX];
        AstraLaunchArguments arguments;
        uint32_t image = thread->context.data[1];
        uint32_t image_size = thread->context.data[2];
        uint32_t grant_address = thread->context.data[3];
        uint32_t grant_count = thread->context.data[4];
        uint32_t argument_address = thread->context.data[5];
        uint32_t child_id = 0u;
        KernelHandle child_handle = KERNEL_HANDLE_INVALID;
        KernelProcessStatus launch_status;
        int copy_status;

        if (image == 0u || image_size == 0u ||
            grant_count > ASTRA_LAUNCH_GRANT_MAX ||
            (grant_count != 0u) != (grant_address != 0u)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        /*
         * Room for the handle the caller will be given, checked before
         * anything is built. A child that exists and cannot be handed to its
         * launcher is a process nobody can wait for, and unwinding one is
         * strictly harder than not starting it.
         */
        if (kernel_handle_available(&current->handles) == 0u) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        kernel_bytes_clear(requested, sizeof(requested));
        if (grant_count != 0u) {
            copy_status = kernel_copy_from_user(
                grants, grant_address,
                grant_count * (uint32_t)sizeof(grants[0]));
            if (copy_status != KERNEL_USER_COPY_OK) {
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
        }
        result = copy_launch_arguments(argument_address, &arguments);
        if (result != ASTRA_SYSCALL_OK)
            break;
        if (argument_address != 0u) {
            if ((arguments.flags & ASTRA_LAUNCH_FLAG_ESSENTIAL) != 0u &&
                current->id != initial_image_process_id) {
                result = ASTRA_SYSCALL_ACCESS_DENIED;
                break;
            }
        }
        for (uint32_t index = 0u; index < grant_count; ++index) {
            /*
             * The name is copied out of the wire record and terminated here,
             * because everything below takes a C string and bytes off a
             * caller's memory are not one until somebody says so.
             */
            for (uint32_t at = 0u; at + 1u < ASTRA_CAPABILITY_NAME_MAX; ++at)
                names[index][at] = grants[index].name[at];
            names[index][ASTRA_CAPABILITY_NAME_MAX - 1u] = '\0';
            /*
             * A flag bit nobody interprets today is a bit that means something
             * else tomorrow, so an unknown one is refused rather than carried.
             * Accepting it would make the field unversionable: a child built
             * against a later ABI could not tell a flag it was granted from one
             * that happened to be set.
             */
            if (names[index][0] == '\0' || grants[index].rights == 0u ||
                (grants[index].flags & ~(uint32_t)ASTRA_CAPABILITY_FLAG_MASK) !=
                    0u) {
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                break;
            }
            /*
             * The root is validated in place rather than copied out: `grants`
             * is the kernel's own copy already, so a field with a NUL in it is
             * a C string here. A field without one is not a root and is
             * refused -- carrying it would read past the record.
             *
             * A leading separator or a `..` component is refused for a
             * different reason: this is where a root enters the system from
             * outside, and a root that climbs is not a root. Resolution's own
             * `..` rule is unaffected and still runs later.
             */
            {
                uint32_t at;
                int terminated = 0;

                for (at = 0u; at < ASTRA_CAPABILITY_ROOT_MAX; ++at) {
                    if (grants[index].root[at] == '\0') {
                        terminated = 1;
                        break;
                    }
                }
                if (!terminated || grants[index].root[0] == '/') {
                    result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                    break;
                }
                for (at = 0u; at + 1u < ASTRA_CAPABILITY_ROOT_MAX &&
                                grants[index].root[at] != '\0'; ++at) {
                    if (grants[index].root[at] != '.' ||
                        grants[index].root[at + 1u] != '.')
                        continue;
                    if ((at == 0u || grants[index].root[at - 1u] == '/') &&
                        (grants[index].root[at + 2u] == '\0' ||
                         grants[index].root[at + 2u] == '/')) {
                        result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                        break;
                    }
                }
                if (result != ASTRA_SYSCALL_OK)
                    break;
            }
            requested[index].root = grants[index].root;
            requested[index].name = names[index];
            requested[index].rights = grants[index].rights;
            requested[index].flags = grants[index].flags;
            requested[index].source_handle = grants[index].handle;
            requested[index].kind = KERNEL_PROCESS_BOOTSTRAP_HANDLE;
        }
        if (result != ASTRA_SYSCALL_OK)
            break;

        launch_status = kernel_process_launch(
            NULL, image_size, image, &current->handles, requested, grant_count,
            arguments.count != 0u || arguments.environment_count != 0u ||
                    arguments.flags != 0u ?
                &arguments : NULL,
            arguments.count != 0u ? syscall_data : NULL,
            arguments.environment_count != 0u ?
                syscall_data + arguments.length : NULL,
            &child_id);
        if (launch_status != KERNEL_PROCESS_OK) {
            ++scheduler_stats.launch_failures;
            scheduler_stats.last_launch_failure = (uint32_t)launch_status;
        }
        if (launch_status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (launch_status == KERNEL_PROCESS_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (launch_status == KERNEL_PROCESS_INVALID_HANDLE) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (launch_status == KERNEL_PROCESS_NO_SLOT ||
            launch_status == KERNEL_PROCESS_OUT_OF_MEMORY ||
            launch_status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (launch_status != KERNEL_PROCESS_OK)
            return KERNEL_PROCESS_CORRUPT;

        /*
         * What the launcher gets back: enough to watch it, wait for it, end it
         * and set its ordinary priority. DEBUG is not implied by having
         * started something -- reading another process's account of itself is
         * a separate grant, and it has to stay one.
         */
        launch_status = kernel_process_grant_handle(
            current->id, child_id,
            KERNEL_PROCESS_RIGHT_QUERY | KERNEL_PROCESS_RIGHT_WAIT |
                KERNEL_PROCESS_RIGHT_TERMINATE |
                KERNEL_PROCESS_RIGHT_PRIORITY,
            &child_handle);
        /* The slot was reserved above, so a failure here is not a shortage. */
        if (launch_status != KERNEL_PROCESS_OK)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = child_handle;
        thread->context.data[2] = child_id;
        break;
    }
    case ASTRA_SYSCALL_PROCESS_CLONE: {
        KernelHandle child_handle = KERNEL_HANDLE_INVALID;
        uint32_t child_id = 0u;
        KernelProcessStatus clone_status = clone_current_process(
            current, thread, &child_id, &child_handle);

        if (clone_status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            break;
        }
        if (clone_status == KERNEL_PROCESS_NO_SLOT ||
            clone_status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (clone_status != KERNEL_PROCESS_OK)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = child_handle;
        thread->context.data[2] = child_id;
        break;
    }
    case ASTRA_SYSCALL_PROCESS_EXEC: {
        AstraExecRequest request;
        uint32_t request_address = thread->context.data[3];
        KernelProcessStatus exec_status;

        if (thread->context.data[1] == 0u ||
            thread->context.data[2] == 0u ||
            request_address < KERNEL_VM_USER_MIN ||
            request_address > UINT32_MAX -
                (uint32_t)offsetof(AstraExecRequest, arguments) ||
            kernel_copy_from_user(&request, request_address,
                                  sizeof(request)) != KERNEL_USER_COPY_OK) {
            result = ASTRA_SYSCALL_BAD_ADDRESS;
            break;
        }
        result = copy_launch_arguments(
            request_address + (uint32_t)offsetof(AstraExecRequest, arguments),
            &request.arguments);
        if (result != ASTRA_SYSCALL_OK)
            break;
        exec_status = replace_process_image(
            current, thread, thread->context.data[1],
            thread->context.data[2], &request, next_context);
        if (exec_status == KERNEL_PROCESS_OK) {
            if (*next_context == NULL)
                return KERNEL_PROCESS_CORRUPT;
            return KERNEL_PROCESS_OK;
        }
        if (exec_status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (exec_status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            break;
        }
        if (exec_status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        return exec_status;
    }
    case ASTRA_SYSCALL_TRACE_READ: {
        /*
         * Reading the stream, which is the half that is authority.
         *
         * Emitting is universal because an account of the machine with holes
         * where something went wrong is not an account. Reading is the
         * opposite: it is every process's events at once, which is where a
         * secret leaks, so it takes a capability and only a build carrying a
         * diagnostic surface puts one on a process's own handle.
         */
        AstraEventDrained events[ASTRA_TRACE_READ_BATCH_MAX];
        KernelProcess *target = NULL;
        KernelHandleStatus handle_status;
        uint32_t cursor = thread->context.data[2];
        uint32_t user_buffer = thread->context.data[3];
        uint32_t capacity = thread->context.data[4];
        uint32_t copied;
        uint32_t lost = 0u;
        int copy_status;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_PROCESS,
            KERNEL_PROCESS_RIGHT_DEBUG, (void **)&target);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || target == NULL)
            return KERNEL_PROCESS_CORRUPT;
        /*
         * The handle must name the caller. DEBUG over another process is a
         * debugger's authority over that process; using it to read the whole
         * machine's stream would be laundering an authority nobody granted
         * through a bystander that happens to be observable.
         */
        if (target != current) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (user_buffer == 0u || capacity == 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (capacity > ASTRA_TRACE_READ_BATCH_MAX)
            capacity = ASTRA_TRACE_READ_BATCH_MAX;

        /*
         * Somebody with the authority to read the stream is reading it, so the
         * console stops narrating. See diagnostic_console_open.
         */
        diagnostic_console_open = 0u;
        copied = kernel_trace_drain_user(cursor, events, capacity, &cursor,
                                         &lost);
        if (copied != 0u) {
            copy_status = kernel_copy_to_user(
                user_buffer, events, copied * ASTRA_EVENT_DRAINED_SIZE);
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
        }
        thread->context.data[1] = copied;
        thread->context.data[2] = cursor;
        thread->context.data[3] = lost;
        break;
    }
#if ASTRA_KERNEL_DEBUG_SURFACE
    case ASTRA_SYSCALL_IRQ_ENDPOINT_INFO: {
        /*
         * What the machine's interrupt endpoints are doing.
         *
         * On the same terms as reading the trace stream, and for the same
         * reason: it is every process's devices at once, so it takes the
         * diagnostic capability rather than being universal. A device that
         * quarantined itself is otherwise invisible -- it looks exactly like a
         * device nobody is using, while every call against it comes back with
         * an I/O error whose cause is three layers down. Finding that out once
         * without this cost a session.
         */
        AstraIrqEndpointInfo info;
        KernelProcess *target = NULL;
        KernelHandleStatus handle_status;
        uint32_t slot = thread->context.data[2];
        uint32_t user_buffer = thread->context.data[3];
        int copy_status;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_PROCESS,
            KERNEL_PROCESS_RIGHT_DEBUG, (void **)&target);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || target == NULL)
            return KERNEL_PROCESS_CORRUPT;
        /* The handle must name the caller. See ASTRA_SYSCALL_TRACE_READ. */
        if (target != current) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        /*
         * How many slots there are, always -- so a caller can size its loop
         * from the first call rather than from a constant it compiled in.
         */
        thread->context.data[1] = KERNEL_IRQ_ENDPOINT_MAX;
        if (user_buffer == 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (!kernel_irq_endpoint_info(slot, &info)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        copy_status = kernel_copy_to_user(user_buffer, &info, sizeof(info));
        if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
            copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_BAD_ADDRESS;
            break;
        }
        if (copy_status != KERNEL_USER_COPY_OK)
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
#endif
    case ASTRA_SYSCALL_PROGRESS:
        {
            bool qualification_handled;

            status = qualification_command(
                current, thread, &result, &qualification_handled);
            if (status != KERNEL_PROCESS_OK)
                return status;
            if (qualification_handled) {
                qualification_progress = true;
                break;
            }
            if (thread->context.data[1] < current->progress)
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            else {
                current->progress = thread->context.data[1];
                if (current->progress >= KERNEL_PROCESS_PROGRESS_GOAL)
                    milestone_progress_ready = 1u;
                /*
                 * The initial image reports how far it has come up through
                 * the same counter every process has. Nothing new is needed
                 * for the kernel to know its service reached each stage.
                 */
                if (initial_image_process_id != 0u &&
                    current->id == initial_image_process_id &&
                    current->progress > initial_image_progress) {
                    initial_image_progress = current->progress;
                    kernel_process_initial_image_progress(
                        initial_image_progress);
                }
            }
        }
        break;
    case ASTRA_SYSCALL_YIELD: {
        KernelThread *next = NULL;

        thread->context.data[0] = ASTRA_SYSCALL_OK;
        if (kernel_thread_make_ready(thread) != KERNEL_THREAD_OK ||
            kernel_thread_take_next(&next) != KERNEL_THREAD_OK ||
            next == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (next != thread)
            ++scheduler_stats.voluntary_switches;
        return activate(next, next_context);
    }
    case ASTRA_SYSCALL_EXIT:
        thread->context.data[0] = ASTRA_SYSCALL_OK;
        return retire_current(KERNEL_PROCESS_EXIT_SYSCALL,
                              thread->context.data[1], next_context);
    case ASTRA_SYSCALL_THREAD_CREATE: {
        KernelPreparedThread prepared_thread;
        KernelHandle created_handle;
        KernelProcessStatus create_status;
        uint32_t created_id;
        uint32_t entry = thread->context.data[1];
        uint32_t priority = thread->context.data[3];
        uint32_t rights = thread->context.data[4];

        if (!entry_within_code(current, entry) ||
            priority < KERNEL_THREAD_PRIORITY_USER_MIN ||
            priority > current->priority_ceiling || priority > UINT8_MAX ||
            rights == 0u || (rights & ~KERNEL_THREAD_RIGHTS) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            ++scheduler_stats.thread_creation_failures;
            break;
        }
        /* Keep allocation, clearing, rollback, and PMMU work interruptible. */
        kernel_enable_interrupts();
        create_status = prepare_thread(
            current, entry, thread->context.data[2], (uint8_t)priority,
            rights, &prepared_thread);
        /* Publication and ready-queue accounting are one masked commit. */
        kernel_disable_interrupts();
        if (create_status == KERNEL_PROCESS_OK) {
            create_status = commit_thread(&prepared_thread, &created_id,
                                          &created_handle);
            if (create_status != KERNEL_PROCESS_OK) {
                kernel_enable_interrupts();
                (void)abort_prepared_thread(&prepared_thread);
                kernel_disable_interrupts();
            }
        }
        if (create_status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            ++scheduler_stats.thread_creation_failures;
            break;
        }
        if (create_status == KERNEL_PROCESS_RESOURCE_LIMIT ||
            create_status == KERNEL_PROCESS_NO_SLOT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            ++scheduler_stats.thread_creation_failures;
            break;
        }
        if (create_status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            ++scheduler_stats.thread_creation_failures;
            break;
        }
        if (create_status != KERNEL_PROCESS_OK ||
            created_handle == KERNEL_HANDLE_INVALID || created_id == 0u)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = created_handle;
        thread->context.data[2] = created_id;
        break;
    }
    case ASTRA_SYSCALL_THREAD_EXIT:
        thread->context.data[0] = ASTRA_SYSCALL_OK;
        return retire_current_thread(thread->context.data[1], next_context);
    case ASTRA_SYSCALL_CLOSE:
        switch (kernel_handle_close(&current->handles,
                                    thread->context.data[1])) {
        case KERNEL_HANDLE_OK:
#if defined(KERNEL_PROCESS_HOST_TEST)
            if (!kernel_sync_pool_valid() || !kernel_thread_pool_valid() ||
                !kernel_port_pool_valid() ||
                !kernel_area_pool_valid() || !kernel_ring_pool_valid() ||
                !kernel_irq_pool_valid() || !kernel_device_pool_valid() ||
                !kernel_handle_transfer_pool_valid() ||
                !process_pool_valid())
#else
            if (!kernel_sync_pool_healthy() || !kernel_thread_pool_valid() ||
                !kernel_port_pool_healthy() ||
                !kernel_area_pool_healthy() || !kernel_ring_pool_healthy() ||
                !kernel_irq_pool_healthy() || !kernel_device_pool_valid() ||
                !kernel_handle_transfer_pool_healthy() ||
                !process_pool_healthy())
#endif
                return KERNEL_PROCESS_CORRUPT;
            scheduler_timer_rearm();
            break;
        case KERNEL_HANDLE_INVALID_HANDLE:
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        case KERNEL_HANDLE_ACCESS_DENIED:
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        default:
            return KERNEL_PROCESS_CORRUPT;
        }
        break;
    case ASTRA_SYSCALL_HANDLE_DUPLICATE: {
        KernelHandle duplicate = KERNEL_HANDLE_INVALID;
        KernelHandleStatus handle_status = kernel_handle_duplicate(
            &current->handles, thread->context.data[1],
            thread->context.data[2], &duplicate);

        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (handle_status == KERNEL_HANDLE_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (handle_status == KERNEL_HANDLE_INVALID_STATE) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK ||
            duplicate == KERNEL_HANDLE_INVALID)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = duplicate;
        break;
    }
    case ASTRA_SYSCALL_CLOCK_MONOTONIC: {
        uint64_t nanoseconds = kernel_platform_monotonic_ns();

        thread->context.data[1] = (uint32_t)(nanoseconds >> 32);
        thread->context.data[2] = (uint32_t)nanoseconds;
        break;
    }
    case ASTRA_SYSCALL_CLOCK_REALTIME: {
        uint64_t nanoseconds = 0u;
        int32_t utc_offset = 0;
        uint32_t zone = 0u;

        /*
         * Read at the moment it is asked for, not derived from a boot-time
         * sample and the monotonic counter. The clock this machine reads is
         * kept right by something else -- NTP, one layer down -- and a value
         * carried forward from boot would answer with this machine's own
         * drift rather than with the correction.
         */
        if (!kernel_platform_wall_clock(&nanoseconds, &utc_offset, &zone)) {
            result = ASTRA_SYSCALL_UNSUPPORTED;
            break;
        }
        thread->context.data[1] = (uint32_t)(nanoseconds >> 32);
        thread->context.data[2] = (uint32_t)nanoseconds;
        /*
         * The zone rides with the instant rather than being a second call: a
         * program that asked twice could straddle a summer-time change and
         * render an hour that never happened.
         */
        thread->context.data[3] = (uint32_t)utc_offset;
        thread->context.data[4] = zone;
        break;
    }
    case ASTRA_SYSCALL_EVENT_CREATE:
    case ASTRA_SYSCALL_SEMAPHORE_CREATE: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandle handle;
        uint32_t rights = syscall == ASTRA_SYSCALL_EVENT_CREATE ?
            thread->context.data[2] : thread->context.data[3];

        if (rights == 0u || (rights & ~KERNEL_SYNC_RIGHTS) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (syscall == ASTRA_SYSCALL_EVENT_CREATE) {
            sync_status = kernel_sync_create_event(
                current->id, thread->context.data[1], &object);
        } else {
            sync_status = kernel_sync_create_semaphore(
                current->id, thread->context.data[1],
                thread->context.data[2], &object);
        }
        if (sync_status == KERNEL_SYNC_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (sync_status == KERNEL_SYNC_NO_SLOT ||
            sync_status == KERNEL_SYNC_QUOTA_EXCEEDED) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        switch (kernel_handle_install_cloneable(
            &current->handles, KERNEL_OBJECT_SYNC, rights, object,
            kernel_sync_handle_retain, kernel_sync_handle_release, NULL,
            &handle)) {
        case KERNEL_HANDLE_OK:
            thread->context.data[1] = handle;
            break;
        case KERNEL_HANDLE_TABLE_FULL:
            kernel_sync_abandon_unpublished(object);
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        default:
            kernel_sync_abandon_unpublished(object);
            return KERNEL_PROCESS_CORRUPT;
        }
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_sync_pool_valid())
#else
        if (!kernel_sync_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_TIMER_CREATE: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandle handle;
        uint32_t rights = thread->context.data[1];

        if (rights == 0u || (rights & ~KERNEL_TIMER_RIGHTS) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        sync_status = kernel_sync_create_timer(current->id, &object);
        if (sync_status == KERNEL_SYNC_NO_SLOT ||
            sync_status == KERNEL_SYNC_QUOTA_EXCEEDED) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        switch (kernel_handle_install_cloneable(
            &current->handles, KERNEL_OBJECT_TIMER, rights, object,
            kernel_sync_handle_retain, kernel_sync_handle_release, NULL,
            &handle)) {
        case KERNEL_HANDLE_OK:
            thread->context.data[1] = handle;
            ++scheduler_stats.timer_created;
            break;
        case KERNEL_HANDLE_TABLE_FULL:
            kernel_sync_abandon_unpublished(object);
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        default:
            kernel_sync_abandon_unpublished(object);
            return KERNEL_PROCESS_CORRUPT;
        }
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_sync_pool_valid())
#else
        if (!kernel_sync_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_AREA_CREATE: {
        KernelArea *area = NULL;
        KernelAreaStatus area_status;
        KernelHandle handle = KERNEL_HANDLE_INVALID;
        KernelHandleStatus handle_status;
        uint32_t rights = thread->context.data[2];
        uint32_t area_flags = thread->context.data[3];

        if (rights == 0u || (rights & ~KERNEL_AREA_RIGHTS) != 0u ||
            (area_flags & ~(uint32_t)ASTRA_AREA_CREATE_RESERVED) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        kernel_enable_interrupts();
        area_status = kernel_area_create(
            current->id, thread->context.data[1],
            (area_flags & ASTRA_AREA_CREATE_RESERVED) != 0u ?
                KERNEL_AREA_CREATE_RESERVED : 0u,
            &area);
        kernel_disable_interrupts();
        if (area_status != KERNEL_AREA_OK) {
            if (!area_status_to_syscall(area_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (area == NULL)
            return KERNEL_PROCESS_CORRUPT;
        handle_status = kernel_handle_install_cloneable(
            &current->handles, KERNEL_OBJECT_AREA, rights, area,
            kernel_area_handle_retain, kernel_area_handle_release, NULL,
            &handle);
        if (handle_status != KERNEL_HANDLE_OK) {
            kernel_enable_interrupts();
            kernel_area_abandon_unpublished(area);
            kernel_disable_interrupts();
            if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
                result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                break;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        thread->context.data[1] = handle;
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_area_pool_valid())
#else
        if (!kernel_area_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_AREA_MAP: {
        KernelArea *area = NULL;
        KernelAreaStatus area_status;
        KernelHandleStatus handle_status;
        uint32_t permissions = thread->context.data[2];
        uint32_t virtual_base;
        uint32_t byte_size;
        uint32_t required_rights;

        if ((permissions & ASTRA_AREA_MAP_READ) == 0u ||
            (permissions & ~(ASTRA_AREA_MAP_READ |
                             ASTRA_AREA_MAP_WRITE)) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        required_rights = ASTRA_RIGHT_MAP | permissions;
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_AREA,
            required_rights, (void **)&area);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || area == NULL)
            return KERNEL_PROCESS_CORRUPT;
        kernel_enable_interrupts();
        area_status = kernel_area_map(
            area, current->id, &current->address_space, permissions,
            &virtual_base, &byte_size);
        kernel_disable_interrupts();
        if (area_status != KERNEL_AREA_OK) {
            if (!area_status_to_syscall(area_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        thread->context.data[1] = virtual_base;
        thread->context.data[2] = byte_size;
        break;
    }
    case ASTRA_SYSCALL_AREA_UNMAP: {
        KernelAreaStatus area_status;

        kernel_enable_interrupts();
        area_status = kernel_area_unmap(
            current->id, &current->address_space, thread->context.data[1]);
        kernel_disable_interrupts();
        if (area_status != KERNEL_AREA_OK &&
            !area_status_to_syscall(area_status, &result))
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_AREA_DECOMMIT: {
        KernelAreaStatus area_status;
        uint32_t released = 0u;

        kernel_enable_interrupts();
        area_status = kernel_area_decommit(
            current->id, &current->address_space, thread->context.data[1],
            thread->context.data[2], &released);
        kernel_disable_interrupts();
        if (area_status != KERNEL_AREA_OK) {
            if (!area_status_to_syscall(area_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        thread->context.data[1] = released;
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_area_pool_valid())
#else
        if (!kernel_area_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_VM_PRIVATE_RESERVE: {
        uint32_t virtual_base = 0u;
        uint32_t mapped_span = 0u;
        KernelVmStatus vm_status = kernel_vm_private_reserve(
            &current->address_space, thread->context.data[1],
            thread->context.data[2], &virtual_base, &mapped_span);

        if (vm_status != KERNEL_VM_OK) {
            if (!vm_status_to_syscall(vm_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        thread->context.data[1] = virtual_base;
        thread->context.data[2] = mapped_span;
        break;
    }
    case ASTRA_SYSCALL_VM_PRIVATE_DECOMMIT: {
        uint32_t released = 0u;
        KernelVmStatus vm_status = kernel_vm_private_decommit(
            &current->address_space, thread->context.data[1],
            thread->context.data[2], &released);

        if (vm_status != KERNEL_VM_OK) {
            if (!vm_status_to_syscall(vm_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        thread->context.data[1] = released;
        break;
    }
    case ASTRA_SYSCALL_SIGNAL_CONFIGURE: {
        uint32_t trampoline = thread->context.data[1];
        uint32_t stack_top = thread->context.data[2];
        uint32_t blocked = thread->context.data[3];

        if (trampoline < KERNEL_VM_USER_MIN ||
            trampoline > KERNEL_VM_USER_MAX || stack_top < 8u ||
            (stack_top & 3u) != 0u ||
            kernel_vm_probe_current(trampoline, false, NULL) ==
                KERNEL_VM_MAPPING_UNMAPPED ||
            kernel_vm_probe_current(stack_top - 8u, false, NULL) !=
                KERNEL_VM_MAPPING_READ_WRITE) {
            result = ASTRA_SYSCALL_BAD_ADDRESS;
            break;
        }
        thread->context.data[1] = current->signal_pending;
        thread->context.data[2] = current->signal_blocked;
        current->signal_trampoline = trampoline;
        current->signal_stack_top = stack_top;
        current->signal_blocked =
            blocked & ~((1u << 9u) | (1u << 19u));
        break;
    }
    case ASTRA_SYSCALL_INTERVAL_TIMER: {
        uint64_t delay_ns = ((uint64_t)thread->context.data[1] << 32) |
                            thread->context.data[2];
        uint64_t period_ns = ((uint64_t)thread->context.data[3] << 32) |
                             thread->context.data[4];
        uint64_t now = scheduler_cycles();
        uint64_t old_remaining =
            current->interval_deadline > now ?
                kernel_platform_cycles_to_ns(
                    current->interval_deadline - now) : 0u;
        uint64_t old_period =
            kernel_platform_cycles_to_ns(current->interval_period);
        uint64_t delay_cycles = 0u;
        uint64_t period_cycles = 0u;
        bool query = delay_ns == UINT64_MAX && period_ns == UINT64_MAX;

        if (!query && ((delay_ns & (1ull << 63)) != 0u ||
            (period_ns & (1ull << 63)) != 0u ||
            (delay_ns != 0u &&
             !kernel_platform_deadline_to_cycles((int64_t)delay_ns,
                                                  &delay_cycles)) ||
            (period_ns != 0u &&
             !kernel_platform_deadline_to_cycles((int64_t)period_ns,
                                                  &period_cycles)) ||
            (delay_cycles != 0u && UINT64_MAX - now < delay_cycles))) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (!query) {
            current->interval_deadline =
                delay_cycles == 0u ? 0u : now + delay_cycles;
            current->interval_period = period_cycles;
            current->signal_target_thread = thread->id;
        }
        thread->context.data[1] = (uint32_t)(old_remaining >> 32);
        thread->context.data[2] = (uint32_t)old_remaining;
        thread->context.data[3] = (uint32_t)(old_period >> 32);
        thread->context.data[4] = (uint32_t)old_period;
        if (!query)
            scheduler_timer_rearm_at(now);
        break;
    }
    case ASTRA_SYSCALL_SIGNAL_RETURN:
        if (thread->slot >= KERNEL_THREAD_MAX ||
            signal_context_active[thread->slot] == 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        kernel_bytes_copy(&thread->context,
                          &signal_saved_context[thread->slot],
                          sizeof(thread->context));
        signal_context_active[thread->slot] = 0u;
        *next_context = runtime_resume(thread);
        return KERNEL_PROCESS_OK;
    case ASTRA_SYSCALL_RING_CREATE: {
        KernelArea *area = NULL;
        KernelRing *ring = NULL;
        KernelHandle producer = KERNEL_HANDLE_INVALID;
        KernelHandle consumer = KERNEL_HANDLE_INVALID;
        KernelHandleStatus handle_status;
        KernelRingStatus ring_status;
        bool cloneable =
            (thread->context.data[5] &
             ASTRA_BULK_RING_CREATE_KERNEL_COPY) != 0u;

        if (kernel_handle_available(&current->handles) < 2u) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_AREA,
            ASTRA_RIGHT_ADMINISTER, (void **)&area);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || area == NULL)
            return KERNEL_PROCESS_CORRUPT;
        ring_status = kernel_ring_create_flagged(
            current->id, area, thread->context.data[2],
            thread->context.data[3], thread->context.data[4],
            thread->context.data[5], &ring);
        if (ring_status != KERNEL_RING_OK) {
            if (!ring_status_to_syscall(ring_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (ring == NULL)
            return KERNEL_PROCESS_CORRUPT;
        handle_status = cloneable ?
            kernel_handle_install_cloneable(
                &current->handles, KERNEL_OBJECT_RING_PRODUCER,
                KERNEL_RING_PRODUCER_RIGHTS, ring, kernel_ring_handle_retain,
                kernel_ring_handle_release,
                (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER, &producer) :
            kernel_handle_install(
                &current->handles, KERNEL_OBJECT_RING_PRODUCER,
                KERNEL_RING_PRODUCER_RIGHTS, ring, kernel_ring_handle_release,
                (void *)(uintptr_t)KERNEL_RING_ENDPOINT_PRODUCER, &producer);
        if (handle_status != KERNEL_HANDLE_OK) {
            kernel_ring_abandon_unpublished(ring);
            if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
                result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                break;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        handle_status = cloneable ?
            kernel_handle_install_cloneable(
                &current->handles, KERNEL_OBJECT_RING_CONSUMER,
                KERNEL_RING_CONSUMER_RIGHTS, ring, kernel_ring_handle_retain,
                kernel_ring_handle_release,
                (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER, &consumer) :
            kernel_handle_install(
                &current->handles, KERNEL_OBJECT_RING_CONSUMER,
                KERNEL_RING_CONSUMER_RIGHTS, ring, kernel_ring_handle_release,
                (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER, &consumer);
        if (handle_status != KERNEL_HANDLE_OK) {
            if (kernel_handle_close(&current->handles, producer) !=
                KERNEL_HANDLE_OK)
                return KERNEL_PROCESS_CORRUPT;
            kernel_ring_handle_release(
                ring, (void *)(uintptr_t)KERNEL_RING_ENDPOINT_CONSUMER);
            if (handle_status == KERNEL_HANDLE_TABLE_FULL) {
                result = ASTRA_SYSCALL_RESOURCE_LIMIT;
                break;
            }
            return KERNEL_PROCESS_CORRUPT;
        }
        thread->context.data[1] = producer;
        thread->context.data[2] = consumer;
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_ring_pool_valid() || !kernel_area_pool_valid())
#else
        if (!kernel_ring_pool_healthy() || !kernel_area_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_RING_NOTIFY: {
        KernelObjectType type;
        KernelRing *ring = NULL;
        KernelHandleStatus handle_status;
        KernelRingStatus ring_status;
        uint32_t producer_position;
        uint32_t consumer_position;
        uint32_t woken = 0u;

        handle_status = kernel_handle_lookup_any(
            &current->handles, thread->context.data[1], ASTRA_RIGHT_SIGNAL,
            &type, (void **)&ring);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || ring == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (type != KERNEL_OBJECT_RING_PRODUCER &&
            type != KERNEL_OBJECT_RING_CONSUMER) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (thread->context.data[4] !=
            (uint32_t)ring_endpoint_for_type(type)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        ring_status = kernel_ring_notify(
            ring, ring_endpoint_for_type(type), thread->context.data[2],
            thread->context.data[3], &producer_position,
            &consumer_position, &woken);
        if (ring_status != KERNEL_RING_OK) {
            if (!ring_status_to_syscall(ring_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        thread->context.data[1] = producer_position;
        thread->context.data[2] = consumer_position;
        if (woken != 0u) {
            wake_preemption_pending = ready_thread_outranks(thread);
            if (wake_preemption_pending)
                ++scheduler_stats.wake_preemptions;
            else
                scheduler_timer_rearm();
        }
#if defined(KERNEL_PROCESS_HOST_TEST)
        if (!kernel_ring_pool_valid() || !kernel_area_pool_valid())
#else
        if (!kernel_ring_pool_healthy() || !kernel_area_pool_healthy())
#endif
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_RING_READ_TRY: {
        KernelRing *ring = NULL;
        KernelHandleStatus handle_status;
        KernelRingStatus ring_status;
        uint32_t copied = 0u;
        uint32_t woken = 0u;
        uint32_t started;
        int copy_status;

        if (thread->context.data[3] == 0u ||
            thread->context.data[3] > ASTRA_BULK_RING_TRANSFER_MAX) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1],
            KERNEL_OBJECT_RING_CONSUMER, ASTRA_RIGHT_READ, (void **)&ring);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || ring == NULL)
            return KERNEL_PROCESS_CORRUPT;
        started = kernel_performance_cycles_low();
        ring_status = kernel_ring_copy_peek(
            ring, syscall_data, thread->context.data[3], &copied);
        if (ring_status != KERNEL_RING_OK) {
            kernel_ring_record_copy_cycles(
                kernel_performance_cycles_low() - started, 0u);
            if (!ring_status_to_syscall(ring_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        copy_status = kernel_copy_to_user(thread->context.data[2],
                                          syscall_data, copied);
        if (copy_status != KERNEL_USER_COPY_OK) {
            kernel_ring_record_copy_cycles(
                kernel_performance_cycles_low() - started, 0u);
            result = copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                             copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT ?
                         ASTRA_SYSCALL_BAD_ADDRESS :
                         ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        ring_status = kernel_ring_copy_consume(ring, copied, &woken);
        if (ring_status != KERNEL_RING_OK)
            return KERNEL_PROCESS_CORRUPT;
        kernel_ring_record_copy_cycles(
            kernel_performance_cycles_low() - started, copied);
        thread->context.data[1] = copied;
        if (woken != 0u) {
            wake_preemption_pending = ready_thread_outranks(thread);
            if (wake_preemption_pending)
                ++scheduler_stats.wake_preemptions;
            else
                scheduler_timer_rearm();
        }
        break;
    }
    case ASTRA_SYSCALL_RING_WRITE_TRY: {
        KernelRing *ring = NULL;
        KernelHandleStatus handle_status;
        KernelRingStatus ring_status;
        uint32_t written = 0u;
        uint32_t woken = 0u;
        uint32_t started;
        int copy_status;

        if (thread->context.data[3] == 0u ||
            thread->context.data[3] > ASTRA_BULK_RING_TRANSFER_MAX ||
            (thread->context.data[4] &
             ~ASTRA_BULK_RING_WRITE_FLAG_MASK) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1],
            KERNEL_OBJECT_RING_PRODUCER, ASTRA_RIGHT_WRITE, (void **)&ring);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || ring == NULL)
            return KERNEL_PROCESS_CORRUPT;
        started = kernel_performance_cycles_low();
        copy_status = kernel_copy_from_user(syscall_data,
                                            thread->context.data[2],
                                            thread->context.data[3]);
        if (copy_status != KERNEL_USER_COPY_OK) {
            kernel_ring_record_copy_cycles(
                kernel_performance_cycles_low() - started, 0u);
            result = copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                             copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT ?
                         ASTRA_SYSCALL_BAD_ADDRESS :
                         ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        ring_status = kernel_ring_copy_write(
            ring, syscall_data, thread->context.data[3],
            (thread->context.data[4] & ASTRA_BULK_RING_WRITE_ATOMIC) != 0u,
            &written, &woken);
        if (ring_status != KERNEL_RING_OK) {
            kernel_ring_record_copy_cycles(
                kernel_performance_cycles_low() - started, 0u);
            if (!ring_status_to_syscall(ring_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        kernel_ring_record_copy_cycles(
            kernel_performance_cycles_low() - started, written);
        thread->context.data[1] = written;
        if (woken != 0u) {
            wake_preemption_pending = ready_thread_outranks(thread);
            if (wake_preemption_pending)
                ++scheduler_stats.wake_preemptions;
            else
                scheduler_timer_rearm();
        }
        break;
    }
    case ASTRA_SYSCALL_PORT_CREATE:
    case ASTRA_SYSCALL_PORT_SEND_TRY:
    case ASTRA_SYSCALL_PORT_RECEIVE_TRY:
        status = port_syscall(current, thread, syscall, &result);
        if (status != KERNEL_PROCESS_OK)
            return status;
        break;
    case ASTRA_SYSCALL_IRQ_READ:
    case ASTRA_SYSCALL_IRQ_ACK:
    case ASTRA_SYSCALL_IRQ_ARM:
    case ASTRA_SYSCALL_IRQ_MASK:
    case ASTRA_SYSCALL_IRQ_RECOVER:
    case ASTRA_SYSCALL_IRQ_REVOKE: {
        KernelIrqEndpoint *endpoint = NULL;
        KernelHandleStatus handle_status;
        KernelIrqStatus irq_status;
        uint32_t required_rights;

        if (syscall == ASTRA_SYSCALL_IRQ_READ)
            required_rights = ASTRA_RIGHT_READ;
        else if (syscall == ASTRA_SYSCALL_IRQ_ACK)
            required_rights = ASTRA_RIGHT_SIGNAL;
        else
            required_rights = ASTRA_RIGHT_ADMINISTER;
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_IRQ,
            required_rights, (void **)&endpoint);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || endpoint == NULL)
            return KERNEL_PROCESS_CORRUPT;

        switch (syscall) {
        case ASTRA_SYSCALL_IRQ_READ: {
            KernelIrqRecord record;
            uint32_t event_flags = 0u;
            uint32_t user_record = thread->context.data[2];
            int copy_status;

            if ((user_record & (sizeof(uint32_t) - 1u)) != 0u) {
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                break;
            }
            irq_status = kernel_irq_read(endpoint, &record, &event_flags);
            thread->context.data[1] = event_flags;
            if (irq_status != KERNEL_IRQ_OK) {
                if (!irq_status_to_syscall(irq_status, &result))
                    return KERNEL_PROCESS_CORRUPT;
                break;
            }
            copy_status = kernel_copy_to_user(user_record, &record,
                                              sizeof(record));
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        case ASTRA_SYSCALL_IRQ_ACK:
            irq_status = kernel_irq_ack(endpoint, thread->context.data[2]);
            if (!irq_status_to_syscall(irq_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        case ASTRA_SYSCALL_IRQ_ARM:
            irq_status = kernel_irq_arm(endpoint);
            if (!irq_status_to_syscall(irq_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        case ASTRA_SYSCALL_IRQ_MASK:
            irq_status = kernel_irq_mask(endpoint);
            if (!irq_status_to_syscall(irq_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        case ASTRA_SYSCALL_IRQ_RECOVER:
            irq_status = kernel_irq_recover(endpoint);
            if (!irq_status_to_syscall(irq_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        case ASTRA_SYSCALL_IRQ_REVOKE: {
            uint32_t woken = 0u;

            irq_status = kernel_irq_revoke(endpoint, &woken);
            thread->context.data[1] = woken;
            if (!irq_status_to_syscall(irq_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        default:
            return KERNEL_PROCESS_CORRUPT;
        }
        if (!kernel_irq_pool_valid())
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_INPUT_READ_TRY: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceSnapshot snapshot;
        KernelHandleStatus handle_status;
        KernelDeviceStatus device_status;
        KernelHandle device_handle = thread->context.data[1];
        uint32_t user_events = thread->context.data[2];
        uint32_t capacity = thread->context.data[3];
        uint32_t count = 0u;
        uint32_t flags = 0u;
        bool copy_failed = false;

        thread->context.data[1] = 0u;
        thread->context.data[2] = 0u;
        handle_status = kernel_handle_lookup(
            &current->handles, device_handle, KERNEL_OBJECT_DEVICE,
            ASTRA_RIGHT_READ, (void **)&lease);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (capacity == 0u || capacity > ASTRA_INPUT_READ_BATCH_MAX ||
            (user_events & (sizeof(uint32_t) - 1u)) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        device_status = kernel_device_query(lease, &snapshot);
        if (!device_status_to_syscall(device_status, &result))
            return KERNEL_PROCESS_CORRUPT;
        if (device_status != KERNEL_DEVICE_OK)
            break;
        if (snapshot.class_id != ASTRA_DEVICE_CLASS_INPUT ||
            snapshot.device_id != ASTRA_DEVICE_ID_INPUT0) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if ((kernel_platform_input_status() & ASTRA_INPUT_STATUS_OVERFLOW) !=
            0u)
            flags |= ASTRA_INPUT_READ_OVERFLOW;
        while (count < capacity) {
            KernelInputEvent event;
            int copy_status;

            if (!kernel_input_peek(&event))
                break;
            copy_status = kernel_copy_to_user(
                user_events + count * sizeof(event), &event, sizeof(event));
            if (copy_status != KERNEL_USER_COPY_OK) {
                thread->context.data[1] = count;
                thread->context.data[2] = flags;
                if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                    copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                    result = ASTRA_SYSCALL_BAD_ADDRESS;
                    copy_failed = true;
                    break;
                }
                return KERNEL_PROCESS_CORRUPT;
            }
            if (!kernel_input_consume())
                return KERNEL_PROCESS_CORRUPT;
            ++count;
        }
        thread->context.data[1] = count;
        thread->context.data[2] = flags;
        if (copy_failed)
            break;
        if ((flags & ASTRA_INPUT_READ_OVERFLOW) != 0u)
            kernel_platform_input_ack_overflow();
        result = count == 0u && flags == 0u ? ASTRA_SYSCALL_WOULD_BLOCK :
                                             ASTRA_SYSCALL_OK;
        break;
    }
    case ASTRA_SYSCALL_BLOCK_QUERY:
    case ASTRA_SYSCALL_BLOCK_SUBMIT:
    case ASTRA_SYSCALL_BLOCK_COLLECT: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceSnapshot snapshot;
        KernelHandleStatus handle_status;
        /*
         * Reading geometry is a query; moving data is a transfer. These are
         * the device right names, which is what a lease actually carries.
         */
        uint32_t required_rights = syscall == ASTRA_SYSCALL_BLOCK_QUERY ?
            KERNEL_DEVICE_RIGHT_QUERY : KERNEL_DEVICE_RIGHT_TRANSFER;

        /*
         * Authority first: every block operation is gated on the lease
         * handle, never on the process being trusted. The lease also carries
         * the device identity, so a handle to some other device cannot reach
         * the block engine.
         */
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            required_rights, (void **)&lease);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (kernel_device_query(lease, &snapshot) != KERNEL_DEVICE_OK) {
            result = ASTRA_SYSCALL_IO_ERROR;
            break;
        }
        if (snapshot.device_id != ASTRA_DEVICE_ID_BLOCK0) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (snapshot.lease_state != ASTRA_DEVICE_LEASE_ACTIVE) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        result = block_syscall(current, thread, syscall);
        break;
    }
    case ASTRA_SYSCALL_DMA_CREATE: {
        AstraDmaBufferInfo info;
        uint32_t user_info = thread->context.data[2];
        int copy_status;

        if ((user_info & (sizeof(uint32_t) - 1u)) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        status = create_dma_buffer(current, thread->context.data[1], &info);
        if (status == KERNEL_PROCESS_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (status == KERNEL_PROCESS_RESOURCE_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (status == KERNEL_PROCESS_OUT_OF_MEMORY) {
            result = ASTRA_SYSCALL_OUT_OF_MEMORY;
            break;
        }
        if (status != KERNEL_PROCESS_OK)
            return status;

        copy_status = kernel_copy_to_user(user_info, &info, sizeof(info));
        if (copy_status != KERNEL_USER_COPY_OK) {
            /*
             * The caller cannot learn the handle, so it can never close it.
             * Release it here rather than leak pinned pages for the life of
             * the process.
             */
            (void)kernel_handle_close(&current->handles, info.handle);
            result = copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                     copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT ?
                ASTRA_SYSCALL_BAD_ADDRESS : ASTRA_SYSCALL_IO_ERROR;
            break;
        }
        thread->context.data[1] = info.handle;
        thread->context.data[2] = info.virtual_base;
        thread->context.data[3] = info.byte_size;
        break;
    }
    case ASTRA_SYSCALL_PROCESS_INFO: {
        KernelProcess *target = NULL;
        KernelHandleStatus handle_status;
        AstraProcessInfo info;
        uint32_t user_info = thread->context.data[2];
        uint32_t frames = 0u;
        int copy_status;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_PROCESS,
            KERNEL_PROCESS_RIGHT_QUERY, (void **)&target);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || target == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if ((user_info & (sizeof(uint32_t) - 1u)) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }

        kernel_bytes_clear(&info, sizeof(info));
        info.size = sizeof(info);
        info.id = target->id;
        info.generation = target->generation;
        info.owner = target->owner;
        /* Constant-time owner ledger lookup; this is the PROC: mem leaf. */
        if (kernel_memory_owner_frames(target->owner, &frames))
            info.resident_frames = frames;
        info.run_count = kernel_thread_process_run_count(
            (uint16_t)(target - processes));
        info.timer_ticks = kernel_thread_process_timer_ticks(
            (uint16_t)(target - processes));
        info.syscall_count = kernel_thread_process_syscalls(
            (uint16_t)(target - processes));
        info.exit_status = target->exit_status;
        info.handle_references = target->handle_references;
        info.process_state = target->process_state;
        info.thread_count = target->thread_count;
        info.live_threads = target->live_threads;
        info.default_priority = target->default_priority;
        info.priority_ceiling = target->priority_ceiling;
        info.exit_reason = target->exit_reason;
        info.thread_state = (uint8_t)
            kernel_thread_process_representative_state(
                (uint16_t)(target - processes));
        info.runtime_ns = kernel_platform_cycles_to_ns(
            target->runtime_cycles);
        info.elapsed_ns = kernel_platform_cycles_to_ns(
            scheduler_cycles() - target->started_cycles);
        info.fault_pc = target->fault_pc;
        info.fault_address = target->fault_address;
        info.fault_vector = target->fault_vector;
        info.fault_status = target->fault_status;

        copy_status = kernel_copy_to_user(user_info, &info, sizeof(info));
        if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
            copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_BAD_ADDRESS;
            break;
        }
        if (copy_status != KERNEL_USER_COPY_OK)
            return KERNEL_PROCESS_CORRUPT;
        result = ASTRA_SYSCALL_OK;
        break;
    }

    case ASTRA_SYSCALL_PROCESS_PRIORITY: {
        KernelProcess *target = NULL;
        KernelHandleStatus handle_status;
        uint32_t priority = thread->context.data[2];
        uint32_t previous;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_PROCESS,
            KERNEL_PROCESS_RIGHT_PRIORITY, (void **)&target);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || target == NULL)
            return KERNEL_PROCESS_CORRUPT;
        if (priority < KERNEL_THREAD_PRIORITY_USER_MIN ||
            priority > KERNEL_THREAD_PRIORITY_USER_MAX) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (priority > target->priority_ceiling) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (target->process_state != KERNEL_PROCESS_CREATED &&
            target->process_state != KERNEL_PROCESS_RUNNING) {
            result = ASTRA_SYSCALL_PEER_DEAD;
            break;
        }
        previous = target->default_priority;
        if (kernel_thread_set_process_priority(
                (uint16_t)(target - processes), (uint8_t)priority) !=
            KERNEL_THREAD_OK)
            return KERNEL_PROCESS_CORRUPT;
        target->default_priority = (uint8_t)priority;
        thread->context.data[1] = previous;
        break;
    }

    case ASTRA_SYSCALL_DEVICE_QUERY:
    case ASTRA_SYSCALL_DEVICE_RESET:
    case ASTRA_SYSCALL_DEVICE_REVOKE: {
        KernelDeviceLease *lease = NULL;
        KernelDeviceStatus device_status;
        KernelHandleStatus handle_status;
        uint32_t required_rights = syscall == ASTRA_SYSCALL_DEVICE_QUERY ?
            ASTRA_RIGHT_READ : ASTRA_RIGHT_ADMINISTER;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_DEVICE,
            required_rights, (void **)&lease);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || lease == NULL)
            return KERNEL_PROCESS_CORRUPT;

        if (syscall == ASTRA_SYSCALL_DEVICE_QUERY) {
            KernelDeviceSnapshot snapshot;
            AstraDeviceInfo info;
            uint32_t user_info = thread->context.data[2];
            int copy_status;

            if ((user_info & (sizeof(uint32_t) - 1u)) != 0u) {
                result = ASTRA_SYSCALL_INVALID_ARGUMENT;
                break;
            }
            device_status = kernel_device_query(lease, &snapshot);
            if (!device_status_to_syscall(device_status, &result))
                return KERNEL_PROCESS_CORRUPT;
            if (device_status != KERNEL_DEVICE_OK)
                break;
            info.size = sizeof(info);
            info.device_id = snapshot.device_id;
            info.class_id = snapshot.class_id;
            info.capabilities = snapshot.capabilities;
            info.generation = snapshot.generation;
            info.device_state = snapshot.device_state;
            info.lease_state = snapshot.lease_state;
            info.reserved = 0u;
            copy_status = kernel_copy_to_user(user_info, &info, sizeof(info));
            if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
                copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
                result = ASTRA_SYSCALL_BAD_ADDRESS;
                break;
            }
            if (copy_status != KERNEL_USER_COPY_OK)
                return KERNEL_PROCESS_CORRUPT;
        } else if (syscall == ASTRA_SYSCALL_DEVICE_RESET) {
            KernelDeviceSnapshot reset_snapshot;
            KernelDeviceStatus reset_query;

            /*
             * A reset the service asked for must not leave it waiting on
             * completions the device will never send. Ending them first, with
             * a status the service can still collect, is what makes a reset
             * recoverable rather than a lost request.
             */
            reset_query = kernel_device_query(lease, &reset_snapshot);
            if (reset_query == KERNEL_DEVICE_OK) {
                if (reset_snapshot.device_id == ASTRA_DEVICE_ID_BLOCK0 &&
                    kernel_block_terminate_owner(
                        current->owner, KERNEL_BLOCK_COMPLETION_RESET, NULL) !=
                        KERNEL_BLOCK_OK)
                    return KERNEL_PROCESS_CORRUPT;
                if (reset_snapshot.device_id == ASTRA_DEVICE_ID_DISPLAY0 &&
                    !display_dma_abort_owner(current->owner))
                    return KERNEL_PROCESS_CORRUPT;
            }
            device_status = kernel_device_reset(lease);
            if (!device_status_to_syscall(device_status, &result))
                return KERNEL_PROCESS_CORRUPT;
        } else {
            KernelDeviceSnapshot revoke_snapshot;

            if (kernel_device_query(lease, &revoke_snapshot) ==
                    KERNEL_DEVICE_OK &&
                revoke_snapshot.device_id == ASTRA_DEVICE_ID_DISPLAY0 &&
                !display_dma_abort_owner(current->owner))
                return KERNEL_PROCESS_CORRUPT;
            device_status = kernel_device_revoke(lease);
            if (!device_status_to_syscall(device_status, &result))
                return KERNEL_PROCESS_CORRUPT;
        }
        if (!kernel_device_pool_valid())
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_WAIT_ONE: {
        KernelHandle handle = thread->context.data[1];
        uint64_t deadline_cycles;
        uint64_t now;
        bool blocked;

        if (!decode_wait_deadline(thread->context.data[2],
                                  thread->context.data[3],
                                  &deadline_cycles)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        now = scheduler_cycles();
        status = wait_handle_set(current, thread, &handle, 1u, now,
                                 deadline_cycles, false, port_probe_handle,
                                 port_probe_sequence, &blocked, &result);
        if (status != KERNEL_PROCESS_OK)
            return status;
        if (!blocked)
            break;
        ++scheduler_stats.wait_blocks;
        status = schedule_next(next_context);
        if (status != KERNEL_PROCESS_OK &&
            status != KERNEL_PROCESS_NO_RUNNABLE)
            return status;
        check_milestone();
        return status;
    }
    case ASTRA_SYSCALL_WAIT_MULTIPLE: {
        KernelHandle handles[ASTRA_WAIT_MULTIPLE_MAX];
        uint32_t user_handles = thread->context.data[1];
        uint32_t count = thread->context.data[2];
        uint64_t deadline_cycles;
        uint64_t now;
        int copy_status;
        bool blocked;

        ++scheduler_stats.wait_set_calls;
        thread->context.data[1] = ASTRA_WAIT_INDEX_NONE;
        thread->context.data[2] = 0u;
        if (count == 0u || count > ASTRA_WAIT_MULTIPLE_MAX ||
            (user_handles & (sizeof(KernelHandle) - 1u)) != 0u ||
            !decode_wait_deadline(thread->context.data[3],
                                  thread->context.data[4],
                                  &deadline_cycles)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        copy_status = kernel_copy_from_user(
            handles, user_handles, count * sizeof(handles[0]));
        if (copy_status == KERNEL_USER_COPY_BAD_ADDRESS ||
            copy_status == KERNEL_USER_COPY_INVALID_ARGUMENT) {
            result = ASTRA_SYSCALL_BAD_ADDRESS;
            break;
        }
        if (copy_status != KERNEL_USER_COPY_OK)
            return KERNEL_PROCESS_CORRUPT;
        now = scheduler_cycles();
        status = wait_handle_set(current, thread, handles, count, now,
                                 deadline_cycles, true, port_probe_handle,
                                 port_probe_sequence, &blocked, &result);
        if (status != KERNEL_PROCESS_OK)
            return status;
        if (!blocked)
            break;
        ++scheduler_stats.wait_blocks;
        status = schedule_next(next_context);
        if (status != KERNEL_PROCESS_OK &&
            status != KERNEL_PROCESS_NO_RUNNABLE)
            return status;
        check_milestone();
        return status;
    }
    case ASTRA_SYSCALL_SIGNAL: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandleStatus handle_status;
        uint32_t woken = 0u;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_SYNC,
            KERNEL_SYNC_RIGHT_SIGNAL, (void **)&object);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        sync_status = kernel_sync_signal(
            object, thread->context.data[2], ASTRA_SYSCALL_OK, &woken);
        if (sync_status == KERNEL_SYNC_COUNT_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (sync_status == KERNEL_SYNC_INVALID_ARGUMENT ||
            sync_status == KERNEL_SYNC_INVALID_STATE) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (sync_status == KERNEL_SYNC_CLOSED) {
            result = kernel_sync_terminal_result(object);
            if (result == ASTRA_SYSCALL_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = woken;
        scheduler_stats.sync_wakeups += woken;
        if (woken != 0u && ready_thread_outranks(thread))
            ++scheduler_stats.wake_preemptions;
        scheduler_timer_rearm();
        break;
    }
    case ASTRA_SYSCALL_TIMER_SET: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandleStatus handle_status;
        uint64_t deadline;
        uint64_t now;
        uint32_t woken = 0u;

        if (!decode_wait_deadline(thread->context.data[2],
                                  thread->context.data[3], &deadline)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_TIMER,
            KERNEL_SYNC_RIGHT_ADMINISTER, (void **)&object);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        now = scheduler_cycles();
        sync_status = kernel_sync_timer_set(object, now, deadline, &woken);
        if (sync_status == KERNEL_SYNC_CLOSED) {
            result = kernel_sync_terminal_result(object);
            if (result == ASTRA_SYSCALL_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (sync_status == KERNEL_SYNC_INVALID_ARGUMENT ||
            sync_status == KERNEL_SYNC_INVALID_STATE) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = woken;
        ++scheduler_stats.timer_arms;
        scheduler_stats.sync_wakeups += woken;
        scheduler_timer_rearm_at(now);
        break;
    }
    case ASTRA_SYSCALL_TIMER_CANCEL: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandleStatus handle_status;
        uint32_t woken = 0u;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_TIMER,
            KERNEL_SYNC_RIGHT_ADMINISTER, (void **)&object);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        sync_status = kernel_sync_timer_cancel(
            object, ASTRA_SYSCALL_CANCELLED, &woken);
        if (sync_status == KERNEL_SYNC_CLOSED) {
            result = kernel_sync_terminal_result(object);
            if (result == ASTRA_SYSCALL_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (sync_status == KERNEL_SYNC_INVALID_ARGUMENT ||
            sync_status == KERNEL_SYNC_INVALID_STATE) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK)
            return KERNEL_PROCESS_CORRUPT;
        thread->context.data[1] = woken;
        ++scheduler_stats.timer_cancellations;
        scheduler_stats.sync_wakeups += woken;
        scheduler_timer_rearm();
        break;
    }
    case ASTRA_SYSCALL_EVENT_RESET: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandleStatus handle_status;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_SYNC,
            KERNEL_SYNC_RIGHT_ADMINISTER, (void **)&object);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || object == NULL)
            return KERNEL_PROCESS_CORRUPT;
        sync_status = kernel_sync_reset(object);
        if (sync_status == KERNEL_SYNC_INVALID_STATE) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        if (sync_status == KERNEL_SYNC_CLOSED) {
            result = kernel_sync_terminal_result(object);
            if (result == ASTRA_SYSCALL_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (sync_status != KERNEL_SYNC_OK)
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_CANCEL_WAIT: {
        KernelThread *target = NULL;
        KernelThreadStatus thread_status;
        KernelHandleStatus handle_status;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_THREAD,
            KERNEL_THREAD_RIGHT_CANCEL_WAIT, (void **)&target);
        if (handle_status == KERNEL_HANDLE_INVALID_HANDLE ||
            handle_status == KERNEL_HANDLE_TYPE_MISMATCH) {
            result = ASTRA_SYSCALL_INVALID_HANDLE;
            break;
        }
        if (handle_status == KERNEL_HANDLE_ACCESS_DENIED) {
            result = ASTRA_SYSCALL_ACCESS_DENIED;
            break;
        }
        if (handle_status != KERNEL_HANDLE_OK || target == NULL)
            return KERNEL_PROCESS_CORRUPT;
        thread_status = kernel_thread_cancel_wait(
            target, ASTRA_SYSCALL_CANCELLED);
        if (thread_status == KERNEL_THREAD_INVALID_STATE) {
            result = ASTRA_SYSCALL_WOULD_BLOCK;
            break;
        }
        if (thread_status != KERNEL_THREAD_OK)
            return KERNEL_PROCESS_CORRUPT;
        if (ready_thread_outranks(thread))
            ++scheduler_stats.wake_preemptions;
        scheduler_timer_rearm();
        break;
    }
    default:
        result = ASTRA_SYSCALL_BAD_SYSCALL;
        break;
    }
    thread->context.data[0] = result;
    if (quantum_preempt_pending != 0u ||
        deadline_preempt_pending != 0u ||
        wake_preemption_pending ||
        ready_thread_outranks(thread)) {
        status = schedule_pending(next_context);
        if (status != KERNEL_PROCESS_OK)
            return status;
    } else {
        *next_context = runtime_resume(thread);
    }
    if (syscall == ASTRA_SYSCALL_PROGRESS && !qualification_progress)
        check_milestone();
    return KERNEL_PROCESS_OK;
}

/*
 * Names the address for the reader. A guard page is the interesting answer:
 * it is a stack that ran past its reservation, and it looks exactly like a
 * wild pointer to anyone reading hex.
 */
static uint32_t classify_fault_address(const KernelThread *thread,
                                       uint32_t address)
{
    uint32_t arena_end = KERNEL_THREAD_STACK_BASE +
                         (KERNEL_PROCESS_THREAD_MAX *
                          KERNEL_THREAD_STACK_STRIDE);

    if (thread != NULL && thread->stack_slot < KERNEL_PROCESS_THREAD_MAX) {
        uint32_t slot_base = stack_slot_base(thread->stack_slot);

        if (address >= slot_base && address < slot_base +
                                                  KERNEL_THREAD_STACK_GUARD_SIZE)
            return KERNEL_PROCESS_FAULT_STACK_GUARD;
    }
    if (address >= KERNEL_THREAD_STACK_BASE && address < arena_end)
        return KERNEL_PROCESS_FAULT_STACK_ARENA;
    if (address >= KERNEL_VM_AREA_BASE &&
        address < KERNEL_VM_AREA_BASE +
                      (KERNEL_VM_AREA_SLOT_COUNT * KERNEL_VM_AREA_SLOT_SIZE))
        return KERNEL_PROCESS_FAULT_AREA_WINDOW;
    if (address >= KERNEL_VM_PRIVATE_BASE &&
        address < KERNEL_VM_PRIVATE_END)
        return KERNEL_PROCESS_FAULT_PRIVATE_WINDOW;
    return KERNEL_PROCESS_FAULT_OTHER;
}

/*
 * The copy path's way in. The user thread never faulted on this page -- the
 * kernel reached it first, on the user's behalf -- so the growth that its own
 * access would have triggered has to be done here instead, or a syscall
 * writing into a fresh frame would answer BAD_ADDRESS for a perfectly good
 * stack address.
 *
 * Only the lowest address of the range is offered: growth commits everything
 * between it and what the thread already holds, so the rest of the range is
 * covered by the same call.
 */
bool kernel_process_prepare_user_copy(uint32_t address, uint32_t size,
                                      bool write)
{
    KernelProcess *process;
    KernelVmStatus vm_status;

    if (size == 0u || current_thread == NULL)
        return false;
    process = process_for_thread(current_thread);
    if (process == NULL || process->process_state != KERNEL_PROCESS_RUNNING)
        return false;
    vm_status = kernel_vm_private_commit_range(
        &process->address_space, address, size, write);
    if (vm_status == KERNEL_VM_OK)
        return true;
    if (vm_status != KERNEL_VM_NOT_MAPPED)
        return false;
    if (grow_user_stack(process, current_thread, address))
        return true;
    if (write) {
        uint32_t last;
        uint32_t page;
        bool cow_resolved = false;

        if (address > UINT32_MAX - (size - 1u))
            return false;
        last = (address + size - 1u) & ~(KERNEL_PAGE_SIZE - 1u);
        page = address & ~(KERNEL_PAGE_SIZE - 1u);
        for (;;) {
            vm_status = kernel_vm_cow_fault(&process->address_space, page);
            if (vm_status == KERNEL_VM_OK) {
                cow_resolved = true;
            } else if (!(vm_status == KERNEL_VM_NOT_OWNED &&
                         kernel_vm_probe_current(page, false, NULL) ==
                             KERNEL_VM_MAPPING_READ_WRITE)) {
                return false;
            }
            if (page == last)
                return cow_resolved;
            page += KERNEL_PAGE_SIZE;
        }
    }
    return false;
}

KernelProcessStatus kernel_process_on_fault(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context)
{
    KernelExceptionFrame frame;
    KernelProcess *current;
    KernelThread *thread;
    KernelProcessStatus status;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current,
                             &thread);
    if (status != KERNEL_PROCESS_OK)
        return status;
    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK ||
        frame.from_user == 0u)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    /*
     * A stack reaching a page it has not committed yet is not a fault to die
     * of, and it is the only fault this kernel answers rather than reports.
     * Resuming works because the context carries the faulting instruction's
     * own program counter -- capture_current took it from the frame -- so
     * re-entering user mode re-runs the access against the page now mapped.
     * A machine that resumed a bus cycle from the frame instead of restarting
     * the instruction would need the frame returned intact rather than this.
     */
    if (frame.access_fault != 0u &&
        (grow_user_stack(current, thread, frame.fault_address) ||
         ((frame.special_status & M68K_SSW_READ) == 0u &&
          kernel_vm_cow_fault(&current->address_space,
                              frame.fault_address) == KERNEL_VM_OK) ||
         kernel_area_fault(current->id, &current->address_space,
                           frame.fault_address) ||
         kernel_vm_private_fault(
             &current->address_space, frame.fault_address,
             (frame.special_status & M68K_SSW_READ) == 0u) == KERNEL_VM_OK)) {
        *next_context = runtime_resume(thread);
        return KERNEL_PROCESS_OK;
    }
    current->fault_pc = frame.program_counter;
    current->fault_vector = (uint16_t)(frame.vector_offset >> 2);
    current->fault_status = frame.special_status;
    current->fault_address = frame.fault_address;
    ++scheduler_stats.user_faults;
    kernel_process_fault_report(current->id, thread->id,
                                frame.program_counter, frame.fault_address,
                                frame.vector_offset >> 2,
                                classify_fault_address(thread,
                                                       frame.fault_address));
    return retire_current(KERNEL_PROCESS_EXIT_USER_FAULT, 0u,
                          next_context);
}

#if ASTRA_KERNEL_SOAK_SELFTEST
static KernelProcessStatus soak_relaunch(void)
{
    KernelProcessStatus status;
    uint32_t process_id;
    uint32_t cycles = scheduler_stats.soak_cycles;

    if (soak_state.relaunch_pending == 0u ||
        scheduler_stats.milestone_complete == 0u || cycles == 0u)
        return KERNEL_PROCESS_INVALID_STATE;
    if (soak_state.milestone_reported == 0u || cycles == 1u ||
        cycles == 10u || cycles == 100u || cycles == 1000u ||
        cycles % soak_state.report_interval == 0u) {
        kernel_process_soak_checkpoint(cycles,
                                       soak_state.baseline_free_frames);
        soak_state.milestone_reported = 1u;
    }
    status = create_process(
        soak_state.image, soak_state.image_size, soak_state.entry_offset,
        cycles, &process_id);
    if (status != KERNEL_PROCESS_OK || process_id == 0u)
        return maintenance_failed(
            KERNEL_PROCESS_MAINTENANCE_CREATE,
            status == KERNEL_PROCESS_OK ? KERNEL_PROCESS_CORRUPT : status,
            status == KERNEL_PROCESS_OK ? process_id : (uint32_t)status,
            status == KERNEL_PROCESS_OK ? 1u :
                                          (uint32_t)KERNEL_PROCESS_OK);
    soak_state.relaunch_pending = 0u;
    return KERNEL_PROCESS_OK;
}
#endif

KernelProcessStatus kernel_process_maintenance(void)
{
    KernelProcessStatus status = KERNEL_PROCESS_OK;

#if defined(KERNEL_PROCESS_HOST_TEST)
    if (!process_pool_valid())
#else
    if (!process_pool_healthy() || !kernel_port_pool_healthy() ||
        !kernel_area_pool_healthy() || !kernel_ring_pool_healthy() ||
        !kernel_irq_pool_healthy() ||
        !kernel_handle_transfer_pool_healthy())
#endif
        return KERNEL_PROCESS_CORRUPT;
    kernel_bytes_clear(&maintenance_diagnostics,
                       sizeof(maintenance_diagnostics));

    if (kernel_irq_revocation_pending()) {
        if (!kernel_interrupt_schedule_device_reset())
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_IRQ_REVOCATION,
                KERNEL_PROCESS_CORRUPT, 0u, 1u);
    }

    if (kernel_process_maintenance_pending()) {
        KernelBlockStatus block_status = kernel_block_service(NULL);

        if (block_status != KERNEL_BLOCK_OK)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_BLOCK_SERVICE,
                KERNEL_PROCESS_CORRUPT, (uint32_t)block_status,
                (uint32_t)KERNEL_BLOCK_OK);
        status = kernel_process_reap_deferred();
        if (status != KERNEL_PROCESS_OK)
            return maintenance_failed(KERNEL_PROCESS_MAINTENANCE_REAP,
                                      status, (uint32_t)status,
                                      (uint32_t)KERNEL_PROCESS_OK);
    }
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (soak_state.enabled != 0u &&
        scheduler_stats.completed_teardowns !=
            soak_state.last_completed_teardowns) {
        KernelMemoryStats memory_stats;
        uint32_t survivor_frames;
        uint32_t survivor_frame_delta;
        uint32_t expected_free_frames;
        uint32_t cycles;

        if (scheduler_stats.completed_teardowns !=
            soak_state.last_completed_teardowns + 1u)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_TEARDOWN_SEQUENCE,
                KERNEL_PROCESS_CORRUPT,
                scheduler_stats.completed_teardowns,
                soak_state.last_completed_teardowns + 1u);
        if (scheduler_stats.live_processes != 1u)
            return maintenance_failed(KERNEL_PROCESS_MAINTENANCE_LIVE_COUNT,
                                      KERNEL_PROCESS_CORRUPT,
                                      scheduler_stats.live_processes, 1u);
        if (scheduler_stats.completed_user_fault_teardowns !=
            scheduler_stats.user_faults)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_FAULT_TEARDOWN_COUNT,
                KERNEL_PROCESS_CORRUPT,
                scheduler_stats.completed_user_fault_teardowns,
                scheduler_stats.user_faults);
        if (scheduler_stats.completed_teardowns !=
            scheduler_stats.user_faults)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_TOTAL_TEARDOWN_COUNT,
                KERNEL_PROCESS_CORRUPT,
                scheduler_stats.completed_teardowns,
                scheduler_stats.user_faults);
        if (!kernel_memory_stats(&memory_stats))
            return maintenance_failed(KERNEL_PROCESS_MAINTENANCE_MEMORY_STATS,
                                      KERNEL_PROCESS_CORRUPT, 0u, 1u);
        if (!kernel_memory_owner_frames(soak_state.survivor_owner,
                                        &survivor_frames))
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_OWNER_FRAMES,
                KERNEL_PROCESS_CORRUPT, 0u, 1u);
        if (survivor_frames < soak_state.baseline_survivor_frames)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_OWNER_FRAME_UNDERFLOW,
                KERNEL_PROCESS_CORRUPT, survivor_frames,
                soak_state.baseline_survivor_frames);
        survivor_frame_delta =
            survivor_frames - soak_state.baseline_survivor_frames;
        if (survivor_frame_delta > soak_state.baseline_free_frames)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_OWNER_FRAME_UNDERFLOW,
                KERNEL_PROCESS_CORRUPT, survivor_frame_delta,
                soak_state.baseline_free_frames);
        expected_free_frames =
            soak_state.baseline_free_frames - survivor_frame_delta;
        if (memory_stats.free_frames != expected_free_frames)
            return maintenance_failed(KERNEL_PROCESS_MAINTENANCE_FREE_FRAMES,
                                      KERNEL_PROCESS_CORRUPT,
                                      memory_stats.free_frames,
                                      expected_free_frames);

        cycles = scheduler_stats.soak_cycles + 1u;
        if (cycles == 0u)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_CYCLE_OVERFLOW,
                KERNEL_PROCESS_CORRUPT, scheduler_stats.soak_cycles,
                UINT32_MAX);
        scheduler_stats.soak_cycles = cycles;
        soak_state.last_completed_teardowns =
            scheduler_stats.completed_teardowns;
        soak_state.relaunch_pending = 1u;
    }
    if (soak_state.enabled != 0u &&
        soak_state.relaunch_pending != 0u &&
        scheduler_stats.milestone_complete != 0u)
        return soak_relaunch();
#endif
    return KERNEL_PROCESS_OK;
}

bool kernel_process_maintenance_diagnostics(
    KernelProcessMaintenanceDiagnostics *diagnostics)
{
    if (diagnostics == NULL)
        return false;
    diagnostics->failure = maintenance_diagnostics.failure;
    diagnostics->status = maintenance_diagnostics.status;
    diagnostics->observed = maintenance_diagnostics.observed;
    diagnostics->expected = maintenance_diagnostics.expected;
    return true;
}

bool kernel_process_maintenance_pending(void)
{
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (soak_state.enabled != 0u &&
        soak_state.relaunch_pending != 0u &&
        scheduler_stats.milestone_complete != 0u)
        return true;
#endif
    if (kernel_irq_revocation_pending())
        return true;
    if (kernel_thread_reap_pending())
        return true;
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        if (processes[index].process_state == KERNEL_PROCESS_EXITING)
            return true;
    }
    return false;
}

KernelProcessStatus kernel_process_reap_deferred(void)
{
    bool deferred = false;
    KernelProcessStatus thread_status = finish_thread_reaps();

    if (thread_status != KERNEL_PROCESS_OK)
        return thread_status;

    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        KernelProcessStatus status;

        if (processes[index].process_state != KERNEL_PROCESS_EXITING)
            continue;
        status = finish_reap(&processes[index]);
        if (status == KERNEL_PROCESS_DEFERRED)
            deferred = true;
        else if (status != KERNEL_PROCESS_OK)
            return status;
    }
    check_milestone();
    return deferred ? KERNEL_PROCESS_DEFERRED : KERNEL_PROCESS_OK;
}

bool kernel_process_snapshot(uint32_t slot, KernelProcessSnapshot *snapshot)
{
    const KernelProcess *process;
    uint32_t death_waiters;

    if (slot >= KERNEL_PROCESS_MAX || snapshot == NULL)
        return false;
    process = &processes[slot];
    death_waiters = kernel_thread_wait_queue_count(&process->death_waiters);
    if (death_waiters > UINT16_MAX)
        return false;
    snapshot->id = process->id;
    snapshot->owner = process->owner;
    snapshot->generation = process->generation;
    snapshot->progress = process->progress;
    snapshot->timer_ticks =
        kernel_thread_process_timer_ticks((uint16_t)slot);
    snapshot->run_count = kernel_thread_process_run_count((uint16_t)slot);
    snapshot->syscall_count =
        kernel_thread_process_syscalls((uint16_t)slot);
    snapshot->fault_pc = process->fault_pc;
    snapshot->fault_address = process->fault_address;
    snapshot->exit_status = process->exit_status;
    snapshot->terminal_result = process->terminal_result;
    snapshot->self_handle = process->self_handle;
    snapshot->fault_vector = process->fault_vector;
    snapshot->fault_status = process->fault_status;
    snapshot->process_state = process->process_state;
    snapshot->thread_state = (uint8_t)
        kernel_thread_process_representative_state((uint16_t)slot);
    snapshot->exit_reason = process->exit_reason;
    snapshot->default_priority = process->default_priority;
    snapshot->priority_ceiling = process->priority_ceiling;
    snapshot->thread_count = process->thread_count;
    snapshot->live_threads = process->live_threads;
    snapshot->user_stack_pages = process->user_stack_pages;
    snapshot->user_guard_pages = process->user_guard_pages;
    snapshot->supervisor_stack_pages = process->supervisor_stack_pages;
    snapshot->supervisor_guard_pages = process->supervisor_guard_pages;
    snapshot->handle_references = process->handle_references;
    snapshot->death_waiters = (uint16_t)death_waiters;
    snapshot->reserved[0] = 0u;
    snapshot->reserved[1] = 0u;
    return true;
}

bool kernel_process_stats(KernelSchedulerStats *stats)
{
    KernelAreaPoolStats area_stats;
    KernelHandleTransferStats handle_stats;
    KernelPortPoolStats port_stats;
    KernelRingPoolStats ring_stats;
    KernelSyncPoolStats sync_stats;
    KernelThreadPoolStats thread_stats;

    if (stats == NULL || !process_pool_valid() ||
        !kernel_thread_pool_stats(&thread_stats) ||
        !kernel_sync_pool_stats(&sync_stats) ||
        !kernel_port_pool_stats(&port_stats) ||
        !kernel_area_pool_stats(&area_stats) ||
        !kernel_ring_pool_stats(&ring_stats) ||
        !kernel_handle_transfer_stats(&handle_stats))
        return false;
    if (thread_stats.created_threads != scheduler_stats.created_threads ||
        thread_stats.live_threads != scheduler_stats.live_threads ||
        thread_stats.dead_threads != scheduler_stats.dead_threads)
        return false;
    stats->created_processes = scheduler_stats.created_processes;
    stats->live_processes = scheduler_stats.live_processes;
    stats->dead_processes = scheduler_stats.dead_processes;
    stats->launch_failures = scheduler_stats.launch_failures;
    stats->last_launch_failure = scheduler_stats.last_launch_failure;
    stats->context_switches = scheduler_stats.context_switches;
    stats->timer_preemptions = scheduler_stats.timer_preemptions;
    stats->voluntary_switches = scheduler_stats.voluntary_switches;
    stats->total_syscalls_low = scheduler_stats.total_syscalls_low;
    stats->total_syscalls_high = scheduler_stats.total_syscalls_high;
    stats->user_faults = scheduler_stats.user_faults;
    stats->user_stack_growths = scheduler_stats.user_stack_growths;
    stats->user_stack_pages_committed =
        scheduler_stats.user_stack_pages_committed;
    stats->diagnostic_logs = scheduler_stats.diagnostic_logs;
    stats->diagnostic_log_bytes = scheduler_stats.diagnostic_log_bytes;
    stats->diagnostic_log_refusals = scheduler_stats.diagnostic_log_refusals;
    stats->completed_user_fault_teardowns =
        scheduler_stats.completed_user_fault_teardowns;
    stats->completed_teardowns = scheduler_stats.completed_teardowns;
    stats->forced_frame_releases = scheduler_stats.forced_frame_releases;
    stats->soak_cycles = scheduler_stats.soak_cycles;
    stats->current_process_id = scheduler_stats.current_process_id;
    stats->created_threads = thread_stats.created_threads;
    stats->live_threads = thread_stats.live_threads;
    stats->dead_threads = thread_stats.dead_threads;
    stats->current_thread_id = scheduler_stats.current_thread_id;
    stats->same_address_space_switches =
        scheduler_stats.same_address_space_switches;
    stats->cross_address_space_switches =
        scheduler_stats.cross_address_space_switches;
    stats->priority_preemptions = scheduler_stats.priority_preemptions;
    stats->wait_blocks = scheduler_stats.wait_blocks;
    stats->sync_wakeups = scheduler_stats.sync_wakeups;
    stats->wake_preemptions = scheduler_stats.wake_preemptions;
    stats->quantum_cycles = scheduler_stats.quantum_cycles;
    stats->quantum_expirations = scheduler_stats.quantum_expirations;
    stats->deadline_expirations = scheduler_stats.deadline_expirations;
    stats->deadline_preemptions = scheduler_stats.deadline_preemptions;
    stats->timer_rearms = scheduler_stats.timer_rearms;
    stats->supervisor_timer_deferrals =
        scheduler_stats.supervisor_timer_deferrals;
    stats->deadline_depth = thread_stats.deadline_depth;
    stats->deadline_max_depth = thread_stats.deadline_max_depth;
    stats->sync_created_events = sync_stats.created_events;
    stats->sync_created_semaphores = sync_stats.created_semaphores;
    stats->sync_live_objects = sync_stats.live_objects;
    stats->sync_max_live_objects = sync_stats.max_live_objects;
    stats->sync_wait_calls = sync_stats.wait_calls;
    stats->sync_blocked_waits = sync_stats.blocked_waits;
    stats->sync_signal_calls = sync_stats.signal_calls;
    stats->sync_cancellations = thread_stats.wait_cancellations;
    stats->sync_close_wakeups = sync_stats.close_wakeups;
    stats->sync_owner_deaths = sync_stats.owner_deaths;
    stats->ready_bitmap = thread_stats.ready_bitmap;
    stats->blocked_threads = thread_stats.blocked_threads;
    stats->kernel_stack_entries = thread_stats.kernel_stack_entries;
    stats->kernel_stack_max_used = thread_stats.kernel_stack_max_used;
    stats->thread_exits = scheduler_stats.thread_exits;
    stats->thread_death_waits = thread_stats.death_waits;
    stats->thread_death_wakeups = thread_stats.death_wakeups;
    stats->process_death_waits = scheduler_stats.process_death_waits;
    stats->process_death_wakeups = scheduler_stats.process_death_wakeups;
    stats->completed_thread_reaps =
        scheduler_stats.completed_thread_reaps;
    stats->thread_creation_failures =
        scheduler_stats.thread_creation_failures;
    stats->wait_set_calls = scheduler_stats.wait_set_calls;
    stats->wait_set_blocks = thread_stats.wait_set_blocks;
    stats->wait_set_wakeups = thread_stats.wait_set_wakeups;
    stats->wait_set_registrations = thread_stats.wait_registrations;
    stats->wait_set_registration_max =
        thread_stats.wait_registration_max;
    stats->wait_set_max_members = thread_stats.max_wait_members;
    stats->timer_created = sync_stats.created_timers;
    stats->timer_arms = sync_stats.timer_arms;
    stats->timer_cancellations = sync_stats.timer_cancellations;
    stats->timer_expirations = sync_stats.timer_expirations;
    stats->port_created = port_stats.created_ports;
    stats->port_active = port_stats.active_ports;
    stats->port_max_active = port_stats.max_active_ports;
    stats->port_sends = port_stats.sends;
    stats->port_receives = port_stats.receives;
    stats->port_send_would_block = port_stats.send_would_block;
    stats->port_receive_would_block = port_stats.receive_would_block;
    stats->port_receive_buffer_too_small =
        port_stats.receive_buffer_too_small;
    stats->port_wait_wakeups = port_stats.wait_wakeups;
    stats->port_owner_deaths = port_stats.owner_deaths;
    stats->port_queued_messages = port_stats.queued_messages;
    stats->port_queued_bytes = port_stats.queued_bytes;
    stats->port_queued_handles = port_stats.queued_handles;
    stats->handle_transfers = handle_stats.committed_exports;
    stats->handle_transfer_imports = handle_stats.committed_imports;
    stats->handle_transfer_import_rollbacks = handle_stats.import_rollbacks;
    stats->handle_transfer_live_detached = handle_stats.live_detached;
    stats->handle_transfer_pool_exhaustions =
        handle_stats.pool_exhaustions;
    stats->handle_transfer_max_detached = handle_stats.max_live_detached;
    stats->area_created = area_stats.created_areas;
    stats->area_active = area_stats.active_areas;
    stats->area_mappings = area_stats.active_mappings;
    stats->area_map_operations = area_stats.map_operations;
    stats->area_unmap_operations = area_stats.unmap_operations;
    stats->area_committed_pages = area_stats.committed_pages;
    stats->ring_created = ring_stats.created_rings;
    stats->ring_active = ring_stats.active_rings;
    stats->ring_notifications = ring_stats.producer_notifications +
                                ring_stats.consumer_notifications;
    stats->ring_producer_notifications = ring_stats.producer_notifications;
    stats->ring_consumer_notifications = ring_stats.consumer_notifications;
    stats->ring_wait_wakeups = ring_stats.wait_wakeups;
    stats->ring_peer_closures = ring_stats.peer_closures;
    stats->ring_copy_reads = ring_stats.copied_reads;
    stats->ring_copy_writes = ring_stats.copied_writes;
    stats->ring_copy_read_bytes = ring_stats.copied_read_bytes;
    stats->ring_copy_write_bytes = ring_stats.copied_write_bytes;
    stats->ring_copy_max_cycles = ring_stats.copied_max_cycles;
    stats->ring_copy_cycle_overruns = ring_stats.copied_cycle_overruns;
    stats->irq_wake_to_run_max_cycles =
        thread_stats.irq_wake_to_run_max_cycles;
    stats->milestone_complete = scheduler_stats.milestone_complete;
    stats->reserved[0] = 0u;
    stats->reserved[1] = 0u;
    stats->reserved[2] = 0u;
    return true;
}

#if ASTRA_KERNEL_SOAK_SELFTEST
KernelProcessStatus kernel_process_soak_configure(
    const void *image, uint32_t image_size, uint32_t entry_offset,
    uint32_t baseline_free_frames, uint32_t report_interval)
{
    KernelProcess *survivor = NULL;
    uint32_t survivor_frames;

    if (image == NULL || image_size == 0u || image_size > KERNEL_PAGE_SIZE ||
        entry_offset >= image_size || baseline_free_frames == 0u ||
        report_interval == 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (soak_state.enabled != 0u || scheduler_stats.live_processes != 2u ||
        scheduler_stats.completed_teardowns != 0u)
        return KERNEL_PROCESS_INVALID_STATE;

    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        KernelProcess *candidate = &processes[index];

        if ((candidate->process_state != KERNEL_PROCESS_CREATED &&
             candidate->process_state != KERNEL_PROCESS_RUNNING) ||
            candidate->live_threads < 2u)
            continue;
        if (survivor != NULL)
            return KERNEL_PROCESS_INVALID_STATE;
        survivor = candidate;
    }
    if (survivor == NULL ||
        !kernel_memory_owner_frames(survivor->owner, &survivor_frames) ||
        survivor_frames == 0u)
        return KERNEL_PROCESS_INVALID_STATE;

    soak_state.image = image;
    soak_state.image_size = image_size;
    soak_state.entry_offset = entry_offset;
    soak_state.baseline_free_frames = baseline_free_frames;
    soak_state.survivor_owner = survivor->owner;
    soak_state.baseline_survivor_frames = survivor_frames;
    soak_state.report_interval = report_interval;
    soak_state.last_completed_teardowns = 0u;
    soak_state.milestone_reported = 0u;
    soak_state.relaunch_pending = 0u;
    soak_state.reserved = 0u;
    soak_state.enabled = 1u;
    return KERNEL_PROCESS_OK;
}
#endif
