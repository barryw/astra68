#ifndef ASTRA_KERNEL_PROCESS_H
#define ASTRA_KERNEL_PROCESS_H

#include "context.h"
#include "device.h"
#include "handle.h"
#include "irq.h"
#include "thread.h"
#include "trace.h"

#include <stdbool.h>
#include <stdint.h>

#include <astra/process.h>

#ifndef ASTRA_KERNEL_SOAK_SELFTEST
#define ASTRA_KERNEL_SOAK_SELFTEST 0
#endif

/*
 * Whether this build carries a debug surface at all: the serial monitor, and
 * ASTRA_RIGHT_DEBUG on a process's handle to itself.
 *
 * On during development, which is every build today. A shipped machine turns
 * it off and loses both in one decision rather than in several -- the monitor
 * answers anyone on the UART with page tables and handle tables, and the
 * diagnostic channel is a process writing to the console the operator reads.
 * Neither is a defect; both are a choice that has to be made once, somewhere
 * findable.
 */
#ifndef ASTRA_KERNEL_DEBUG_SURFACE
#define ASTRA_KERNEL_DEBUG_SURFACE 1
#endif

#define KERNEL_PROCESS_MAX 4u
#define KERNEL_PROCESS_CODE_BASE 0x00100000u
#define KERNEL_PROCESS_STACK_BASE KERNEL_THREAD_STACK_BASE
/* The top of the first slot's reservation: where its stack pointer starts. */
#define KERNEL_PROCESS_STACK_TOP \
    (KERNEL_PROCESS_STACK_BASE + KERNEL_THREAD_STACK_STRIDE)
#define KERNEL_PROCESS_PROGRESS_GOAL 64u
#define KERNEL_PROCESS_THREAD_MAX 15u

/*
 * The ceiling on text, data and BSS a single process image may map. 64 pages
 * (256 KiB) was too small for the first service that holds a mounted volume:
 * the filesystem's bounded arena alone is 216 KiB of BSS, and the image needed
 * 78 pages. KERNEL_PROCESS_MAX is 4, so this ceiling bounds image memory at
 * 2 MiB of the 32 MiB machine.
 *
 * It lives here rather than in process.c because the loader's test measures a
 * real executable against it. Held in two places it went stale, and the test
 * kept refusing an image the kernel had been loading happily for weeks.
 */
#define KERNEL_PROCESS_IMAGE_PAGES_MAX 128u

#define KERNEL_PROCESS_RIGHT_QUERY     (1u << 0)
#define KERNEL_PROCESS_RIGHT_TERMINATE (1u << 1)
#define KERNEL_PROCESS_RIGHT_WAIT      (1u << 4)
/*
 * ASTRA_RIGHT_DEBUG in the same bit. Deliberately outside
 * KERNEL_PROCESS_RIGHTS: it is granted to a process over itself when the build
 * carries a debug surface, and to nothing otherwise, which is what makes the
 * diagnostic channel a decision rather than an ambient capability.
 */
