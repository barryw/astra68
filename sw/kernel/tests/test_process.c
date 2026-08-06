#include "allocation.h"
#include "area.h"
#include "block.h"
#include "dma.h"
#include "exception.h"
#include "irq.h"
#include "memory.h"
#include "platform.h"
#include "pmmu.h"
#include "port.h"
#include "process.h"

/*
 * A thread's committed stack in pages. Written out once here rather than at
 * each assertion: the counts below are about how many stacks a process holds,
 * not about how big one is, and a change to the size should not read as a
 * change to what these tests check.
 */
#define TEST_STACK_PAGES (KERNEL_THREAD_STACK_SIZE / KERNEL_PAGE_SIZE)

/*
 * A slot is a reservation and the stack is committed at the top of it, so the
 * base a thread starts with is a stride up from its slot and a stack down
 * again -- not the slot's own base, which is the guard.
 */
#define TEST_STACK_TOP(slot) \
    (KERNEL_THREAD_STACK_BASE + \
     ((uint32_t)(slot) + 1u) * KERNEL_THREAD_STACK_STRIDE)
#define TEST_STACK_BASE(slot) (TEST_STACK_TOP(slot) - KERNEL_THREAD_STACK_SIZE)
#define TEST_STACK_FLOOR(slot) \
    (KERNEL_THREAD_STACK_BASE + \
     (uint32_t)(slot) * KERNEL_THREAD_STACK_STRIDE + \
     KERNEL_THREAD_STACK_GUARD_SIZE)
#include "ohci.h"
#include "qualification.h"
#include "ring.h"
#include "sync.h"
#include "trace.h"
#include "user_copy.h"
#include "vm.h"
#include "worker.h"

#include <astra/block.h>
#include <vesta.h>
#include <astra/syscall.h>
#include <astra/display.h>
#include <astra/input.h>
#include <astra/process.h>
#include <astra/event.h>
#include <astra/status.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SYNC_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | \
     ASTRA_RIGHT_ADMINISTER)
#define TEST_TIMER_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_ADMINISTER)
#define TEST_TRANSFER_SYNC_RIGHTS \
    (TEST_SYNC_RIGHTS | ASTRA_RIGHT_TRANSFER)
#define TEST_PROCESS_WAIT_RIGHTS \
    (ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT)

static uint8_t physical_memory[32u * 1024u * 1024u];
static uint32_t milestone_calls;
static uint32_t initial_image_exits;
static uint32_t initial_image_progress_reports;
static uint32_t last_initial_image_stage;
static uint32_t last_initial_image_status;
static uint32_t last_initial_image_reason;
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
static uint32_t irq_configure_count;
static uint32_t irq_mask_count;
static uint32_t irq_enable_count;
static uint32_t irq_ack_count;
static uint32_t irq_capture_count;
static uint32_t irq_complete_count;
static uint32_t irq_quiesce_count;
static uint32_t irq_quiesce_failures_remaining;
static uint32_t irq_reset_schedule_count;
static uint32_t irq_capture_status;
static uint8_t irq_test_source;
static uint32_t qualification_prepare_count;
static uint32_t qualification_consume_count;
static uint32_t qualification_consumed_status;
static uint32_t device_quiesce_count;
static uint32_t device_reset_count;
static bool device_reset_ok;
static KernelInputEvent input_events[ASTRA_INPUT_READ_BATCH_MAX + 1u];
static uint32_t input_event_head;
static uint32_t input_event_count;
static uint32_t input_event_status;
static uint32_t input_overflow_acks;

/*
 * The character plane the console syscalls drive. Modelled rather than
 * stubbed, so the tests can assert what actually reached the screen.
 */
#define TEST_CONSOLE_COLUMNS 90u
#define TEST_CONSOLE_ROWS 30u

static uint8_t console_cells[TEST_CONSOLE_COLUMNS * TEST_CONSOLE_ROWS];
static uint32_t console_writes;
static bool console_present = true;

bool kernel_platform_post_text_present(void)
{
    return console_present;
}

void kernel_platform_post_text_geometry(uint32_t *columns, uint32_t *rows)
{
    if (columns != NULL)
        *columns = TEST_CONSOLE_COLUMNS;
    if (rows != NULL)
        *rows = TEST_CONSOLE_ROWS;
}

bool kernel_platform_post_text_write(uint32_t cell, uint8_t value)
{
    if (cell >= sizeof(console_cells))
        return false;
    console_cells[cell] = value;
    ++console_writes;
    return true;
}

uint32_t kernel_platform_input_status(void)
{
    uint32_t status = input_event_status;

    if (input_event_count != 0u)
        status |= ASTRA_INPUT_STATUS_VALID;
    return status;
}

bool kernel_input_peek(KernelInputEvent *event)
{
    if (event == NULL || input_event_count == 0u)
        return false;
    *event = input_events[input_event_head];
    return true;
}

bool kernel_input_consume(void)
{
    if (input_event_count == 0u)
        return false;
    ++input_event_head;
    --input_event_count;
    return true;
}

void kernel_platform_input_ack_overflow(void)
{
    input_event_status &= ~ASTRA_INPUT_STATUS_OVERFLOW;
    ++input_overflow_acks;
}

static bool test_input_quiesce(uint32_t device_id, uint32_t generation,
                               void *context)
{
    (void)context;
    return device_id == ASTRA_DEVICE_ID_INPUT0 && generation != 0u;
}

static bool test_input_reset(uint32_t device_id, uint32_t generation,
                             void *context)
{
    (void)context;
    input_event_head = 0u;
    input_event_count = 0u;
    input_event_status = 0u;
    input_overflow_acks = 0u;
    return device_id == ASTRA_DEVICE_ID_INPUT0 && generation != 0u;
}

static bool test_device_quiesce(uint32_t device_id, uint32_t generation,
                                void *context)
{
    (void)context;
    assert(device_id == 1u && generation != 0u);
    ++device_quiesce_count;
    return true;
}

static bool test_device_reset(uint32_t device_id, uint32_t generation,
                              void *context)
{
    (void)context;
    assert(device_id == 1u && generation != 0u);
    ++device_reset_count;
    return device_reset_ok;
}

static bool irq_controller_configure(uint8_t source, uint8_t trigger,
                                     uint8_t ipl, uint8_t vector,
                                     void *context)
{
    (void)context;
    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    assert(trigger == KERNEL_IRQ_TRIGGER_LEVEL ||
           trigger == KERNEL_IRQ_TRIGGER_EDGE);
    assert(ipl >= 1u && ipl <= 7u);
    assert(vector == KERNEL_IRQ_COMMON_VECTOR);
    ++irq_configure_count;
    return true;
}

static bool irq_controller_mask(uint8_t source, void *context)
{
    (void)context;
    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    ++irq_mask_count;
    return true;
}

static bool irq_controller_enable(uint8_t source, void *context)
{
    (void)context;
    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    ++irq_enable_count;
    return true;
}

static bool irq_controller_acknowledge(uint8_t source, void *context)
{
    (void)context;
    assert(source < KERNEL_IRQ_SOURCE_COUNT);
    ++irq_ack_count;
    return true;
}

static bool irq_device_capture(uint8_t source, uint32_t *status,
                               void *context)
{
    (void)context;
    assert(source == irq_test_source);
    assert(status != NULL);
    *status = irq_capture_status;
    ++irq_capture_count;
    return true;
}

static bool irq_device_complete(uint8_t source,
                                const KernelIrqRecord *record,
                                void *context)
{
    (void)context;
    assert(source == irq_test_source);
    assert(record != NULL && record->sequence != 0u);
    ++irq_complete_count;
    return true;
}

static bool irq_device_quiesce(uint8_t source, void *context)
{
    (void)context;
    assert(source == irq_test_source);
    ++irq_quiesce_count;
    if (irq_quiesce_failures_remaining == 0u)
        return true;
    --irq_quiesce_failures_remaining;
    return false;
}

bool kernel_interrupt_device_binding(uint8_t source,
                                     KernelIrqBinding *binding)
{
    if (binding == NULL || source != irq_test_source)
        return false;
    *binding = (KernelIrqBinding){
        .capture = irq_device_capture,
        .complete = irq_device_complete,
        .quiesce = irq_device_quiesce,
        .context = NULL,
        .source = source,
        .trigger = KERNEL_IRQ_TRIGGER_LEVEL,
        .ipl = 3u,
        .vector = KERNEL_IRQ_COMMON_VECTOR,
    };
    return true;
}

bool kernel_interrupt_schedule_device_reset(void)
{
    if (!kernel_irq_revocation_pending())
        return false;
    ++irq_reset_schedule_count;
    return true;
}

bool kernel_platform_qualification_irq_prepare(uint8_t source)
{
    assert(source == irq_test_source);
    ++qualification_prepare_count;
    return true;
}

bool kernel_platform_qualification_irq_consume(uint8_t source,
                                               uint32_t status)
{
    assert(source == irq_test_source);
    ++qualification_consume_count;
    qualification_consumed_status = status;
    return true;
}

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

static void fire_timer_before_mask(void)
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
}

void kernel_disable_interrupts(void)
{
    fire_timer_before_mask();
    interrupts_enabled = false;
    ++interrupt_disable_count;
}

uint16_t kernel_interrupt_save_disable(void)
{
    uint16_t saved = interrupts_enabled ? 0x2000u : 0x2700u;

    fire_timer_before_mask();
    interrupts_enabled = false;
    return saved;
}

void kernel_interrupt_restore(uint16_t status_register)
{
    interrupts_enabled = (status_register & 0x0700u) != 0x0700u;
}

void kernel_platform_cpu_cycles(KernelPlatformCycleCount *cycles)
{
    assert(cycles != NULL);
    cycles->high = (uint32_t)(scheduler_test_cycles >> 32);
    cycles->low = (uint32_t)scheduler_test_cycles;
}

/*
 * The real ring, not a stub. Emitting is a copy into it now, so a test that
 * stubbed the ring out could not tell an event that was recorded from one that
 * was dropped -- which is the property this file has to check.
 */

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

void kernel_pmmu_read_tc(uint32_t *value)
{
    *value = 0u;
}

void kernel_pmmu_read_srp(KernelPmmuRootPointer *root)
{
    *root = (KernelPmmuRootPointer){0};
}

