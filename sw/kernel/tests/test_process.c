#include "block.h"
#include "dma.h"
#include "exception.h"
#include "memory.h"
#include "platform.h"
#include "pmmu.h"
#include "process.h"
#include "qualification.h"
#include "vm.h"

#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t physical_memory[32u * 1024u * 1024u];
static uint32_t milestone_calls;
static uint32_t soak_checkpoint_calls;
static uint32_t last_soak_checkpoint;
static uint32_t last_soak_free_frames;
static uint64_t scheduler_test_cycles;
static uint32_t timer_arm_count;
static uint32_t timer_last_load;

uint32_t kernel_platform_quantum_cycles(void)
{
    return 62500u;
}

void kernel_platform_timer_arm(uint32_t cycles)
{
    assert(cycles != 0u);
    timer_last_load = cycles;
    ++timer_arm_count;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    assert(cycles != NULL);
    cycles->high = (uint32_t)(scheduler_test_cycles >> 32);
    cycles->low = (uint32_t)scheduler_test_cycles;
}

static void advance_quantum(void)
{
    scheduler_test_cycles += kernel_platform_quantum_cycles();
}

void kernel_pmmu_load_tc(const uint32_t *value)
{
    (void)value;
}

void kernel_pmmu_load_srp(const KernelPmmuRootPointer *root)
{
    (void)root;
}

void kernel_pmmu_load_crp(const KernelPmmuRootPointer *root)
{
    (void)root;
}

void kernel_pmmu_load_tt0(const uint32_t *value)
{
    (void)value;
}

void kernel_pmmu_load_tt1(const uint32_t *value)
{
    (void)value;
}

void kernel_pmmu_flush_all(void)
{
}

void kernel_pmmu_set_user_function_codes(void)
{
}

void kernel_cache_invalidate_all(void)
{
}

bool kernel_platform_block_present(void)
{
    return false;
}

bool kernel_platform_block_state(KernelPlatformBlockState *state)
{
    (void)state;
    return false;
}

uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer)
{
    (void)id;
    (void)operation;
    (void)flags;
    (void)lba;
    (void)sectors;
    (void)physical_buffer;
    return 1u;
}

bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion)
{
    (void)completion;
    return false;
}

void kernel_platform_block_ack_state(void)
{
}

void kernel_process_milestone_reached(void)
{
    ++milestone_calls;
}

void kernel_process_soak_checkpoint(uint32_t cycles,
                                    uint32_t baseline_free_frames)
{
    ++soak_checkpoint_calls;
    last_soak_checkpoint = cycles;
    last_soak_free_frames = baseline_free_frames;
}

static void put_be16(uint8_t *bytes, uint32_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1u] = (uint8_t)value;
}

static void put_be32(uint8_t *bytes, uint32_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1u] = (uint8_t)(value >> 16);
    bytes[offset + 2u] = (uint8_t)(value >> 8);
    bytes[offset + 3u] = (uint8_t)value;
}

static void make_frame(uint8_t *frame, uint8_t format, uint16_t vector,
                       uint32_t pc, uint32_t fault_address)
{
    memset(frame, 0, KERNEL_EXCEPTION_FRAME_MAX_SIZE);
    put_be16(frame, 0u, 0u);
    put_be32(frame, 2u, pc);
    put_be16(frame, 6u,
             (uint16_t)((uint16_t)format << 12 | (vector << 2)));
    if (format == 0xau || format == 0xbu)
        put_be32(frame, 16u, fault_address);
}

static void add_range(AstraBootInfo *info, uint32_t base, uint32_t size,
                      uint32_t type, uint32_t flags)
{
    AstraBootMemoryRange *range =
        &info->memory_ranges[info->memory_range_count++];

    range->base = base;
    range->size = size;
    range->type = type;
    range->flags = flags;
}

