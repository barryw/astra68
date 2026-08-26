#ifndef ASTRA_KERNEL_THREAD_H
#define ASTRA_KERNEL_THREAD_H

#define KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET 76

#define KERNEL_THREAD_MAX 32
#define KERNEL_THREAD_PRIORITY_LEVELS 32
#define KERNEL_THREAD_PRIORITY_IDLE 0
#define KERNEL_THREAD_PRIORITY_USER_MIN 1
#define KERNEL_THREAD_PRIORITY_NORMAL 16
#define KERNEL_THREAD_PRIORITY_USER_MAX 23

#define KERNEL_THREAD_SUPERVISOR_GUARD_SIZE 0x00001000
#define KERNEL_THREAD_SUPERVISOR_STACK_SIZE 0x00002000
#define KERNEL_THREAD_SUPERVISOR_SLOT_SIZE 0x00003000

#ifndef __ASSEMBLER__

#include "context.h"
#include "handle.h"

#define KERNEL_THREAD_WAIT_MEMBER_MAX KERNEL_HANDLE_MAX_ENTRIES

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE 0x02050000u

#define KERNEL_THREAD_SLOT_NONE UINT16_MAX

/*
 * Reserve-and-grow. Each thread owns one slot of address space:
 *
 *   slot_base                                              slot_base + STRIDE
 *   |                                                                       |
 *   [ GUARD ][ . . . . . . . grows down into here . . . . . ][ committed at ]
 *   [ 1 page][                                              ][   creation   ]
 *
 * The stride is a **reservation**. It costs page-table entries, not RAM, so
 * the common case needs no number from anybody: a thread starts with SIZE
 * committed at the top of its slot and the fault handler maps another page
 * each time the stack reaches one, up to the floor. The floor page is never
 * mapped, so a wild pointer still dies where it always did.
 *
 * The committed part was one page, and that was too small the moment a
 * request crossed a service boundary: a shell command through the storage
 * protocol is shell -> Kit -> service -> backend -> lwext4's write path, and
 * it faulted 20 bytes below the slot base. It was then three pages, which is a
 * guess that happens to fit today's deepest chain and says nothing about the
 * next one. Growth is what removes the guess.
 *
 * Linux does the same for a main thread and Windows carries reserve and commit
 * in the PE header. Go copies growable stacks instead, which needs compiler
 * cooperation Astra does not have.
 *
 * The growth itself rests on one property of the machine: an access fault
 * leaves the program counter on the faulting instruction, so re-entering user
 * mode there re-runs the access against the page that has since been mapped.
 * That is what the emulator does -- see the resume note in process.c, which is
 * where a machine that reran the bus cycle instead would have to be handled.
 */
#define KERNEL_THREAD_STACK_BASE 0x70000000u
#define KERNEL_THREAD_STACK_STRIDE 0x00010000u
#define KERNEL_THREAD_STACK_GUARD_SIZE 0x00001000u
#define KERNEL_THREAD_STACK_SIZE 0x00001000u

#define KERNEL_THREAD_RIGHT_QUERY       (1u << 0)
#define KERNEL_THREAD_RIGHT_WAIT        (1u << 4)
#define KERNEL_THREAD_RIGHT_CANCEL_WAIT (1u << 6)
#define KERNEL_THREAD_RIGHTS \
    (KERNEL_THREAD_RIGHT_QUERY | KERNEL_THREAD_RIGHT_WAIT | \
     KERNEL_THREAD_RIGHT_CANCEL_WAIT)

#define KERNEL_THREAD_DEADLINE_NEVER UINT64_MAX

typedef enum KernelThreadState {
    KERNEL_THREAD_UNUSED = 0,
    KERNEL_THREAD_CREATED,
    KERNEL_THREAD_READY,
    KERNEL_THREAD_RUNNING,
    KERNEL_THREAD_BLOCKED,
    KERNEL_THREAD_DEAD
} KernelThreadState;

typedef enum KernelThreadStatus {
    KERNEL_THREAD_OK = 0,
    KERNEL_THREAD_INVALID_ARGUMENT,
    KERNEL_THREAD_INVALID_STATE,
    KERNEL_THREAD_NO_SLOT,
    KERNEL_THREAD_NO_RUNNABLE,
    KERNEL_THREAD_CONDITION_CHANGED,
    KERNEL_THREAD_DEADLINE_EXPIRED,
    KERNEL_THREAD_CORRUPT
} KernelThreadStatus;

typedef struct KernelThreadWaitQueue {
    uint32_t sequence;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint16_t reserved;
} KernelThreadWaitQueue;

typedef struct KernelThreadWaitSpec {
    KernelThreadWaitQueue *queue;
    uint32_t sequence;
} KernelThreadWaitSpec;