void kernel_pmmu_read_crp(KernelPmmuRootPointer *root)
{
    *root = (KernelPmmuRootPointer){0};
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

uint32_t kernel_cache_read_control(void)
{
    return 0u;
}

/*
 * A block controller the admission tests can actually drive: it accepts one
 * request at a time, records what it was asked to do, and completes when the
 * test says so. Without it the syscall layer could only be tested for its
 * rejections, which is the half that does not carry data.
 */
static bool block_device_present = true;
static uint32_t block_submit_error;
static uint32_t block_submitted_id;
static uint32_t block_submitted_buffer;
static uint8_t block_submitted_operation;
static uint16_t block_submitted_sectors;
static uint64_t block_submitted_lba;
static uint32_t block_submit_calls;
static bool block_completion_ready;
static KernelPlatformBlockCompletion block_completion;
static uint32_t block_media_generation = 7u;
static uint32_t block_host_generation = 3u;
static uint32_t block_state_flags =
    BLOCK_STATE_LINK_UP | BLOCK_STATE_MEDIA_PRESENT | BLOCK_STATE_WRITE_ENABLE;
static uint32_t block_ack_calls;

bool kernel_platform_block_present(void)
{
    return block_device_present;
}

bool kernel_platform_block_state(KernelPlatformBlockState *state)
{
    if (state == NULL || !block_device_present)
        return false;
    memset(state, 0, sizeof(*state));
    state->capabilities = BLOCK_CAP_READ | BLOCK_CAP_WRITE | BLOCK_CAP_FLUSH;
    state->state_flags = block_state_flags;
    state->media_generation = block_media_generation;
    state->host_generation = block_host_generation;
    state->media_sectors = 2048u;
    state->max_sectors = 16u;
    return true;
}

uint32_t kernel_platform_block_submit(uint32_t id, uint8_t operation,
                                      uint8_t flags, uint64_t lba,
                                      uint16_t sectors,
                                      uint32_t physical_buffer)
{
    (void)flags;
    ++block_submit_calls;
    if (block_submit_error != 0u)
        return block_submit_error;
    block_submitted_id = id;
    block_submitted_operation = operation;
    block_submitted_lba = lba;
    block_submitted_sectors = sectors;
    block_submitted_buffer = physical_buffer;
    return 0u;
}

/* Makes the pending request complete on the next service pass. */
static void block_complete_request(uint16_t status, uint16_t sectors)
{
    memset(&block_completion, 0, sizeof(block_completion));
    block_completion.id = block_submitted_id;
    block_completion.status = status;
    block_completion.sectors = sectors;
    block_completion.media_generation = block_media_generation;
    block_completion.host_generation = block_host_generation;
    block_completion_ready = true;
}

bool kernel_platform_block_pop_completion(
    KernelPlatformBlockCompletion *completion)
{
    if (completion == NULL || !block_completion_ready)
        return false;
    *completion = block_completion;
    block_completion_ready = false;
    return true;
}

void kernel_platform_block_ack_state(void)
{
    ++block_ack_calls;
}

static bool test_block_quiesce(uint32_t device_id, uint32_t generation,
                               void *context)
{
    (void)generation;
    (void)context;
    return device_id == ASTRA_DEVICE_ID_BLOCK0;
}

static bool test_block_reset(uint32_t device_id, uint32_t generation,
                             void *context)
{
    (void)generation;
    (void)context;
    ++block_ack_calls;
    return device_id == ASTRA_DEVICE_ID_BLOCK0;
}

static void reset_block_device(void)
{
    block_device_present = true;
    block_submit_error = 0u;
    block_submitted_id = 0u;
    block_submitted_buffer = 0u;
    block_submitted_operation = 0u;
    block_submitted_sectors = 0u;
    block_submitted_lba = 0u;
    block_submit_calls = 0u;
    block_completion_ready = false;
    block_media_generation = 7u;
    block_host_generation = 3u;
    block_state_flags = BLOCK_STATE_LINK_UP | BLOCK_STATE_MEDIA_PRESENT |
                        BLOCK_STATE_WRITE_ENABLE;
    block_ack_calls = 0u;
    memset(&block_completion, 0, sizeof(block_completion));
}

void kernel_process_milestone_reached(const KernelSchedulerStats *stats)
{
    assert(stats != NULL);
    ++milestone_calls;
}

void kernel_process_initial_image_exited(uint32_t exit_status,
                                         uint32_t exit_reason)
{
    ++initial_image_exits;
    last_initial_image_status = exit_status;
    last_initial_image_reason = exit_reason;
}

void kernel_process_initial_image_progress(uint32_t stage)
{
    ++initial_image_progress_reports;
    last_initial_image_stage = stage;
}

static uint32_t fault_reports;
static uint32_t last_fault_process;
static uint32_t last_fault_pc;
static uint32_t last_fault_address;
static uint32_t last_fault_vector;
static uint32_t last_fault_kind;

void kernel_process_fault_report(uint32_t process_id, uint32_t thread_id,
                                 uint32_t program_counter,
                                 uint32_t fault_address, uint32_t vector,
                                 uint32_t kind)
{
    (void)thread_id;
    ++fault_reports;
    last_fault_process = process_id;
    last_fault_pc = program_counter;
    last_fault_address = fault_address;
    last_fault_vector = vector;
    last_fault_kind = kind;
}

static uint32_t diagnostic_log_reports;
static uint32_t last_diagnostic_process;
static char last_diagnostic_text[ASTRA_LOG_MAX_BYTES + 1u];
static uint32_t last_diagnostic_length;

static uint16_t last_diagnostic_flags;
static uint32_t last_diagnostic_message;

void kernel_process_diagnostic_log(const KernelTraceUserRecord *record,
                                   const void *payload, uint32_t length)
{
    /* The real sink's gate, mirrored: the console is a debug surface. */
    if (!kernel_process_debug_surface())
        return;
    ++diagnostic_log_reports;
    last_diagnostic_process = record->process;
    last_diagnostic_flags = record->flags;
    last_diagnostic_message = record->message;
    last_diagnostic_length = length;
    assert(length <= ASTRA_EVENT_ARGUMENT_MAX);
    memcpy(last_diagnostic_text, payload, length);
    last_diagnostic_text[length] = '\0';
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
    const KernelIrqControllerOps irq_ops = {
        .configure = irq_controller_configure,
        .mask = irq_controller_mask,
        .enable = irq_controller_enable,
        .acknowledge = irq_controller_acknowledge,
        .context = NULL,
    };

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
    add_range(&info, ASTRA_USER_IMAGE_ADDRESS, ASTRA_USER_IMAGE_MAX_SIZE,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_LOAD_ADDRESS, ASTRA_KERNEL_RESERVED_SIZE,
              ASTRA_MEMORY_RANGE_KERNEL,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_EXECUTE | ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_KERNEL_USABLE_ADDRESS, ASTRA_KERNEL_USABLE_SIZE,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, ASTRA_ROM_BACKING_ADDRESS, ASTRA_ROM_BACKING_SIZE,
              ASTRA_MEMORY_RANGE_ROM_BACKING,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_EXECUTE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, 0x03e40000u, 0x000c0000u,
              ASTRA_MEMORY_RANGE_USABLE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE |
                  ASTRA_MEMORY_CACHEABLE);
    add_range(&info, OHCI_DMA_POOL_BASE, OHCI_DMA_POOL_SIZE,
              ASTRA_MEMORY_RANGE_DEVICE,
              ASTRA_MEMORY_READ | ASTRA_MEMORY_WRITE);
    astra_boot_info_finalize(&info);
    assert(kernel_memory_init(&info) == KERNEL_MEMORY_OK);
    memset(physical_memory, 0xa5, sizeof(physical_memory));
    kernel_memory_test_bind_physical_memory(physical_memory, 0x02000000u,
                                            sizeof(physical_memory));
    kernel_vm_test_bind_physical_memory(physical_memory, 0x02000000u,
                                        sizeof(physical_memory));
    kernel_process_test_bind_physical_memory(physical_memory, 0x02000000u,
                                             sizeof(physical_memory));
    assert(kernel_vm_init() == KERNEL_VM_OK);
    assert(kernel_vm_enable() == KERNEL_VM_OK);
    reset_block_device();
    kernel_dma_init();
    kernel_block_init();
    assert(kernel_device_init());
    {
        const KernelDeviceDefinition device = {
            .quiesce = test_device_quiesce,
            .reset = test_device_reset,
            .context = NULL,
            .device_id = 1u,
            .class_id = 0x47505500u,
            .capabilities = 0x00000003u,
        };

        assert(kernel_device_register(&device) == KERNEL_DEVICE_OK);
    }
    {
        const KernelDeviceDefinition input = {
            .quiesce = test_input_quiesce,
            .reset = test_input_reset,
            .context = NULL,
            .device_id = ASTRA_DEVICE_ID_INPUT0,
            .class_id = ASTRA_DEVICE_CLASS_INPUT,
            .capabilities = ASTRA_INPUT_CAP_KEYBOARD |
                            ASTRA_INPUT_CAP_POINTER,
        };

        assert(kernel_device_register(&input) == KERNEL_DEVICE_OK);
    }
    {
        const KernelDeviceDefinition display = {
            .quiesce = test_input_quiesce,
            .reset = test_input_reset,
            .context = NULL,
            .device_id = ASTRA_DEVICE_ID_DISPLAY0,
            .class_id = ASTRA_DEVICE_CLASS_DISPLAY,
            .capabilities = ASTRA_DISPLAY_CAP_TEXT,
        };

        assert(kernel_device_register(&display) == KERNEL_DEVICE_OK);
    }
    {
        const KernelDeviceDefinition block = {
            .quiesce = test_block_quiesce,
            .reset = test_block_reset,
            .context = NULL,
            .device_id = ASTRA_DEVICE_ID_BLOCK0,
            .class_id = ASTRA_DEVICE_CLASS_BLOCK,
            .capabilities = ASTRA_BLOCK_CAP_READ | ASTRA_BLOCK_CAP_WRITE |
                            ASTRA_BLOCK_CAP_FLUSH,
        };

        assert(kernel_device_register(&block) == KERNEL_DEVICE_OK);
    }
    assert(kernel_device_seal_registry());
    device_quiesce_count = 0u;
    device_reset_count = 0u;
    device_reset_ok = true;
    memset(console_cells, 0, sizeof(console_cells));
    console_writes = 0u;
    console_present = true;
    input_event_head = 0u;
    input_event_count = 0u;
    input_event_status = 0u;
    input_overflow_acks = 0u;
    irq_configure_count = 0u;
    irq_mask_count = 0u;
    irq_enable_count = 0u;
    irq_ack_count = 0u;
    irq_capture_count = 0u;
    irq_complete_count = 0u;
    irq_quiesce_count = 0u;
    irq_quiesce_failures_remaining = 0u;
    irq_reset_schedule_count = 0u;
    irq_capture_status = 0u;
    irq_test_source = 0u;
    qualification_prepare_count = 0u;
    qualification_consume_count = 0u;
    qualification_consumed_status = 0u;
    assert(kernel_irq_pool_init(&irq_ops));
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
    initial_image_exits = 0u;
    initial_image_progress_reports = 0u;
    last_initial_image_stage = 0u;
    last_initial_image_status = 0u;
    last_initial_image_reason = 0u;
    soak_checkpoint_calls = 0u;
    last_soak_checkpoint = 0u;
    last_soak_free_frames = 0u;
}

typedef struct TestAllocationBaseline {
    uint32_t units[KERNEL_ALLOCATION_TAG_COUNT];
    uint32_t bytes[KERNEL_ALLOCATION_TAG_COUNT];
} TestAllocationBaseline;

static void capture_allocation_baseline(TestAllocationBaseline *baseline)
{
    for (uint32_t tag = 1u; tag < KERNEL_ALLOCATION_TAG_COUNT; ++tag) {
        KernelAllocationStats stats;

        assert(kernel_allocation_tag_stats((KernelAllocationTag)tag,
                                           &stats));
        baseline->units[tag] = stats.current_units;
        baseline->bytes[tag] = stats.current_bytes;
    }
}

static void assert_allocation_baseline(
    const TestAllocationBaseline *baseline)
{
    for (uint32_t tag = 1u; tag < KERNEL_ALLOCATION_TAG_COUNT; ++tag) {
        KernelAllocationStats stats;

        assert(kernel_allocation_tag_stats((KernelAllocationTag)tag,
                                           &stats));
        assert(stats.current_units == baseline->units[tag]);
        assert(stats.current_bytes == baseline->bytes[tag]);
    }
    assert(kernel_allocation_valid());
}

static uint32_t injected_failure_total(void)
{
    uint32_t total = 0u;

    for (uint32_t site = 1u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site) {
        KernelAllocationStats stats;

        assert(kernel_allocation_site_stats((KernelAllocationSite)site,
                                            &stats));
        total += stats.injected_failures;
    }
    return total;
}

static uint32_t allocation_attempt_total(void)
{
    uint32_t total = 0u;

    for (uint32_t site = 1u; site < KERNEL_ALLOCATION_SITE_COUNT; ++site) {
        KernelAllocationStats stats;

        assert(kernel_allocation_site_stats((KernelAllocationSite)site,
                                            &stats));
        total += stats.attempts;
    }
    return total;
}

static void terminate_only_process(void)
{
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];

    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    registers[0] = ASTRA_SYSCALL_EXIT;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u,
                                     frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
}

static void assert_failed_process_create_baseline(
    const KernelMemoryStats *memory_baseline,
    const TestAllocationBaseline *allocation_baseline)
{
    KernelMemoryStats memory;
    KernelSchedulerStats scheduler;
    KernelThreadPoolStats threads;

    assert(kernel_memory_stats(&memory));
    assert(memory.free_frames == memory_baseline->free_frames);
    assert(memory.owner_slots_used == memory_baseline->owner_slots_used);
    assert(memory.emergency_available_frames ==
           memory_baseline->emergency_available_frames);
    assert(kernel_process_stats(&scheduler));
    assert(scheduler.live_processes == 0u);
    assert(scheduler.live_threads == 0u);
    assert(kernel_thread_pool_stats(&threads));
    assert(threads.live_threads == 0u);
    assert(threads.ready_threads == 0u);
    assert_allocation_baseline(allocation_baseline);
}

static void test_process_allocation_failure_matrix(void)
{
    static const struct {
        KernelAllocationSite site;
        KernelProcessStatus status;
    } cases[] = {
        {KERNEL_ALLOCATION_SITE_PROCESS_RECORD, KERNEL_PROCESS_NO_SLOT},
        {KERNEL_ALLOCATION_SITE_VM_PAGE_TABLE,
         KERNEL_PROCESS_OUT_OF_MEMORY},
        {KERNEL_ALLOCATION_SITE_PROCESS_CODE_PAGE,
         KERNEL_PROCESS_OUT_OF_MEMORY},
        {KERNEL_ALLOCATION_SITE_THREAD_RECORD,
         KERNEL_PROCESS_RESOURCE_LIMIT},
        {KERNEL_ALLOCATION_SITE_THREAD_STACK_PAGE,
         KERNEL_PROCESS_OUT_OF_MEMORY},
        {KERNEL_ALLOCATION_SITE_HANDLE_SLOT,
         KERNEL_PROCESS_RESOURCE_LIMIT}
    };
    static const uint8_t image[] = {0x4eu, 0x71u, 0x60u, 0xfcu};

    for (uint32_t index = 0u;
         index < sizeof(cases) / sizeof(cases[0]); ++index) {
        KernelAllocationStats site_stats;
        TestAllocationBaseline allocation_baseline;
        KernelMemoryStats memory_baseline;
        uint32_t process_id = UINT32_MAX;

        initialize_test();
        assert(kernel_memory_stats(&memory_baseline));
        capture_allocation_baseline(&allocation_baseline);
        kernel_allocation_test_fail_site(cases[index].site, 1u);
        assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                     &process_id) == cases[index].status);
        assert(process_id == 0u);
        assert(kernel_allocation_site_stats(cases[index].site,
                                            &site_stats));
        assert(site_stats.injected_failures == 1u);
        assert(injected_failure_total() == 1u);
        assert_failed_process_create_baseline(&memory_baseline,
                                              &allocation_baseline);
    }
}

static void test_process_global_nth_failure_matrix(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u, 0x60u, 0xfcu};
    bool reached_success = false;
    uint32_t failed_stages = 0u;

    for (uint32_t target = 1u; target <= 16u; ++target) {
        TestAllocationBaseline allocation_baseline;
        KernelMemoryStats memory_baseline;
        KernelMemoryStats final;
        KernelProcessStatus status;
        uint32_t process_id = UINT32_MAX;

        initialize_test();
        assert(kernel_memory_stats(&memory_baseline));
        capture_allocation_baseline(&allocation_baseline);
        kernel_allocation_test_fail_global(target);
        status = kernel_process_create(image, sizeof(image), 0u, 0u,
                                       &process_id);
        kernel_allocation_test_clear_failure();
        if (status == KERNEL_PROCESS_OK) {
            assert(process_id != 0u);
            terminate_only_process();
            assert(kernel_memory_stats(&final));
            assert(final.free_frames == memory_baseline.free_frames);
            assert_allocation_baseline(&allocation_baseline);
            reached_success = true;
            break;
        }
        assert(status == KERNEL_PROCESS_NO_SLOT ||
               status == KERNEL_PROCESS_OUT_OF_MEMORY ||
               status == KERNEL_PROCESS_RESOURCE_LIMIT);
        assert(process_id == 0u);
        assert(injected_failure_total() == 1u);
        assert_failed_process_create_baseline(&memory_baseline,
                                              &allocation_baseline);
        ++failed_stages;
    }
    assert(reached_success);
    assert(failed_stages >= 8u);
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
    KernelSyncPoolStats sync_stats;
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
    assert(survivor_thread.user_stack_base == TEST_STACK_BASE(0));
    assert(sibling_thread.user_stack_base == TEST_STACK_BASE(1));
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
    /*
     * It never returned a status, so it must not report the one that means
     * success. A waiter reading only this value has to see that it was killed.
     */
    assert(offender.exit_status == (uint32_t)ASTRA_STATUS_FAULTED);
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
    assert(kernel_sync_pool_stats(&sync_stats));
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
    assert(stats.sync_blocked_waits == sync_stats.blocked_waits);
    assert(stats.sync_blocked_waits >= 5u);
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

