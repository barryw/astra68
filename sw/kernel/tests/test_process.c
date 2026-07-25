#include "block.h"
#include "dma.h"
#include "exception.h"
#include "memory.h"
#include "platform.h"
#include "pmmu.h"
#include "process.h"
#include "qualification.h"
#include "sync.h"
#include "user_copy.h"
#include "vm.h"

#include <astra/syscall.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SYNC_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | \
     ASTRA_RIGHT_ADMINISTER)
#define TEST_TIMER_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_ADMINISTER)
#define TEST_PROCESS_WAIT_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT)

static uint8_t physical_memory[32u * 1024u * 1024u];
static uint32_t milestone_calls;
static uint32_t soak_checkpoint_calls;
static uint32_t last_soak_checkpoint;
static uint32_t last_soak_free_frames;
static uint64_t scheduler_test_cycles;
static uint32_t timer_arm_count;
static uint32_t timer_last_load;
static uint32_t interrupt_enable_count;
static uint32_t interrupt_disable_count;
static uint32_t disable_hook_process_slot;
static uint32_t disable_hook_thread_count;
static bool interrupts_enabled;
static bool timer_on_disable_armed;
static bool timer_on_disable_fired;

static int copy_user_bytes(void *buffer, uint32_t user_address,
                           uint32_t size, bool to_user)
{
    uint8_t *bytes = buffer;

    while (size != 0u) {
        uint32_t physical;
        uint32_t page_remaining =
            KERNEL_PAGE_SIZE - (user_address & (KERNEL_PAGE_SIZE - 1u));
        uint32_t chunk = size < page_remaining ? size : page_remaining;

        if (!kernel_vm_test_translate_current(user_address, to_user,
                                              &physical) ||
            physical < 0x02000000u ||
            physical - 0x02000000u > sizeof(physical_memory) - chunk)
            return KERNEL_USER_COPY_BAD_ADDRESS;
        if (to_user)
            memcpy(&physical_memory[physical - 0x02000000u], bytes, chunk);
        else
            memcpy(bytes, &physical_memory[physical - 0x02000000u], chunk);
        bytes += chunk;
        user_address += chunk;
        size -= chunk;
    }
    return KERNEL_USER_COPY_OK;
}

int kernel_user_copy_from_asm(void *destination, uint32_t source,
                              uint32_t size)
{
    return copy_user_bytes(destination, source, size, false);
}

int kernel_user_copy_to_asm(uint32_t destination, const void *source,
                            uint32_t size)
{
    return copy_user_bytes((void *)source, destination, size, true);
}

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

void kernel_enable_interrupts(void)
{
    interrupts_enabled = true;
    ++interrupt_enable_count;
}

void kernel_disable_interrupts(void)
{
    if (timer_on_disable_armed) {
        KernelProcessSnapshot process;

        assert(interrupts_enabled);
        timer_on_disable_armed = false;
        assert(kernel_process_snapshot(disable_hook_process_slot, &process));
        assert(process.thread_count == disable_hook_thread_count);
        assert(kernel_process_on_supervisor_timer() == KERNEL_PROCESS_OK);
        assert(kernel_thread_pool_valid());
        timer_on_disable_fired = true;
    }
    interrupts_enabled = false;
    ++interrupt_disable_count;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    assert(cycles != NULL);
    cycles->high = (uint32_t)(scheduler_test_cycles >> 32);
    cycles->low = (uint32_t)scheduler_test_cycles;
}

uint64_t kernel_platform_monotonic_ns(void)
{
    return scheduler_test_cycles * KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
}