#define KERNEL_PROCESS_RIGHT_DEBUG     (1u << 7)
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
    KERNEL_PROCESS_CORRUPT,
    /*
     * Appended rather than inserted: the monitor prints these by number, and a
     * renumbering would silently change what an old capture means.
     */
    KERNEL_PROCESS_ACCESS_DENIED,
    KERNEL_PROCESS_INVALID_HANDLE
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
    KERNEL_PROCESS_MAINTENANCE_OWNER_FRAME_UNDERFLOW,
    KERNEL_PROCESS_MAINTENANCE_IRQ_REVOCATION
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
    /*
     * Faults answered by committing a stack page instead of retiring the
     * process. They are deliberately not counted as user faults: one is the
     * system working and the other is a process dying, and a single number
     * that mixed them could not be read for either.
     */
    uint32_t user_stack_growths;
    uint32_t user_stack_pages_committed;
    uint32_t diagnostic_logs;
    uint32_t diagnostic_log_bytes;
    /* Counted rather than silent: a denied channel looks like a broken one. */
    uint32_t diagnostic_log_refusals;
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
    uint32_t sync_blocked_waits;
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
    uint32_t port_created;
    uint32_t port_active;
    uint32_t port_max_active;
    uint32_t port_sends;
    uint32_t port_receives;
    uint32_t port_send_would_block;
    uint32_t port_receive_would_block;
    uint32_t port_receive_buffer_too_small;
    uint32_t port_wait_wakeups;
    uint32_t port_owner_deaths;
    uint32_t port_queued_messages;
    uint32_t port_queued_bytes;
    uint32_t port_queued_handles;
    uint32_t handle_transfers;
    uint32_t handle_transfer_imports;
    uint32_t handle_transfer_import_rollbacks;
    uint32_t handle_transfer_live_detached;
    uint32_t handle_transfer_pool_exhaustions;
    uint32_t handle_transfer_max_detached;
    uint32_t area_created;
    uint32_t area_active;
    uint32_t area_mappings;
    uint32_t area_map_operations;
    uint32_t area_unmap_operations;
    uint32_t area_committed_pages;
    uint32_t ring_created;
    uint32_t ring_active;
    uint32_t ring_notifications;
    uint32_t ring_producer_notifications;
    uint32_t ring_consumer_notifications;
    uint32_t ring_wait_wakeups;
    uint32_t ring_peer_closures;
    uint32_t irq_wake_to_run_max_cycles;
    uint8_t milestone_complete;
    uint8_t reserved[3];
} KernelSchedulerStats;

void kernel_process_init(void);
KernelProcessStatus kernel_process_create(const void *image,
                                          uint32_t image_size,
                                          uint32_t entry_offset,
                                          uint32_t initial_argument,
                                          uint32_t *process_id);
#define KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX 4u

typedef enum KernelProcessBootstrapKind {
    KERNEL_PROCESS_BOOTSTRAP_DEVICE = 1,
    KERNEL_PROCESS_BOOTSTRAP_IRQ,
    /*
     * A handle the launcher already holds, copied into the child with rights
     * that are a subset of the launcher's. This is the only kind a syscall can
     * ask for: the other two name objects the kernel decides about, and letting
     * a program request one of those by number would be an authority the caller
     * did not have to hold first.
     */
    KERNEL_PROCESS_BOOTSTRAP_HANDLE
} KernelProcessBootstrapKind;

/*
 * One object handed to a process at launch. Granting happens inside creation
 * so a failed grant unwinds with the load and the published capability table
 * can never name a handle that does not exist.
 */
typedef struct KernelProcessBootstrapCapability {
    /*
     * The name the process will see in its startup block. A pointer to a
     * string literal rather than a copy: these descriptors are built by the
     * kernel from constants, and the copy into the bounded ABI field happens
     * where the startup block is written.
     */
    const char *name;
    uint32_t rights;
    uint32_t device_id;      /* KERNEL_PROCESS_BOOTSTRAP_DEVICE */
    uint32_t source_handle;  /* KERNEL_PROCESS_BOOTSTRAP_HANDLE */
    uint8_t kind;
    uint8_t irq_source;      /* KERNEL_PROCESS_BOOTSTRAP_IRQ */
    uint8_t reserved[2];
} KernelProcessBootstrapCapability;

/*
 * Loads a validated big-endian MC68030 executable into a new process, publishes
 * its startup block and self capabilities, and makes its initial thread
 * runnable. All or nothing: a failure anywhere leaves no frames, mappings, or
 * handles behind.
 */
KernelProcessStatus kernel_process_create_executable(
    const void *image, uint32_t image_size,
    const KernelProcessBootstrapCapability *capabilities,
    uint32_t capability_count, uint32_t *process_id);