static void initialize_test(void)
{
    AstraBootInfo info;

    memset(&info, 0, sizeof(info));
    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.abi_major = ASTRA_BOOT_ABI_MAJOR;
    info.abi_minor = ASTRA_BOOT_ABI_MINOR;
    info.total_size = sizeof(info);
    info.flags = ASTRA_BOOT_REQUIRED_FLAGS;
    info.machine_id = 0x41363801u;
    info.hardware_build_id = 0x12345678u;
    info.cpu_model = 0x00068030u;
    info.cpu_implementation = 0x54474d32u;
    info.cpu_features = 0x0000000du;
    info.cpu_hz = 12500000u;
    info.ram_base = 0x02000000u;
    info.ram_size = 0x02000000u;
    info.rom_base = 0xffe00000u;
    info.rom_size = ASTRA_ROM_BACKING_SIZE;
    info.kernel_base = ASTRA_KERNEL_LOAD_ADDRESS;
    info.kernel_image_size = 0x00010000u;
    info.kernel_memory_size = ASTRA_KERNEL_RESERVED_SIZE;
    info.kernel_entry = ASTRA_KERNEL_LOAD_ADDRESS;
    info.early_log_base = ASTRA_EARLY_LOG_ADDRESS;
    info.early_log_size = ASTRA_EARLY_LOG_SIZE;
    info.memory_range_entry_size = sizeof(AstraBootMemoryRange);
    add_range(&info, ASTRA_BOOT_SCRATCH_ADDRESS, ASTRA_BOOT_SCRATCH_SIZE,
              ASTRA_MEMORY_RANGE_FIRMWARE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(&info, ASTRA_EARLY_LOG_ADDRESS, ASTRA_EARLY_LOG_SIZE,
              ASTRA_MEMORY_RANGE_EARLY_LOG,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    add_range(&info, 0x02004000u, 0x0000c000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x02090000u, 0x01d70000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_ROM_BACKING_ADDRESS, ASTRA_ROM_BACKING_SIZE,
              ASTRA_MEMORY_RANGE_ROM_BACKING,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_EXECUTE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x03e40000u, 0x001c0000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    astra_boot_info_finalize(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    memset(physical_memory, 0xa5, sizeof(physical_memory));
    kernel_vm_test_bind_physical_memory(physical_memory, 0x02000000u,
                                        sizeof(physical_memory));
    kernel_process_test_bind_physical_memory(physical_memory, 0x02000000u,
                                             sizeof(physical_memory));
    assert(kernel_vm_init() == KERNEL_VM_OK);
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    kernel_dma_init();
    kernel_block_init();
    scheduler_test_cycles = 0u;
    timer_arm_count = 0u;
    timer_last_load = 0u;
    kernel_process_init();
    milestone_calls = 0u;
    soak_checkpoint_calls = 0u;
    last_soak_checkpoint = 0u;
    last_soak_free_frames = 0u;
}

static void test_preemption_fault_containment_and_teardown(void)
{
    static const uint8_t image[] = {0x70u, 0x01u, 0x4eu, 0x4fu,
                                    0x60u, 0xfau, 0x4eu, 0x71u};
    KernelMemoryStats baseline;
    KernelMemoryStats before_fault;
    KernelMemoryStats after_fault;
    KernelMemoryStats final;
    KernelProcessSnapshot survivor;
    KernelProcessSnapshot offender;
    KernelThreadSnapshot survivor_thread;
    KernelThreadSnapshot sibling_thread;
    KernelSchedulerStats stats;
    KernelVmStats before_same_space_switch;
    KernelVmStats after_same_space_switch;
    KernelVmStats after_cross_space_switch;
    KernelCpuContext *next;
    KernelDmaHandle dma_handle;
    KernelDmaToken dma_token;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t survivor_id;
    uint32_t process_id;
    uint32_t sibling_id;
    uint32_t leaked_physical;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &survivor_id) == KERNEL_PROCESS_OK);
    assert(survivor_id != 0u);
    assert(kernel_process_create_thread(
               survivor_id, 0u, 2u, KERNEL_THREAD_PRIORITY_NORMAL + 1u,
               &sibling_id) == KERNEL_PROCESS_OK);
    assert(sibling_id != 0u);
    assert(kernel_process_create(image, sizeof(image), 0u, 1u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(1u, &offender));
    assert(kernel_process_snapshot(0u, &survivor));
    assert(survivor.thread_count == 2u);
    assert(survivor.live_threads == 2u);
    assert(kernel_thread_snapshot(0u, &survivor_thread));
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(survivor_thread.process_id == survivor_id);
    assert(sibling_thread.process_id == survivor_id);
    assert(survivor_thread.user_stack_base == KERNEL_THREAD_STACK_BASE);
    assert(sibling_thread.user_stack_base ==
           KERNEL_THREAD_STACK_BASE + KERNEL_THREAD_STACK_STRIDE);
    assert(survivor_thread.self_handle != sibling_thread.self_handle);

    assert(kernel_dma_create(offender.owner, KERNEL_PAGE_SIZE, 1u,
                             &dma_handle) == KERNEL_DMA_OK);
    assert(kernel_dma_begin(dma_handle, offender.owner, 0u,
                            KERNEL_PAGE_SIZE, KERNEL_DMA_FROM_DEVICE, 7u,
                            &dma_token) == KERNEL_DMA_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_PROCESS, offender.owner,
                               &leaked_physical) == KERNEL_MEMORY_OK);
    (void)leaked_physical;

    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_process_current_context() == next);
    assert(!kernel_process_maintenance_pending());
    assert(kernel_process_snapshot(0u, &survivor));
    assert(survivor.process_state == KERNEL_PROCESS_RUNNING);
    assert(survivor.thread_state == KERNEL_THREAD_RUNNING);
    assert(kernel_vm_stats(&before_same_space_switch));

    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 16u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_process_current_context() == next);
    assert(!kernel_process_maintenance_pending());
    assert(kernel_vm_stats(&after_same_space_switch));
    assert(after_same_space_switch.switches ==
           before_same_space_switch.switches);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);

    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    advance_quantum();
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_vm_stats(&after_cross_space_switch));
    assert(after_cross_space_switch.switches ==
           after_same_space_switch.switches + 1u);
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_RUNNING);
    assert(offender.thread_state == KERNEL_THREAD_RUNNING);

    advance_quantum();
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_SIGNAL;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_RUNNING);
    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT_TIMEOUT;
    registers[1] = 1000u;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);
    assert(timer_last_load == 1000u);

    scheduler_test_cycles += 1000u;
    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_RUNNING);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);

    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);

    advance_quantum();
    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_RUNNING);

    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 6u,
               0x60000000u);
    registers[2] = 1u;
    assert(kernel_memory_stats(&before_fault));
    assert(kernel_process_on_fault(registers,
                                   KERNEL_PROCESS_STACK_TOP - 32u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&after_fault));
    assert(after_fault.free_frames == before_fault.free_frames);
    assert(after_fault.owner_release_operations ==
           before_fault.owner_release_operations);
    assert(next != NULL);
    assert(kernel_process_current_context() == next);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_EXITING);
    assert(offender.thread_state == KERNEL_THREAD_DEAD);
    assert(offender.exit_reason == KERNEL_PROCESS_EXIT_USER_FAULT);
    assert(offender.fault_vector == 2u);
    assert(offender.fault_address == 0x60000000u);
    assert(kernel_process_stats(&stats));
    assert(stats.completed_teardowns == 0u);
    assert(stats.completed_user_fault_teardowns == 0u);

    assert(kernel_process_maintenance() == KERNEL_PROCESS_DEFERRED);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_EXITING);
    assert(kernel_dma_complete(&dma_token) == KERNEL_DMA_OK);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(!kernel_process_maintenance_pending());
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_DEAD);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_PROCESS_PROGRESS_GOAL + 1u;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(milestone_calls == 1u);
    assert(kernel_process_stats(&stats));
    assert(stats.live_processes == 1u);
    assert(stats.dead_processes == 1u);
    assert(stats.created_threads == 3u);
    assert(stats.live_threads == 2u);
    assert(stats.dead_threads == 1u);
    assert(stats.timer_preemptions == 3u);
    assert(stats.quantum_cycles == 62500u);
    assert(stats.quantum_expirations == 3u);
    assert(stats.deadline_expirations == 1u);
    assert(stats.deadline_preemptions == 1u);
    assert(stats.deadline_depth == 0u);
    assert(stats.deadline_max_depth == 1u);
    assert(stats.same_address_space_switches >= 1u);
    assert(stats.cross_address_space_switches >= 2u);
    assert(stats.total_syscalls_low == 5u);
    assert(stats.total_syscalls_high == 0u);
    assert(stats.user_faults == 1u);
    assert(stats.completed_user_fault_teardowns == 1u);
    assert(stats.completed_teardowns == 1u);
    assert(stats.forced_frame_releases == 1u);
    assert(stats.wait_blocks == 3u);
    assert(stats.event_wakeups == 1u);
    assert(stats.wake_preemptions == 1u);
    assert(stats.blocked_threads == 1u);
    assert(stats.kernel_stack_entries != 0u);
    assert(stats.kernel_stack_max_used != 0u);
    assert(stats.milestone_complete == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(!kernel_process_active());
    assert(kernel_process_current_context() == NULL);
    assert(kernel_process_maintenance_pending());
    assert(kernel_memory_stats(&final));
    assert(final.free_frames < baseline.free_frames);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(!kernel_process_maintenance_pending());
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
    assert(kernel_process_stats(&stats));
    assert(stats.live_processes == 0u);
    assert(stats.dead_processes == 2u);
    assert(stats.live_threads == 0u);
    assert(stats.dead_threads == 3u);
    assert(stats.completed_teardowns == 2u);
    assert(milestone_calls == 1u);
}

