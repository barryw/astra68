#ifndef ASTRA_KERNEL_THREAD_H
#define ASTRA_KERNEL_THREAD_H

#define KERNEL_THREAD_KERNEL_STACK_TOP_OFFSET 76

#define KERNEL_THREAD_MAX 16
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

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_THREAD_SUPERVISOR_HOST_ARENA_BASE 0x02050000u

#define KERNEL_THREAD_SLOT_NONE UINT16_MAX

#define KERNEL_THREAD_STACK_BASE 0x70000000u
#define KERNEL_THREAD_STACK_STRIDE 0x00002000u
#define KERNEL_THREAD_STACK_SIZE 0x00001000u

#define KERNEL_THREAD_RIGHT_QUERY     (1u << 0)
#define KERNEL_THREAD_RIGHT_TERMINATE (1u << 1)

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
    KERNEL_THREAD_CORRUPT
} KernelThreadStatus;

typedef struct KernelThreadWaitQueue {
    uint32_t sequence;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint16_t reserved;
} KernelThreadWaitQueue;

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
    KernelHandle self_handle;
    KernelThreadWaitQueue *wait_queue;
    uint32_t wait_sequence;
    uint16_t slot;
    uint16_t process_slot;
    uint16_t stack_slot;
    uint16_t ready_previous;
    uint16_t ready_next;
    uint16_t wait_previous;
    uint16_t wait_next;
    uint8_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
    uint8_t occupied;
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
    KernelHandle self_handle;
    uint16_t process_slot;
    uint16_t stack_slot;
    uint8_t state;
    uint8_t base_priority;
    uint8_t effective_priority;
    uint8_t occupied;
    uint8_t waiting;
    uint8_t reserved[3];
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
KernelThreadStatus kernel_thread_make_ready(KernelThread *thread);
KernelThreadStatus kernel_thread_take_next(KernelThread **thread);
void kernel_thread_wait_queue_init(KernelThreadWaitQueue *queue);
uint32_t kernel_thread_wait_queue_sequence(
    const KernelThreadWaitQueue *queue);
uint32_t kernel_thread_wait_queue_count(const KernelThreadWaitQueue *queue);
KernelThreadStatus kernel_thread_block(KernelThread *thread,
                                       KernelThreadWaitQueue *queue,
                                       uint32_t expected_sequence);
KernelThreadStatus kernel_thread_wake_one(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          KernelThread **thread);
KernelThreadStatus kernel_thread_wake_all(KernelThreadWaitQueue *queue,
                                          uint32_t result,
                                          uint32_t *woken_threads);
KernelThreadStatus kernel_thread_retire_process(uint16_t process_slot,
                                                uint32_t *retired_threads);
KernelThreadStatus kernel_thread_release_process(uint16_t process_slot);
KernelThread *kernel_thread_at(uint16_t slot);
bool kernel_thread_snapshot(uint32_t slot, KernelThreadSnapshot *snapshot);
bool kernel_thread_pool_stats(KernelThreadPoolStats *stats);
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