static void test_irq_syscalls_waits_rights_and_owner_cleanup(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    const uint32_t full_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;
    KernelIrqBinding binding = {
        .capture = irq_device_capture,
        .complete = irq_device_complete,
        .quiesce = irq_device_quiesce,
        .context = NULL,
        .source = 5u,
        .trigger = KERNEL_IRQ_TRIGGER_LEVEL,
        .ipl = 3u,
        .vector = KERNEL_IRQ_COMMON_VECTOR,
    };
    KernelCpuContext *next;
    KernelIrqPoolStats irq_stats;
    AstraIrqRecord record;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t irq_handle;
    uint32_t reduced_handle;
    uint32_t user_record = KERNEL_PROCESS_STACK_TOP - 64u;
    uint32_t woken;

    initialize_test();
    irq_test_source = binding.source;
    irq_capture_status = 0x5a68c030u;
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_irq(process_id, &binding, full_rights,
                                    &irq_handle) == KERNEL_PROCESS_OK);
    assert(irq_handle != KERNEL_HANDLE_INVALID);
    assert(irq_configure_count == 1u && irq_mask_count == 1u);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = irq_handle;
    registers[2] = ASTRA_RIGHT_READ | ASTRA_RIGHT_WAIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    reduced_handle = next->data[1];
    assert(reduced_handle != irq_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ARM;
    registers[1] = reduced_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ARM;
    registers[1] = irq_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(irq_enable_count == 1u);

    assert(kernel_irq_dispatch(binding.source, binding.vector,
                               UINT64_C(0x1122334455667788), &woken) ==
           KERNEL_IRQ_OK);
    assert(woken == 0u && irq_capture_count == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = reduced_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_READ;
    registers[1] = reduced_handle;
    registers[2] = user_record;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);
    assert(kernel_user_copy_from_asm(&record, user_record, sizeof(record)) ==
           KERNEL_USER_COPY_OK);
    assert(record.timestamp_high == 0x11223344u);
    assert(record.timestamp_low == 0x55667788u);
    assert(record.status == irq_capture_status);
    assert(record.sequence != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ACK;
    registers[1] = reduced_handle;
    registers[2] = record.sequence;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ACK;
    registers[1] = irq_handle;
    registers[2] = record.sequence + 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    registers[2] = record.sequence;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(irq_complete_count == 1u && irq_ack_count == 1u);
    assert(irq_enable_count == 2u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_REVOKE;
    registers[1] = irq_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(irq_quiesce_count == 0u);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(irq_reset_schedule_count == 1u);
    assert(kernel_irq_service_revocations(
               KERNEL_WORKER_DEVICE_RESET_BATCH, &woken) == KERNEL_IRQ_OK);
    assert(woken == 1u);
    assert(irq_quiesce_count == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = reduced_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_PEER_DEAD);

    for (uint32_t close = 0u; close < 2u; ++close) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_CLOSE;
        registers[1] = close == 0u ? reduced_handle : irq_handle;
        assert(kernel_process_on_syscall(
                   registers, KERNEL_PROCESS_STACK_TOP - 8u, frame,
                   &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ARM;
    registers[1] = irq_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_irq_pool_stats(&irq_stats));
    assert(irq_stats.live_endpoints == 0u);

    initialize_test();
    binding.source = 6u;
    irq_test_source = binding.source;
    irq_quiesce_failures_remaining = 2u;
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_irq(process_id, &binding, full_rights,
                                    &irq_handle) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(irq_reset_schedule_count == 1u);
    assert(kernel_irq_service_revocations(
               KERNEL_WORKER_DEVICE_RESET_BATCH, &woken) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(woken == 0u && irq_quiesce_count == 1u);
    assert(kernel_irq_pool_stats(&irq_stats));
    assert(irq_stats.live_endpoints == 1u);
    assert(irq_stats.revoking_endpoints == 1u);
    assert(kernel_process_maintenance_pending());
    assert(kernel_irq_service_revocations(
               KERNEL_WORKER_DEVICE_RESET_BATCH, &woken) ==
           KERNEL_IRQ_DEVICE_ERROR);
    assert(woken == 0u && irq_quiesce_count == 2u);
    assert(kernel_irq_service_revocations(
               KERNEL_WORKER_DEVICE_RESET_BATCH, &woken) == KERNEL_IRQ_OK);
    assert(woken == 1u);
    assert(irq_quiesce_count == 3u);
    assert(kernel_irq_pool_stats(&irq_stats));
    assert(irq_stats.live_endpoints == 0u);
    assert(irq_stats.revoking_endpoints == 0u);
    assert(!kernel_process_maintenance_pending());
}

static void test_device_lease_syscalls_rights_and_owner_cleanup(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    const uint32_t full_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;
    KernelCpuContext *next;
    KernelDeviceStats device_stats;
    AstraDeviceInfo info;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t device_handle;
    uint32_t query_handle;
    uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_device(process_id, 1u, full_rights,
                                       &device_handle) == KERNEL_PROCESS_OK);
    assert(device_handle != KERNEL_HANDLE_INVALID);
    assert(kernel_process_grant_device(process_id, 1u, full_rights,
                                       &query_handle) ==
           KERNEL_PROCESS_RESOURCE_LIMIT);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = device_handle;
    registers[2] = ASTRA_RIGHT_READ;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    query_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DEVICE_RESET;
    registers[1] = query_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DEVICE_QUERY;
    registers[1] = query_handle;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&info, user_info, sizeof(info)) ==
           KERNEL_USER_COPY_OK);
    assert(info.size == sizeof(info));
    assert(info.device_id == 1u && info.class_id == 0x47505500u);
    assert(info.capabilities == 3u && info.generation != 0u);
    assert(info.reserved == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DEVICE_RESET;
    registers[1] = device_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(device_reset_count == 1u);

    device_reset_ok = false;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(device_quiesce_count == 1u && device_reset_count == 2u);
    assert(kernel_device_stats(&device_stats));
    assert(device_stats.live_leases == 0u);
    assert(device_stats.owner_deaths == 1u);
    assert(device_stats.reset_failures == 1u);
    assert(kernel_device_pool_valid());
}

/*
 * The console syscalls are device operations, not a privileged shortcut: they
 * are reached through a display lease carrying the right the call needs, and
 * they refuse anything that would write outside the plane.
 */
static void test_console_writes_through_a_display_lease(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t display_handle;
    uint32_t read_only_handle;
    uint32_t input_handle;
    uint32_t user_cells = KERNEL_PROCESS_STACK_TOP - 128u;
    const uint8_t text[4] = {'A', 'B', 'C', 'D'};

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_device(
               process_id, ASTRA_DEVICE_ID_DISPLAY0,
               ASTRA_RIGHT_READ | ASTRA_RIGHT_TRANSFER,
               &display_handle) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_device(
               process_id, ASTRA_DEVICE_ID_INPUT0, ASTRA_RIGHT_READ,
               &input_handle) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);

    /*
     * A device has exactly one lease, so the query-only handle is a duplicate
     * with a narrowed rights mask -- which is how a service hands a client a
     * handle it may look at but not draw with.
     */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = display_handle;
    registers[2] = ASTRA_RIGHT_READ;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    read_only_handle = next->data[1];

    /* Geometry comes back in D1/D2. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_INFO;
    registers[1] = display_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == TEST_CONSOLE_COLUMNS);
    assert(next->data[2] == TEST_CONSOLE_ROWS);

    /* A handle without TRANSFER cannot draw. */
    assert(kernel_user_copy_to_asm(user_cells, text, sizeof(text)) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = read_only_handle;
    registers[2] = 0u;
    registers[3] = user_cells;
    registers[4] = sizeof(text);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(console_writes == 0u);

    /* A lease on another device is not a display lease. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = input_handle;
    registers[2] = 0u;
    registers[3] = user_cells;
    registers[4] = sizeof(text);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED ||
           next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(console_writes == 0u);

    /* The write lands where it was asked to. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = display_handle;
    registers[2] = TEST_CONSOLE_COLUMNS + 2u;
    registers[3] = user_cells;
    registers[4] = sizeof(text);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(console_writes == sizeof(text));
    assert(console_cells[TEST_CONSOLE_COLUMNS + 2u] == 'A');
    assert(console_cells[TEST_CONSOLE_COLUMNS + 5u] == 'D');
    assert(console_cells[TEST_CONSOLE_COLUMNS + 1u] == 0u);

    /*
     * A run that would leave the plane is refused whole. The last cell is
     * addressable; one past it is not, and neither is a run that straddles
     * the end.
     */
    console_writes = 0u;
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = display_handle;
    registers[2] = TEST_CONSOLE_COLUMNS * TEST_CONSOLE_ROWS - 2u;
    registers[3] = user_cells;
    registers[4] = sizeof(text);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(console_writes == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = display_handle;
    registers[2] = 0u;
    registers[3] = user_cells;
    registers[4] = ASTRA_CONSOLE_WRITE_MAX + 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(console_writes == 0u);

    /* An unreadable source is reported, and nothing reaches the plane. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CONSOLE_WRITE;
    registers[1] = display_handle;
    registers[2] = 0u;
    registers[3] = 0u;
    registers[4] = sizeof(text);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(console_writes == 0u);
}

static void test_input_batch_read_is_bounded_and_fault_atomic(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    AstraInputEvent received[3];
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t input_handle;
    uint32_t user_events = KERNEL_PROCESS_STACK_TOP - 128u;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_device(
               process_id, ASTRA_DEVICE_ID_INPUT0, ASTRA_RIGHT_READ,
               &input_handle) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_INPUT_READ_TRY;
    registers[1] = input_handle;
    registers[2] = user_events;
    registers[3] = 2u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_WOULD_BLOCK);
    assert(next->data[1] == 0u && next->data[2] == 0u);

    for (uint32_t index = 0u; index < 3u; ++index) {
        input_events[index] = (KernelInputEvent){
            .header = ASTRA_INPUT_HEADER(ASTRA_INPUT_CLASS_KEYBOARD,
                                         ASTRA_INPUT_KEY_PHYSICAL,
                                         ASTRA_INPUT_FLAG_DOWN),
            .value = 4u + index,
            .timestamp_ms = 100u + index,
            .device_sequence = (1u << 16) | (index + 1u),
            .host_generation = 7u,
        };
    }
    input_event_count = 3u;
    input_event_status = ASTRA_INPUT_STATUS_OVERFLOW;

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_INPUT_READ_TRY;
    registers[1] = input_handle;
    registers[2] = 4u;
    registers[3] = 2u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(next->data[1] == 0u && input_event_count == 3u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_INPUT_READ_TRY;
    registers[1] = input_handle;
    registers[2] = user_events;
    registers[3] = 2u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 2u);
    assert(next->data[2] == ASTRA_INPUT_READ_OVERFLOW);
    assert(input_event_count == 1u);
    assert(input_overflow_acks == 1u);
    assert((input_event_status & ASTRA_INPUT_STATUS_OVERFLOW) == 0u);
    assert(kernel_user_copy_from_asm(received, user_events,
                                     2u * sizeof(received[0])) ==
           KERNEL_USER_COPY_OK);
    assert(received[0].value == 4u && received[1].value == 5u);
    assert(received[0].host_generation == 7u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_INPUT_READ_TRY;
    registers[1] = input_handle;
    registers[2] = user_events;
    registers[3] = ASTRA_INPUT_READ_BATCH_MAX + 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(input_event_count == 1u);
}

static void test_private_irq_qualification_control(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    const uint32_t source_mask = 1u << 5;
    KernelCpuContext *next;
    KernelIrqPoolStats irq_stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t irq_handle;
    uint32_t authorized;
    uint32_t completed;
    uint32_t reset_completed;

    initialize_test();
    irq_test_source = 5u;
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_qualification_authorize(
               process_id, 1u << 31) == KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_qualification_authorize(
               process_id, source_mask) == KERNEL_PROCESS_OK);
    assert(kernel_process_qualification_status(
        process_id, &authorized, &completed));
    assert(authorized == source_mask && completed == 0u);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_QUERY_IRQS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == source_mask);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_BIND_IRQ;
    registers[2] = 4u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_BIND_IRQ;
    registers[2] = irq_test_source;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    irq_handle = next->data[1];
    assert(irq_handle != KERNEL_HANDLE_INVALID);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_PREPARE_IRQ;
    registers[2] = irq_test_source;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(qualification_prepare_count == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_CONSUME_IRQ;
    registers[2] = irq_test_source;
    registers[3] = 0xa568c030u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(qualification_consume_count == 1u);
    assert(qualification_consumed_status == 0xa568c030u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_COMPLETE_IRQS;
    registers[2] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    registers[2] = source_mask;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_process_qualification_status(
        process_id, &authorized, &completed));
    assert(completed == source_mask);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = irq_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_IRQ_ARM;
    registers[1] = irq_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);
    assert(kernel_irq_pool_stats(&irq_stats));
    assert(irq_stats.live_endpoints == 1u);
    assert(irq_stats.revoking_endpoints == 1u);
    assert(kernel_process_maintenance_pending());
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(irq_reset_schedule_count == 1u);
    assert(kernel_irq_service_revocations(
               KERNEL_WORKER_DEVICE_RESET_BATCH, &reset_completed) ==
           KERNEL_IRQ_OK);
    assert(reset_completed == 1u);
    assert(kernel_irq_pool_stats(&irq_stats));
    assert(irq_stats.live_endpoints == 0u);
    assert(irq_stats.revoking_endpoints == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = KERNEL_QUALIFICATION_COMMAND_QUERY_IRQS;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
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

static void test_prestart_timer_cannot_consume_published_thread(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u, 0x4eu, 0x71u};
    KernelCpuContext *next;
    KernelProcessSnapshot process;
    uint32_t process_id;
    uint32_t thread_id;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    interrupts_enabled = true;
    disable_hook_process_slot = 0u;
    disable_hook_thread_count = 1u;
    timer_on_disable_armed = true;
    assert(kernel_process_create_thread(
               process_id, 2u, 0u, KERNEL_THREAD_PRIORITY_NORMAL,
               &thread_id) == KERNEL_PROCESS_OK);
    assert(timer_on_disable_fired);
    assert(!kernel_process_active());
    assert(kernel_process_snapshot(0u, &process));
    assert(process.thread_count == 2u);
    assert(process.live_threads == 2u);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    assert(kernel_process_active());
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
    /* Two threads, each committing a whole stack. Derived from the
     * constant so a change to the stack size is not a test edit. */
    assert(process.user_stack_pages == 2u * TEST_STACK_PAGES);
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
    assert(process.user_stack_pages == TEST_STACK_PAGES);
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
    assert(process.user_stack_pages == TEST_STACK_PAGES);

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
    uint32_t attempts_before_fault;

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

    attempts_before_fault = allocation_attempt_total();
    memset(registers, 0, sizeof(registers));
    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 2u,
               0x60000000u);
    assert(kernel_process_on_fault(registers,
                                   KERNEL_PROCESS_STACK_TOP - 32u, frame,
                                   &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(allocation_attempt_total() == attempts_before_fault);
    assert(kernel_memory_release_owner(pressure_owner, &released) ==
           KERNEL_MEMORY_OK);
    assert(released == pressure_frames);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
    assert(final.emergency_available_frames ==
           baseline.emergency_available_frames);
    assert(kernel_allocation_valid());
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
    /*
     * The waiter is told what killed it, not zero. Zero is what a program
     * returns when it succeeded, and a waiter that reads only the status --
     * which is the whole reason the status exists -- used to be told that a
     * process which had crashed had finished cleanly.
     */
    assert(next->data[2] == (uint32_t)ASTRA_STATUS_FAULTED);
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

static void test_message_port_syscall_atomicity_and_cleanup(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    struct TestMessage {
        AstraMessageHeader header;
        uint32_t payload[2];
    } message;
    const uint32_t input_message_address = KERNEL_PROCESS_STACK_TOP - 1024u;
    const uint32_t output_message_address = KERNEL_PROCESS_STACK_TOP - 768u;
    const uint32_t input_handles_address = KERNEL_PROCESS_STACK_TOP - 512u;
    const uint32_t output_handles_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 1536u;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelSchedulerStats stats;
    KernelHandleTransferStats transfer_stats;
    KernelPortPoolStats port_stats;
    KernelCpuContext *next;
    KernelThread *port_thread;
    KernelHandle attached;
    KernelHandle imported;
    struct TestMessage received;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t receive_handle;
    uint32_t send_handle;
    uint32_t event_handle;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_CREATE;
    registers[1] = 1u;
    registers[2] = sizeof(message);
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    receive_handle = next->data[1];
    send_handle = next->data[2];
    assert(receive_handle != KERNEL_HANDLE_INVALID);
    assert(send_handle != KERNEL_HANDLE_INVALID);
    assert(receive_handle != send_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message);
    registers[4] = output_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_WOULD_BLOCK);
    assert(next->data[1] == 0u);
    assert(next->data[2] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_TRANSFER_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    event_handle = next->data[1];

    memset(&message, 0, sizeof(message));
    message.header.total_size = sizeof(message);
    message.header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.header.protocol = 0x4b375450u;
    message.header.protocol_version = 1u;
    message.header.operation = 7u;
    message.header.transaction_id = 0x12345678u;
    message.payload[0] = 0x11223344u;
    message.payload[1] = 0x55667788u;
    attached = event_handle;
    assert(kernel_user_copy_to_asm(input_message_address, &message,
                                   sizeof(message)) == KERNEL_USER_COPY_OK);
    assert(kernel_user_copy_to_asm(input_handles_address, &attached,
                                   sizeof(attached)) == KERNEL_USER_COPY_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    registers[4] = input_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_process_test_handle_count(process_id) == 4u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = event_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    /* Required sizes are reported without reserving destination slots. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message) - 1u;
    registers[4] = output_handles_address;
    registers[5] = 0u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BUFFER_TOO_SMALL);
    assert(next->data[1] == sizeof(message));
    assert(next->data[2] == 1u);
    assert(kernel_process_test_handle_count(process_id) == 4u);

    /* Either copy may fault; neither publishes the queued authority. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = 0u;
    registers[3] = sizeof(message);
    registers[4] = output_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(kernel_process_test_handle_count(process_id) == 4u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message);
    registers[4] = 0u;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(kernel_process_test_handle_count(process_id) == 4u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = receive_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message);
    registers[4] = output_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == sizeof(message));
    assert(next->data[2] == 1u);
    assert(kernel_process_test_handle_count(process_id) == 5u);
    assert(kernel_user_copy_from_asm(&received, output_message_address,
                                     sizeof(received)) ==
           KERNEL_USER_COPY_OK);
    assert(memcmp(&received, &message, sizeof(message)) == 0);
    assert(kernel_user_copy_from_asm(&imported, output_handles_address,
                                     sizeof(imported)) ==
           KERNEL_USER_COPY_OK);
    assert(imported != event_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_SIGNAL;
    registers[1] = imported;
    registers[2] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = imported;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    /* A full queue is checked before attached handles are touched. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EVENT_CREATE;
    registers[2] = TEST_TRANSFER_SYNC_RIGHTS;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    event_handle = next->data[1];
    attached = event_handle;
    assert(kernel_user_copy_to_asm(input_handles_address, &attached,
                                   sizeof(attached)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    registers[4] = input_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_WOULD_BLOCK);
    assert(kernel_process_test_handle_count(process_id) == 5u);
    port_thread = kernel_thread_at(0u);
    assert(port_thread != NULL);
    assert(port_thread->port_probe_handle == send_handle);
    assert(port_thread->port_probe_sequence != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message);
    registers[5] = 0u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(port_thread->port_probe_handle == KERNEL_HANDLE_INVALID);
    assert(port_thread->port_probe_sequence == 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = send_handle;
    registers[2] = ASTRA_DEADLINE_NONE_HI;
    registers[3] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    registers[4] = input_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(message);
    registers[4] = output_handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&imported, output_handles_address,
                                     sizeof(imported)) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = imported;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);

    message.header.total_size = sizeof(message) - 1u;
    assert(kernel_user_copy_to_asm(input_message_address, &message,
                                   sizeof(message)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = receive_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    message.header.total_size = sizeof(message);
    assert(kernel_user_copy_to_asm(input_message_address, &message,
                                   sizeof(message)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_PEER_DEAD);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = send_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    assert(kernel_process_stats(&stats));
    assert(kernel_port_pool_stats(&port_stats));
    assert(kernel_handle_transfer_stats(&transfer_stats));
    assert(stats.port_created == 1u);
    assert(stats.port_active == 0u);
    assert(stats.port_sends == 3u);
    assert(stats.port_receives == 3u);
    assert(stats.port_send_would_block == 1u);
    assert(stats.port_receive_buffer_too_small ==
           port_stats.receive_buffer_too_small);
    assert(stats.port_receive_buffer_too_small != 0u);
    assert(stats.port_queued_messages == 0u);
    assert(stats.port_queued_bytes == 0u);
    assert(stats.port_queued_handles == 0u);
    assert(stats.handle_transfers == 2u);
    assert(stats.handle_transfer_imports == transfer_stats.committed_imports);
    assert(stats.handle_transfer_imports != 0u);
    assert(stats.handle_transfer_import_rollbacks ==
           transfer_stats.import_rollbacks);
    assert(stats.handle_transfer_import_rollbacks != 0u);
    assert(stats.handle_transfer_live_detached ==
           transfer_stats.live_detached);
    assert(stats.handle_transfer_live_detached == 0u);
    assert(stats.handle_transfer_max_detached == 1u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

#define MALFORMED_SYSCALL_SEED 0x68c0304bu
#define MALFORMED_SYSCALL_CASES 4096u
#define MALFORMED_MESSAGE_CASES 1024u

typedef struct MalformedSyscallCase {
    uint32_t syscall;
    uint32_t expected_result;
} MalformedSyscallCase;

static uint32_t malformed_random(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void malformed_check(bool condition, const char *phase,
                            uint32_t case_index, uint32_t syscall,
                            uint32_t observed, uint32_t expected)
{
    if (!condition) {
        fprintf(stderr,
                "malformed corpus failure: seed=%08x phase=%s case=%u "
                "syscall=%08x observed=%08x expected=%08x\n",
                MALFORMED_SYSCALL_SEED, phase, case_index, syscall,
                observed, expected);
        fflush(stderr);
    }
    assert(condition);
}

static void test_malformed_syscall_and_message_corpus(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    static const MalformedSyscallCase cases[] = {
        {UINT32_MAX, ASTRA_SYSCALL_BAD_SYSCALL},
        {ASTRA_SYSCALL_CONSOLE_INFO, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_CONSOLE_WRITE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_CLOSE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_HANDLE_DUPLICATE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_THREAD_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_EVENT_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_SEMAPHORE_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_WAIT_ONE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_WAIT_MULTIPLE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_SIGNAL, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_EVENT_RESET, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_CANCEL_WAIT, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_TIMER_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_TIMER_SET, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_TIMER_CANCEL, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_PORT_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_PORT_SEND_TRY, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_PORT_RECEIVE_TRY, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_AREA_CREATE, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_AREA_MAP, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_AREA_UNMAP, ASTRA_SYSCALL_INVALID_ARGUMENT},
        {ASTRA_SYSCALL_RING_CREATE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_RING_NOTIFY, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_READ, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_ACK, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_ARM, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_MASK, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_RECOVER, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_IRQ_REVOKE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_DEVICE_QUERY, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_DEVICE_RESET, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_DEVICE_REVOKE, ASTRA_SYSCALL_INVALID_HANDLE},
        {ASTRA_SYSCALL_INPUT_READ_TRY, ASTRA_SYSCALL_INVALID_HANDLE},
    };
    static const uint32_t bad_message_addresses[] = {
        0u,
        1u,
        UINT32_MAX - 7u,
        KERNEL_PROCESS_STACK_BASE - 8u,
        KERNEL_PROCESS_STACK_TOP - 8u,
    };
    const uint32_t message_address = KERNEL_PROCESS_STACK_TOP - 2048u;
    const uint32_t handles_address = KERNEL_PROCESS_STACK_TOP - 1536u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 3072u;
    KernelMemoryStats system_baseline;
    KernelMemoryStats process_baseline;
    KernelMemoryStats current_memory;
    TestAllocationBaseline allocation_baseline;
    KernelSchedulerStats stats;
    KernelPortPoolStats port_stats;
    KernelCpuContext *next;
    KernelProcessStatus process_status;
    AstraMessageHeader header;
    uint8_t message[ASTRA_MESSAGE_SIZE_MAX];
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t receive_handle;
    uint32_t send_handle;
    uint32_t state = MALFORMED_SYSCALL_SEED;
    uint32_t handle_baseline;
    uint32_t invalid_handle = KERNEL_HANDLE_INVALID;

    initialize_test();
    assert(kernel_memory_stats(&system_baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_memory_stats(&process_baseline));
    capture_allocation_baseline(&allocation_baseline);
    handle_baseline = kernel_process_test_handle_count(process_id);

    for (uint32_t case_index = 0u;
         case_index < MALFORMED_SYSCALL_CASES; ++case_index) {
        const MalformedSyscallCase *test_case =
            &cases[case_index % (sizeof(cases) / sizeof(cases[0]))];
        uint32_t syscall = test_case->syscall;

        for (uint32_t reg = 0u; reg < KERNEL_CONTEXT_REGISTER_COUNT; ++reg)
            registers[reg] = malformed_random(&state);
        if (syscall == UINT32_MAX)
            syscall = malformed_random(&state) | 0x80000000u;
        registers[0] = syscall;
        switch (test_case->syscall) {
        case ASTRA_SYSCALL_CLOSE:
        case ASTRA_SYSCALL_HANDLE_DUPLICATE:
        case ASTRA_SYSCALL_SIGNAL:
        case ASTRA_SYSCALL_EVENT_RESET:
        case ASTRA_SYSCALL_CANCEL_WAIT:
        case ASTRA_SYSCALL_TIMER_CANCEL:
        case ASTRA_SYSCALL_PORT_SEND_TRY:
        case ASTRA_SYSCALL_PORT_RECEIVE_TRY:
        case ASTRA_SYSCALL_RING_CREATE:
        case ASTRA_SYSCALL_RING_NOTIFY:
        case ASTRA_SYSCALL_IRQ_READ:
        case ASTRA_SYSCALL_IRQ_ACK:
        case ASTRA_SYSCALL_IRQ_ARM:
        case ASTRA_SYSCALL_IRQ_MASK:
        case ASTRA_SYSCALL_IRQ_RECOVER:
        case ASTRA_SYSCALL_IRQ_REVOKE:
        case ASTRA_SYSCALL_DEVICE_QUERY:
        case ASTRA_SYSCALL_DEVICE_RESET:
        case ASTRA_SYSCALL_DEVICE_REVOKE:
        case ASTRA_SYSCALL_INPUT_READ_TRY:
            registers[1] = KERNEL_HANDLE_INVALID;
            break;
        case ASTRA_SYSCALL_THREAD_CREATE:
            registers[1] = KERNEL_PROCESS_CODE_BASE + 1u;
            break;
        case ASTRA_SYSCALL_EVENT_CREATE:
            registers[2] = 1u << 31;
            break;
        case ASTRA_SYSCALL_SEMAPHORE_CREATE:
            registers[3] = 1u << 31;
            break;
        case ASTRA_SYSCALL_WAIT_ONE:
            registers[1] = KERNEL_HANDLE_INVALID;
            registers[2] = ASTRA_DEADLINE_NONE_HI;
            registers[3] = ASTRA_DEADLINE_NONE_LO;
            break;
        case ASTRA_SYSCALL_WAIT_MULTIPLE:
            registers[2] = 0u;
            break;
        case ASTRA_SYSCALL_TIMER_CREATE:
            registers[1] = 1u << 31;
            break;
        case ASTRA_SYSCALL_TIMER_SET:
            registers[1] = KERNEL_HANDLE_INVALID;
            registers[2] = ASTRA_DEADLINE_NONE_HI;
            registers[3] = ASTRA_DEADLINE_NONE_LO;
            break;
        case ASTRA_SYSCALL_PORT_CREATE:
            registers[1] = 0u;
            break;
        case ASTRA_SYSCALL_AREA_CREATE:
            registers[2] = 1u << 31;
            break;
        case ASTRA_SYSCALL_AREA_MAP:
            registers[2] = 0u;
            break;
        case ASTRA_SYSCALL_AREA_UNMAP:
            registers[1] = 0u;
            break;
        default:
            break;
        }

        process_status = kernel_process_on_syscall(
            registers, user_stack, frame, &next);
        malformed_check(process_status == KERNEL_PROCESS_OK, "syscall-status",
                        case_index, syscall, (uint32_t)process_status,
                        (uint32_t)KERNEL_PROCESS_OK);
        malformed_check(next != NULL, "syscall-context", case_index,
                        syscall, next == NULL ? 0u : 1u, 1u);
        malformed_check(next->data[0] == test_case->expected_result,
                        "syscall-result", case_index, syscall,
                        next->data[0], test_case->expected_result);

        if ((case_index & 63u) == 63u) {
            assert(kernel_process_test_handle_count(process_id) ==
                   handle_baseline);
            assert(kernel_memory_stats(&current_memory));
            assert(current_memory.free_frames ==
                   process_baseline.free_frames);
            assert_allocation_baseline(&allocation_baseline);
            assert(kernel_sync_pool_valid());
            assert(kernel_thread_pool_valid());
            assert(kernel_port_pool_valid());
            assert(kernel_area_pool_valid());
            assert(kernel_ring_pool_valid());
            assert(kernel_irq_pool_valid());
            assert(kernel_handle_transfer_pool_valid());
        }
    }

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_CREATE;
    registers[1] = ASTRA_PORT_MESSAGES_MAX;
    registers[2] = ASTRA_PORT_BYTES_MAX;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    receive_handle = next->data[1];
    send_handle = next->data[2];
    handle_baseline = kernel_process_test_handle_count(process_id);
    capture_allocation_baseline(&allocation_baseline);
    assert(kernel_memory_stats(&process_baseline));

    for (uint32_t case_index = 0u;
         case_index < MALFORMED_MESSAGE_CASES; ++case_index) {
        uint32_t message_size = ASTRA_MESSAGE_HEADER_SIZE +
            malformed_random(&state) % (ASTRA_MESSAGE_INLINE_MAX + 1u);

        for (uint32_t offset = 0u; offset < message_size; ++offset)
            message[offset] = (uint8_t)malformed_random(&state);
        memset(&header, 0, sizeof(header));
        header.total_size = message_size;
        header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        switch (case_index & 3u) {
        case 0u:
            header.total_size = message_size ^ 1u;
            break;
        case 1u:
            header.header_size = ASTRA_MESSAGE_HEADER_SIZE + 4u;
            break;
        case 2u:
            header.flags = (uint16_t)(malformed_random(&state) | 1u);
            break;
        default:
            header.reserved = (uint16_t)(malformed_random(&state) | 1u);
            break;
        }
        memcpy(message, &header, sizeof(header));
        assert(kernel_user_copy_to_asm(message_address, message,
                                       message_size) ==
               KERNEL_USER_COPY_OK);
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
        registers[1] = send_handle;
        registers[2] = message_address;
        registers[3] = message_size;
        process_status = kernel_process_on_syscall(
            registers, user_stack, frame, &next);
        malformed_check(process_status == KERNEL_PROCESS_OK,
                        "message-status", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY,
                        (uint32_t)process_status,
                        (uint32_t)KERNEL_PROCESS_OK);
        malformed_check(next != NULL, "message-context", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY,
                        next == NULL ? 0u : 1u, 1u);
        malformed_check(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT,
                        "message-result", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY, next->data[0],
                        ASTRA_SYSCALL_INVALID_ARGUMENT);
    }

    memset(message, 0, ASTRA_MESSAGE_HEADER_SIZE);
    memset(&header, 0, sizeof(header));
    header.total_size = ASTRA_MESSAGE_HEADER_SIZE;
    header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    memcpy(message, &header, sizeof(header));
    assert(kernel_user_copy_to_asm(message_address, message,
                                   ASTRA_MESSAGE_HEADER_SIZE) ==
           KERNEL_USER_COPY_OK);
    for (uint32_t case_index = 0u;
         case_index < sizeof(bad_message_addresses) /
                          sizeof(bad_message_addresses[0]); ++case_index) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
        registers[1] = send_handle;
        registers[2] = bad_message_addresses[case_index];
        registers[3] = ASTRA_MESSAGE_HEADER_SIZE;
        process_status = kernel_process_on_syscall(
            registers, user_stack, frame, &next);
        malformed_check(process_status == KERNEL_PROCESS_OK,
                        "message-address-status", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY,
                        (uint32_t)process_status,
                        (uint32_t)KERNEL_PROCESS_OK);
        malformed_check(next != NULL, "message-address-context", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY,
                        next == NULL ? 0u : 1u, 1u);
        malformed_check(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS,
                        "message-address-result", case_index,
                        ASTRA_SYSCALL_PORT_SEND_TRY, next->data[0],
                        ASTRA_SYSCALL_BAD_ADDRESS);
    }

    assert(kernel_user_copy_to_asm(handles_address, &invalid_handle,
                                   sizeof(invalid_handle)) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = message_address;
    registers[3] = ASTRA_MESSAGE_HEADER_SIZE;
    registers[4] = handles_address;
    registers[5] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    registers[4] = handles_address + 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    registers[4] = handles_address;
    registers[5] = ASTRA_MESSAGE_HANDLES_MAX + 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    assert(kernel_process_test_handle_count(process_id) == handle_baseline);
    assert(kernel_memory_stats(&current_memory));
    assert(current_memory.free_frames == process_baseline.free_frames);
    assert_allocation_baseline(&allocation_baseline);
    assert(kernel_process_stats(&stats));
    assert(kernel_port_pool_stats(&port_stats));
    assert(stats.port_queued_messages == 0u);
    assert(stats.port_queued_bytes == 0u);
    assert(stats.port_queued_handles == 0u);
    assert(port_stats.active_ports == 1u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = receive_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    registers[1] = send_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&current_memory));
    assert(current_memory.free_frames == system_baseline.free_frames);
}

static void test_shared_area_and_bulk_ring_syscalls(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t area_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;
    const uint32_t reduced_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    AstraBulkRingHeader *header;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelSchedulerStats stats;
    KernelRingPoolStats ring_stats;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t area_handle;
    uint32_t reduced_handle;
    uint32_t producer_handle;
    uint32_t consumer_handle;
    uint32_t area_base;
    uint32_t physical;
    uint32_t timer_arms;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_CREATE;
    registers[1] = 2u * KERNEL_PAGE_SIZE;
    registers[2] = area_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = area_handle;
    registers[2] = reduced_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    reduced_handle = next->data[1];
    assert(reduced_handle != area_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = reduced_handle;
    registers[2] = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = reduced_handle;
    registers[2] = ASTRA_AREA_MAP_READ;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_base = next->data[1];
    assert(area_base == KERNEL_VM_AREA_BASE);
    assert(next->data[2] == 2u * KERNEL_PAGE_SIZE);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_UNMAP;
    registers[1] = area_base;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = area_handle;
    registers[2] = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_base = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_CREATE;
    registers[1] = area_handle;
    registers[2] = 0u;
    registers[3] = 16u;
    registers[4] = 4u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    producer_handle = next->data[1];
    consumer_handle = next->data[2];
    assert(producer_handle != consumer_handle);
    assert(kernel_vm_test_translate_current(area_base, true, &physical));
    header = (AstraBulkRingHeader *)(void *)&physical_memory[
        physical - 0x02000000u];
    assert(header->magic == ASTRA_BULK_RING_MAGIC);
    assert(header->element_size == 16u && header->capacity == 4u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = producer_handle;
    registers[2] = ASTRA_RIGHT_WRITE | ASTRA_RIGHT_SIGNAL;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_NOTIFY;
    registers[1] = producer_handle;
    registers[2] = 2u;
    registers[4] = ASTRA_BULK_RING_CONSUMER;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_NOTIFY;
    registers[1] = producer_handle;
    registers[2] = 2u;
    registers[4] = ASTRA_BULK_RING_PRODUCER;
    timer_arms = timer_arm_count;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 2u && next->data[2] == 0u);
    assert(timer_arm_count == timer_arms);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = consumer_handle;
    registers[2] = 0u;
    registers[3] = 0u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_NOTIFY;
    registers[1] = consumer_handle;
    registers[2] = 2u;
    registers[4] = ASTRA_BULK_RING_CONSUMER;
    timer_arms = timer_arm_count;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 2u && next->data[2] == 2u);
    assert(timer_arm_count == timer_arms);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = consumer_handle;
    registers[2] = 0u;
    registers[3] = 0u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_TIMED_OUT);

    for (uint32_t close = 0u; close < 4u; ++close) {
        uint32_t handle = close == 0u ? producer_handle :
            (close == 1u ? consumer_handle :
             (close == 2u ? reduced_handle : area_handle));

        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_CLOSE;
        registers[1] = handle;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    assert(kernel_process_stats(&stats));
    assert(kernel_ring_pool_stats(&ring_stats));
    assert(stats.area_created == 1u);
    assert(stats.area_active == 0u);
    assert(stats.area_mappings == 0u);
    assert(stats.area_committed_pages == 0u);
    assert(stats.ring_created == 1u);
    assert(stats.ring_active == 0u);
    assert(stats.ring_notifications == 2u);
    assert(stats.ring_peer_closures == ring_stats.peer_closures);
    assert(stats.ring_peer_closures != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

static void test_area_publication_rolls_back_when_handle_table_full(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t area_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelAreaPoolStats before_area;
    KernelAreaPoolStats after_area;
    KernelMemoryStats initial_memory;
    KernelMemoryStats before_failure;
    KernelMemoryStats after_failure;
    KernelMemoryStats final_memory;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t area_handle;

    initialize_test();
    assert(kernel_memory_stats(&initial_memory));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_AREA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = area_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_handle = next->data[1];
    while (kernel_process_test_handle_count(process_id) <
           KERNEL_HANDLE_MAX_ENTRIES) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
        registers[1] = area_handle;
        registers[2] = ASTRA_RIGHT_READ;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    assert(kernel_area_pool_stats(&before_area));
    assert(before_area.active_areas == 1u);
    assert(before_area.committed_pages == 1u);
    assert(kernel_memory_stats(&before_failure));

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_CREATE;
    registers[1] = 2u * KERNEL_PAGE_SIZE;
    registers[2] = area_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_RESOURCE_LIMIT);
    assert(kernel_process_test_handle_count(process_id) ==
           KERNEL_HANDLE_MAX_ENTRIES);
    assert(kernel_area_pool_stats(&after_area));
    assert(after_area.created_areas == before_area.created_areas + 1u);
    assert(after_area.active_areas == before_area.active_areas);
    assert(after_area.closing_areas == before_area.closing_areas);
    assert(after_area.committed_pages == before_area.committed_pages);
    assert(after_area.active_mappings == before_area.active_mappings);
    assert(kernel_memory_stats(&after_failure));
    assert(after_failure.free_frames == before_failure.free_frames);
    assert(after_failure.owner_slots_used ==
           before_failure.owner_slots_used);
    assert(kernel_area_pool_valid());

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final_memory));
    assert(final_memory.free_frames == initial_memory.free_frames);
}

static void test_area_and_ring_endpoint_transfer_over_port(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    const uint32_t area_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
        ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER;
    const uint32_t shared_rights =
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER;
    const uint32_t input_message_address = KERNEL_PROCESS_STACK_TOP - 1024u;
    const uint32_t output_message_address = KERNEL_PROCESS_STACK_TOP - 768u;
    const uint32_t input_handles_address = KERNEL_PROCESS_STACK_TOP - 512u;
    const uint32_t output_handles_address = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 1536u;
    AstraMessageHeader message;
    AstraMessageHeader received;
    KernelHandle attached[2];
    KernelHandle imported[2];
    KernelHandleTransferStats transfer_stats;
    KernelMemoryStats baseline;
    KernelMemoryStats final;
    KernelSchedulerStats stats;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t area_handle;
    uint32_t shared_handle;
    uint32_t producer_handle;
    uint32_t consumer_handle;
    uint32_t receive_handle;
    uint32_t send_handle;
    uint32_t area_base;

    initialize_test();
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_AREA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = area_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_HANDLE_DUPLICATE;
    registers[1] = area_handle;
    registers[2] = shared_rights;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    shared_handle = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_CREATE;
    registers[1] = area_handle;
    registers[2] = 0u;
    registers[3] = 16u;
    registers[4] = 4u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    producer_handle = next->data[1];
    consumer_handle = next->data[2];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_CREATE;
    registers[1] = 1u;
    registers[2] = sizeof(message);
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    receive_handle = next->data[1];
    send_handle = next->data[2];

    memset(&message, 0, sizeof(message));
    message.total_size = sizeof(message);
    message.header_size = ASTRA_MESSAGE_HEADER_SIZE;
    message.protocol = 0x4b384950u;
    message.protocol_version = 1u;
    message.operation = 1u;
    message.transaction_id = 0x12345678u;
    attached[0] = shared_handle;
    attached[1] = consumer_handle;
    assert(kernel_user_copy_to_asm(input_message_address, &message,
                                   sizeof(message)) == KERNEL_USER_COPY_OK);
    assert(kernel_user_copy_to_asm(input_handles_address, attached,
                                   sizeof(attached)) == KERNEL_USER_COPY_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_SEND_TRY;
    registers[1] = send_handle;
    registers[2] = input_message_address;
    registers[3] = sizeof(message);
    registers[4] = input_handles_address;
    registers[5] = 2u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_process_test_handle_count(process_id) == 6u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = shared_handle;
    registers[2] = ASTRA_AREA_MAP_READ;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_ONE;
    registers[1] = consumer_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PORT_RECEIVE_TRY;
    registers[1] = receive_handle;
    registers[2] = output_message_address;
    registers[3] = sizeof(received);
    registers[4] = output_handles_address;
    registers[5] = 2u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == sizeof(received));
    assert(next->data[2] == 2u);
    assert(kernel_process_test_handle_count(process_id) == 8u);
    assert(kernel_user_copy_from_asm(&received, output_message_address,
                                     sizeof(received)) ==
           KERNEL_USER_COPY_OK);
    assert(memcmp(&received, &message, sizeof(message)) == 0);
    assert(kernel_user_copy_from_asm(imported, output_handles_address,
                                     sizeof(imported)) ==
           KERNEL_USER_COPY_OK);
    assert(imported[0] != shared_handle);
    assert(imported[1] != consumer_handle);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = imported[0];
    registers[2] = ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_CREATE;
    registers[1] = imported[0];
    registers[2] = 512u;
    registers[3] = 16u;
    registers[4] = 4u;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_MAP;
    registers[1] = imported[0];
    registers[2] = ASTRA_AREA_MAP_READ;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    area_base = next->data[1];

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_NOTIFY;
    registers[1] = producer_handle;
    registers[2] = 1u;
    registers[4] = ASTRA_BULK_RING_PRODUCER;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_to_asm(input_handles_address, &imported[1],
                                   sizeof(imported[1])) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_WAIT_MULTIPLE;
    registers[1] = input_handles_address;
    registers[2] = 1u;
    registers[3] = ASTRA_DEADLINE_NONE_HI;
    registers[4] = ASTRA_DEADLINE_NONE_LO;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u && next->data[2] == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_RING_NOTIFY;
    registers[1] = imported[1];
    registers[2] = 1u;
    registers[4] = ASTRA_BULK_RING_CONSUMER;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_AREA_UNMAP;
    registers[1] = area_base;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    for (uint32_t close = 0u; close < 6u; ++close) {
        uint32_t handle = close == 0u ? imported[1] :
            (close == 1u ? producer_handle :
             (close == 2u ? imported[0] :
              (close == 3u ? area_handle :
               (close == 4u ? receive_handle : send_handle))));

        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_CLOSE;
        registers[1] = handle;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
    }
    assert(kernel_process_stats(&stats));
    assert(kernel_handle_transfer_stats(&transfer_stats));
    assert(stats.handle_transfers == 1u);
    assert(stats.handle_transfer_imports == transfer_stats.committed_imports);
    assert(stats.handle_transfer_imports != 0u);
    assert(stats.handle_transfer_live_detached ==
           transfer_stats.live_detached);
    assert(stats.handle_transfer_live_detached == 0u);
    assert(stats.handle_transfer_max_detached == 2u);
    assert(transfer_stats.committed_exports == 1u);
    assert(transfer_stats.committed_imports == 1u);
    assert(transfer_stats.live_detached == 0u);
    assert(stats.area_active == 0u && stats.area_mappings == 0u);
    assert(stats.ring_active == 0u);
    assert(stats.port_active == 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_EXIT;
    assert(kernel_process_on_syscall(registers, user_stack, frame,
                                     &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&final));
    assert(final.free_frames == baseline.free_frames);
}

/*
 * Executable loading. The strongest available proof that the loader is
 * transactional is that a failure at any allocation site returns the free
 * frame count to exactly its pre-load value, so the sweep below drives the
 * global-Nth selector across every site the load touches.
 */
#define LOADER_TEXT_VADDR 0x00100000u
#define LOADER_DATA_VADDR 0x00101000u
#define LOADER_PHOFF 52u

static uint8_t loader_image[3u * KERNEL_PAGE_SIZE];
static uint32_t loader_image_size;

static void loader_put16(uint32_t offset, uint16_t value)
{
    loader_image[offset] = (uint8_t)(value >> 8);
    loader_image[offset + 1u] = (uint8_t)value;
}

static void loader_put32(uint32_t offset, uint32_t value)
{
    loader_image[offset] = (uint8_t)(value >> 24);
    loader_image[offset + 1u] = (uint8_t)(value >> 16);
    loader_image[offset + 2u] = (uint8_t)(value >> 8);
    loader_image[offset + 3u] = (uint8_t)value;
}

/* Read-execute text at 0x100000, read-write data with a BSS tail at 0x101000. */
static void loader_build_image(void)
{
    memset(loader_image, 0, sizeof(loader_image));
    loader_image_size = 3u * KERNEL_PAGE_SIZE;

    loader_image[0] = 0x7fu;
    loader_image[1] = 'E';
    loader_image[2] = 'L';
    loader_image[3] = 'F';
    loader_image[4] = 1u;
    loader_image[5] = 2u;
    loader_image[6] = 1u;

    loader_put16(16u, 2u); /* ET_EXEC */
    loader_put16(18u, 4u); /* EM_68K */
    loader_put32(20u, 1u);
    loader_put32(24u, LOADER_TEXT_VADDR);
    loader_put32(28u, LOADER_PHOFF);
    loader_put16(40u, 52u);
    loader_put16(42u, 32u);
    loader_put16(44u, 2u);

    loader_put32(LOADER_PHOFF + 0u, 1u);
    loader_put32(LOADER_PHOFF + 4u, KERNEL_PAGE_SIZE);
    loader_put32(LOADER_PHOFF + 8u, LOADER_TEXT_VADDR);
    loader_put32(LOADER_PHOFF + 16u, KERNEL_PAGE_SIZE);
    loader_put32(LOADER_PHOFF + 20u, KERNEL_PAGE_SIZE);
    loader_put32(LOADER_PHOFF + 24u, 5u); /* PF_R | PF_X */
    loader_put32(LOADER_PHOFF + 28u, KERNEL_PAGE_SIZE);

    loader_put32(LOADER_PHOFF + 32u + 0u, 1u);
    loader_put32(LOADER_PHOFF + 32u + 4u, 2u * KERNEL_PAGE_SIZE);
    loader_put32(LOADER_PHOFF + 32u + 8u, LOADER_DATA_VADDR);
    loader_put32(LOADER_PHOFF + 32u + 16u, 0x40u);
    loader_put32(LOADER_PHOFF + 32u + 20u, KERNEL_PAGE_SIZE);
    loader_put32(LOADER_PHOFF + 32u + 24u, 6u); /* PF_R | PF_W */
    loader_put32(LOADER_PHOFF + 32u + 28u, KERNEL_PAGE_SIZE);

    /* A NOP at the entry point so the text page holds real instructions. */
    loader_image[KERNEL_PAGE_SIZE] = 0x4eu;
    loader_image[KERNEL_PAGE_SIZE + 1u] = 0x71u;
}

static void test_executable_loading(void)
{
    KernelProcessSnapshot snapshot;
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t process_id = 0u;
    uint32_t frames = 0u;

    loader_build_image();
    initialize_test();
    assert(kernel_memory_stats(&before));

    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(process_id != 0u);
    {
        uint32_t slot;
        bool found = false;

        for (slot = 0u; slot < KERNEL_PROCESS_MAX; ++slot) {
            if (!kernel_process_snapshot(slot, &snapshot) ||
                snapshot.id != process_id)
                continue;
            found = true;
            break;
        }
        assert(found);
    }
    assert(snapshot.live_threads == 1u);
    assert(snapshot.thread_count == 1u);

    /* Text, data, and the startup block are all charged to the process. */
    assert(kernel_memory_owner_frames(process_id, &frames));
    assert(frames >= 3u);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames < before.free_frames);
}

/*
 * The kernel must learn how the firmware-supplied image ended at the moment it
 * ends: nothing else keeps that outcome once the process record is reclaimed.
 */
/*
 * Objects handed over at launch must be in the process's handle table and
 * named in its startup capability table before it runs, and a grant that
 * cannot be satisfied must leave no trace of the launch at all.
 */
/*
 * Transfer memory is the one thing a block service must own before it can
 * submit anything, so the ceilings and the release path matter more than the
 * happy case: a leaked pinned page is a page the system never gets back.
 */
/*
 * The admission path a filesystem will sit on: geometry, a submitted read
 * against process-owned transfer memory, and a collected completion. Every
 * rejection here is one a filesystem would otherwise discover as corruption.
 */
static void test_block_admission(void)
{
    const uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    const uint32_t user_request = KERNEL_PROCESS_STACK_TOP - 128u;
    const uint32_t user_out = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelProcessBootstrapCapability capabilities[1];
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    AstraDmaBufferInfo buffer;
    AstraBlockLeaseInfo geometry;
    AstraBlockRequest request;
    AstraBlockCompletion completion;
    uint32_t process_id = 0u;
    uint32_t lease_handle;
    uint32_t block_request;

    loader_build_image();
    initialize_test();
    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = ASTRA_CAPABILITY_BLOCK_DEVICE;
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = ASTRA_DEVICE_ID_BLOCK0;
    capabilities[0].rights = KERNEL_DEVICE_RIGHTS;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);

    /* The lease is the third handle: self process, self thread, then it. */
    lease_handle = 0u;
    {
        AstraStartupCapability table[4];

        assert(kernel_user_copy_from_asm(
                   table, KERNEL_VM_USER_MIN + ASTRA_STARTUP_INFO_SIZE,
                   sizeof(table)) == KERNEL_USER_COPY_OK);
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (astra_capability_name_equal(table[index].name,
                                            ASTRA_CAPABILITY_BLOCK_DEVICE))
                lease_handle = table[index].handle;
        }
    }
    assert(lease_handle != 0u);

    /* Geometry is what the transport reports, not what the caller assumes. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_QUERY;
    registers[1] = lease_handle;
    registers[2] = user_out;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&geometry, user_out, sizeof(geometry)) ==
           KERNEL_USER_COPY_OK);
    assert(geometry.size == ASTRA_BLOCK_LEASE_INFO_SIZE);
    assert(geometry.sector_bytes == ASTRA_BLOCK_SECTOR_BYTES);
    assert(geometry.sector_count == 2048u);
    assert(geometry.max_transfer_sectors == 16u);
    assert((geometry.capabilities & ASTRA_BLOCK_CAP_READ) != 0u);

    /* Transfer memory, then one read of the first sector. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&buffer, user_info, sizeof(buffer)) ==
           KERNEL_USER_COPY_OK);

    memset(&request, 0, sizeof(request));
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.operation = ASTRA_BLOCK_OP_READ;
    request.buffer = buffer.handle;
    request.sectors = 1u;
    request.lba = 0u;
    assert(kernel_user_copy_to_asm(user_request, &request,
                                   sizeof(request)) == KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_SUBMIT;
    registers[1] = lease_handle;
    registers[2] = user_request;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    block_request = next->data[1];
    assert(block_request != 0u);
    assert(block_submit_calls == 1u);
    assert(block_submitted_operation == BLOCK_OP_READ);
    assert(block_submitted_sectors == 1u);
    /* The device is handed a physical address the caller never saw. */
    assert(block_submitted_buffer != 0u);
    assert(block_submitted_buffer != buffer.virtual_base);

    /* Until the device answers, collection is a would-block, not an error. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_COLLECT;
    registers[1] = lease_handle;
    registers[2] = user_out;
    registers[3] = block_request;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_WOULD_BLOCK);

    block_complete_request(0u, 1u);
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&completion, user_out,
                                     sizeof(completion)) ==
           KERNEL_USER_COPY_OK);
    assert(completion.size == ASTRA_BLOCK_COMPLETION_SIZE);
    assert(completion.request == block_request);
    assert(completion.status == ASTRA_BLOCK_COMPLETION_OK);
    assert(completion.sectors == 1u);

    /* Read bytes commit exactly once: the same request cannot be collected
     * twice. */
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);
}

/*
 * Fault injection across the admission surface. STORAGE_AND_VFS.md requires
 * this before a filesystem is allowed to depend on the API, and the reason is
 * concrete: every rejection below is a case where a filesystem would otherwise
 * read someone else's bytes, wait forever, or believe a transfer happened.
 */
static void test_block_admission_faults(void)
{
    const uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    const uint32_t user_request = KERNEL_PROCESS_STACK_TOP - 128u;
    const uint32_t user_out = KERNEL_PROCESS_STACK_TOP - 256u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelProcessBootstrapCapability capabilities[1];
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    AstraDmaBufferInfo buffer;
    AstraBlockRequest request;
    AstraBlockCompletion completion;
    uint32_t process_id = 0u;
    uint32_t lease_handle = 0u;
    uint32_t requests[ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE];
    uint32_t index;

    loader_build_image();
    initialize_test();
    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = ASTRA_CAPABILITY_BLOCK_DEVICE;
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = ASTRA_DEVICE_ID_BLOCK0;
    capabilities[0].rights = KERNEL_DEVICE_RIGHTS;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    {
        AstraStartupCapability table[4];

        assert(kernel_user_copy_from_asm(
                   table, KERNEL_VM_USER_MIN + ASTRA_STARTUP_INFO_SIZE,
                   sizeof(table)) == KERNEL_USER_COPY_OK);
        for (index = 0u; index < 4u; ++index) {
            if (astra_capability_name_equal(table[index].name,
                                            ASTRA_CAPABILITY_BLOCK_DEVICE))
                lease_handle = table[index].handle;
        }
    }
    assert(lease_handle != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&buffer, user_info, sizeof(buffer)) ==
           KERNEL_USER_COPY_OK);

    memset(&request, 0, sizeof(request));
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.operation = ASTRA_BLOCK_OP_READ;
    request.buffer = buffer.handle;
    request.sectors = 1u;
    request.lba = 0u;

#define SUBMIT_EXPECT(expected)                                              \
    do {                                                                     \
        assert(kernel_user_copy_to_asm(user_request, &request,               \
                                       sizeof(request)) ==                   \
               KERNEL_USER_COPY_OK);                                         \
        memset(registers, 0, sizeof(registers));                             \
        registers[0] = ASTRA_SYSCALL_BLOCK_SUBMIT;                           \
        registers[1] = lease_handle;                                         \
        registers[2] = user_request;                                         \
        assert(kernel_process_on_syscall(registers, user_stack, frame,       \
                                         &next) == KERNEL_PROCESS_OK);       \
        assert(next->data[0] == (expected));                                 \
    } while (0)

    /* A request the caller did not fill in as this ABI describes. */
    request.size = ASTRA_BLOCK_REQUEST_SIZE - 4u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.size = ASTRA_BLOCK_REQUEST_SIZE;
    request.reserved = 1u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.reserved = 0u;

    /* Transfer memory the caller does not own, or is not memory at all. */
    request.buffer = lease_handle;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_HANDLE);
    request.buffer = buffer.handle + 0x1000u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_HANDLE);
    request.buffer = buffer.handle;

    /* A transfer that would run past the buffer it names. */
    request.sectors = (KERNEL_PAGE_SIZE / ASTRA_BLOCK_SECTOR_BYTES) + 1u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.sectors = 1u;
    request.buffer_offset = KERNEL_PAGE_SIZE;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.buffer_offset = 0u;

    /* Beyond the media, and beyond one transfer. */
    request.lba = 2048u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.lba = 0u;
    request.sectors = 17u;
    SUBMIT_EXPECT(ASTRA_SYSCALL_INVALID_ARGUMENT);
    request.sectors = 1u;

    /* Write protection and absent media are the device's answer, not ours. */
    block_state_flags &= ~(uint32_t)BLOCK_STATE_WRITE_ENABLE;
    request.operation = ASTRA_BLOCK_OP_WRITE;
    SUBMIT_EXPECT(ASTRA_SYSCALL_ACCESS_DENIED);
    request.operation = ASTRA_BLOCK_OP_READ;
    block_state_flags |= BLOCK_STATE_WRITE_ENABLE;

    block_state_flags &= ~(uint32_t)BLOCK_STATE_MEDIA_PRESENT;
    SUBMIT_EXPECT(ASTRA_SYSCALL_PEER_DEAD);
    block_state_flags |= BLOCK_STATE_MEDIA_PRESENT;

    /* A transport that refuses the request reports, and nothing is queued. */
    block_submit_error = BLOCK_ERROR_BAD_ID;
    SUBMIT_EXPECT(ASTRA_SYSCALL_IO_ERROR);
    block_submit_error = 0u;

    /*
     * One buffer carries one transfer at a time, so filling the request queue
     * takes a buffer each. That is the engine refusing to let two transfers
     * share memory it cannot police, and it is worth pinning down here.
     */
    request.buffer = buffer.handle;
    SUBMIT_EXPECT(ASTRA_SYSCALL_OK);
    requests[0] = next->data[1];
    SUBMIT_EXPECT(ASTRA_SYSCALL_RESOURCE_LIMIT);

    for (index = 1u; index < ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE; ++index) {
        AstraDmaBufferInfo extra;

        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_DMA_CREATE;
        registers[1] = KERNEL_PAGE_SIZE;
        registers[2] = user_info;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
        assert(kernel_user_copy_from_asm(&extra, user_info, sizeof(extra)) ==
               KERNEL_USER_COPY_OK);
        request.buffer = extra.handle;
        SUBMIT_EXPECT(ASTRA_SYSCALL_OK);
        requests[index] = next->data[1];
    }
    /* The queue is full: the next request is refused, not queued. */
    request.buffer = buffer.handle;
    SUBMIT_EXPECT(ASTRA_SYSCALL_RESOURCE_LIMIT);

    /* A device error is reported as one, with the transport detail intact. */
    block_submitted_id = requests[0];
    block_complete_request(BLOCK_ERROR_LBA_RANGE, 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_COLLECT;
    registers[1] = lease_handle;
    registers[2] = user_out;
    registers[3] = requests[0];
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&completion, user_out,
                                     sizeof(completion)) ==
           KERNEL_USER_COPY_OK);
    assert(completion.status == ASTRA_BLOCK_COMPLETION_DEVICE_ERROR);

    /* Media that changed under a request is distinct from a device error. */
    block_submitted_id = requests[1];
    block_media_generation += 1u;
    block_complete_request(0u, 1u);
    registers[3] = requests[1];
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_COLLECT;
    registers[1] = lease_handle;
    registers[2] = user_out;
    registers[3] = requests[1];
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&completion, user_out,
                                     sizeof(completion)) ==
           KERNEL_USER_COPY_OK);
    assert(completion.status == ASTRA_BLOCK_COMPLETION_MEDIA_CHANGED);

    /* A reset ends what is in flight, and the service can still be told. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DEVICE_RESET;
    registers[1] = lease_handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_COLLECT;
    registers[1] = lease_handle;
    registers[2] = user_out;
    registers[3] = requests[2];
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&completion, user_out,
                                     sizeof(completion)) ==
           KERNEL_USER_COPY_OK);
    assert(completion.status == ASTRA_BLOCK_COMPLETION_RESET);
    assert(completion.sectors == 0u);

    /* A request that never existed is not a completion. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_COLLECT;
    registers[1] = lease_handle;
    registers[2] = user_out;
    registers[3] = 0xdeadbeefu;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    /* Geometry needs a block lease, not any device handle. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_BLOCK_QUERY;
    registers[1] = 0xbadf00du;
    registers[2] = user_out;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

#undef SUBMIT_EXPECT
}

static void test_dma_transfer_memory(void)
{
    const uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelCpuContext *next;
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    AstraDmaBufferInfo info;
    uint32_t process_id = 0u;
    uint32_t handles[ASTRA_DMA_MAX_BUFFERS_PER_SERVICE];
    uint32_t index;

    loader_build_image();
    initialize_test();
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    assert(kernel_memory_stats(&before));

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&info, user_info, sizeof(info)) ==
           KERNEL_USER_COPY_OK);
    assert(info.size == ASTRA_DMA_BUFFER_INFO_SIZE);
    assert(info.handle != 0u);
    assert(info.byte_size == KERNEL_PAGE_SIZE);
    assert(info.page_count == 1u);
    assert(info.virtual_base >= KERNEL_VM_DMA_BASE);
    assert(info.virtual_base <
           KERNEL_VM_DMA_BASE +
               (KERNEL_VM_DMA_SLOT_COUNT * KERNEL_VM_DMA_SLOT_SIZE));

    /* The mapping is real: the buffer is writable through user copy. */
    {
        static const uint8_t pattern[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
        uint8_t readback[8] = {0u};

        assert(kernel_user_copy_to_asm(info.virtual_base, pattern,
                                       sizeof(pattern)) ==
               KERNEL_USER_COPY_OK);
        assert(kernel_user_copy_from_asm(readback, info.virtual_base,
                                         sizeof(readback)) ==
               KERNEL_USER_COPY_OK);
        assert(memcmp(readback, pattern, sizeof(pattern)) == 0);
    }

    /* Closing the handle returns every pinned page. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = info.handle;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);

    /* Sizes outside one slot are refused before anything is allocated. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = 0u;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    registers[1] = KERNEL_VM_DMA_SLOT_SIZE + 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    /* An unwritable info pointer must not leave the buffer behind. */
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = KERNEL_VM_USER_MAX & ~3u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);

    /* The buffer ceiling is a rejection, not a stall. */
    for (index = 0u; index < ASTRA_DMA_MAX_BUFFERS_PER_SERVICE; ++index) {
        memset(registers, 0, sizeof(registers));
        registers[0] = ASTRA_SYSCALL_DMA_CREATE;
        registers[1] = KERNEL_PAGE_SIZE;
        registers[2] = user_info;
        assert(kernel_process_on_syscall(registers, user_stack, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
        assert(kernel_user_copy_from_asm(&info, user_info, sizeof(info)) ==
               KERNEL_USER_COPY_OK);
        handles[index] = info.handle;
    }
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_RESOURCE_LIMIT);

    /* Freeing one slot admits exactly one more buffer. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_CLOSE;
    registers[1] = handles[0];
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    /* The page ceiling binds independently of the buffer count. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_DMA_CREATE;
    registers[1] = ASTRA_DMA_MAX_PAGES_PER_SERVICE * KERNEL_PAGE_SIZE;
    registers[2] = user_info;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_RESOURCE_LIMIT);
}

static void test_bootstrap_capabilities(void)
{
    KernelProcessBootstrapCapability capabilities[2];
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t process_id = 0u;
    uint32_t handles_before;

    loader_build_image();
    initialize_test();
    assert(kernel_memory_stats(&before));

    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = "DEV1";
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = 1u;
    capabilities[0].rights = KERNEL_DEVICE_RIGHT_QUERY;

    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u,
                                            &process_id) == KERNEL_PROCESS_OK);
    /* Self process, self thread, and the granted lease. */
    assert(kernel_process_test_handle_count(process_id) == 3u);

    /* An unknown device cannot be granted, and the launch leaves nothing. */
    loader_build_image();
    initialize_test();
    assert(kernel_memory_stats(&before));
    handles_before = 0u;
    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = "DEV2";
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = 0x5a5a5a5au;
    capabilities[0].rights = KERNEL_DEVICE_RIGHT_QUERY;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u, &process_id) !=
           KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
    (void)handles_before;

    /* A malformed request is refused before anything is allocated. */
    memset(capabilities, 0, sizeof(capabilities));
    capabilities[0].name = 0u;
    capabilities[0].kind = KERNEL_PROCESS_BOOTSTRAP_DEVICE;
    capabilities[0].device_id = 1u;
    capabilities[0].rights = KERNEL_DEVICE_RIGHT_QUERY;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            capabilities, 1u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create_executable(
               loader_image, loader_image_size, capabilities,
               KERNEL_PROCESS_BOOTSTRAP_CAPABILITY_MAX + 1u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 1u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
}