static void test_soak_relaunches_only_after_exact_teardown(void)
{
    static const uint8_t image[] = {0x70u, 0x01u, 0x4eu, 0x4fu,
                                    0x60u, 0xfau, 0x4eu, 0x71u};
    KernelMemoryStats survivor_baseline;
    KernelMemoryStats before_fault;
    KernelMemoryStats between_cycles;
    KernelSchedulerStats stats;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t sibling_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(
               process_id, 0u, 2u, KERNEL_THREAD_PRIORITY_NORMAL + 1u,
               &sibling_id) == KERNEL_PROCESS_OK);
    assert(sibling_id != 0u);
    assert(kernel_memory_stats(&survivor_baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 1u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_soak_configure(
               image, sizeof(image), 0u, survivor_baseline.free_frames, 3u) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_SIGNAL;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT_TIMEOUT;
    registers[1] = 1000u;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    scheduler_test_cycles += 1000u;
    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);
    memset(registers, 0, sizeof(registers));
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);

    for (uint32_t cycle = 1u; cycle <= 3u; ++cycle) {
        make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
        advance_quantum();
        assert(kernel_process_on_timer(registers,
                                       KERNEL_PROCESS_STACK_TOP - 16u,
                                       frame, &next) == KERNEL_PROCESS_OK);

        make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 6u,
                   0x60000000u);
        assert(kernel_memory_stats(&before_fault));
        assert(kernel_process_on_fault(registers,
                                       KERNEL_PROCESS_STACK_TOP - 32u, frame,
                                       &next) == KERNEL_PROCESS_OK);
        assert(kernel_process_maintenance_pending());
        assert(kernel_memory_stats(&between_cycles));
        assert(between_cycles.free_frames == before_fault.free_frames);
        assert(between_cycles.owner_release_operations ==
               before_fault.owner_release_operations);
        assert(between_cycles.free_frames < survivor_baseline.free_frames);

        if (cycle == 1u) {
            assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
            assert(soak_checkpoint_calls == 0u);
            memset(registers, 0, sizeof(registers));
            registers[0] = ASTRA_SYSCALL_PROGRESS;
            registers[1] = KERNEL_PROCESS_PROGRESS_GOAL;
            make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
                       KERNEL_PROCESS_CODE_BASE + 4u, 0u);
            assert(kernel_process_on_syscall(
                       registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                       &next) == KERNEL_PROCESS_OK);
            assert(kernel_process_stats(&stats));
            assert(stats.milestone_complete == 1u);
            assert(milestone_calls == 1u);
        } else {
            assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
        }
        assert(!kernel_process_maintenance_pending());
        assert(kernel_process_stats(&stats));
        assert(stats.live_processes == 2u);
        assert(stats.live_threads == 3u);
        assert(stats.user_faults == cycle);
        assert(stats.completed_user_fault_teardowns == cycle);
        assert(stats.completed_teardowns == cycle);
        assert(stats.soak_cycles == cycle);
        if (cycle == 1u)
            assert(soak_checkpoint_calls == 0u);
        else
            assert(soak_checkpoint_calls == cycle - 1u);
    }

    assert(soak_checkpoint_calls == 2u);
    assert(last_soak_checkpoint == 3u);
    assert(last_soak_free_frames == survivor_baseline.free_frames);
}