bool kernel_platform_deadline_to_cycles(int64_t deadline_ns,
                                        uint64_t *deadline_cycles)
{
    uint64_t nanoseconds;

    if (deadline_cycles == NULL || deadline_ns < 0)
        return false;
    if (deadline_ns == INT64_MAX) {
        *deadline_cycles = UINT64_MAX;
        return true;
    }
    nanoseconds = (uint64_t)deadline_ns;
    *deadline_cycles = nanoseconds / KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    if (nanoseconds % KERNEL_PLATFORM_NS_PER_CPU_CYCLE != 0u)
        ++*deadline_cycles;
    return true;
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

static void select_process(uint32_t process_id, uint32_t *registers,
                           uint8_t *frame, KernelCpuContext **next)
{
    KernelSchedulerStats stats;

    for (uint32_t attempt = 0u; attempt < KERNEL_THREAD_MAX; ++attempt) {
        KernelThreadSnapshot current;
        bool found = false;

        assert(kernel_process_stats(&stats));
        if (stats.current_process_id == process_id)
            return;
        for (uint32_t slot = 0u; slot < KERNEL_THREAD_MAX; ++slot) {
            assert(kernel_thread_snapshot(slot, &current));
            if (current.id == stats.current_thread_id &&
                current.state == KERNEL_THREAD_RUNNING) {
                found = true;
                break;
            }
        }
        assert(found);
        memset(registers, 0, KERNEL_CONTEXT_REGISTER_COUNT *
                   sizeof(registers[0]));
        registers[0] = ASTRA_SYSCALL_YIELD;
        make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
                   KERNEL_PROCESS_CODE_BASE + 4u, 0u);
        assert(kernel_process_on_syscall(registers,
                                         current.user_stack_top - 8u, frame,
                                         next) == KERNEL_PROCESS_OK);
    }
    assert(!"target process did not become runnable");
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
    interrupt_enable_count = 0u;
    interrupt_disable_count = 0u;
    disable_hook_process_slot = 0u;
    disable_hook_thread_count = 0u;
    interrupts_enabled = false;
    timer_on_disable_armed = false;
    timer_on_disable_fired = false;
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
    uint32_t event_handle;
    uint32_t semaphore_handle;
    uint32_t close_event_handle;
    uint32_t final_event_handle;
    uint64_t deadline_ns;
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
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[1] = 0u;
    registers[2] = TEST_SYNC_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 16u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    event_handle = next->data[1];
    assert(event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 16u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_current_context() == next);
    assert(!kernel_process_maintenance_pending());
    assert(kernel_vm_stats(&after_same_space_switch));
    assert(after_same_space_switch.switches ==
           before_same_space_switch.switches);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = event_handle;
    registers[2] = 1u;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_RUNNING);

    memset(registers, 0, sizeof(registers));
    deadline_ns = (scheduler_test_cycles + 1000u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);
    assert(timer_last_load == 1000u);
    assert(kernel_vm_stats(&after_cross_space_switch));
    assert(after_cross_space_switch.switches ==
           after_same_space_switch.switches + 1u);
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_RUNNING);
    assert(offender.thread_state == KERNEL_THREAD_RUNNING);

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
    registers[0] = ASTRA_SYSCALL_SEMAPHORE_CREATE;
    registers[1] = 0u;
    registers[2] = 2u;
    registers[3] = TEST_SYNC_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    semaphore_handle = next->data[1];
    assert(semaphore_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = semaphore_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_BLOCKED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = semaphore_handle;
    registers[2] = 1u;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_RUNNING);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(1u, &offender));
    assert(offender.process_state == KERNEL_PROCESS_RUNNING);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[1] = 0u;
    registers[2] = TEST_SYNC_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    advance_quantum();
    make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_timer(registers,
                                   KERNEL_PROCESS_STACK_TOP - 16u,
                                   frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CANCEL_WAIT;
    registers[1] = sibling_thread.self_handle;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CANCELLED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[1] = 0u;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    close_event_handle = next->data[1];
    assert(close_event_handle != KERNEL_HANDLE_INVALID);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = close_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);

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

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = close_event_handle;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CLOSED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = event_handle;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    registers[1] = semaphore_handle;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[1] = 0u;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);
    final_event_handle = next->data[1];
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = final_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 8u, frame,
               &next) == KERNEL_PROCESS_OK);

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
    assert(stats.timer_preemptions == 1u);
    assert(stats.quantum_cycles == 62500u);
    assert(stats.quantum_expirations == 1u);
    assert(stats.deadline_expirations == 1u);
    assert(stats.deadline_preemptions == 1u);
    assert(stats.deadline_depth == 0u);
    assert(stats.deadline_max_depth == 1u);
    assert(stats.same_address_space_switches >= 1u);
    assert(stats.cross_address_space_switches >= 2u);
    assert(stats.total_syscalls_low == 18u);
    assert(stats.total_syscalls_high == 0u);
    assert(stats.user_faults == 1u);
    assert(stats.completed_user_fault_teardowns == 1u);
    assert(stats.completed_teardowns == 1u);
    assert(stats.forced_frame_releases == 1u);
    assert(stats.wait_blocks == 6u);
    assert(stats.sync_wakeups == 2u);
    assert(stats.wake_preemptions == 3u);
    assert(stats.sync_created_events == 4u);
    assert(stats.sync_created_semaphores == 1u);
    assert(stats.sync_live_objects == 1u);
    assert(stats.sync_max_live_objects >= 4u);
    assert(stats.sync_wait_calls == 6u);
    assert(stats.sync_signal_calls == 2u);
    assert(stats.sync_cancellations == 1u);
    assert(stats.sync_close_wakeups == 1u);
    assert(stats.sync_owner_deaths == 1u);
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
    KernelProcessSnapshot survivor;
    KernelThreadSnapshot sibling_thread;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t survivor_id;
    uint32_t process_id;
    uint32_t sibling_id;
    uint32_t event_handle;
    uint32_t semaphore_handle;
    uint32_t close_event_handle;
    uint32_t final_event_handle;
    uint32_t transient_survivor_page = 0u;
    uint64_t deadline_ns;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &survivor_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(
               survivor_id, 0u, 2u, KERNEL_THREAD_PRIORITY_NORMAL + 1u,
               &sibling_id) == KERNEL_PROCESS_OK);
    assert(sibling_id != 0u);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(kernel_process_snapshot(0u, &survivor));
    assert(survivor.id == survivor_id);
    assert(kernel_memory_stats(&survivor_baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 1u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_soak_configure(
               image, sizeof(image), 0u, survivor_baseline.free_frames, 3u) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 2u, 0u);
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    event_handle = next->data[1];
    assert(event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    select_process(survivor_id, registers, frame, &next);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    deadline_ns = (scheduler_test_cycles + 1000u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
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
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE + 4u, 0u);
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    select_process(survivor_id, registers, frame, &next);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CANCEL_WAIT;
    registers[1] = sibling_thread.self_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CANCELLED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SEMAPHORE_CREATE;
    registers[2] = 1u;
    registers[3] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    semaphore_handle = next->data[1];
    assert(semaphore_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = semaphore_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    select_process(survivor_id, registers, frame, &next);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = semaphore_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(sibling_thread.state == KERNEL_THREAD_RUNNING);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    close_event_handle = next->data[1];
    assert(close_event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = close_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    select_process(survivor_id, registers, frame, &next);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = close_event_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(1u, &sibling_thread));
    assert(next->data[0] == ASTRA_SYSCALL_CLOSED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    final_event_handle = next->data[1];
    assert(final_event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = final_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    select_process(survivor_id, registers, frame, &next);

    for (uint32_t cycle = 1u; cycle <= 3u; ++cycle) {
        make_frame(frame, 0u, 80u, KERNEL_PROCESS_CODE_BASE + 2u, 0u);
        advance_quantum();
        assert(kernel_process_on_timer(registers,
                                       KERNEL_PROCESS_STACK_TOP - 16u,
                                       frame, &next) == KERNEL_PROCESS_OK);

        if (cycle == 1u) {
            assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_SHARED,
                                       survivor.owner,
                                       &transient_survivor_page) ==
                   KERNEL_MEMORY_OK);
            memset(registers, 0, sizeof(registers));
            registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
            registers[2] = TEST_SYNC_RIGHTS;
            make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
                       KERNEL_PROCESS_CODE_BASE + 2u, 0u);
            assert(kernel_process_on_syscall(
                       registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                       &next) == KERNEL_PROCESS_OK);
            assert(next->data[0] == ASTRA_SYSCALL_OK);
        }

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
            assert(!kernel_process_maintenance_pending());
            assert(kernel_memory_release(transient_survivor_page, 1u,
                                         survivor.owner) ==
                   KERNEL_MEMORY_OK);
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
            assert(kernel_process_maintenance_pending());
            assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
            assert(soak_checkpoint_calls == 1u);
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
        assert(soak_checkpoint_calls == (cycle == 3u ? 2u : 1u));
    }

    assert(soak_checkpoint_calls == 2u);
    assert(last_soak_checkpoint == 3u);
    assert(last_soak_free_frames == survivor_baseline.free_frames);
}

static void test_soak_rejects_unexplained_frame_loss(void)
{
    static const uint8_t image[] = {0x70u, 0x01u, 0x4eu, 0x4fu,
                                    0x60u, 0xfau, 0x4eu, 0x71u};
    KernelMemoryStats survivor_baseline;
    KernelProcessMaintenanceDiagnostics diagnostics;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t survivor_id;
    uint32_t offender_id;
    uint32_t sibling_id;
    uint32_t unexplained_page;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &survivor_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(
               survivor_id, 0u, 2u, KERNEL_THREAD_PRIORITY_NORMAL,
               &sibling_id) == KERNEL_PROCESS_OK);
    assert(sibling_id != 0u);
    assert(kernel_memory_stats(&survivor_baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 1u,
                                 &offender_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_soak_configure(
               image, sizeof(image), 0u, survivor_baseline.free_frames, 3u) ==
           KERNEL_PROCESS_OK);
    assert(kernel_memory_alloc(1u, 1u, KERNEL_FRAME_SHARED, 0x70000001u,
                               &unexplained_page) == KERNEL_MEMORY_OK);
    (void)unexplained_page;
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    select_process(offender_id, registers, frame, &next);

    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 6u,
               0x60000000u);
    assert(kernel_process_on_fault(registers,
                                   KERNEL_PROCESS_STACK_TOP - 32u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_CORRUPT);
    assert(kernel_process_maintenance_diagnostics(&diagnostics));
    assert(diagnostics.failure ==
           KERNEL_PROCESS_MAINTENANCE_FREE_FRAMES);
    assert(diagnostics.observed == survivor_baseline.free_frames - 1u);
    assert(diagnostics.expected == survivor_baseline.free_frames);
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

static void test_sync_syscall_rights_and_stale_handles(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t wait_handle;
    uint32_t signal_handle;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = ASTRA_RIGHT_WAIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    wait_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = wait_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_RESET;
    registers[1] = wait_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = wait_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = wait_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = wait_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = ASTRA_RIGHT_SIGNAL;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    signal_handle = next->data[1];
    assert(signal_handle != wait_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = signal_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = signal_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = signal_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = 1u << 31;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(kernel_process_stats(&stats));
    assert(stats.sync_created_events == 2u);
    assert(stats.sync_live_objects == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
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
    uint32_t event_handle;
    uint64_t deadline_ns;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    event_handle = next->data[1];
    assert(event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    deadline_ns = (scheduler_test_cycles + 2500u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
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

static void test_worker_time_does_not_consume_user_quantum(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *first;
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    uint32_t first_process_id;
    uint32_t second_process_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &first_process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &second_process_id) == KERNEL_PROCESS_OK);
    assert(first_process_id != second_process_id);
    assert(kernel_process_start(&first) == KERNEL_PROCESS_OK);
    assert(first != NULL);
    assert(kernel_process_worker_enter());
    assert(!kernel_process_worker_enter());

    advance_quantum();
    assert(kernel_process_on_supervisor_timer() == KERNEL_PROCESS_OK);
    assert(kernel_process_stats(&stats));
    assert(stats.current_process_id == first_process_id);
    assert(stats.quantum_expirations == 0u);
    assert(stats.timer_preemptions == 0u);

    next = kernel_process_worker_resume();
    assert(next == first);
    assert(timer_last_load == kernel_platform_quantum_cycles());
    assert(kernel_process_worker_resume() == NULL);
    assert(kernel_process_stats(&stats));
    assert(stats.current_process_id == first_process_id);
    assert(stats.ready_bitmap == (1u << KERNEL_THREAD_PRIORITY_NORMAL));
}

static void test_public_thread_lifecycle_and_exact_charges(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    KernelCpuContext *next;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelProcessSnapshot process;
    KernelThreadSnapshot child;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t first_handle;
    uint32_t second_handle;
    uint32_t first_id;
    uint32_t process_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(interrupt_enable_count == 0u);
    assert(interrupt_disable_count == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[2] = 0x12345678u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    first_handle = next->data[1];
    first_id = next->data[2];
    assert(first_handle != KERNEL_HANDLE_INVALID);
    assert(first_id != 0u);
    assert(interrupt_enable_count == 1u);
    assert(interrupt_disable_count == 1u);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 2u);
    assert(process.live_threads == 2u);
    assert(process.user_stack_pages == 2u);
    assert(process.user_guard_pages == 2u);
    assert(process.supervisor_stack_pages == 4u);
    assert(process.supervisor_guard_pages == 2u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = first_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[2] == 0x12345678u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    registers[1] = 0x89abcdefu;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0x89abcdefu);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 2u);
    assert(process.live_threads == 1u);
    assert(process.user_stack_pages == 1u);
    assert(process.user_guard_pages == 1u);
    assert(process.supervisor_stack_pages == 4u);
    assert(process.supervisor_guard_pages == 2u);
    assert(kernel_thread_snapshot(1u, &child));
    assert(child.id == first_id);
    assert(child.state == KERNEL_THREAD_DEAD);
    assert(child.exit_status == 0x89abcdefu);
    assert(child.handle_references == 1u);
    assert(child.stack_released == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = first_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0x89abcdefu);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = first_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 1u);
    assert(process.supervisor_stack_pages == 2u);
    assert(process.supervisor_guard_pages == 1u);
    assert(kernel_thread_at(1u) == NULL);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = first_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[2] = 0x87654321u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHT_QUERY;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    second_handle = next->data[1];
    assert(second_handle != KERNEL_HANDLE_INVALID);
    assert(second_handle != first_handle);
    assert(interrupt_enable_count == 2u);
    assert(interrupt_disable_count == 2u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = second_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = second_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_YIELD;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[2] == 0x87654321u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    registers[1] = 7u;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 1u);
    assert(process.live_threads == 1u);
    assert(process.user_stack_pages == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    registers[1] = 0x55aa55aau;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.process_state == KERNEL_PROCESS_DEAD);
    assert(process.thread_count == 0u);
    assert(process.live_threads == 0u);
    assert(process.user_stack_pages == 0u);
    assert(process.supervisor_stack_pages == 0u);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_thread_publish_commit_excludes_supervisor_timer(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    KernelCpuContext *next;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelProcessSnapshot process;
    KernelSchedulerStats scheduler;
    KernelThreadPoolStats threads;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t event_handle;
    uint32_t process_id;
    uint64_t deadline_ns;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    event_handle = next->data[1];
    assert(event_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[2] = 0x11111111u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    deadline_ns = (scheduler_test_cycles + 2500u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = event_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[2] == 0x11111111u);

    scheduler_test_cycles += 2500u;
    disable_hook_process_slot = 0u;
    disable_hook_thread_count = 2u;
    timer_on_disable_armed = true;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 4u;
    registers[2] = 0x22222222u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(timer_on_disable_fired);
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 3u);
    assert(process.live_threads == 3u);
    assert(kernel_process_stats(&scheduler));
    assert(scheduler.deadline_expirations == 1u);
    assert(scheduler.deadline_depth == 0u);
    assert(kernel_thread_pool_stats(&threads));
    assert(threads.ready_threads == 2u);
    assert(kernel_thread_pool_valid());

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_thread_create_transaction_rolls_back_every_stage(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    static const KernelProcessThreadCreateFault faults[] = {
        KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_ALLOC,
        KERNEL_PROCESS_THREAD_CREATE_FAULT_STACK_MAP,
        KERNEL_PROCESS_THREAD_CREATE_FAULT_HANDLE_INSTALL,
        KERNEL_PROCESS_THREAD_CREATE_FAULT_PUBLISH,
    };
    static const uint32_t expected_results[] = {
        ASTRA_SYSCALL_OUT_OF_MEMORY,
        ASTRA_SYSCALL_OUT_OF_MEMORY,
        ASTRA_SYSCALL_RESOURCE_LIMIT,
        ASTRA_SYSCALL_RESOURCE_LIMIT,
    };

    for (uint32_t index = 0u;
         index < sizeof(faults) / sizeof(faults[0]); ++index) {
        KernelCpuContext *next;
        KernelMemoryStats baseline;
        KernelMemoryStats before_memory;
        KernelMemoryStats after_memory;
        KernelMemoryStats final;
        KernelProcessSnapshot before_process;
        KernelProcessSnapshot after_process;
        KernelSchedulerStats scheduler;
        KernelThreadPoolStats before_threads;
        KernelThreadPoolStats after_threads;
        KernelVmStats before_vm;
        KernelVmStats after_vm;
        uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
        uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
        uint32_t process_id;

        initialize_test();
        assert(kernel_memory_stats(&baseline));
        assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                     &process_id) == KERNEL_PROCESS_OK);
        assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
        assert(kernel_process_snapshot(0u, &before_process));
        assert(kernel_memory_stats(&before_memory));
        assert(kernel_vm_stats(&before_vm));
        assert(kernel_thread_pool_stats(&before_threads));
        assert(kernel_process_test_handle_count(process_id) == 2u);

        kernel_process_test_fail_next_thread_create(faults[index]);
        registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
        registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
        registers[2] = 0x12340000u + index;
        registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
        registers[4] = KERNEL_THREAD_RIGHTS;
        make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
                   KERNEL_PROCESS_CODE_BASE, 0u);
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_OK);
        assert(next != NULL);
        assert(next->data[0] == expected_results[index]);
        assert(interrupt_enable_count == 1u);
        assert(interrupt_disable_count == 1u);

        assert(kernel_process_snapshot(0u, &after_process));
        assert(after_process.thread_count == before_process.thread_count);
        assert(after_process.live_threads == before_process.live_threads);
        assert(after_process.user_stack_pages ==
               before_process.user_stack_pages);
        assert(after_process.user_guard_pages ==
               before_process.user_guard_pages);
        assert(after_process.supervisor_stack_pages ==
               before_process.supervisor_stack_pages);
        assert(after_process.supervisor_guard_pages ==
               before_process.supervisor_guard_pages);
        assert(kernel_process_test_handle_count(process_id) == 2u);
        assert(kernel_memory_stats(&after_memory));
        assert(after_memory.free_frames == before_memory.free_frames);
        assert(kernel_vm_stats(&after_vm));
        assert(after_vm.user_mappings == before_vm.user_mappings);
        assert(after_vm.user_table_pages == before_vm.user_table_pages);
        assert(kernel_thread_pool_stats(&after_threads));
        assert(after_threads.created_threads == before_threads.created_threads);
        assert(after_threads.live_threads == before_threads.live_threads);
        assert(after_threads.dead_threads == before_threads.dead_threads);
        assert(after_threads.ready_threads == before_threads.ready_threads);
        assert(after_threads.creation_rollbacks ==
               before_threads.creation_rollbacks + 1u);
        assert(kernel_process_stats(&scheduler));
        assert(scheduler.thread_creation_failures == 1u);
        assert(kernel_thread_pool_valid());

        /* The fault is one-shot; the same valid request must now publish. */
        registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_OK);
        assert(next != NULL);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
        assert(kernel_process_test_handle_count(process_id) == 3u);
        assert(interrupt_enable_count == 2u);
        assert(interrupt_disable_count == 2u);

        registers[0] = ASTRA_SYSCALL_EXIT;
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_NO_RUNNABLE);
        assert(next == NULL);
        assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
        assert(kernel_memory_stats(&final));
        assert(final.free_frames == baseline.free_frames);
    }
}

