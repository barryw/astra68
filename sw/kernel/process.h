#ifndef ASTRA_KERNEL_PROCESS_H
#define ASTRA_KERNEL_PROCESS_H

#include "context.h"
#include "handle.h"
#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef ASTRA_KERNEL_SOAK_SELFTEST
#define ASTRA_KERNEL_SOAK_SELFTEST 0
#endif

#define KERNEL_PROCESS_MAX 4u
#define KERNEL_PROCESS_CODE_BASE 0x00100000u
#define KERNEL_PROCESS_STACK_BASE KERNEL_THREAD_STACK_BASE
#define KERNEL_PROCESS_STACK_TOP \
    (KERNEL_PROCESS_STACK_BASE + KERNEL_THREAD_STACK_SIZE)
#define KERNEL_PROCESS_PROGRESS_GOAL 64u
#define KERNEL_PROCESS_THREAD_MAX 15u

#define KERNEL_PROCESS_RIGHT_QUERY     (1u << 0)
#define KERNEL_PROCESS_RIGHT_TERMINATE (1u << 1)
#define KERNEL_PROCESS_RIGHT_WAIT      (1u << 4)
#define KERNEL_PROCESS_RIGHTS \
    (KERNEL_PROCESS_RIGHT_QUERY | KERNEL_PROCESS_RIGHT_TERMINATE | \
     KERNEL_PROCESS_RIGHT_WAIT)

typedef enum KernelProcessState {
    KERNEL_PROCESS_UNUSED = 0,
    KERNEL_PROCESS_CREATED,
    KERNEL_PROCESS_RUNNING,
    KERNEL_PROCESS_EXITING,
    KERNEL_PROCESS_DEAD
} KernelProcessState;

typedef enum KernelProcessExitReason {
    KERNEL_PROCESS_EXIT_NONE = 0,
    KERNEL_PROCESS_EXIT_SYSCALL,
    KERNEL_PROCESS_EXIT_LAST_THREAD,
    KERNEL_PROCESS_EXIT_USER_FAULT
} KernelProcessExitReason;

typedef enum KernelProcessStatus {
    KERNEL_PROCESS_OK = 0,
    KERNEL_PROCESS_INVALID_ARGUMENT,
    KERNEL_PROCESS_NO_SLOT,
    KERNEL_PROCESS_OUT_OF_MEMORY,
    KERNEL_PROCESS_INVALID_STATE,
    KERNEL_PROCESS_INVALID_CONTEXT,
    KERNEL_PROCESS_NO_RUNNABLE,
    KERNEL_PROCESS_DEFERRED,
    KERNEL_PROCESS_RESOURCE_LIMIT,
    KERNEL_PROCESS_CORRUPT
} KernelProcessStatus;

typedef enum KernelProcessMaintenanceFailure {
    KERNEL_PROCESS_MAINTENANCE_NONE = 0,
    KERNEL_PROCESS_MAINTENANCE_BLOCK_SERVICE,
    KERNEL_PROCESS_MAINTENANCE_REAP,
    KERNEL_PROCESS_MAINTENANCE_TEARDOWN_SEQUENCE,
    KERNEL_PROCESS_MAINTENANCE_LIVE_COUNT,
    KERNEL_PROCESS_MAINTENANCE_FAULT_TEARDOWN_COUNT,
    KERNEL_PROCESS_MAINTENANCE_TOTAL_TEARDOWN_COUNT,
    KERNEL_PROCESS_MAINTENANCE_MEMORY_STATS,
    KERNEL_PROCESS_MAINTENANCE_FREE_FRAMES,
    KERNEL_PROCESS_MAINTENANCE_CYCLE_OVERFLOW,
    KERNEL_PROCESS_MAINTENANCE_CREATE,
    KERNEL_PROCESS_MAINTENANCE_THREAD_REAP,
    KERNEL_PROCESS_MAINTENANCE_OWNER_FRAMES,
    KERNEL_PROCESS_MAINTENANCE_OWNER_FRAME_UNDERFLOW
} KernelProcessMaintenanceFailure;

typedef struct KernelProcessMaintenanceDiagnostics {
    uint32_t failure;
    uint32_t status;
    uint32_t observed;
    uint32_t expected;
} KernelProcessMaintenanceDiagnostics;

typedef struct KernelProcessSnapshot {
    uint32_t id;
    uint32_t owner;
    uint32_t generation;
    uint32_t progress;
    uint32_t timer_ticks;
    uint32_t run_count;
    uint32_t syscall_count;
    uint32_t fault_address;
    uint32_t exit_status;
    uint32_t terminal_result;
    KernelHandle self_handle;
    uint16_t fault_vector;
    uint8_t process_state;
    uint8_t thread_state;
    uint8_t exit_reason;
    uint8_t default_priority;
    uint8_t priority_ceiling;
    uint8_t thread_count;
    uint8_t live_threads;
    uint8_t user_stack_pages;
    uint8_t user_guard_pages;
    uint8_t supervisor_stack_pages;
    uint8_t supervisor_guard_pages;
    uint16_t handle_references;
    uint16_t death_waiters;
    uint8_t reserved[2];
} KernelProcessSnapshot;