static void test_invalid_creation_does_not_allocate(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t process_id = 0xdeadbeefu;

    initialize_test();
    assert(kernel_memory_stats(&before));
    assert(kernel_process_create(NULL, sizeof(image), 0u, 0u,
                                 &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create(image, sizeof(image), sizeof(image), 0u,
                                 &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create(image, KERNEL_PAGE_SIZE + 1u, 0u, 0u,
                                 &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
}

static void test_priority_selection_and_equal_priority_rotation(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u, 0x60u, 0xfcu};
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    KernelThreadSnapshot high_first;
    KernelThreadSnapshot high_second;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t thread_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(process_id, 0u, 1u, 20u,
                                        &thread_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(process_id, 0u, 2u, 20u,
                                        &thread_id) == KERNEL_PROCESS_OK);

    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    advance_quantum();
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(1u, &high_first));
    assert(high_first.state == KERNEL_THREAD_RUNNING);
    assert(high_first.effective_priority == 20u);
    assert(kernel_process_stats(&stats));
    assert(stats.priority_preemptions == 1u);

    advance_quantum();
    assert(kernel_process_on_timer(registers,
                                   high_first.user_stack_top - 16u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(2u, &high_second));
    assert(high_second.state == KERNEL_THREAD_RUNNING);
    assert(high_second.effective_priority == 20u);
    assert(kernel_process_stats(&stats));
    assert(stats.priority_preemptions == 1u);
    assert(stats.same_address_space_switches == 2u);
}

static void test_per_process_thread_limit_is_bounded_and_reclaimable(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelMemoryStats baseline;
    KernelMemoryStats at_limit;
    KernelMemoryStats after_failure;
    KernelMemoryStats final;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t thread_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(
               process_id, 0u, 0u, KERNEL_THREAD_PRIORITY_IDLE,
               &thread_id) == KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create_thread(
               process_id, 0u, 0u, KERNEL_THREAD_PRIORITY_USER_MAX + 1u,
               &thread_id) == KERNEL_PROCESS_INVALID_ARGUMENT);
    for (uint32_t count = 1u; count < KERNEL_PROCESS_THREAD_MAX; ++count) {
        assert(kernel_process_create_thread(
                   process_id, 0u, count,
                   KERNEL_THREAD_PRIORITY_NORMAL, &thread_id) ==
               KERNEL_PROCESS_OK);
    }
    assert(kernel_memory_stats(&at_limit));
    assert(kernel_process_create_thread(
               process_id, 0u, 0u, KERNEL_THREAD_PRIORITY_NORMAL,
               &thread_id) == KERNEL_PROCESS_RESOURCE_LIMIT);
    assert(kernel_memory_stats(&after_failure));
    assert(after_failure.free_frames == at_limit.free_frames);

    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_last_runnable_timed_wait_wakes_from_supervisor_idle(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    registers[0] = KERNEL_QUALIFICATION_EVENT_WAIT_TIMEOUT;
    registers[1] = 2500u;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(!kernel_process_active());
    assert(timer_last_load == 2500u);

    scheduler_test_cycles += 2500u;
    assert(kernel_process_on_supervisor_timer() == KERNEL_PROCESS_OK);
    assert(kernel_process_active());
    next = kernel_process_current_context();
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);
    assert(kernel_process_stats(&stats));
    assert(stats.deadline_expirations == 1u);
    assert(stats.deadline_preemptions == 0u);
    assert(stats.deadline_depth == 0u);
}

static void test_normal_syscalls_do_not_renew_quantum(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t arms_before_syscall;
    uint32_t process_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(timer_last_load == 62500u);
    arms_before_syscall = timer_arm_count;

    scheduler_test_cycles = 1000u;
    registers[0] = ASTRA_SYSCALL_QUERY_ABI;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(timer_arm_count == arms_before_syscall);

    scheduler_test_cycles = 62500u;
    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(timer_last_load == 62500u);
    assert(kernel_process_stats(&stats));
    assert(stats.quantum_expirations == 1u);
    assert(stats.timer_preemptions == 0u);
}

int main(void)
{
    test_preemption_fault_containment_and_teardown();
    test_soak_relaunches_only_after_exact_teardown();
    test_invalid_creation_does_not_allocate();
    test_priority_selection_and_equal_priority_rotation();
    test_per_process_thread_limit_is_bounded_and_reclaimable();
    test_last_runnable_timed_wait_wakes_from_supervisor_idle();
    test_normal_syscalls_do_not_renew_quantum();
    puts("process tests passed");
    return 0;
}