static void test_real_handle_exhaustion_rolls_back_thread_create(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t child_count = KERNEL_HANDLE_MAX_ENTRIES - 2u -
                                 KERNEL_SYNC_OWNER_MAX;
    KernelCpuContext *next;
    KernelMemoryStats baseline;
    KernelMemoryStats before_failure;
    KernelMemoryStats after_failure;
    KernelMemoryStats final;
    KernelProcessSnapshot before_process;
    KernelProcessSnapshot after_process;
    KernelThreadPoolStats before_threads;
    KernelThreadPoolStats after_threads;
    KernelVmStats before_vm;
    KernelVmStats after_vm;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    for (uint32_t index = 0u; index < KERNEL_SYNC_OWNER_MAX; ++index) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
        registers[2] = TEST_SYNC_RIGHTS;
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    for (uint32_t index = 0u; index < child_count; ++index) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
        registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
        registers[2] = index;
        registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
        registers[4] = KERNEL_THREAD_RIGHTS;
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    assert(kernel_process_test_handle_count(process_id) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_process_snapshot(0u, &before_process));
    assert(before_process.thread_count == child_count + 1u);
    assert(kernel_memory_stats(&before_failure));
    assert(kernel_vm_stats(&before_vm));
    assert(kernel_thread_pool_stats(&before_threads));

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_RESOURCE_LIMIT);
    assert(kernel_process_test_handle_count(process_id) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_process_snapshot(0u, &after_process));
    assert(after_process.thread_count == before_process.thread_count);
    assert(after_process.live_threads == before_process.live_threads);
    assert(after_process.user_stack_pages == before_process.user_stack_pages);
    assert(after_process.user_guard_pages == before_process.user_guard_pages);
    assert(after_process.supervisor_stack_pages ==
           before_process.supervisor_stack_pages);
    assert(after_process.supervisor_guard_pages ==
           before_process.supervisor_guard_pages);
    assert(kernel_memory_stats(&after_failure));
    assert(after_failure.free_frames == before_failure.free_frames);
    assert(kernel_vm_stats(&after_vm));
    assert(after_vm.user_mappings == before_vm.user_mappings);
    assert(after_vm.user_table_pages == before_vm.user_table_pages);
    assert(kernel_thread_pool_stats(&after_threads));
    assert(after_threads.created_threads == before_threads.created_threads);
    assert(after_threads.live_threads == before_threads.live_threads);
    assert(after_threads.ready_threads == before_threads.ready_threads);
    assert(after_threads.creation_rollbacks ==
           before_threads.creation_rollbacks + 1u);
    assert(kernel_thread_pool_valid());

    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_real_stack_oom_rolls_back_thread_create(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t pressure_owner = 0x70000001u;
    KernelCpuContext *next;
    KernelMemoryStats baseline;
    KernelMemoryStats exhausted;
    KernelMemoryStats after_failure;
    KernelMemoryStats final;
    KernelProcessSnapshot before_process;
    KernelProcessSnapshot after_process;
    KernelThreadPoolStats before_threads;
    KernelThreadPoolStats after_threads;
    KernelVmStats before_vm;
    KernelVmStats after_vm;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t physical;
    uint32_t pressure_frames = 0u;
    uint32_t process_id;
    uint32_t released;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &before_process));
    assert(kernel_vm_stats(&before_vm));
    assert(kernel_thread_pool_stats(&before_threads));

    for (;;) {
        KernelMemoryStatus status = kernel_memory_alloc(
            1u, 1u, KERNEL_FRAME_PROCESS, pressure_owner, &physical);

        if (status == KERNEL_MEMORY_OUT_OF_MEMORY)
            break;
        assert(status == KERNEL_MEMORY_OK);
        ++pressure_frames;
    }
    assert(pressure_frames != 0u);
    assert(kernel_memory_stats(&exhausted));
    assert(exhausted.free_frames == 0u);

    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL;
    registers[4] = KERNEL_THREAD_RIGHTS;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OUT_OF_MEMORY);
    assert(kernel_process_test_handle_count(process_id) == 2u);
    assert(kernel_process_snapshot(0u, &after_process));
    assert(after_process.thread_count == before_process.thread_count);
    assert(after_process.live_threads == before_process.live_threads);
    assert(after_process.user_stack_pages == before_process.user_stack_pages);
    assert(after_process.user_guard_pages == before_process.user_guard_pages);
    assert(after_process.supervisor_stack_pages ==
           before_process.supervisor_stack_pages);
    assert(after_process.supervisor_guard_pages ==
           before_process.supervisor_guard_pages);
    assert(kernel_memory_stats(&after_failure));
    assert(after_failure.free_frames == 0u);
    assert(kernel_vm_stats(&after_vm));
    assert(after_vm.user_mappings == before_vm.user_mappings);
    assert(after_vm.user_table_pages == before_vm.user_table_pages);
    assert(kernel_thread_pool_stats(&after_threads));
    assert(after_threads.created_threads == before_threads.created_threads);
    assert(after_threads.live_threads == before_threads.live_threads);
    assert(after_threads.ready_threads == before_threads.ready_threads);
    assert(after_threads.creation_rollbacks ==
           before_threads.creation_rollbacks + 1u);

    assert(kernel_memory_release_owner(pressure_owner, &released) ==
           KERNEL_MEMORY_OK);
    assert(released == pressure_frames);
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_wait_multiple_syscall_contract_and_races(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t wait_array_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t main_user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    const uint32_t sibling_exit_status = 0x4b365758u;
    KernelHandle handles[ASTRA_WAIT_MULTIPLE_MAX];
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelThreadPoolStats thread_stats;
    KernelThreadSnapshot main_thread;
    KernelThreadSnapshot sibling_thread;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t close_event_handle;
    uint32_t first_event_handle;
    uint32_t process_id;
    uint32_t second_event_handle;
    uint32_t sibling_handle;
    uint64_t deadline_ns;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    first_event_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    second_event_handle = next->data[1];
    assert(first_event_handle != KERNEL_HANDLE_INVALID);
    assert(second_event_handle != KERNEL_HANDLE_INVALID);

    handles[0] = first_event_handle;
    handles[1] = second_event_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);

    /* Invalid shape and address failures never retain a user pointer. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address + 2u;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = 0u;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = ASTRA_WAIT_MULTIPLE_MAX + 1u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);

    /* A bad trailing handle cannot consume a ready earlier member. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = first_event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    handles[0] = first_event_handle;
    handles[1] = KERNEL_HANDLE_INVALID;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = first_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    /* The public upper bound is accepted, including duplicate handles. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = first_event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    for (uint32_t index = 0u; index < ASTRA_WAIT_MULTIPLE_MAX; ++index)
        handles[index] = first_event_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   sizeof(handles)) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = ASTRA_WAIT_MULTIPLE_MAX;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);
    assert(next->data[2] == 0u);

    /* If several members are ready, the lowest input index wins. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = first_event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = second_event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    handles[0] = first_event_handle;
    handles[1] = second_event_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);
    assert(next->data[2] == 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = second_event_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    /* A timed-out set reports no winning member. */
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    deadline_ns = (scheduler_test_cycles + 25u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = (uint32_t)(deadline_ns >> 32);
    registers[4] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    scheduler_test_cycles += 25u;
    assert(kernel_process_on_supervisor_timer() == KERNEL_PROCESS_OK);
    next = kernel_process_current_context();
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    close_event_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL - 1u;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    sibling_handle = next->data[1];
    assert(sibling_handle != KERNEL_HANDLE_INVALID);
    assert(kernel_thread_snapshot(0u, &main_thread));
    assert(kernel_thread_snapshot(1u, &sibling_thread));

    /* A signal wakes the requested member and withdraws the whole set. */
    handles[0] = first_event_handle;
    handles[1] = second_event_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = second_event_handle;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 1u);
    assert(next->data[2] == 0u);

    /* Explicit cancellation has no winning member. */
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CANCEL_WAIT;
    registers[1] = main_thread.self_handle;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CANCELLED);
    assert(next->data[1] == ASTRA_WAIT_INDEX_NONE);
    assert(next->data[2] == 0u);

    /* Closing a member reports that member and removes every other link. */
    handles[0] = first_event_handle;
    handles[1] = close_event_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = close_event_handle;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CLOSED);
    assert(next->data[1] == 1u);
    assert(next->data[2] == 0u);

    /* Thread death supplies its exit status as the member detail. */
    handles[0] = first_event_handle;
    handles[1] = sibling_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   2u * sizeof(handles[0])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    registers[1] = sibling_exit_status;
    assert(kernel_process_on_syscall(
               registers, sibling_thread.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 1u);
    assert(next->data[2] == sibling_exit_status);

    assert(kernel_thread_pool_stats(&thread_stats));
    assert(thread_stats.wait_set_blocks == 5u);
    assert(thread_stats.wait_set_wakeups == 5u);
    assert(thread_stats.wait_registrations == 0u);
    assert(thread_stats.wait_registration_max == 2u);
    assert(thread_stats.max_wait_members == 2u);
    assert(kernel_thread_pool_valid());
    assert(kernel_sync_pool_valid());

    for (uint32_t close_index = 0u; close_index < 3u; ++close_index) {
        uint32_t handle = close_index == 0u ? sibling_handle :
            (close_index == 1u ? first_event_handle :
                                 second_event_handle);

        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_CLOSE;
        registers[1] = handle;
        assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_bootstrap_argument_is_prestart_only(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t injected_argument = 0x50524f43u;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t sibling_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0x11111111u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create_thread(
               process_id, 0u, 0x22222222u,
               KERNEL_THREAD_PRIORITY_NORMAL + 1u, &sibling_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_set_thread_bootstrap_argument(
               process_id, sibling_id, injected_argument) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_set_thread_bootstrap_argument(
               process_id + 1u, sibling_id, 0u) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_set_thread_bootstrap_argument(
               process_id, sibling_id + 1u, 0u) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[2] == injected_argument);
    assert(kernel_process_set_thread_bootstrap_argument(
               process_id, sibling_id, 0u) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    registers[0] = ASTRA_SYSCALL_THREAD_EXIT;
    assert(kernel_process_on_syscall(
               registers,
               KERNEL_PROCESS_STACK_TOP + KERNEL_THREAD_STACK_STRIDE - 8u,
               frame, &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(next->data[2] == 0x11111111u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_waitable_timer_syscalls_and_terminal_races(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t wait_array_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t main_user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelHandle handles[2];
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelSchedulerStats stats;
    KernelThreadSnapshot sibling;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t event_handle;
    uint32_t process_id;
    uint32_t sibling_handle;
    uint32_t timer_handle;
    uint64_t deadline_ns;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    event_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TIMER_CREATE;
    registers[1] = TEST_TIMER_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    timer_handle = next->data[1];
    assert(timer_handle != KERNEL_HANDLE_INVALID);

    deadline_ns = (scheduler_test_cycles + 50u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TIMER_SET;
    registers[1] = timer_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);

    handles[0] = event_handle;
    handles[1] = timer_handle;
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   sizeof(handles)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(next == NULL);
    scheduler_test_cycles += 50u;
    assert(kernel_process_on_supervisor_timer() == KERNEL_PROCESS_OK);
    next = kernel_process_current_context();
    assert(next != NULL);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 1u);
    assert(next->data[2] == 0u);

    /* Expiration is level-triggered until the timer is rearmed. */
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   sizeof(handles)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = 0u;
    registers[4] = 0u;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_THREAD_CREATE;
    registers[1] = KERNEL_PROCESS_CODE_BASE + 2u;
    registers[3] = KERNEL_THREAD_PRIORITY_NORMAL - 1u;
    registers[4] = KERNEL_THREAD_RIGHTS;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    sibling_handle = next->data[1];
    assert(kernel_thread_snapshot(1u, &sibling));

    deadline_ns = (scheduler_test_cycles + 100u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TIMER_SET;
    registers[1] = timer_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   sizeof(handles)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TIMER_CANCEL;
    registers[1] = timer_handle;
    assert(kernel_process_on_syscall(
               registers, sibling.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CANCELLED);
    assert(next->data[1] == 1u);
    assert(next->data[2] == 0u);

    deadline_ns = (scheduler_test_cycles + 100u) *
                  KERNEL_PLATFORM_NS_PER_CPU_CYCLE;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TIMER_SET;
    registers[1] = timer_handle;
    registers[2] = (uint32_t)(deadline_ns >> 32);
    registers[3] = (uint32_t)deadline_ns;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(kernel_user_copy_to_asm(wait_array_address, handles,
                                   sizeof(handles)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 2u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = timer_handle;
    assert(kernel_process_on_syscall(
               registers, sibling.user_stack_top - 128u, frame,
               &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_CLOSED);
    assert(next->data[1] == 1u);
    assert(next->data[2] == 0u);

    assert(kernel_process_stats(&stats));
    assert(stats.timer_created == 1u);
    assert(stats.timer_arms == 3u);
    assert(stats.timer_cancellations == 1u);
    assert(stats.timer_expirations == 1u);
    assert(stats.wait_set_blocks == 3u);
    assert(stats.wait_set_wakeups == 3u);

    (void)sibling_handle;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, main_user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_process_death_wait_handle_lifetime_and_slot_reuse(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t wait_array_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    const uint32_t exit_status = 0x50524f43u;
    KernelHandle handle;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelProcessSnapshot target;
    KernelSchedulerStats stats;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t observer_id;
    uint32_t retained_slot_process_id;
    uint32_t reused_process_id;
    uint32_t target_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &observer_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &target_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_handle(
               observer_id, target_id, TEST_PROCESS_WAIT_RIGHTS,
               &handle) == KERNEL_PROCESS_OK);
    assert(handle != KERNEL_HANDLE_INVALID);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_user_copy_to_asm(wait_array_address, &handle,
                                   sizeof(handle)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 1u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    registers[1] = exit_status;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);
    assert(next->data[2] == exit_status);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(1u, &target));
    assert(target.process_state == KERNEL_PROCESS_DEAD);
    assert(target.handle_references == 1u);
    assert(target.death_waiters == 0u);

    /* A retained process handle prevents reuse of the dead object slot. */
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &retained_slot_process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(2u, &target));
    assert(target.id == retained_slot_process_id);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = handle;
    registers[2] = 0u;
    registers[3] = 0u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &reused_process_id) == KERNEL_PROCESS_OK);
    assert(reused_process_id != target_id);
    assert(kernel_process_snapshot(1u, &target));
    assert(target.id == reused_process_id);
    assert(target.generation != 0u);
    assert(kernel_process_stats(&stats));
    assert(stats.process_death_waits == 1u);
    assert(stats.process_death_wakeups == 1u);

    for (uint32_t remaining = 3u; remaining != 0u; --remaining) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) ==
               (remaining == 1u ? KERNEL_PROCESS_NO_RUNNABLE :
                                  KERNEL_PROCESS_OK));
    }
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_process_fault_death_reports_peer_dead(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t wait_array_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelHandle handle;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t observer_id;
    uint32_t target_id;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &observer_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &target_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_handle(
               observer_id, target_id, TEST_PROCESS_WAIT_RIGHTS,
               &handle) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_user_copy_to_asm(wait_array_address, &handle,
                                   sizeof(handle)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = wait_array_address;
    registers[2] = 1u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE,
               0x60000000u);
    assert(kernel_process_on_fault(registers, user_stack, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_PEER_DEAD);
    assert(next->data[1] == 0u);
    assert(next->data[2] == 0u);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

int main(void)
{
    test_preemption_fault_containment_and_teardown();
    test_soak_relaunches_only_after_exact_teardown();
    test_soak_rejects_unexplained_frame_loss();
    test_invalid_creation_does_not_allocate();
    test_sync_syscall_rights_and_stale_handles();
    test_priority_selection_and_equal_priority_rotation();
    test_per_process_thread_limit_is_bounded_and_reclaimable();
    test_last_runnable_timed_wait_wakes_from_supervisor_idle();
    test_normal_syscalls_do_not_renew_quantum();
    test_worker_time_does_not_consume_user_quantum();
    test_public_thread_lifecycle_and_exact_charges();
    test_thread_publish_commit_excludes_supervisor_timer();
    test_thread_create_transaction_rolls_back_every_stage();
    test_real_handle_exhaustion_rolls_back_thread_create();
    test_real_stack_oom_rolls_back_thread_create();
    test_wait_multiple_syscall_contract_and_races();
    test_bootstrap_argument_is_prestart_only();
    test_waitable_timer_syscalls_and_terminal_races();
    test_process_death_wait_handle_lifetime_and_slot_reuse();
    test_process_fault_death_reports_peer_dead();
    puts("process tests passed");
    return 0;
}