/*
 * The verdict bit is the system's alone. A program that sets one in its own
 * exit status is claiming the machine killed it, and the value is worth
 * reading only if that claim cannot be made.
 */
/*
 * An activity is the thread's own, held by the kernel, and stamped on every
 * event that thread emits. No call site passes one, because the call sites
 * that matter are the ones that would forget.
 */
static void test_an_activity_is_the_threads_own(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    static const char line[] = "doing a thing";
    KernelCpuContext *next;
    KernelThreadSnapshot thread;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_text = KERNEL_PROCESS_STACK_TOP - 512u;
    uint32_t process_id = 0u;
    uint32_t first;
    uint32_t second;

    initialize_test();
    assert(kernel_trace_init());
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);

    /* A thread starts inside no story at all, and zero is how that reads. */
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.activity == 0u);

    /* Beginning one yields an id that is never zero -- zero already means
     * "no activity", and an allocator that could return it would make the
     * two indistinguishable. */
    registers[0] = ASTRA_SYSCALL_ACTIVITY;
    registers[1] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    first = next->data[1];
    assert(first != 0u);
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.activity == first);

    /* The next one is a different story. */
    registers[1] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    second = next->data[1];
    assert(second != first && second != 0u);

    /* Adopting is how a service joins the story it was called from. */
    registers[1] = first;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[1] == first);
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.activity == first);

    /* And the event carries it without anybody saying so. */
    assert(kernel_user_copy_to_asm(user_text, line, sizeof(line) - 1u) ==
           KERNEL_USER_COPY_OK);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = ASTRA_EVENT_LEVEL_NOTICE | ASTRA_EVENT_FLAG_INLINE_STRING;
    registers[3] = user_text;
    registers[4] = sizeof(line) - 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    {
        KernelTraceUserRecord record;
        KernelTraceHeader header;
        uint8_t payload[ASTRA_EVENT_ARGUMENT_MAX];
        uint32_t length = 0u;

        assert(kernel_trace_header(&header));
        assert(kernel_trace_read_user(header.write_index - 2u, &record,
                                      payload, sizeof(payload), &length));
        assert(record.activity == first);
    }
    assert(kernel_process_stats(&stats));
    assert(stats.diagnostic_logs == 1u);
}

