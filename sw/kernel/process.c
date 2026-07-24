#include "process.h"

#include <astra/syscall.h>

#include "block.h"
#include "bytes.h"
#include "exception.h"
#include "generation.h"
#include "memory.h"
#include "performance.h"
#include "platform.h"
#include "sync.h"
#include "vm.h"

#include <stddef.h>

#define PROCESS_OWNER_PREFIX 0x10000000u

typedef struct KernelProcess {
    KernelAddressSpace address_space;
    KernelHandleTable handles;
    uint32_t id;
    uint32_t owner;
    uint32_t generation;
    uint32_t image_size;
    uint32_t progress;
    uint32_t fault_address;
    KernelHandle self_handle;
    uint16_t fault_vector;
    uint16_t stack_slots;
    uint8_t process_state;
    uint8_t exit_reason;
    uint8_t default_priority;
    uint8_t priority_ceiling;
    uint8_t thread_count;
    uint8_t live_threads;
    uint8_t handles_closed;
    uint8_t address_space_destroyed;
    uint8_t reserved;
} KernelProcess;

static KernelProcess processes[KERNEL_PROCESS_MAX];
static KernelSchedulerStats scheduler_stats;
static KernelProcessMaintenanceDiagnostics maintenance_diagnostics;
static KernelThread *current_thread;
static uint64_t quantum_deadline;
static uint32_t scheduler_quantum_cycles;
static uint8_t scheduler_initialized;
static uint8_t quantum_active;
static uint8_t quantum_preempt_pending;
static uint8_t deadline_preempt_pending;

_Static_assert(KERNEL_THREAD_STACK_SIZE == KERNEL_PAGE_SIZE,
               "one user-stack slot must map exactly one VM page");
_Static_assert(KERNEL_PROCESS_THREAD_MAX <= 16u,
               "stack slot bitmap exceeds its storage");
_Static_assert(ASTRA_EVENT_MANUAL_RESET ==
                   KERNEL_SYNC_EVENT_MANUAL_RESET,
               "event flag ABI mismatch");
_Static_assert(ASTRA_EVENT_INITIALLY_SIGNALED ==
                   KERNEL_SYNC_EVENT_INITIALLY_SIGNALED,
               "event flag ABI mismatch");
_Static_assert(ASTRA_RIGHT_READ == KERNEL_SYNC_RIGHT_QUERY &&
                   ASTRA_RIGHT_SIGNAL == KERNEL_SYNC_RIGHT_SIGNAL &&
                   ASTRA_RIGHT_WAIT == KERNEL_SYNC_RIGHT_WAIT &&
                   ASTRA_RIGHT_ADMINISTER ==
                       KERNEL_SYNC_RIGHT_ADMINISTER,
               "synchronization-right ABI mismatch");

#if ASTRA_KERNEL_SOAK_SELFTEST
typedef struct KernelProcessSoakState {
    const void *image;
    uint32_t image_size;
    uint32_t entry_offset;
    uint32_t baseline_free_frames;
    uint32_t report_interval;
    uint32_t last_completed_teardowns;
    uint8_t enabled;
    uint8_t milestone_reported;
    uint8_t reserved[2];
} KernelProcessSoakState;

static KernelProcessSoakState soak_state;
#endif

#if defined(KERNEL_PROCESS_HOST_TEST)
static uint8_t *host_physical_memory;
static uint32_t host_physical_base;
static uint32_t host_physical_size;

