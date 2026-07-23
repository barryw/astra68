#include "process.h"

#include <astra/syscall.h>

#include "block.h"
#include "exception.h"
#include "memory.h"
#include "vm.h"

#include <stddef.h>

#define PROCESS_OWNER_PREFIX 0x10000000u

typedef struct KernelProcess {
    KernelAddressSpace address_space;
    KernelCpuContext context;
    KernelHandleTable handles;
    uint32_t id;
    uint32_t owner;
    uint32_t generation;
    uint32_t progress;
    uint32_t timer_ticks;
    uint32_t run_count;
    uint32_t syscall_count;
    uint32_t fault_address;
    KernelHandle self_handle;
    uint16_t fault_vector;
    uint8_t process_state;
    uint8_t thread_state;
    uint8_t exit_reason;
    uint8_t handles_closed;
    uint8_t address_space_destroyed;
    uint8_t reserved[2];
} KernelProcess;

_Static_assert(offsetof(KernelProcess, context) % KERNEL_CONTEXT_ALIGNMENT ==
                   0u,
               "process context is not longword aligned");
_Static_assert(sizeof(KernelProcess) % KERNEL_CONTEXT_ALIGNMENT == 0u,
               "process array stride can misalign a context");

static KernelProcess processes[KERNEL_PROCESS_MAX];
static KernelSchedulerStats scheduler_stats;
static int32_t current_slot = -1;

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

static void clear_bytes(void *value, uint32_t size)
{
    uint8_t *bytes = value;

    while (size-- != 0u)
        *bytes++ = 0u;
}