/*
 * Emitting is universal and reading is authority. This is the reading half:
 * one call returns a bounded page of the machine's stream and a cursor, and it
 * takes a capability that only a diagnostic build hands a process over itself.
 */
static void test_reading_the_stream_is_the_privileged_half(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    static const char line[] = "said";
    KernelCpuContext *next;
    KernelProcessSnapshot snapshot;
    KernelHandle observer_handle = KERNEL_HANDLE_INVALID;
    AstraEventDrained drained[ASTRA_TRACE_READ_BATCH_MAX];
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_text = KERNEL_PROCESS_STACK_TOP - 512u;
    uint32_t user_events = KERNEL_PROCESS_STACK_TOP - 1024u;
    uint32_t process_id = 0u;
    uint32_t other_id = 0u;
    uint32_t cursor;

    initialize_test();
    assert(kernel_trace_init());
    assert(kernel_process_debug_surface());
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &snapshot));
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_user_copy_to_asm(user_text, line, sizeof(line) - 1u) ==
           KERNEL_USER_COPY_OK);

    /*
     * The ring survives a re-init on purpose -- it is what a machine that just
     * crashed is read from -- so whatever earlier tests left in it is drained
     * first and the cursor that leaves is where this test starts.
     */
    registers[0] = ASTRA_SYSCALL_TRACE_READ;
    registers[1] = snapshot.self_handle;
    registers[2] = 0u;
    registers[3] = user_events;
    registers[4] = ASTRA_TRACE_READ_BATCH_MAX;
    do {
        assert(kernel_process_on_syscall(registers,
                                         KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                         &next) == KERNEL_PROCESS_OK);
        assert(next->data[0] == ASTRA_SYSCALL_OK);
        registers[2] = next->data[2];
    } while (next->data[1] != 0u);
    cursor = registers[2];

    /* Two events to read back: one bare, one carrying its text. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = ASTRA_EVENT_LEVEL_NOTICE;
    registers[3] = 0u;
    registers[4] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    registers[2] = ASTRA_EVENT_LEVEL_ERROR | ASTRA_EVENT_FLAG_INLINE_STRING;
    registers[3] = user_text;
    registers[4] = sizeof(line) - 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TRACE_READ;
    registers[1] = snapshot.self_handle;
    registers[2] = cursor;
    registers[3] = user_events;
    registers[4] = 2u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 2u);
    assert(next->data[3] == 0u);
    cursor = next->data[2];
    assert(cursor != 0u);
    assert(kernel_user_copy_from_asm(drained, user_events,
                                     2u * ASTRA_EVENT_DRAINED_SIZE) ==
           KERNEL_USER_COPY_OK);
    assert(drained[0].process == process_id);
    assert(drained[0].payload_length == 0u);
    assert(KERNEL_TRACE_LEVEL_OF(drained[1].flags) == ASTRA_EVENT_LEVEL_ERROR);
    assert(drained[1].payload_length == sizeof(line) - 1u);
    assert(drained[1].payload[0] == 's');
    assert(drained[1].sequence == cursor);

    /* The cursor is where it stopped: nothing new, nothing repeated. */
    registers[2] = cursor;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == 0u);
    assert(next->data[2] == cursor);

    /* A buffer that is not there is refused before anything is drained. */
    registers[2] = 0u;
    registers[3] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    registers[3] = 0x00001000u; /* unmapped */
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);

    /* A handle that names nothing, and a handle of the wrong kind. */
    registers[3] = user_events;
    registers[1] = KERNEL_HANDLE_INVALID;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    /*
     * DEBUG over another process is a debugger's authority over that process.
     * Reading the whole machine's stream through it would launder an authority
     * nobody granted through a bystander that happens to be observable.
     */
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &other_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_grant_handle(process_id, other_id,
                                       KERNEL_PROCESS_RIGHT_DEBUG,
                                       &observer_handle) == KERNEL_PROCESS_OK);
    registers[1] = observer_handle;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);
}