/*
 * The same load, launched by a program rather than by the firmware: the
 * capability list may name handles out of `source_table`, and the child
 * receives the argument vector.
 *
 * `source_table` is the launcher's own handle table and is what makes the
 * subset rule checkable; NULL is the firmware's case, where there is no
 * launcher and no handle to copy.
 *
 * Exactly one of `image` and `user_image` is supplied. `image` is a pointer the
 * kernel may read; `user_image` is an address in the launcher's own space,
 * which the kernel may only reach through user_copy -- so the headers arrive
 * through a bounded window and every page bounces through one page of kernel
 * memory.
 */
KernelProcessStatus kernel_process_launch(
    const void *image, uint32_t image_size, uint32_t user_image,
    const KernelHandleTable *source_table,
    const KernelProcessBootstrapCapability *capabilities,
    uint32_t capability_count, const AstraLaunchArguments *arguments,
    uint32_t *process_id);
/* Names the process loaded from the firmware-supplied image. */
void kernel_process_register_initial_image(uint32_t process_id);
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
KernelProcessStatus kernel_process_grant_irq(
    uint32_t recipient_process_id, const KernelIrqBinding *binding,
    uint32_t rights, KernelHandle *handle);
KernelProcessStatus kernel_process_grant_device(
    uint32_t recipient_process_id, uint32_t device_id, uint32_t rights,
    KernelHandle *handle);
KernelProcessStatus kernel_process_qualification_authorize(
    uint32_t process_id, uint32_t irq_source_mask);
bool kernel_process_qualification_status(uint32_t process_id,
                                         uint32_t *authorized_sources,
                                         uint32_t *completed_sources);
KernelProcessStatus kernel_process_start(KernelCpuContext **next_context);
bool kernel_process_active(void);
KernelCpuContext *kernel_process_current_context(void);
bool kernel_process_worker_enter(void);
KernelCpuContext *kernel_process_worker_resume(void);
KernelProcessStatus kernel_process_on_timer(const uint32_t *registers,
                                            uint32_t user_stack,
                                            const void *raw_frame,
                                            KernelCpuContext **next_context);
KernelProcessStatus kernel_process_on_interrupt_wakeup(
    const uint32_t *registers, uint32_t user_stack, const void *raw_frame,
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

void kernel_process_milestone_reached(const KernelSchedulerStats *stats);
/*
 * Reported the moment the firmware-supplied image ends, because its record is
 * reclaimed with its last handle and no later poll can recover the outcome.
 */
void kernel_process_initial_image_exited(uint32_t exit_status,
                                         uint32_t exit_reason);
/* Each new stage the firmware-supplied image reports as it comes up. */
void kernel_process_initial_image_progress(uint32_t stage);
/*
 * Where a process's diagnostic line goes. The kernel owns the console, so the
 * sink lives with it; `bytes` is already bounded and stripped of anything
 * unprintable by the time it arrives.
 */
void kernel_process_diagnostic_log(const KernelTraceUserRecord *record,
                                   const void *payload, uint32_t length);

/*
 * What a faulting address turned out to be. The kernel knows where every
 * thread's stack and guard page are and the developer does not, so saying
 * "this was your stack guard" costs nothing here and saves the reader from
 * matching hex against a memory map.
 */
typedef enum KernelProcessFaultKind {
    KERNEL_PROCESS_FAULT_OTHER = 0,
    KERNEL_PROCESS_FAULT_STACK_GUARD,
    KERNEL_PROCESS_FAULT_STACK_ARENA
} KernelProcessFaultKind;

/*
 * A user fault, reported before the process is retired. The address and the
 * program counter existed only as raw trace records before this, which meant
 * a program dying said nothing legible about where.
 */
void kernel_process_fault_report(uint32_t process_id, uint32_t thread_id,
                                 uint32_t program_counter,
                                 uint32_t fault_address, uint32_t vector,
                                 uint32_t kind);
/* True when this build grants ASTRA_RIGHT_DEBUG and stands the monitor up. */
bool kernel_process_debug_surface(void);
#if defined(KERNEL_PROCESS_HOST_TEST)
/* Host suites only: stand in the shipped configuration. -1 restores the build's. */
void kernel_process_set_debug_surface(int enabled);
#endif

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