typedef enum KernelThreadWaitMode {
    KERNEL_THREAD_WAIT_NONE = 0,
    KERNEL_THREAD_WAIT_ONE,
    KERNEL_THREAD_WAIT_MULTIPLE
} KernelThreadWaitMode;

typedef struct KernelThread {
    KernelCpuContext context;
    uint32_t kernel_stack_top;
    uint32_t kernel_stack_base;
    uint32_t kernel_stack_low_water;
    uint32_t kernel_stack_entries;
    uint32_t id;
    uint32_t generation;
    uint32_t process_id;
    uint32_t user_stack_base;
    uint32_t user_stack_top;
    uint32_t timer_ticks;
    uint32_t run_count;
    uint32_t syscall_count;
    /*
     * What this thread is doing, for correlation. Set by the thread itself
     * through ASTRA_SYSCALL_ACTIVITY and stamped on every event it emits, so
     * no call site has to remember to pass one -- the ones that matter would
     * be the ones that forgot. Zero means no activity, which is what an event
     * outside any unit of work carries.
     */
    uint32_t activity;
    KernelHandle port_probe_handle;
    uint32_t port_probe_sequence;
    KernelHandle self_handle;
    uint16_t slot;
    uint16_t process_slot;
    uint16_t stack_slot;
    uint16_t ready_previous;
    uint16_t ready_next;
    uint8_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
    uint8_t occupied;
    uint8_t wait_member_count;
    uint8_t wait_mode;
    uint16_t wait_reserved;
    KernelThreadWaitQueue death_waiters;
    uint32_t exit_status;
    uint32_t terminal_result;
    uint16_t handle_references;
    uint8_t stack_released;
    uint8_t reap_pending;
    /*
     * Pages committed to this thread's stack, always contiguous and always
     * ending at user_stack_top. Growth moves user_stack_base down and this up;
     * the reap path unmaps exactly this many, which is why it is counted here
     * rather than derived from a constant that is no longer fixed.
     */
    uint8_t stack_pages;
} KernelThread;

typedef struct KernelThreadSnapshot {
    uint32_t id;
    uint32_t process_id;
    uint32_t user_stack_base;
    uint32_t user_stack_top;
    uint32_t kernel_stack_guard;
    uint32_t kernel_stack_base;
    uint32_t kernel_stack_top;
    uint32_t kernel_stack_used;
    uint32_t kernel_stack_entries;
    uint32_t timer_ticks;
    uint32_t run_count;
    uint32_t syscall_count;
    uint32_t activity;
    KernelHandle self_handle;
    uint16_t process_slot;
    uint16_t stack_slot;
    uint8_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
    uint8_t occupied;
    uint8_t waiting;
    uint8_t wait_members;
    uint8_t deadline_waiting;
    uint8_t stack_released;
    uint8_t reap_pending;
    uint8_t stack_pages;
    uint8_t reserved[2];
    uint32_t exit_status;
    uint32_t terminal_result;
    uint16_t handle_references;
    uint16_t death_waiters;
} KernelThreadSnapshot;

typedef struct KernelThreadPoolStats {
    uint32_t created_threads;
    uint32_t live_threads;
    uint32_t dead_threads;
    uint32_t ready_bitmap;
    uint32_t ready_threads;
    uint32_t blocked_threads;
    uint32_t kernel_stack_entries;
    uint32_t kernel_stack_max_used;
    uint32_t kernel_stack_measurements;
    uint32_t kernel_stack_scan_words;
    uint32_t deadline_waits;
    uint32_t deadline_expirations;
    uint32_t deadline_cancellations;
    uint32_t wait_cancellations;
    uint32_t deadline_depth;
    uint32_t deadline_max_depth;
    uint32_t thread_exits;
    uint32_t death_waits;
    uint32_t death_wakeups;
    uint32_t handle_close_wakeups;
    uint32_t reaped_threads;
    uint32_t creation_rollbacks;
    uint32_t max_death_waiters;
    uint32_t wait_set_blocks;
    uint32_t wait_set_wakeups;
    uint32_t wait_registrations;
    uint32_t wait_registration_max;
    uint32_t max_wait_members;
    uint32_t irq_wake_to_run_samples;
    uint32_t irq_wake_to_run_max_cycles;
} KernelThreadPoolStats;

void kernel_thread_pool_init(void);
KernelThreadStatus kernel_thread_allocate(uint16_t process_slot,
                                          uint32_t process_id,
                                          uint16_t stack_slot,
                                          uint32_t program_counter,
                                          uint32_t user_stack,
                                          uint32_t initial_argument,
                                          uint8_t priority,
                                          KernelThread **thread);
KernelThreadStatus kernel_thread_publish(KernelThread *thread);
KernelThreadStatus kernel_thread_abort(KernelThread *thread);
KernelThreadStatus kernel_thread_attach_handle(KernelThread *thread,
                                                KernelHandle handle);