/*
 * No diagnostic surface, no reading. The events are still emitted and still in
 * the ring -- a production machine keeps its account of itself -- and there is
 * simply nothing on this build that can hand them out.
 */
static void test_no_debug_surface_closes_the_stream(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    KernelProcessSnapshot snapshot;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_events = KERNEL_PROCESS_STACK_TOP - 1024u;
    uint32_t process_id = 0u;

    kernel_process_set_debug_surface(0);
    initialize_test();
    assert(kernel_trace_init());
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &snapshot));
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);

    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = ASTRA_EVENT_LEVEL_NOTICE;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_TRACE_READ;
    registers[1] = snapshot.self_handle;
    registers[3] = user_events;
    registers[4] = 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_ACCESS_DENIED);

    /* The record is in the ring regardless; only the door is shut. */
    {
        KernelTraceUserRecord user;
        KernelTraceHeader header;
        uint32_t length = 0u;

        assert(kernel_trace_header(&header));
        assert(kernel_trace_read_user(header.write_index - 1u, &user, NULL, 0u,
                                      &length));
        assert(user.process == process_id);
    }

    kernel_process_set_debug_surface(-1);
    assert(kernel_process_debug_surface());
}

static void test_a_program_cannot_forge_a_verdict(void)
{
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    KernelProcessSnapshot process;
    uint32_t process_id = 0u;

    loader_build_image();
    initialize_test();
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    registers[1] = (uint32_t)ASTRA_STATUS_FAULTED;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.exit_reason == KERNEL_PROCESS_EXIT_SYSCALL);
    assert(process.exit_status == (uint32_t)ASTRA_STATUS_BAD_EXIT);

    /* A status of its own is carried through untouched. */
    loader_build_image();
    initialize_test();
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    registers[1] = (uint32_t)ASTRA_STATUS_PROGRAM_FIRST + 7u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.exit_status == (uint32_t)ASTRA_STATUS_PROGRAM_FIRST + 7u);
}