static void copy_bytes(void *destination, const void *source, uint32_t size)
{
    uint8_t *out = destination;
    const uint8_t *in = source;

    while (size-- != 0u)
        *out++ = *in++;
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

static uint32_t next_generation(uint32_t generation)
{
    ++generation;
    return generation == 0u ? 1u : generation;
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

static int32_t find_next_ready(int32_t after)
{
    for (uint32_t offset = 1u; offset <= KERNEL_PROCESS_MAX; ++offset) {
        uint32_t index = ((uint32_t)after + offset) % KERNEL_PROCESS_MAX;

        if ((processes[index].process_state == KERNEL_PROCESS_CREATED ||
             processes[index].process_state == KERNEL_PROCESS_RUNNING) &&
            processes[index].thread_state == KERNEL_THREAD_READY)
            return (int32_t)index;
    }
    return -1;
}

static KernelProcessStatus activate(int32_t slot,
                                    KernelCpuContext **next_context)
{
    KernelProcess *next;

    if (slot < 0 || slot >= (int32_t)KERNEL_PROCESS_MAX ||
        next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    next = &processes[slot];
    if ((next->process_state != KERNEL_PROCESS_CREATED &&
         next->process_state != KERNEL_PROCESS_RUNNING) ||
        next->thread_state != KERNEL_THREAD_READY ||
        !kernel_context_valid(&next->context))
        return KERNEL_PROCESS_INVALID_STATE;
    if (current_slot != slot) {
        if (kernel_vm_switch(&next->address_space) != KERNEL_VM_OK)
            return KERNEL_PROCESS_CORRUPT;
        ++scheduler_stats.context_switches;
    }
    next->process_state = KERNEL_PROCESS_RUNNING;
    next->thread_state = KERNEL_THREAD_RUNNING;
    ++next->run_count;
    current_slot = slot;
    scheduler_stats.current_process_id = next->id;
    *next_context = &next->context;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus capture_current(const uint32_t *registers,
                                           uint32_t user_stack,
                                           const void *raw_frame,
                                           KernelProcess **process)
{
    KernelProcess *current;

    if (current_slot < 0 || current_slot >= (int32_t)KERNEL_PROCESS_MAX ||
        process == NULL)
        return KERNEL_PROCESS_INVALID_STATE;
    current = &processes[current_slot];
    if (current->process_state != KERNEL_PROCESS_RUNNING ||
        current->thread_state != KERNEL_THREAD_RUNNING)
        return KERNEL_PROCESS_INVALID_STATE;
    if (kernel_context_capture(&current->context, registers, user_stack,
                               raw_frame) != KERNEL_CONTEXT_OK)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    *process = current;
    return KERNEL_PROCESS_OK;
}

static KernelProcessStatus finish_reap(KernelProcess *process)
{
    uint32_t released_buffers = 0u;
    uint32_t deferred_buffers = 0u;
    uint32_t released_frames = 0u;

    if (process->handles_closed == 0u) {
        (void)kernel_handle_close_all(&process->handles);
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
    process->process_state = KERNEL_PROCESS_DEAD;
    process->thread_state = KERNEL_THREAD_DEAD;
    if (process->exit_reason == KERNEL_PROCESS_EXIT_USER_FAULT)
        ++scheduler_stats.completed_user_fault_teardowns;
    ++scheduler_stats.dead_processes;
    ++scheduler_stats.completed_teardowns;
    return KERNEL_PROCESS_OK;
}

static void check_milestone(void)
{
    bool survivor_ready = false;

    if (scheduler_stats.milestone_complete != 0u ||
        scheduler_stats.created_processes < 2u ||
        scheduler_stats.timer_preemptions == 0u ||
        scheduler_stats.user_faults == 0u ||
        scheduler_stats.completed_user_fault_teardowns == 0u)
        return;
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        const KernelProcess *process = &processes[index];

        if ((process->process_state == KERNEL_PROCESS_CREATED ||
             process->process_state == KERNEL_PROCESS_RUNNING) &&
            (process->thread_state == KERNEL_THREAD_READY ||
             process->thread_state == KERNEL_THREAD_RUNNING) &&
            process->progress >= KERNEL_PROCESS_PROGRESS_GOAL &&
            process->run_count != 0u)
            survivor_ready = true;
    }
    if (!survivor_ready)
        return;
    scheduler_stats.milestone_complete = 1u;
    kernel_process_milestone_reached();
}

static KernelProcessStatus retire_current(KernelProcessExitReason reason,
                                          KernelCpuContext **next_context)
{
    KernelProcess *retiring;
    KernelProcessStatus status;
    int32_t retiring_slot;
    int32_t next_slot;

    if (next_context == NULL || current_slot < 0)
        return KERNEL_PROCESS_INVALID_STATE;
    retiring_slot = current_slot;
    retiring = &processes[retiring_slot];
    retiring->process_state = KERNEL_PROCESS_EXITING;
    retiring->thread_state = KERNEL_THREAD_DEAD;
    retiring->exit_reason = (uint8_t)reason;
    --scheduler_stats.live_processes;
    next_slot = find_next_ready(retiring_slot);
    if (next_slot < 0) {
        if (kernel_vm_switch_to_empty() != KERNEL_VM_OK)
            return KERNEL_PROCESS_CORRUPT;
        current_slot = -1;
        scheduler_stats.current_process_id = 0u;
        *next_context = NULL;
    } else {
        status = activate(next_slot, next_context);
        if (status != KERNEL_PROCESS_OK)
            return status;
    }
    check_milestone();
    return next_slot < 0 ? KERNEL_PROCESS_NO_RUNNABLE : KERNEL_PROCESS_OK;
}

void kernel_process_init(void)
{
    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index)
        clear_bytes(&processes[index], sizeof(processes[index]));
    clear_bytes(&scheduler_stats, sizeof(scheduler_stats));
#if ASTRA_KERNEL_SOAK_SELFTEST
    clear_bytes(&soak_state, sizeof(soak_state));
#endif
    current_slot = -1;
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
    uint32_t stack_physical = 0u;
    bool code_held = false;
    bool stack_held = false;
    int32_t slot;

    if (image == NULL || image_size == 0u || image_size > KERNEL_PAGE_SIZE ||
        entry_offset >= image_size || process_id == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *process_id = 0u;
    slot = find_free_slot();
    if (slot < 0)
        return KERNEL_PROCESS_NO_SLOT;
    process = &processes[slot];
    generation = next_generation(process->generation);
    clear_bytes(process, sizeof(*process));
    process->generation = generation;
    process->id = PROCESS_OWNER_PREFIX |
                  ((generation & 0x000fffffu) << 4) |
                  ((uint32_t)slot + 1u);
    process->owner = process->id;
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
    clear_bytes(code, KERNEL_PAGE_SIZE);
    copy_bytes(code, image, image_size);
    if (kernel_vm_map_page(&process->address_space, KERNEL_PROCESS_CODE_BASE,
                           code_physical,
                           KERNEL_VM_READ | KERNEL_VM_EXEC) != KERNEL_VM_OK)
        goto failed;
    if (kernel_memory_release(code_physical, 1u, process->owner) !=
        KERNEL_MEMORY_OK)
        goto failed;
    code_held = false;

    if (kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, process->owner,
                            &stack_physical) != KERNEL_MEMORY_OK) {
        result = KERNEL_PROCESS_OUT_OF_MEMORY;
        goto failed;
    }
    stack_held = true;
    uint8_t *stack = physical_bytes(stack_physical, KERNEL_PAGE_SIZE);
    if (stack == NULL)
        goto failed;
    clear_bytes(stack, KERNEL_PAGE_SIZE);
    if (kernel_vm_map_page(&process->address_space, KERNEL_PROCESS_STACK_BASE,
                           stack_physical,
                           KERNEL_VM_READ | KERNEL_VM_WRITE) != KERNEL_VM_OK)
        goto failed;
    if (kernel_memory_release(stack_physical, 1u, process->owner) !=
        KERNEL_MEMORY_OK)
        goto failed;
    stack_held = false;

    kernel_context_initialize(&process->context,
                              KERNEL_PROCESS_CODE_BASE + entry_offset,
                              KERNEL_PROCESS_STACK_TOP);
    process->context.data[2] = initial_argument;
    if (kernel_handle_install(&process->handles, KERNEL_OBJECT_PROCESS,
                              KERNEL_PROCESS_RIGHT_QUERY, process, NULL, NULL,
                              &self_handle) != KERNEL_HANDLE_OK)
        goto failed;
    process->self_handle = self_handle;
    process->context.data[4] = self_handle;
    process->process_state = KERNEL_PROCESS_CREATED;
    process->thread_state = KERNEL_THREAD_READY;
    ++scheduler_stats.created_processes;
    ++scheduler_stats.live_processes;
    *process_id = process->id;
    return KERNEL_PROCESS_OK;

failed:
    if (stack_held)
        (void)kernel_memory_release(stack_physical, 1u, process->owner);
    if (code_held)
        (void)kernel_memory_release(code_physical, 1u, process->owner);
    (void)kernel_handle_close_all(&process->handles);
    if (process->address_space.initialized != 0u)
        (void)kernel_vm_destroy_address_space(&process->address_space);
    (void)kernel_memory_release_owner(process->owner, NULL);
    process->process_state = KERNEL_PROCESS_DEAD;
    process->thread_state = KERNEL_THREAD_DEAD;
    return result;
}

KernelProcessStatus kernel_process_start(KernelCpuContext **next_context)
{
    int32_t next;

    if (next_context == NULL || current_slot >= 0)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    next = find_next_ready(-1);
    if (next < 0)
        return KERNEL_PROCESS_NO_RUNNABLE;
    return activate(next, next_context);
}

bool kernel_process_active(void)
{
    return current_slot >= 0;
}

KernelProcessStatus kernel_process_on_timer(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelProcessStatus status;
    int32_t previous;
    int32_t next;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current);
    if (status != KERNEL_PROCESS_OK)
        return status;
    previous = current_slot;
    ++current->timer_ticks;
    current->thread_state = KERNEL_THREAD_READY;
    next = find_next_ready(previous);
    if (next < 0)
        return KERNEL_PROCESS_NO_RUNNABLE;
    if (next != previous)
        ++scheduler_stats.timer_preemptions;
    return activate(next, next_context);
}

KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context)
{
    KernelProcess *current;
    KernelProcessStatus status;
    uint32_t syscall;
    uint32_t result = ASTRA_SYSCALL_OK;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current);
    if (status != KERNEL_PROCESS_OK)
        return status;
    if (current->context.vector != ASTRA_SYSCALL_VECTOR ||
        current->context.frame_format != 0u)
        return KERNEL_PROCESS_INVALID_CONTEXT;
    ++current->syscall_count;
    if (++scheduler_stats.total_syscalls_low == 0u)
        ++scheduler_stats.total_syscalls_high;
    syscall = current->context.data[0];
    switch (syscall) {
    case ASTRA_SYSCALL_QUERY_ABI:
        current->context.data[1] = ASTRA_SYSCALL_ABI_VERSION;
        current->context.data[2] = current->self_handle;
        break;
    case ASTRA_SYSCALL_PROGRESS:
        if (current->context.data[1] < current->progress)
            result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        else
            current->progress = current->context.data[1];
        break;
    case ASTRA_SYSCALL_YIELD: {
        int32_t previous = current_slot;
        int32_t next;

        current->context.data[0] = ASTRA_SYSCALL_OK;
        current->thread_state = KERNEL_THREAD_READY;
        next = find_next_ready(previous);
        if (next < 0)
            return KERNEL_PROCESS_NO_RUNNABLE;
        if (next != previous)
            ++scheduler_stats.voluntary_switches;
        return activate(next, next_context);
    }
    case ASTRA_SYSCALL_EXIT:
        current->context.data[0] = ASTRA_SYSCALL_OK;
        return retire_current(KERNEL_PROCESS_EXIT_SYSCALL, next_context);
    case ASTRA_SYSCALL_CLOSE:
        switch (kernel_handle_close(&current->handles,
                                    current->context.data[1])) {
        case KERNEL_HANDLE_OK:
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
    default:
        result = ASTRA_SYSCALL_BAD_SYSCALL;
        break;
    }
    current->context.data[0] = result;
    *next_context = &current->context;
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
    KernelProcessStatus status;

    if (next_context == NULL)
        return KERNEL_PROCESS_INVALID_ARGUMENT;
    *next_context = NULL;
    status = capture_current(registers, user_stack, raw_frame, &current);
    if (status != KERNEL_PROCESS_OK)
        return status;
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
    bool reap_pending = false;
    KernelProcessStatus status = KERNEL_PROCESS_OK;

    for (uint32_t index = 0u; index < KERNEL_PROCESS_MAX; ++index) {
        if (processes[index].process_state == KERNEL_PROCESS_EXITING) {
            reap_pending = true;
            break;
        }
    }
    if (reap_pending) {
        if (kernel_block_service(NULL) != KERNEL_BLOCK_OK)
            return KERNEL_PROCESS_CORRUPT;
        status = kernel_process_reap_deferred();
        if (status != KERNEL_PROCESS_OK)
            return status;
    }
#if ASTRA_KERNEL_SOAK_SELFTEST
    if (soak_state.enabled != 0u &&
        scheduler_stats.completed_teardowns !=
            soak_state.last_completed_teardowns) {
        KernelMemoryStats memory_stats;
        uint32_t process_id;
        uint32_t cycles;

        if (scheduler_stats.completed_teardowns !=
                soak_state.last_completed_teardowns + 1u ||
            scheduler_stats.live_processes != 1u ||
            scheduler_stats.completed_user_fault_teardowns !=
                scheduler_stats.user_faults ||
            scheduler_stats.completed_teardowns !=
                scheduler_stats.user_faults ||
            !kernel_memory_stats(&memory_stats) ||
            memory_stats.free_frames != soak_state.baseline_free_frames)
            return KERNEL_PROCESS_CORRUPT;

        cycles = scheduler_stats.soak_cycles + 1u;
        if (cycles == 0u)
            return KERNEL_PROCESS_CORRUPT;
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
            return status == KERNEL_PROCESS_OK ? KERNEL_PROCESS_CORRUPT :
                                                status;
    }
#endif
    return KERNEL_PROCESS_OK;
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
    snapshot->timer_ticks = process->timer_ticks;
    snapshot->run_count = process->run_count;
    snapshot->syscall_count = process->syscall_count;
    snapshot->fault_address = process->fault_address;
    snapshot->self_handle = process->self_handle;
    snapshot->fault_vector = process->fault_vector;
    snapshot->process_state = process->process_state;
    snapshot->thread_state = process->thread_state;
    snapshot->exit_reason = process->exit_reason;
    return true;
}

bool kernel_process_stats(KernelSchedulerStats *stats)
{
    if (stats == NULL)
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