void kernel_process_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                              uint32_t size)
{
    host_physical_memory = memory;
    host_physical_base = base;
    host_physical_size = size;
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

static int32_t find_free_slot(void)
{
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        if (processes[index].process_state == KERNEL_PROCESS_UNUSED ||
            processes[index].process_state == KERNEL_PROCESS_DEAD)
            return (int32_t)index;
    }
    return -1;
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

static uint64_t scheduler_cycles(void)
{
    KernelPlatformCycleCount cycles;

    kernel_platform_cpu_cycles(&cycles);
    return ((uint64_t)cycles.high << 32) | cycles.low;
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
    uint8_t highest = 0u;

    if (kernel_thread_next_deadline(&deadline) && deadline <= now) {
        if (kernel_thread_expire_deadlines(now, &expired, &highest) !=
            KERNEL_THREAD_OK)
            return KERNEL_PROCESS_CORRUPT;
        scheduler_stats.deadline_expirations += expired;
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
    *next_context = &next->context;
    scheduler_start_quantum();
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
    *next_context = &previous->context;
    if (quantum_preempt_pending == 0u &&
        deadline_preempt_pending == 0u &&
        !ready_thread_outranks(previous))
        return KERNEL_PROCESS_OK;

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
        (void)kernel_handle_close_all(&process->handles);
        if (!kernel_sync_pool_valid())
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
    scheduler_stats.forced_frame_releases += released_frames;
    if (kernel_thread_release_process(
            (uint16_t)(process - processes)) != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;
    process->process_state = KERNEL_PROCESS_DEAD;
    process->live_threads = 0u;
    process->stack_slots = 0u;
    if (process->exit_reason == KERNEL_PROCESS_EXIT_USER_FAULT)
        ++scheduler_stats.completed_user_fault_teardowns;
    ++scheduler_stats.dead_processes;
    ++scheduler_stats.completed_teardowns;
    return KERNEL_PROCESS_OK;
}

static void check_milestone(void)
{
    KernelSyncPoolStats sync_stats;
    KernelThreadPoolStats thread_stats;
    uint32_t measured_stack_use;
    bool survivor_ready = false;

    if (scheduler_stats.milestone_complete != 0u)
        return;
    if (!kernel_thread_pool_stats(&thread_stats) ||
        !kernel_sync_pool_stats(&sync_stats) ||
        scheduler_stats.created_processes < 2u ||
        scheduler_stats.created_threads < 3u ||
        scheduler_stats.timer_preemptions == 0u ||
        scheduler_stats.same_address_space_switches == 0u ||
        scheduler_stats.cross_address_space_switches == 0u ||
        scheduler_stats.wait_blocks < 2u ||
        scheduler_stats.sync_wakeups == 0u ||
        scheduler_stats.wake_preemptions == 0u ||
        scheduler_stats.quantum_expirations == 0u ||
        scheduler_stats.deadline_expirations == 0u ||
        scheduler_stats.deadline_preemptions == 0u ||
        thread_stats.blocked_threads == 0u ||
        thread_stats.deadline_max_depth == 0u ||
        thread_stats.wait_cancellations == 0u ||
        sync_stats.created_events < 3u ||
        sync_stats.created_semaphores == 0u ||
        sync_stats.blocked_waits < 5u ||
        sync_stats.signal_calls < 2u ||
        sync_stats.close_wakeups == 0u ||
        sync_stats.owner_deaths == 0u ||
        thread_stats.kernel_stack_entries == 0u ||
        thread_stats.kernel_stack_max_used == 0u ||
        !kernel_thread_stacks_valid() ||
        scheduler_stats.user_faults == 0u ||
        scheduler_stats.completed_user_fault_teardowns == 0u)
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
    kernel_process_milestone_reached();
}

static KernelProcessStatus retire_current(KernelProcessExitReason reason,
                                          KernelCpuContext **next_context)
{
    KernelProcess *retiring;
    KernelProcessStatus status;
    uint16_t retiring_slot;
    uint32_t closed_sync_objects;
    uint32_t retired_threads;
    uint32_t woken_sync_waiters;

    if (next_context == NULL || current_thread == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    retiring_slot = current_thread->process_slot;
    if (retiring_slot >= KERNEL_PROCESS_MAX)
        return KERNEL_PROCESS_CORRUPT;
    retiring = &processes[retiring_slot];
    retiring->process_state = KERNEL_PROCESS_EXITING;
    retiring->exit_reason = (uint8_t)reason;
    if (kernel_thread_retire_process(retiring_slot, &retired_threads) !=
        KERNEL_THREAD_OK || retired_threads != retiring->live_threads)
        return KERNEL_PROCESS_CORRUPT;
    retiring->live_threads = 0u;
    if (retired_threads > scheduler_stats.live_threads)
        return KERNEL_PROCESS_CORRUPT;
    scheduler_stats.live_threads -= retired_threads;
    scheduler_stats.dead_threads += retired_threads;
    if (kernel_sync_owner_died(retiring->id, ASTRA_SYSCALL_PEER_DEAD,
                               &closed_sync_objects,
                               &woken_sync_waiters) != KERNEL_SYNC_OK)
        return KERNEL_PROCESS_CORRUPT;
    (void)closed_sync_objects;
    (void)woken_sync_waiters;
    --scheduler_stats.live_processes;
    status = schedule_next(next_context);
    if (status != KERNEL_PROCESS_OK &&
        status != KERNEL_PROCESS_NO_RUNNABLE)
        return status;
    check_milestone();
    return status;
}

void kernel_process_init(void)
{
    scheduler_initialized = 0u;
    kernel_performance_init();
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index)
        kernel_bytes_clear(&processes[index], sizeof(processes[index]));
    kernel_bytes_clear(&scheduler_stats, sizeof(scheduler_stats));
    kernel_bytes_clear(&maintenance_diagnostics,
                       sizeof(maintenance_diagnostics));
    kernel_thread_pool_init();
    kernel_sync_pool_init();
#if ASTRA_KERNEL_SOAK_SELFTEST
    kernel_bytes_clear(&soak_state, sizeof(soak_state));
#endif
    current_thread = NULL;
    quantum_deadline = 0u;
    scheduler_quantum_cycles = kernel_platform_quantum_cycles();
    if (scheduler_quantum_cycles == 0u)
        scheduler_quantum_cycles = 1u;
    scheduler_stats.quantum_cycles = scheduler_quantum_cycles;
    quantum_active = 0u;
    quantum_preempt_pending = 0u;
    deadline_preempt_pending = 0u;
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

static int32_t find_stack_slot(const KernelProcess *process)
{
    for (uint32_t slot = 0u; slot < KERNEL_PROCESS_THREAD_MAX; ++slot) {
        if ((process->stack_slots & (uint16_t)(1u << slot)) == 0u)
            return (int32_t)slot;
    }
    return -1;
}

static KernelProcessStatus create_thread(KernelProcess *process,
                                         uint32_t entry_offset,
                                         uint32_t initial_argument,
                                         uint8_t priority,
                                         uint32_t *thread_id)
{
    KernelThread *thread = NULL;
    KernelThreadStatus thread_status;
    KernelHandle thread_handle = KERNEL_HANDLE_INVALID;
    uint32_t stack_physical = 0u;
    uint32_t stack_base;
    uint32_t stack_top;
    int32_t stack_slot;
    bool stack_held = false;
    bool stack_mapped = false;
    bool handle_installed = false;
    bool cleanup_failed = false;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;

    if (process == NULL || thread_id == NULL ||
        entry_offset >= process->image_size ||
        priority < KERNEL_THREAD_PRIORITY_USER_MIN ||
        priority > process->priority_ceiling)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *thread_id = 0u;
    if (process->thread_count >= KERNEL_PROCESS_THREAD_MAX)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    stack_slot = find_stack_slot(process);
    if (stack_slot < 0)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    stack_base = KERNEL_THREAD_STACK_BASE +
                 (uint32_t)stack_slot * KERNEL_THREAD_STACK_STRIDE;
    stack_top = stack_base + KERNEL_THREAD_STACK_SIZE;

    thread_status = kernel_thread_allocate(
        (uint16_t)(process - processes), process->id,
        (uint16_t)stack_slot, KERNEL_PROCESS_CODE_BASE + entry_offset,
        stack_top, initial_argument, priority, &thread);
    if (thread_status == KERNEL_THREAD_NO_SLOT)
        return KERNEL_PROCESS_RESOURCE_LIMIT;
    if (thread_status != KERNEL_THREAD_OK)
        return KERNEL_PROCESS_CORRUPT;

    if (kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, process->owner,
                            &stack_physical) != KERNEL_MEMORY_OK) {
        result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    stack_held = true;
    uint8_t *stack = physical_bytes(stack_physical, KERNEL_PAGE_SIZE);
    if (stack == NULL)
        goto failed;
    kernel_bytes_clear(stack, KERNEL_PAGE_SIZE);
    KernelVmStatus vm_status = kernel_vm_map_page(
        &process->address_space, stack_base, stack_physical,
        KERNEL_VM_READ | KERNEL_VM_WRITE);
    if (vm_status != KERNEL_VM_OK) {
        if (vm_status == KERNEL_VM_OUT_OF_MEMORY)
            result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    stack_mapped = true;
    if (kernel_memory_release(stack_physical, 1u, process->owner) !=
        KERNEL_MEMORY_OK)
        goto failed;
    stack_held = false;

    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_THREAD,
                              KERNEL_THREAD_RIGHT_QUERY |
                                  KERNEL_THREAD_RIGHT_TERMINATE |
                                  KERNEL_THREAD_RIGHT_CANCEL_WAIT,
                              thread, NULL, NULL, &thread_handle) !=
        KERNEL_HANDLE_OK) {
        result = KERNEL_PROCESS_RESOURCE_LIMIT;
        goto failed;
    }
    handle_installed = true;
    thread->self_handle = thread_handle;
    thread->context.data[4] = process->self_handle;
    thread->context.data[5] = thread_handle;
    if (kernel_thread_publish(thread) != KERNEL_THREAD_OK)
        goto failed;

    process->stack_slots |= (uint16_t)(1u << (uint32_t)stack_slot);
    ++process->thread_count;
    ++process->live_threads;
    ++scheduler_stats.created_threads;
    ++scheduler_stats.live_threads;
    *thread_id = thread->id;
    return KERNEL_PROCESS_OK;

failed:
    if (handle_installed &&
        kernel_handle_close(&process->handles, thread_handle) !=
            KERNEL_HANDLE_OK)
        cleanup_failed = true;
    if (stack_mapped &&
        kernel_vm_unmap_page(&process->address_space, stack_base) !=
            KERNEL_VM_OK)
        cleanup_failed = true;
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

KernelProcessStatus kernel_process_create(const void *image,
                                          uint32_t image_size,
                                          uint32_t entry_offset,
                                          uint32_t initial_argument,
                                          uint32_t *process_id)
{
    KernelProcess *process;
    KernelProcessStatus result = KERNEL_PROCESS_CORRUPT;
    KernelHandle self_handle;
    uint32_t generation;
    uint32_t code_physical = 0u;
    uint32_t initial_thread_id;
    bool code_held = false;
    int32_t slot;

    if (image == NULL || image_size == 0u || image_size > KERNEL_PAGE_SIZE ||
        entry_offset >= image_size || process_id == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *process_id = 0u;
    slot = find_free_slot();
    if (slot < 0)
        return KERNEL_PROCESS_NO_SLOT;
    process = &processes[slot];
    generation = kernel_generation_next(process->generation);
    kernel_bytes_clear(process, sizeof(*process));
    process->generation = generation;
    process->id = PROCESS_OWNER_PREFIX |
                  ((generation & 0x000fffffu) << 4) |
                  ((uint32_t)slot + 1u);
    process->owner = process->id;
    process->image_size = image_size;
    process->default_priority = KERNEL_THREAD_PRIORITY_NORMAL;
    process->priority_ceiling = KERNEL_THREAD_PRIORITY_USER_MAX;
    kernel_handle_table_init(&process->handles);

    if (kernel_vm_create_address_space(process->owner,
                                       &process->address_space) != KERNEL_VM_OK)
        goto failed;
    if (kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, process->owner,
                            &code_physical) != KERNEL_MEMORY_OK) {
        result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    code_held = true;
    uint8_t *code = physical_bytes(code_physical, KERNEL_PAGE_SIZE);
    if (code == NULL)
        goto failed;
    kernel_bytes_clear(code, KERNEL_PAGE_SIZE);
    kernel_bytes_copy(code, image, image_size);
    if (kernel_vm_map_page(&process->address_space, KERNEL_PROCESS_CODE_BASE,
                           code_physical,
                           KERNEL_VM_READ | KERNEL_VM_EXEC) != KERNEL_VM_OK)
        goto failed;
    if (kernel_memory_release(code_physical, 1u, process->owner) !=
        KERNEL_MEMORY_OK)
        goto failed;
    code_held = false;

    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_PROCESS,
                              KERNEL_PROCESS_RIGHT_QUERY, process, NULL, NULL,
                              &self_handle) != KERNEL_HANDLE_OK)
        goto failed;
    process->self_handle = self_handle;
    process->process_state = KERNEL_PROCESS_CREATED;
    result = create_thread(process, entry_offset, initial_argument,
                           process->default_priority, &initial_thread_id);
    if (result != KERNEL_PROCESS_OK || initial_thread_id == 0u)
        goto failed;
    ++scheduler_stats.created_processes;
    ++scheduler_stats.live_processes;
    *process_id = process->id;
    return KERNEL_PROCESS_OK;

failed:
    if (code_held)
        (void)kernel_memory_release(code_physical, 1u, process->owner);
    (void)kernel_handle_close_all(&process->handles);
    if (process->address_space.initialized != 0u)
        (void)kernel_vm_destroy_address_space(&process->address_space);
    (void)kernel_memory_release_owner(process->owner, NULL);
    process->process_state = KERNEL_PROCESS_DEAD;
    return result;
}

KernelProcessStatus kernel_process_create_thread(uint32_t process_id,
                                                 uint32_t entry_offset,
                                                 uint32_t initial_argument,
                                                 uint8_t priority,
                                                 uint32_t *thread_id)
{
    KernelProcess *process = find_process_by_id(process_id);

    if (process == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    return create_thread(process, entry_offset, initial_argument, priority,
                         thread_id);
}

KernelProcessStatus kernel_process_start(KernelCpuContext **next_context)
{
    KernelThread *next = NULL;
    KernelThreadStatus status;

    if (next_context == NULL || current_thread != NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = kernel_thread_take_next(&next);
    if (status == KERNEL_THREAD_NO_RUNNABLE)
        return KERNEL_PROCESS_NO_RUNNABLE;
    if (status != KERNEL_THREAD_OK || next == NULL)
        return KERNEL_PROCESS_CORRUPT;
    return activate(next, next_context);
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
    return &current_thread->context;
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

    *next_context = &previous->context;
    scheduler_timer_rearm();
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

KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelThread *thread;
    KernelProcessStatus status;
    uint32_t syscall;
    uint32_t result = ASTRA_SYSCALL_OK;

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
    switch (syscall) {
    case ASTRA_SYSCALL_QUERY_ABI:
        thread->context.data[1] = ASTRA_SYSCALL_ABI_VERSION;
        thread->context.data[2] = current->self_handle;
        thread->context.data[3] = thread->self_handle;
        break;
    case ASTRA_SYSCALL_PROGRESS:
        if (thread->context.data[1] < current->progress)
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        else
            current->progress = thread->context.data[1];
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
        return retire_current(KERNEL_PROCESS_EXIT_SYSCALL, next_context);
    case ASTRA_SYSCALL_CLOSE:
        switch (kernel_handle_close(&current->handles,
                                    thread->context.data[1])) {
        case KERNEL_HANDLE_OK:
            if (!kernel_sync_pool_valid())
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
    case ASTRA_SYSCALL_CLOCK_MONOTONIC: {
        uint64_t nanoseconds = kernel_platform_monotonic_ns();

        thread->context.data[1] = (uint32_t)(nanoseconds >> 32);
        thread->context.data[2] = (uint32_t)nanoseconds;
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
        switch (kernel_handle_install(
            &current->handles, KERNEL_OBJECT_SYNC, rights, object,
            kernel_sync_handle_release, NULL, &handle)) {
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
        if (!kernel_sync_pool_valid())
            return KERNEL_PROCESS_CORRUPT;
        break;
    }
    case ASTRA_SYSCALL_WAIT_ONE: {
        KernelSyncObject *object = NULL;
        KernelSyncStatus sync_status;
        KernelHandleStatus handle_status;
        uint64_t deadline_bits;
        uint64_t deadline_cycles;
        uint64_t now;

        handle_status = kernel_handle_lookup(
            &current->handles, thread->context.data[1], KERNEL_OBJECT_SYNC,
            KERNEL_SYNC_RIGHT_WAIT, (void **)&object);
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
        if ((thread->context.data[2] & 0x80000000u) != 0u) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        deadline_bits = ((uint64_t)thread->context.data[2] << 32) |
                        thread->context.data[3];
        if (!kernel_platform_deadline_to_cycles((int64_t)deadline_bits,
                                                &deadline_cycles)) {
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
            break;
        }
        now = scheduler_cycles();
        sync_status = kernel_sync_wait(object, thread, now, deadline_cycles,
                                       ASTRA_SYSCALL_TIMED_OUT);
        if (sync_status == KERNEL_SYNC_OK)
            break;
        if (sync_status == KERNEL_SYNC_TIMED_OUT) {
            result = ASTRA_SYSCALL_TIMED_OUT;
            break;
        }
        if (sync_status == KERNEL_SYNC_CLOSED) {
            result = kernel_sync_terminal_result(object);
            if (result == ASTRA_SYSCALL_OK)
                return KERNEL_PROCESS_CORRUPT;
            break;
        }
        if (sync_status == KERNEL_SYNC_WAITER_LIMIT) {
            result = ASTRA_SYSCALL_RESOURCE_LIMIT;
            break;
        }
        if (sync_status != KERNEL_SYNC_BLOCKED)
            return sync_status == KERNEL_SYNC_INVALID_ARGUMENT ||
                           sync_status == KERNEL_SYNC_INVALID_STATE ?
                KERNEL_PROCESS_INVALID_STATE : KERNEL_PROCESS_CORRUPT;
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
        ready_thread_outranks(thread)) {
        status = schedule_pending(next_context);
        if (status != KERNEL_PROCESS_OK)
            return status;
    } else {
        *next_context = &thread->context;
    }
    if (syscall == ASTRA_SYSCALL_PROGRESS)
        check_milestone();
    return KERNEL_PROCESS_OK;
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
    (void)thread;
    if (kernel_exception_decode(raw_frame, KERNEL_EXCEPTION_FRAME_MAX_SIZE,
                                &frame) != KERNEL_EXCEPTION_OK ||
        frame.from_user == 0u)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    current->fault_vector = (uint16_t)(frame.vector_offset >> 2);
    current->fault_address = frame.fault_address;
    ++scheduler_stats.user_faults;
    return retire_current(KERNEL_PROCESS_EXIT_USER_FAULT, next_context);
}

KernelProcessStatus kernel_process_maintenance(void)
{
    KernelProcessStatus status = KERNEL_PROCESS_OK;

    kernel_bytes_clear(&maintenance_diagnostics,
                       sizeof(maintenance_diagnostics));

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
        uint32_t process_id;
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
        if (memory_stats.free_frames != soak_state.baseline_free_frames)
            return maintenance_failed(KERNEL_PROCESS_MAINTENANCE_FREE_FRAMES,
                                      KERNEL_PROCESS_CORRUPT,
                                      memory_stats.free_frames,
                                      soak_state.baseline_free_frames);

        cycles = scheduler_stats.soak_cycles + 1u;
        if (cycles == 0u)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_CYCLE_OVERFLOW,
                KERNEL_PROCESS_CORRUPT, scheduler_stats.soak_cycles,
                UINT32_MAX);
        scheduler_stats.soak_cycles = cycles;
        soak_state.last_completed_teardowns =
            scheduler_stats.completed_teardowns;
        if (scheduler_stats.milestone_complete != 0u &&
            (soak_state.milestone_reported == 0u || cycles == 1u ||
             cycles == 10u || cycles == 100u || cycles == 1000u ||
             cycles % soak_state.report_interval == 0u)) {
            kernel_process_soak_checkpoint(cycles,
                                           soak_state.baseline_free_frames);
            soak_state.milestone_reported = 1u;
        }
        status = kernel_process_create(
            soak_state.image, soak_state.image_size, soak_state.entry_offset,
            cycles, &process_id);
        if (status != KERNEL_PROCESS_OK || process_id == 0u)
            return maintenance_failed(
                KERNEL_PROCESS_MAINTENANCE_CREATE,
                status == KERNEL_PROCESS_OK ? KERNEL_PROCESS_CORRUPT : status,
                status == KERNEL_PROCESS_OK ? process_id : (uint32_t)status,
                status == KERNEL_PROCESS_OK ? 1u :
                                              (uint32_t)KERNEL_PROCESS_OK);
    }
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
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        if (processes[index].process_state == KERNEL_PROCESS_EXITING)
            return true;
    }
    return false;
}

KernelProcessStatus kernel_process_reap_deferred(void)
{
    bool deferred = false;

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

    if (slot >= KERNEL_PROCESS_MAX || snapshot == NULL)
        return false;
    process = &processes[slot];
    snapshot->id = process->id;
    snapshot->owner = process->owner;
    snapshot->progress = process->progress;
    snapshot->timer_ticks =
        kernel_thread_process_timer_ticks((uint16_t)slot);
    snapshot->run_count = kernel_thread_process_run_count((uint16_t)slot);
    snapshot->syscall_count =
        kernel_thread_process_syscalls((uint16_t)slot);
    snapshot->fault_address = process->fault_address;
    snapshot->self_handle = process->self_handle;
    snapshot->fault_vector = process->fault_vector;
    snapshot->process_state = process->process_state;
    snapshot->thread_state = (uint8_t)
        kernel_thread_process_representative_state((uint16_t)slot);
    snapshot->exit_reason = process->exit_reason;
    snapshot->default_priority = process->default_priority;
    snapshot->priority_ceiling = process->priority_ceiling;
    snapshot->thread_count = process->thread_count;
    snapshot->live_threads = process->live_threads;
    snapshot->reserved = 0u;
    return true;
}

bool kernel_process_stats(KernelSchedulerStats *stats)
{
    KernelSyncPoolStats sync_stats;
    KernelThreadPoolStats thread_stats;

    if (stats == NULL || !kernel_thread_pool_stats(&thread_stats) ||
        !kernel_sync_pool_stats(&sync_stats))
        return false;
    if (thread_stats.created_threads != scheduler_stats.created_threads ||
        thread_stats.live_threads != scheduler_stats.live_threads ||
        thread_stats.dead_threads != scheduler_stats.dead_threads)
        return false;
    stats->created_processes = scheduler_stats.created_processes;
    stats->live_processes = scheduler_stats.live_processes;
    stats->dead_processes = scheduler_stats.dead_processes;
    stats->context_switches = scheduler_stats.context_switches;
    stats->timer_preemptions = scheduler_stats.timer_preemptions;
    stats->voluntary_switches = scheduler_stats.voluntary_switches;
    stats->total_syscalls_low = scheduler_stats.total_syscalls_low;
    stats->total_syscalls_high = scheduler_stats.total_syscalls_high;
    stats->user_faults = scheduler_stats.user_faults;
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
    stats->sync_signal_calls = sync_stats.signal_calls;
    stats->sync_cancellations = thread_stats.wait_cancellations;
    stats->sync_close_wakeups = sync_stats.close_wakeups;
    stats->sync_owner_deaths = sync_stats.owner_deaths;
    stats->ready_bitmap = thread_stats.ready_bitmap;
    stats->blocked_threads = thread_stats.blocked_threads;
    stats->kernel_stack_entries = thread_stats.kernel_stack_entries;
    stats->kernel_stack_max_used = thread_stats.kernel_stack_max_used;
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
    if (image == NULL || image_size == 0u || image_size > KERNEL_PAGE_SIZE ||
        entry_offset >= image_size || baseline_free_frames == 0u ||
        report_interval == 0u)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    if (soak_state.enabled != 0u || scheduler_stats.live_processes != 2u ||
        scheduler_stats.completed_teardowns != 0u)
        return KERNEL_PROCESS_INVALID_STATE;

    soak_state.image = image;
    soak_state.image_size = image_size;
    soak_state.entry_offset = entry_offset;
    soak_state.baseline_free_frames = baseline_free_frames;
    soak_state.report_interval = report_interval;
    soak_state.last_completed_teardowns = 0u;
    soak_state.milestone_reported = 0u;
    soak_state.enabled = 1u;
    return KERNEL_PROCESS_OK;
}
#endif