static void test_initial_image_exit_is_reported(void)
{
    const uint32_t user_stack = KERNEL_PROCESS_STACK_TOP - 512u;
    const uint32_t exit_status = 0x53565200u;
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id = 0u;

    loader_build_image();
    initialize_test();
    assert(initial_image_exits == 0u);

    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    kernel_process_register_initial_image(process_id);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    registers[1] = exit_status;
    /* The image is the only process here, so the run queue empties with it. */
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(initial_image_exits == 1u);
    assert(last_initial_image_status == exit_status);
    assert(last_initial_image_reason == KERNEL_PROCESS_EXIT_SYSCALL);

    /*
     * Stages are reported as the counter advances, once each, and only for
     * the registered image.
     */
    loader_build_image();
    initialize_test();
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    kernel_process_register_initial_image(process_id);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROGRESS;
    registers[1] = 1u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(initial_image_progress_reports == 1u);
    assert(last_initial_image_stage == 1u);

    /* Re-reporting the same stage is not a new stage. */
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(initial_image_progress_reports == 1u);

    registers[1] = 3u;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_OK);
    assert(initial_image_progress_reports == 2u);
    assert(last_initial_image_stage == 3u);

    /* An unregistered process must not be mistaken for the initial image. */
    loader_build_image();
    initialize_test();
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, LOADER_TEXT_VADDR, 0u);
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_EXIT;
    registers[1] = exit_status;
    assert(kernel_process_on_syscall(registers, user_stack, frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(initial_image_exits == 0u);
}

static void test_executable_rejections_do_not_allocate(void)
{
    KernelMemoryStats before;
    KernelMemoryStats after;
    uint32_t process_id = 0xdeadbeefu;

    loader_build_image();
    initialize_test();
    assert(kernel_memory_stats(&before));

    assert(kernel_process_create_executable(NULL, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, NULL) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    /* Little-endian identification. */
    loader_build_image();
    loader_image[5] = 1u;
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    /* Writable and executable in one segment. */
    loader_build_image();
    loader_put32(LOADER_PHOFF + 24u, 7u);
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    /* An entry point that is not inside executable code. */
    loader_build_image();
    loader_put32(24u, LOADER_DATA_VADDR);
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    /* A segment that would land on the startup block. */
    loader_build_image();
    loader_put32(LOADER_PHOFF + 8u, KERNEL_VM_USER_MIN);
    loader_put32(24u, KERNEL_VM_USER_MIN);
    assert(kernel_process_create_executable(loader_image, loader_image_size,
                                            NULL, 0u, &process_id) ==
           KERNEL_PROCESS_INVALID_ARGUMENT);

    assert(kernel_memory_stats(&after));
    assert(after.free_frames == before.free_frames);
}

static void test_executable_load_rolls_back_every_allocation(void)
{
    KernelMemoryStats baseline;
    uint32_t attempt;

    loader_build_image();
    initialize_test();
    assert(kernel_memory_stats(&baseline));

    for (attempt = 1u; attempt <= 24u; ++attempt) {
        KernelMemoryStats after;
        uint32_t process_id = 0xdeadbeefu;
        KernelProcessStatus status;

        kernel_allocation_test_fail_global(attempt);
        status = kernel_process_create_executable(loader_image,
                                                  loader_image_size, NULL, 0u,
                                                  &process_id);
        kernel_allocation_test_fail_global(0u);
        if (status == KERNEL_PROCESS_OK) {
            /* Past the last injectable site; the load legitimately succeeded. */
            initialize_test();
            assert(kernel_memory_stats(&baseline));
            continue;
        }
        assert(process_id == 0u);
        assert(kernel_memory_stats(&after));
        assert(after.free_frames == baseline.free_frames);
        assert(kernel_allocation_valid());
    }
}

/*
 * A process may inspect a process it holds a handle to, and always holds its
 * own. Enumerating other processes is not a syscall by design; that authority
 * belongs to an introspection service, per OBSERVABILITY.md.
 */
static void test_process_info_syscall(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_info = KERNEL_PROCESS_STACK_TOP - 64u;
    AstraProcessInfo info;
    uint32_t process_id;
    uint32_t self_handle;

    initialize_test();
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_QUERY_ABI;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(next->data[1] == ASTRA_SYSCALL_ABI_VERSION);
    self_handle = next->data[2];
    assert(self_handle != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_INFO;
    registers[1] = self_handle;
    registers[2] = user_info;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(kernel_user_copy_from_asm(&info, user_info, sizeof(info)) ==
           KERNEL_USER_COPY_OK);
    assert(info.size == sizeof(info));
    assert(info.id == process_id);
    assert(info.generation != 0u);
    assert(info.owner == process_id);
    assert(info.live_threads == 1u && info.thread_count == 1u);
    assert(info.handle_references != 0u);
    assert(info.reserved == 0u);
    /* The owner ledger already charges this process for its own pages. */
    assert(info.resident_frames != 0u);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_INFO;
    registers[1] = self_handle + 0x1000u;
    registers[2] = user_info;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_HANDLE);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_INFO;
    registers[1] = self_handle;
    registers[2] = user_info + 1u;
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);

    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_INFO;
    registers[1] = self_handle;
    registers[2] = 0x00000004u; /* unmapped */
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR,
               KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
}


/*
 * Reserve-and-grow, from both ends: a stack that reaches a page it has not
 * committed gets it and carries on, and one that reaches the floor still dies.
 *
 * The distinction is the whole point of the feature. If growth answered every
 * fault below a stack, the guard page would stop meaning anything and a wild
 * pointer would quietly be given memory instead of killing the process.
 */
static void test_user_stack_grows_on_fault_and_guards_the_floor(void)
{
    static const uint8_t image[] = {
        0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u, 0x4eu, 0x71u
    };
    KernelCpuContext *next;
    KernelMemoryStats baseline;
    KernelMemoryStats after_growth;
    KernelMemoryStats after_teardown;
    KernelProcessSnapshot process;
    KernelThreadSnapshot thread;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT];
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;
    uint32_t fault_pc = KERNEL_PROCESS_CODE_BASE + 2u;

    initialize_test();
    fault_reports = 0u;
    assert(kernel_memory_stats(&baseline));
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.user_stack_top == TEST_STACK_TOP(0));
    assert(thread.user_stack_base == TEST_STACK_BASE(0));
    assert(thread.stack_pages == TEST_STACK_PAGES);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.user_stack_pages == TEST_STACK_PAGES);

    /* One byte under the committed base: the ordinary case, one page. */
    memset(registers, 0, sizeof(registers));
    assert(kernel_memory_stats(&baseline));
    make_frame(frame, 0xau, 2u, fault_pc, TEST_STACK_BASE(0) - 4u);
    assert(kernel_process_on_fault(registers, TEST_STACK_BASE(0) - 4u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(next != NULL);
    /*
     * Resumed, not rescheduled and not retired: the same thread, at the
     * instruction that faulted, which is what makes the access run again.
     */
    assert(next == kernel_process_current_context());
    assert(next->program_counter == fault_pc);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.process_state == KERNEL_PROCESS_RUNNING);
    assert(process.exit_reason == KERNEL_PROCESS_EXIT_NONE);
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.stack_pages == TEST_STACK_PAGES + 1u);
    assert(thread.user_stack_base ==
           TEST_STACK_BASE(0) - KERNEL_PAGE_SIZE);
    assert(thread.user_stack_top == TEST_STACK_TOP(0));
    assert(process.user_stack_pages == TEST_STACK_PAGES + 1u);
    assert(kernel_memory_stats(&after_growth));
    assert(after_growth.free_frames == baseline.free_frames - 1u);
    assert(kernel_process_stats(&stats));
    assert(stats.user_stack_growths == 1u);
    assert(stats.user_stack_pages_committed == 1u);
    /* Growth is not a user fault and must not be counted as one. */
    assert(stats.user_faults == 0u);
    /* Nor reported as one: a fault that was answered is not news. */
    assert(fault_reports == 0u);

    /*
     * A frame bigger than a page can touch its bottom first, so the span
     * between the address and what is held has to arrive at once -- a hole
     * would leave user_stack_base describing pages that are not mapped.
     */
    make_frame(frame, 0xau, 2u, fault_pc,
               TEST_STACK_BASE(0) - (4u * KERNEL_PAGE_SIZE) + 8u);
    assert(kernel_process_on_fault(registers, TEST_STACK_BASE(0) - 4u, frame,
                                   &next) == KERNEL_PROCESS_OK);
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.stack_pages == TEST_STACK_PAGES + 4u);
    assert(thread.user_stack_base ==
           TEST_STACK_BASE(0) - (4u * KERNEL_PAGE_SIZE));
    assert(kernel_process_stats(&stats));
    assert(stats.user_stack_growths == 2u);
    assert(stats.user_stack_pages_committed == 4u);
    assert(kernel_memory_stats(&after_growth));
    assert(after_growth.free_frames == baseline.free_frames - 4u);

    /* The copy path reaches the same growth without a user-visible fault. */
    assert(!kernel_process_commit_user_stack(thread.user_stack_base, 4u));
    assert(kernel_process_commit_user_stack(
        thread.user_stack_base - KERNEL_PAGE_SIZE, 16u));
    assert(kernel_thread_snapshot(0u, &thread));
    assert(thread.stack_pages == TEST_STACK_PAGES + 5u);
    assert(!kernel_process_commit_user_stack(TEST_STACK_FLOOR(0) - 4u, 4u));

    /*
     * The floor is still a wall. This is the only process, so retiring it
     * leaves nothing runnable -- which is the answer, not an error.
     */
    make_frame(frame, 0xau, 2u, fault_pc, TEST_STACK_FLOOR(0) - 4u);
    assert(kernel_process_on_fault(registers, TEST_STACK_FLOOR(0) - 4u, frame,
                                   &next) == KERNEL_PROCESS_NO_RUNNABLE);
    assert(kernel_process_snapshot(0u, &process));
    assert(process.process_state == KERNEL_PROCESS_EXITING);
    assert(process.exit_reason == KERNEL_PROCESS_EXIT_USER_FAULT);
    assert(process.exit_status == (uint32_t)ASTRA_STATUS_FAULTED);
    assert(process.fault_address == TEST_STACK_FLOOR(0) - 4u);
    assert(kernel_process_stats(&stats));
    assert(stats.user_faults == 1u);
    assert(stats.user_stack_growths == 3u);
    /*
     * The fault that killed it is reported once, in terms a reader can use:
     * where the instruction was, what address it touched, and that the address
     * was the guard page rather than a wild pointer.
     */
    assert(fault_reports == 1u);
    assert(last_fault_process == process_id);
    assert(last_fault_pc == fault_pc);
    assert(last_fault_address == TEST_STACK_FLOOR(0) - 4u);
    assert(last_fault_vector == 2u);
    assert(last_fault_kind == KERNEL_PROCESS_FAULT_STACK_GUARD);

    /* Everything committed by growth comes back, not just the first page. */
    while (kernel_process_maintenance_pending())
        assert(kernel_process_maintenance() == KERNEL_PROCESS_OK);
    assert(kernel_memory_stats(&after_teardown));
    assert(after_teardown.free_frames >= baseline.free_frames);
}