void kernel_thread_handle_release(void *object, void *context);
KernelThreadStatus kernel_thread_complete(KernelThread *thread,
                                          uint32_t exit_status,
                                          uint32_t terminal_result,
                                          uint32_t *woken_threads);
KernelThreadStatus kernel_thread_wait_for_death(
    KernelThread *target, KernelThread *waiter, uint64_t now,
    uint64_t deadline, uint32_t timeout_result, bool *blocked,
    uint32_t *wait_result, uint32_t *exit_status);
KernelThreadStatus kernel_thread_prepare_death_wait(
    KernelThread *target, KernelThread *waiter, KernelThreadWaitSpec *spec,
    bool *ready, uint32_t *wait_result, uint32_t *exit_status);
KernelThreadStatus kernel_thread_commit_death_wait(KernelThread *target);
KernelThreadStatus kernel_thread_finish_reap(KernelThread *thread,
                                              bool *released);
bool kernel_thread_reap_pending(void);
uint64_t kernel_thread_reap_slots(void);
KernelThreadStatus kernel_thread_make_ready(KernelThread *thread);
KernelThreadStatus kernel_thread_take_next(KernelThread **thread);
KernelThreadStatus kernel_thread_set_process_priority(uint16_t process_slot,
                                                      uint8_t priority);
void kernel_thread_wait_queue_init(KernelThreadWaitQueue *queue);
uint32_t kernel_thread_wait_queue_sequence(
    const KernelThreadWaitQueue *queue);
uint32_t kernel_thread_wait_queue_count(const KernelThreadWaitQueue *queue);
uint32_t kernel_thread_wait_queue_waiter_count(
    const KernelThreadWaitQueue *queue);
KernelThreadStatus kernel_thread_block(KernelThread *thread,
                                       KernelThreadWaitQueue *queue,
                                       uint32_t expected_sequence);
KernelThreadStatus kernel_thread_block_until(
    KernelThread *thread, KernelThreadWaitQueue *queue,
    uint32_t expected_sequence, uint64_t now, uint64_t deadline,
    uint32_t timeout_result);
KernelThreadStatus kernel_thread_block_wait_set(
    KernelThread *thread, const KernelThreadWaitSpec *specs,
    uint32_t member_count, uint64_t now, uint64_t deadline,
    uint32_t timeout_result);
KernelThreadStatus kernel_thread_wake_one(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          KernelThread **thread);
KernelThreadStatus kernel_thread_wake_all(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          uint32_t *woken_threads);
KernelThreadStatus kernel_thread_wake_all_irq(
    KernelThreadWaitQueue *queue, uint32_t result,
    uint32_t *woken_threads);
KernelThreadStatus kernel_thread_wake_all_detail(
    KernelThreadWaitQueue *queue, uint32_t result, uint32_t detail,
    bool write_one_detail, uint32_t *woken_threads);
KernelThreadStatus kernel_thread_cancel_wait(KernelThread *thread,
                                             uint32_t result);
KernelThreadStatus kernel_thread_expire_deadlines(
    uint64_t now, uint32_t *expired_threads, uint8_t *highest_priority);
bool kernel_thread_next_deadline(uint64_t *deadline);
bool kernel_thread_highest_ready_priority(uint8_t *priority);
KernelThreadStatus kernel_thread_retire_process(uint16_t process_slot,
                                                uint32_t terminal_result,
                                                uint32_t *retired_threads);
KernelThreadStatus kernel_thread_exec_retire_others(
    uint16_t process_slot, KernelThread *survivor, uint32_t terminal_result,
    uint32_t *retired_threads);
KernelThreadStatus kernel_thread_release_process(uint16_t process_slot);
KernelThread *kernel_thread_at(uint16_t slot);
bool kernel_thread_snapshot(uint32_t slot, KernelThreadSnapshot *snapshot);
bool kernel_thread_pool_stats(KernelThreadPoolStats *stats);
bool kernel_thread_pool_valid(void);
bool kernel_thread_process_runnable(uint16_t process_slot);
uint32_t kernel_thread_process_count(uint16_t process_slot, bool live_only);
uint32_t kernel_thread_process_run_count(uint16_t process_slot);
uint32_t kernel_thread_process_timer_ticks(uint16_t process_slot);
uint32_t kernel_thread_process_syscalls(uint16_t process_slot);
KernelThreadState kernel_thread_process_representative_state(
    uint16_t process_slot);
KernelThreadStatus kernel_thread_note_kernel_entry(KernelThread *thread,
                                                   uint32_t stack_pointer);
bool kernel_thread_stacks_valid(void);
bool kernel_thread_measure_stacks(uint32_t *maximum_used);

#endif

#endif