typedef struct KernelSchedulerStats {
    uint32_t created_processes;
    uint32_t live_processes;
    uint32_t dead_processes;
    uint32_t context_switches;
    uint32_t timer_preemptions;
    uint32_t voluntary_switches;
    uint32_t total_syscalls_low;
    uint32_t total_syscalls_high;
    uint32_t user_faults;
    uint32_t completed_user_fault_teardowns;
    uint32_t completed_teardowns;
    uint32_t forced_frame_releases;
    uint32_t soak_cycles;
    uint32_t current_process_id;
    uint32_t created_threads;
    uint32_t live_threads;
    uint32_t dead_threads;
    uint32_t current_thread_id;
    uint32_t same_address_space_switches;
    uint32_t cross_address_space_switches;
    uint32_t priority_preemptions;
    uint32_t wait_blocks;
    uint32_t sync_wakeups;
    uint32_t wake_preemptions;
    uint32_t quantum_cycles;
    uint32_t quantum_expirations;
    uint32_t deadline_expirations;
    uint32_t deadline_preemptions;
    uint32_t timer_rearms;
    uint32_t supervisor_timer_deferrals;
    uint32_t deadline_depth;
    uint32_t deadline_max_depth;
    uint32_t sync_created_events;
    uint32_t sync_created_semaphores;
    uint32_t sync_live_objects;
    uint32_t sync_max_live_objects;
    uint32_t sync_wait_calls;
    uint32_t sync_signal_calls;
    uint32_t sync_cancellations;
    uint32_t sync_close_wakeups;
    uint32_t sync_owner_deaths;
    uint32_t ready_bitmap;
    uint32_t blocked_threads;
    uint32_t kernel_stack_entries;
    uint32_t kernel_stack_max_used;
    uint32_t thread_exits;
    uint32_t thread_death_waits;
    uint32_t thread_death_wakeups;
    uint32_t process_death_waits;
    uint32_t process_death_wakeups;
    uint32_t completed_thread_reaps;
    uint32_t thread_creation_failures;
    uint32_t wait_set_calls;
    uint32_t wait_set_blocks;
    uint32_t wait_set_wakeups;
    uint32_t wait_set_registrations;
    uint32_t wait_set_registration_max;
    uint32_t wait_set_max_members;
    uint32_t timer_created;
    uint32_t timer_arms;
    uint32_t timer_cancellations;
    uint32_t timer_expirations;
    uint8_t milestone_complete;
    uint8_t reserved[3];
} KernelSchedulerStats;

void kernel_process_init(void);
KernelProcessStatus kernel_process_create(const void *image,
                                          uint32_t image_size,
                                          uint32_t entry_offset,
                                          uint32_t initial_argument,
                                          uint32_t *process_id);
KernelProcessStatus kernel_process_create_thread(uint32_t process_id,
                                                 uint32_t entry_offset,
                                                 uint32_t initial_argument,
                                                 uint8_t priority,
                                                 uint32_t *thread_id);
KernelProcessStatus kernel_process_set_thread_bootstrap_argument(
    uint32_t process_id, uint32_t thread_id, uint32_t argument);
KernelProcessStatus kernel_process_grant_handle(
    uint32_t recipient_process_id, uint32_t target_process_id,
    uint32_t rights, KernelHandle *handle);
KernelProcessStatus kernel_process_start(KernelCpuContext **next_context);
bool kernel_process_active(void);
KernelCpuContext *kernel_process_current_context(void);
bool kernel_process_worker_enter(void);
KernelCpuContext *kernel_process_worker_resume(void);
KernelProcessStatus kernel_process_on_timer(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context);
KernelProcessStatus kernel_process_on_supervisor_timer(void);
KernelProcessStatus kernel_process_on_syscall(const uint32_t *registers,
                                              uint32_t user_stack,
                                              const void *raw_frame,
                                              KernelCpuContext **next_context);
KernelProcessStatus kernel_process_on_fault(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context);
KernelProcessStatus kernel_process_maintenance(void);
bool kernel_process_maintenance_diagnostics(
    KernelProcessMaintenanceDiagnostics *diagnostics);
KernelProcessStatus kernel_process_reap_deferred(void);
bool kernel_process_maintenance_pending(void);
bool kernel_process_snapshot(uint32_t slot, KernelProcessSnapshot *snapshot);
bool kernel_process_stats(KernelSchedulerStats *stats);

void kernel_process_milestone_reached(void);

#if ASTRA_KERNEL_SOAK_SELFTEST
KernelProcessStatus kernel_process_soak_configure(
    const void *image, uint32_t image_size, uint32_t entry_offset,
    uint32_t baseline_free_frames, uint32_t report_interval);
void kernel_process_soak_checkpoint(uint32_t cycles,
                                    uint32_t baseline_free_frames);
#endif

#if defined(KERNEL_PROCESS_HOST_TEST)
typedef enum KernelProcessThreadCreateFault {
    KERNEL_PROCESS_THREAD_CREATE_FAULT_NONE = 0,
    KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_ALLOC,
    KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_MAP,
    KERNEL_PROCESS_THREAD_CREATE_FAULT_HANDLE_INSTALL,
    KERNEL_PROCESS_THREAD_CREATE_FAULT_PUBLISH,
    KERNEL_PROCESS_THREAD_CREATE_FAULT_COUNT
} KernelProcessThreadCreateFault;

void kernel_process_test_bind_physical_memory(uint8_t *memory, uint32_t base,
                                              uint32_t size);
void kernel_process_test_fail_next_thread_create(
    KernelProcessThreadCreateFault fault);
uint32_t kernel_process_test_handle_count(uint32_t process_id);
#endif

#endif