/*
 * The diagnostic channel. A process may write a bounded line about itself if
 * it holds ASTRA_RIGHT_DEBUG over itself, and may not do anything else at all.
 *
 * The refusals are the substance here. This is a syscall that reaches the
 * console the operator reads, so every way of asking wrongly has to be a
 * refusal that is counted rather than a line that gets through.
 */
static void test_any_process_may_emit_an_event(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    static const char line[] = "volume mounted";
    KernelCpuContext *next;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_text = KERNEL_PROCESS_STACK_TOP - 512u;
    uint32_t process_id;
    uint32_t refusals;

    initialize_test();
    assert(kernel_trace_init());
    assert(kernel_process_debug_surface());
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_user_copy_to_asm(user_text, line, sizeof(line) - 1u) ==
           KERNEL_USER_COPY_OK);

    /*
     * The reversal. Emitting used to need a process handle carrying
     * ASTRA_RIGHT_DEBUG, and the call took one; there is no handle now and
     * nothing to hold. If the machine's account of what happened depended on
     * a capability, it would have holes exactly where something went wrong.
     */
    diagnostic_log_reports = 0u;
    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = ASTRA_EVENT_LEVEL_WARNING | ASTRA_EVENT_FLAG_INLINE_STRING;
    registers[3] = user_text;
    registers[4] = sizeof(line) - 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(diagnostic_log_reports == 1u);
    assert(last_diagnostic_process == process_id);
    assert(last_diagnostic_message == ASTRA_EVENT_MESSAGE_UNSTRUCTURED);
    assert(last_diagnostic_length == sizeof(line) - 1u);
    assert(strcmp(last_diagnostic_text, line) == 0);
    assert(KERNEL_TRACE_LEVEL_OF(last_diagnostic_flags) ==
           ASTRA_EVENT_LEVEL_WARNING);
    assert(kernel_process_stats(&stats));
    assert(stats.diagnostic_logs == 1u);
    assert(stats.diagnostic_log_bytes == sizeof(line) - 1u);
    assert(stats.diagnostic_log_refusals == 0u);

    /*
     * The event is in the ring, which is the record. The console is a sink on
     * it rather than a second destination, so the two cannot disagree about
     * what was said or about the order it was said in.
     */
    {
        KernelTraceUserRecord user;
        KernelTraceHeader header;
        uint8_t payload[ASTRA_EVENT_ARGUMENT_MAX];
        uint32_t length = 0u;
        uint32_t slot;

        assert(kernel_trace_header(&header));
        /* Two slots back: the header of the pair, then its arguments. */
        slot = header.write_index - 2u;
        assert(kernel_trace_read_user(slot, &user, payload, sizeof(payload),
                                      &length));
        assert(user.message == ASTRA_EVENT_MESSAGE_UNSTRUCTURED);
        assert(user.process == process_id);
        assert(length == sizeof(line) - 1u);
        assert(memcmp(payload, line, length) == 0);
    }

    /* An event with no arguments at all is a legal event. */
    registers[3] = 0u;
    registers[4] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(diagnostic_log_reports == 2u);

    refusals = 0u;
    diagnostic_log_reports = 0u;

    /* A message id of zero names no message. */
    registers[1] = ASTRA_EVENT_MESSAGE_NONE;
    registers[3] = user_text;
    registers[4] = 4u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;

    /* More than one event's payload is refused, never truncated. */
    registers[4] = ASTRA_EVENT_ARGUMENT_MAX + 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[4] = 4u;

    /* A length with no payload, and a payload with no length. */
    registers[3] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[3] = user_text;
    registers[4] = 0u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[4] = 4u;

    /* A level outside the five, and a flag this build does not know. */
    registers[2] = ASTRA_EVENT_LEVEL_ERROR + 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[2] = 0x8000u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_INVALID_ARGUMENT);
    ++refusals;
    registers[2] = ASTRA_EVENT_LEVEL_INFO | ASTRA_EVENT_FLAG_INLINE_STRING;

    /* An address the process does not own is refused, not read. */
    registers[3] = 4u;
    registers[4] = 8u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_BAD_ADDRESS);
    ++refusals;

    /* Not one of those refusals reached the sink, and all were counted. */
    assert(diagnostic_log_reports == 0u);
    assert(kernel_process_stats(&stats));
    assert(stats.diagnostic_log_refusals == refusals);
}

/*
 * The same kernel as a machine ships: no debug surface at all. A process gets
 * no ASTRA_RIGHT_DEBUG over itself, so the diagnostic channel is closed to
 * everyone -- which is the point of having the surface be one decision rather
 * than a scatter of them.
 */
static void test_no_debug_surface_closes_only_the_console(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    static const char line[] = "please";
    KernelCpuContext *next;
    KernelProcessSnapshot snapshot;
    KernelSchedulerStats stats;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t user_text = KERNEL_PROCESS_STACK_TOP - 512u;
    uint32_t process_id;
    uint32_t reports;

    kernel_process_set_debug_surface(0);
    initialize_test();
    assert(!kernel_process_debug_surface());
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    assert(kernel_process_snapshot(0u, &snapshot));
    make_frame(frame, 0u, ASTRA_SYSCALL_VECTOR, KERNEL_PROCESS_CODE_BASE, 0u);
    assert(kernel_user_copy_to_asm(user_text, line, sizeof(line) - 1u) ==
           KERNEL_USER_COPY_OK);

    /*
     * No debug surface closes the console, and nothing else. The event is
     * still emitted and is still in the ring: a production machine keeps its
     * account of itself and simply does not narrate it. The channel used to be
     * refused outright here, which is what put the holes in the account.
     */
    assert(kernel_trace_init());
    reports = diagnostic_log_reports;
    registers[0] = ASTRA_SYSCALL_LOG_WRITE;
    registers[1] = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    registers[2] = ASTRA_EVENT_LEVEL_ERROR | ASTRA_EVENT_FLAG_INLINE_STRING;
    registers[3] = user_text;
    registers[4] = sizeof(line) - 1u;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);
    assert(diagnostic_log_reports == reports);
    assert(kernel_process_stats(&stats));
    assert(stats.diagnostic_logs == 1u);
    assert(stats.diagnostic_log_refusals == 0u);
    {
        KernelTraceUserRecord user;
        KernelTraceHeader header;
        uint8_t payload[ASTRA_EVENT_ARGUMENT_MAX];
        uint32_t length = 0u;

        assert(kernel_trace_header(&header));
        assert(kernel_trace_read_user(header.write_index - 2u, &user, payload,
                                      sizeof(payload), &length));
        assert(user.process == process_id);
        assert(length == sizeof(line) - 1u);
    }

    /* Everything else the handle carries is untouched by the decision. */
    memset(registers, 0, sizeof(registers));
    registers[0] = ASTRA_SYSCALL_PROCESS_INFO;
    registers[1] = snapshot.self_handle;
    registers[2] = user_text;
    assert(kernel_process_on_syscall(registers,
                                     KERNEL_PROCESS_STACK_TOP - 8u, frame,
                                     &next) == KERNEL_PROCESS_OK);
    assert(next->data[0] == ASTRA_SYSCALL_OK);

    /* Back to this build's own answer for whatever runs next. */
    kernel_process_set_debug_surface(-1);
    assert(kernel_process_debug_surface());
}


/*
 * A fault outside any stack is reported as what it is. The classification is
 * only useful if it is narrow: calling a wild pointer a stack overflow would
 * send the reader to the wrong place, which is worse than saying nothing.
 */
static void test_fault_report_names_only_what_it_knows(void)
{
    static const uint8_t image[] = {0x4eu, 0x71u};
    KernelCpuContext *next;
    uint32_t registers[KERNEL_CONTEXT_REGISTER_COUNT] = {0u};
    uint8_t frame[KERNEL_EXCEPTION_FRAME_MAX_SIZE];
    uint32_t process_id;

    initialize_test();
    fault_reports = 0u;
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);

    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 6u, 0x60000000u);
    assert(kernel_process_on_fault(registers, KERNEL_PROCESS_STACK_TOP - 8u,
                                   frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(fault_reports == 1u);
    assert(last_fault_address == 0x60000000u);
    assert(last_fault_kind == KERNEL_PROCESS_FAULT_OTHER);

    /*
     * An address in another thread's slot is inside the arena and outside this
     * thread's stack, which is a third thing and is said as one.
     */
    initialize_test();
    fault_reports = 0u;
    assert(kernel_process_create(image, sizeof(image), 0u, 0u,
                                 &process_id) == KERNEL_PROCESS_OK);
    assert(kernel_process_start(&next) == KERNEL_PROCESS_OK);
    make_frame(frame, 0xau, 2u, KERNEL_PROCESS_CODE_BASE + 6u,
               TEST_STACK_BASE(3));
    assert(kernel_process_on_fault(registers, KERNEL_PROCESS_STACK_TOP - 8u,
                                   frame, &next) ==
           KERNEL_PROCESS_NO_RUNNABLE);
    assert(fault_reports == 1u);
    assert(last_fault_kind == KERNEL_PROCESS_FAULT_STACK_ARENA);
}

int main(void)
{
    test_process_allocation_failure_matrix();
    test_process_global_nth_failure_matrix();
    test_preemption_fault_containment_and_teardown();
    test_soak_relaunches_only_after_exact_teardown();
    test_soak_rejects_unexplained_frame_loss();
    test_invalid_creation_does_not_allocate();
    test_sync_syscall_rights_and_stale_handles();
    test_irq_syscalls_waits_rights_and_owner_cleanup();
    test_device_lease_syscalls_rights_and_owner_cleanup();
    test_console_writes_through_a_display_lease();
    test_input_batch_read_is_bounded_and_fault_atomic();
    test_private_irq_qualification_control();
    test_priority_selection_and_equal_priority_rotation();
    test_per_process_thread_limit_is_bounded_and_reclaimable();
    test_last_runnable_timed_wait_wakes_from_supervisor_idle();
    test_prestart_timer_cannot_consume_published_thread();
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
    test_message_port_syscall_atomicity_and_cleanup();
    test_malformed_syscall_and_message_corpus();
    test_shared_area_and_bulk_ring_syscalls();
    test_area_publication_rolls_back_when_handle_table_full();
    test_area_and_ring_endpoint_transfer_over_port();
    test_executable_loading();
    test_dma_transfer_memory();
    test_block_admission();
    test_block_admission_faults();
    test_bootstrap_capabilities();
    test_an_activity_is_the_threads_own();
    test_reading_the_stream_is_the_privileged_half();
    test_no_debug_surface_closes_the_stream();
    test_a_program_cannot_forge_a_verdict();
    test_initial_image_exit_is_reported();
    test_user_stack_grows_on_fault_and_guards_the_floor();
    test_any_process_may_emit_an_event();
    test_no_debug_surface_closes_only_the_console();
    test_fault_report_names_only_what_it_knows();
    test_executable_rejections_do_not_allocate();
    test_executable_load_rolls_back_every_allocation();
    test_process_info_syscall();
    puts("process tests passed");
    return 0;
}
