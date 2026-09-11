/** @file runtime.h @brief Native process startup and syscall wrappers. */
#ifndef ASTRA_RUNTIME_H
#define ASTRA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <astra/compiler.h>
#include <astra/block.h>
#include <astra/display.h>
#include <astra/event.h>
#include <astra/input.h>
#include <astra/host.h>
#include <astra/library.h>
#include <astra/civil.h>
#include <astra/process.h>
#include <astra/proc.h>
#include <astra/posix_process.h>
#include <astra/network.h>
#include <astra/syscall.h>

/*
 * Exit status of a process that failed an assertion, tagged the same way the
 * supervisor tags its own result: "AS" in the high halfword, the failing
 * source line in the low one.
 */
/** Assertion-exit status tag (`AS`) ORed with the failing source line. */
#define ASTRA_ASSERT_STATUS_TAG 0x41530000u

/**
 * Four answers and a status. D4 joined the set when the clock started
 * returning the zone with the instant: a call that has to answer two things
 * about one moment cannot be split into two calls without straddling the
 * moment.
 */
typedef struct AstraSyscallResult {
    uint32_t status; /**< Astra syscall status. */
    uint32_t value0; /**< First returned word. */
    uint32_t value1; /**< Second returned word. */
    uint32_t value2; /**< Third returned word. */
    uint32_t value3; /**< Fourth returned word. */
} AstraSyscallResult;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraSyscallResult) == 20u,
               "syscall-result layout changed");
/** @endcond */

/**
 * Validate startup record size, encoding, argument, and capability ranges.
 * @param startup Startup record supplied by the kernel.
 * @return Nonzero when the complete record is valid.
 */
int astra_startup_validate(const AstraStartupInfo *startup);
/**
 * Read how the process was launched.
 * @param startup Valid startup record.
 * @return Desktop, shell, service, or other launch-source value.
 */
AstraLaunchSource astra_startup_launch_source(
    const AstraStartupInfo *startup);
/**
 * Access one immutable UTF-8 startup argument.
 * @param startup Valid startup record.
 * @param index Zero-based argument index.
 * @return Borrowed NUL-terminated argument, or NULL when out of range.
 */
const char *astra_startup_argument(const AstraStartupInfo *startup,
                                   uint32_t index);
/**
 * Find a named startup capability.
 * @param startup Valid startup record.
 * @param name Case-sensitive capability name.
 * @return Borrowed capability record, or NULL when absent.
 */
const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *startup, const char *name);
/**
 * Publish service startup completion to the supervisor.
 * @param bootstrap Supervisor bootstrap port.
 * @param status Service-defined startup status.
 * @param handles Capabilities published with the ready message.
 * @param handle_count Number of entries in `handles`.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_service_ready(uint32_t bootstrap, uint32_t status,
                             const uint32_t *handles,
                             uint32_t handle_count);

/**
 * Invoke a raw five-argument syscall.
 * Prefer the typed wrappers below; this is the shared ABI primitive.
 * @param number ASTRA_SYSCALL_* number.
 * @param argument0 First argument word.
 * @param argument1 Second argument word.
 * @param argument2 Third argument word.
 * @param argument3 Fourth argument word.
 * @param argument4 Fifth argument word.
 * @param result Receives syscall status and four result words.
 */
void astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
                    uint32_t argument2, uint32_t argument3,
                    uint32_t argument4, AstraSyscallResult *result);

/** @return ASTRA_SYSCALL_* status after yielding the current thread. */
uint32_t astra_yield(void);
/**
 * Close one capability in the current process.
 * @param handle Capability handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_close(uint32_t handle);
/**
 * Duplicate a capability with equal or reduced rights.
 * @param handle Source capability.
 * @param rights Rights requested for the duplicate.
 * @param duplicate Receives the new handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                                uint32_t *duplicate);
/**
 * Create a waitable event.
 * @param flags Event creation flags.
 * @param rights Rights for the returned handle.
 * @param handle Receives the event handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_event_create(uint32_t flags, uint32_t rights,
                               uint32_t *handle);
/**
 * Create a counting semaphore.
 * @param initial Initial count.
 * @param maximum Maximum count.
 * @param rights Rights for the returned handle.
 * @param handle Receives the semaphore handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle);
/**
 * Signal an event or release semaphore units.
 * @param handle Signal-capable handle.
 * @param count Units to release; event semantics are object-defined.
 * @param woken Receives threads made runnable when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_signal(uint32_t handle, uint32_t count,
                         uint32_t *woken);
/**
 * Reset a manual-reset event to nonsignaled.
 * @param handle Event handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_event_reset(uint32_t handle);
/** Entry point and argument used to create a thread. */
typedef struct AstraThreadStart {
    void (*entry)(uint32_t argument); /**< Function executed by the new thread. */
    uint32_t argument; /**< Word passed to entry. */
} AstraThreadStart;
/**
 * Create a thread in the current process.
 * @param start Entry point and argument copied by the kernel.
 * @param priority Scheduler priority.
 * @param rights Rights for the returned thread handle.
 * @param handle Receives the thread handle.
 * @param thread_id Receives the stable thread identifier.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_thread_create(const AstraThreadStart *start,
                                uint32_t priority, uint32_t rights,
                                uint32_t *handle, uint32_t *thread_id);
/**
 * Create a committed shared-memory area.
 * @param byte_size Requested bytes, rounded to pages.
 * @param rights Rights for the returned handle.
 * @param handle Receives the area handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_area_create(uint32_t byte_size, uint32_t rights,
                           uint32_t *handle);
/**
 * Create an area with explicit ASTRA_AREA_CREATE_* policy.
 * @param byte_size Requested bytes, rounded to pages.
 * @param rights Rights for the returned handle.
 * @param flags ASTRA_AREA_CREATE_* flags.
 * @param handle Receives the area handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_area_create_flagged(uint32_t byte_size, uint32_t rights,
                                   uint32_t flags, uint32_t *handle);
/**
 * Decommit mapped pages while preserving the virtual reservation.
 * @param address Page-aligned address within a reserved area.
 * @param byte_size Page-aligned span.
 * @param released_pages Receives pages returned to the system.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_area_decommit(void *address, uint32_t byte_size,
                             uint32_t *released_pages);
/**
 * Map an area capability into the current process.
 * @param handle Area capability.
 * @param permissions Requested ASTRA_AREA_* access permissions.
 * @param address Receives mapping base.
 * @param byte_size Receives mapped byte span.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                        void **address, uint32_t *byte_size);
/**
 * Remove an area mapping from the current process.
 * @param address Exact mapping base.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_area_unmap(void *address);
/**
 * Reserve private virtual memory for demand commitment.
 * @param byte_size Requested bytes, rounded to pages.
 * @param permissions Requested access permissions.
 * @param address Receives reservation base.
 * @param mapped_span Receives reserved byte span.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_private_reserve(uint32_t byte_size, uint32_t permissions,
                                 void **address, uint32_t *mapped_span);
/**
 * Decommit pages in a private reservation.
 * @param address Page-aligned address within the reservation.
 * @param byte_size Page-aligned span.
 * @param released_pages Receives pages returned to the system.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_private_decommit(void *address, uint32_t byte_size,
                                  uint32_t *released_pages);
/**
 * Configure current-thread POSIX signal delivery.
 * @param trampoline Signal trampoline, or NULL to disable delivery.
 * @param stack_top Top of the signal stack.
 * @param blocked Replacement blocked-signal mask.
 * @param pending Receives pending-signal mask when non-NULL.
 * @param previous_blocked Receives prior blocked mask when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_signal_configure(void (*trampoline)(int), void *stack_top,
                                   uint32_t blocked, uint32_t *pending,
                                   uint32_t *previous_blocked);
/**
 * Replace the current process interval timer.
 * @param delay_ns Nanoseconds until first signal; zero disables.
 * @param interval_ns Reload interval in nanoseconds; zero is one-shot.
 * @param old_delay_ns Receives prior remaining delay when non-NULL.
 * @param old_interval_ns Receives prior interval when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_interval_timer(uint64_t delay_ns, uint64_t interval_ns,
                                 uint64_t *old_delay_ns,
                                 uint64_t *old_interval_ns);
/**
 * Query the current process interval timer.
 * @param delay_ns Receives remaining delay in nanoseconds.
 * @param interval_ns Receives reload interval in nanoseconds.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_interval_timer_get(uint64_t *delay_ns,
                                     uint64_t *interval_ns);
/** @return ASTRA_SYSCALL_* status after restoring interrupted thread state. */
uint32_t astra_rt_signal_return(void);
/**
 * Sleep the current thread until an absolute monotonic deadline.
 * @param deadline_ns Absolute monotonic nanoseconds.
 * @param flags Sleep behavior flags.
 * @param signal_mask Temporary blocked-signal mask.
 * @param previous_signal_mask Receives the prior mask when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_thread_sleep(uint64_t deadline_ns, uint32_t flags,
                               uint32_t signal_mask,
                               uint32_t *previous_signal_mask);
/**
 * Create producer and consumer capabilities over an area-backed ring.
 * @param area Shared-memory area capability.
 * @param offset Byte offset of ring storage within the area.
 * @param element_size Fixed bytes per element.
 * @param capacity Number of elements.
 * @param flags Ring creation flags.
 * @param producer Receives producer capability.
 * @param consumer Receives consumer capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_ring_create(uint32_t area, uint32_t offset,
                              uint32_t element_size, uint32_t capacity,
                              uint32_t flags, uint32_t *producer,
                              uint32_t *consumer);
/**
 * Try to remove one ring element without blocking.
 * @param consumer Consumer capability.
 * @param bytes Receives element bytes.
 * @param capacity Bytes available in `bytes`.
 * @param copied Receives element byte count.
 * @return ASTRA_SYSCALL_* status, including WOULD_BLOCK when empty.
 */
uint32_t astra_rt_ring_read_try(uint32_t consumer, void *bytes,
                               uint32_t capacity, uint32_t *copied);
/**
 * Try to append one ring element without blocking.
 * @param producer Producer capability.
 * @param bytes Element bytes.
 * @param length Number of source bytes.
 * @param flags Per-write ring flags.
 * @param written Receives bytes accepted.
 * @return ASTRA_SYSCALL_* status, including WOULD_BLOCK when full.
 */
uint32_t astra_rt_ring_write_try(uint32_t producer, const void *bytes,
                                uint32_t length, uint32_t flags,
                                uint32_t *written);
/**
 * Validate and map a shared-library ELF image.
 * @param image Complete immutable ELF image.
 * @param length Image byte length.
 * @param base Receives mapped base address.
 * @param span Receives mapped byte span.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_library_map(const void *image, uint32_t length,
                              uint32_t *base, uint32_t *span);
/**
 * Attach a previously registered shared-library mapping.
 * @param reference Validated library reference.
 * @param base Receives mapped base address.
 * @param span Receives mapped byte span.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_library_attach(const AstraLibraryReference *reference,
                                 uint32_t *base, uint32_t *span);
/**
 * Query the runtime ABI and current kernel-owned handles.
 * @param abi_version Receives runtime ABI version.
 * @param process_handle Receives current process handle.
 * @param thread_handle Receives current thread handle.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                         uint32_t *thread_handle);
/**
 * Return the current thread handle, cached independently by each thread.
 * @param thread_handle Receives the current thread capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_current_thread_handle(uint32_t *thread_handle);
/**
 * Query one process visible through a capability.
 * @param handle Process capability, or the ABI-defined current-process value.
 * @param info ABI-sized structure receiving process information.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_info(uint32_t handle, AstraProcessInfo *info);
/**
 * Capture a consistent process-table snapshot.
 * @param observer Process capability authorizing observation.
 * @param records Receives at most `capacity` records.
 * @param capacity Number of records available.
 * @param live_count Receives total live-process count.
 * @return ASTRA_SYSCALL_* status; BUFFER_TOO_SMALL reports required count.
 */
uint32_t astra_process_snapshot(uint32_t observer,
                                AstraProcSnapshot *records,
                                uint32_t capacity,
                                uint32_t *live_count);
/**
 * Replace a process base scheduler priority.
 * @param handle Process capability with priority-administration rights.
 * @param priority New scheduler priority.
 * @param previous_priority Receives prior priority when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_priority(uint32_t handle, uint32_t priority,
                                uint32_t *previous_priority);
/**
 * Publish monotonically increasing startup or work progress.
 * @param value Component-defined progress value.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_progress(uint32_t value);
/**
 * Query a device capability.
 * @param handle Device capability.
 * @param info ABI-sized structure receiving device information.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_device_query(uint32_t handle, AstraDeviceInfo *info);
/**
 * Reset a device through an administrative capability.
 * @param handle Device capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_device_reset(uint32_t handle);
/** @return Monotonic nanoseconds since the current boot. */
uint64_t astra_clock_monotonic(void);
/**
 * Compute a saturated elapsed interval in microseconds.
 * @param from Earlier monotonic nanosecond timestamp.
 * @param to Later monotonic nanosecond timestamp.
 * @return Elapsed microseconds, zero for reversed input, or UINT32_MAX.
 */
static inline uint32_t
astra_elapsed_microseconds(uint64_t from, uint64_t to)
{
    uint64_t elapsed;

    if (to <= from)
        return 0u;
    elapsed = (to - from) / 1000u;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}
/**
 * Read the synchronized realtime clock.
 * @param nanoseconds Receives Unix-epoch nanoseconds.
 * @return ASTRA_SYSCALL_* status; unavailable until host time is valid.
 */
uint32_t astra_clock_realtime(uint64_t *nanoseconds);
/**
 * Atomically read synchronized realtime and local-zone metadata.
 * @param nanoseconds Receives Unix-epoch nanoseconds.
 * @param zone Receives timezone offset and daylight-saving metadata.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_clock_realtime_zone(uint64_t *nanoseconds,
                                   AstraTimeZone *zone);
/**
 * Set a writable system clock.
 * @param clock ASTRA_CLOCK_* identifier.
 * @param nanoseconds Clock value in nanoseconds in that clock's epoch.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_clock_set(uint32_t clock, uint64_t nanoseconds);
/**
 * Wait for one object until an absolute monotonic deadline.
 * @param handle Wait-capable object.
 * @param deadline_ns Absolute deadline, zero to poll, or FOREVER.
 * @param detail Receives object-specific completion detail when non-NULL.
 * @return ASTRA_SYSCALL_* status, including CANCELLED on signal delivery.
 */
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns,
                        uint32_t *detail);
/**
 * Complete an internal wait across signal delivery.
 * Application-facing waits use astra_wait_one() so they can expose EINTR.
 * @param handle Wait-capable object.
 * @param deadline_ns Absolute monotonic deadline.
 * @param detail Receives object-specific completion detail when non-NULL.
 * @return Final ASTRA_SYSCALL_* status other than CANCELLED.
 */
static inline uint32_t astra_wait_one_restart(uint32_t handle,
                                              uint64_t deadline_ns,
                                              uint32_t *detail)
{
    uint32_t status;

    do {
        status = astra_wait_one(handle, deadline_ns, detail);
    } while (status == ASTRA_SYSCALL_CANCELLED);
    return status;
}
/**
 * Wait for the first of several objects.
 * @param handles Array of wait-capable handles.
 * @param count Number of handles.
 * @param deadline_ns Absolute monotonic deadline, zero to poll, or FOREVER.
 * @param index Receives the completed handle's array index.
 * @param detail Receives object-specific completion detail when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_wait_multiple(const uint32_t *handles, uint32_t count,
                             uint64_t deadline_ns, uint32_t *index,
                             uint32_t *detail);
/**
 * Sleep while a user word still equals an expected value.
 * @param address Aligned readable shared word.
 * @param expected Value required before sleeping.
 * @param deadline_ns Absolute monotonic deadline.
 * @return ASTRA_SYSCALL_* status, including WOULD_BLOCK when already changed.
 */
uint32_t astra_futex_wait(volatile uint32_t *address, uint32_t expected,
                          uint64_t deadline_ns);
/**
 * Wake threads waiting on a user word.
 * @param address Aligned shared word used by waiters.
 * @param count Maximum waiters to wake.
 * @param woken Receives number awakened when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_futex_wake(volatile uint32_t *address, uint32_t count,
                          uint32_t *woken);

/**
 * Acquire a shared sleepable mutex; uncontended acquisition stays in user mode.
 * @param state Aligned shared mutex word initialized to zero.
 * @return ASTRA_SYSCALL_* status.
 */
static inline uint32_t astra_mutex_lock(volatile uint32_t *state)
{
    uint32_t expected = 0u;

    if (state == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (__atomic_compare_exchange_n(state, &expected, 1u, 0,
                                    __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
        return ASTRA_SYSCALL_OK;
    for (;;) {
        uint32_t previous = expected;
        uint32_t status;

        if (previous != 2u)
            previous = __atomic_exchange_n(state, 2u, __ATOMIC_ACQUIRE);
        if (previous == 0u)
            return ASTRA_SYSCALL_OK;
        status = astra_futex_wait(state, 2u, ASTRA_DEADLINE_FOREVER);
        if (status != ASTRA_SYSCALL_OK &&
            status != ASTRA_SYSCALL_CANCELLED &&
            status != ASTRA_SYSCALL_WOULD_BLOCK)
            return status;
        expected = __atomic_exchange_n(state, 2u, __ATOMIC_ACQUIRE);
        if (expected == 0u)
            return ASTRA_SYSCALL_OK;
    }
}

/**
 * Release a shared sleepable mutex and wake one contending thread.
 * @param state Mutex word held by the caller.
 * @return ASTRA_SYSCALL_* status.
 */
static inline uint32_t astra_mutex_unlock(volatile uint32_t *state)
{
    uint32_t previous;

    if (state == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    previous = __atomic_exchange_n(state, 0u, __ATOMIC_RELEASE);
    if (previous == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return previous == 2u ? astra_futex_wake(state, 1u, NULL) :
                            ASTRA_SYSCALL_OK;
}
/**
 * Arm a masked interrupt endpoint for its next interrupt.
 * @param handle Interrupt endpoint capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_irq_arm(uint32_t handle);
/**
 * Mask an interrupt endpoint.
 * @param handle Interrupt endpoint capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_irq_mask(uint32_t handle);
/**
 * Read the latest interrupt record and pending count.
 * @param handle Interrupt endpoint capability.
 * @param record Receives sequence and device detail.
 * @param events Receives coalesced pending-event count.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_irq_read(uint32_t handle, AstraIrqRecord *record,
                        uint32_t *events);
/**
 * Acknowledge a consumed interrupt sequence.
 * @param handle Interrupt endpoint capability.
 * @param sequence Sequence read from AstraIrqRecord.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_irq_ack(uint32_t handle, uint32_t sequence);
/**
 * Allocate a DMA-visible buffer.
 * @param byte_size Requested byte count.
 * @param info Receives CPU address, bus address, size, and capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info);
/**
 * Query geometry and queue limits for a block-device lease.
 * @param device Block-device capability.
 * @param geometry Receives lease geometry and limits.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_block_lease_query(uint32_t device, AstraBlockLeaseInfo *geometry);
/**
 * Submit one asynchronous block request.
 * @param device Block-device capability.
 * @param request Validated request and DMA buffer description.
 * @param block_request Receives request token.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_block_lease_submit(uint32_t device, const AstraBlockRequest *request,
                            uint32_t *block_request);
/**
 * Collect one block-request completion.
 * @param device Block-device capability.
 * @param block_request Request token returned by submit.
 * @param completion Receives completion status and transfer count.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_block_lease_collect(uint32_t device, uint32_t block_request,
                            AstraBlockCompletion *completion);
/**
 * Collect a block completion and report service work completed while waiting.
 * @param device Block-device capability.
 * @param block_request Request token returned by submit.
 * @param completion Receives completion status and transfer count.
 * @param serviced_completions Receives completions drained during the call.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_block_lease_collect_ex(uint32_t device,
                                     uint32_t block_request,
                                     AstraBlockCompletion *completion,
                                     uint32_t *serviced_completions);
/**
 * Query queue and shared-memory limits for a network lease.
 * @param device Network-device capability.
 * @param info Receives lease ABI and capacity information.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_network_lease_query(uint32_t device,
                                   AstraNetworkLeaseInfo *info);
/**
 * Execute queued network transport commands.
 * @param device Network-device capability.
 * @param request Shared command/completion ring description.
 * @param executed_commands Receives commands consumed.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_network_lease_execute(
    uint32_t device, const AstraNetworkTransportRequest *request,
    uint32_t *executed_commands);
/**
 * Query queue and shared-memory limits for a host-service lease.
 * @param device Host-service device capability.
 * @param info Receives lease ABI and capacity information.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_lease_query(uint32_t device, AstraHostLeaseInfo *info);
/**
 * Execute queued host-service transport commands.
 * @param device Host-service device capability.
 * @param request Shared command/completion ring description.
 * @param executed_commands Receives commands consumed.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_lease_execute(
    uint32_t device, const AstraHostTransportRequest *request,
    uint32_t *executed_commands);
/**
 * Open the low-latency shared host channel for a device lease.
 * @param device Host-service device capability.
 * @param channel Receives channel addresses, sizes, and generation.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_channel_open(uint32_t device,
                                 AstraHostChannelOpen *channel);
/**
 * Close the current process's shared host channel.
 * @param device Host-service device capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_channel_close(uint32_t device);
/**
 * Notify the host of a new producer position.
 * @param channel_address Valid shared channel address.
 * @param producer_position Monotonic producer byte position.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_channel_kick(uint32_t channel_address,
                                 uint32_t producer_position);
/**
 * Wait for host-channel consumer progress.
 * @param consumer_position Previously observed consumer position.
 * @param deadline_ns Absolute monotonic deadline.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_channel_wait(uint32_t consumer_position,
                                 uint64_t deadline_ns);
/**
 * Query the hardware text console dimensions.
 * @param device Console device capability.
 * @param columns Receives cell columns.
 * @param rows Receives cell rows.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_console_info(uint32_t device, uint32_t *columns,
                            uint32_t *rows);
/**
 * Write consecutive hardware-console cells.
 * @param device Console device capability.
 * @param cell Zero-based destination cell.
 * @param cells Encoded cell bytes.
 * @param count Number of cells.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_console_write(uint32_t device, uint32_t cell,
                             const uint8_t *cells, uint32_t count);
/**
 * Set hardware-console cursor position and visibility.
 * @param device Console device capability.
 * @param row Zero-based row.
 * @param column Zero-based column.
 * @param visible Nonzero to display the cursor.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_console_cursor(uint32_t device, uint32_t row,
                              uint32_t column, uint32_t visible);
/**
 * Submit one display frame to the graphics device.
 * @param device Display device capability.
 * @param request Frame surfaces and presentation metadata.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_display_submit(uint32_t device,
                              const AstraDisplayFrameRequest *request);
/**
 * Collect one submitted display-frame completion.
 * @param device Display device capability.
 * @param completion Receives frame sequence and status.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_display_collect(uint32_t device,
                               AstraDisplayFrameCompletion *completion);
/**
 * Read queued input events without exceeding caller capacity.
 * @param device Input device capability.
 * @param events Receives at most `capacity` events.
 * @param capacity Number of event records available.
 * @param count Receives events written.
 * @param flags Receives input-stream state flags.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_input_read(uint32_t device, AstraInputEvent *events,
                          uint32_t capacity, uint32_t *count,
                          uint32_t *flags);
/**
 * Terminate the current process with an application-defined status.
 * @param status Process exit status.
 */
void astra_process_exit(uint32_t status) __attribute__((noreturn));
/**
 * Terminate only the current thread.
 * @param status Thread completion status.
 */
void astra_thread_exit(uint32_t status) __attribute__((noreturn));

/**
 * Starting a program.
 *
 * `image` is the whole ELF, in this process's own memory; the kernel copies it
 * out through a bounded window rather than reading it where it lies. Each grant
 * names a handle *this* process already holds, under a name the child will know
 * it by, with rights that are a subset of the ones it is held by here. A launch
 * creates no authority, so there is no argument to this call that produces a
 * capability which did not exist a moment earlier.
 *
 * Nothing is validated here that the kernel validates: one answer to one
 * question. The handle that comes back carries QUERY, WAIT, SIGNAL, TERMINATE,
 * TRANSFER, and priority administration, and never DEBUG -- having started
 * something is not permission to read its account of itself.
 * @param image Complete immutable ELF image.
 * @param length Image byte length.
 * @param grants Capabilities and subset rights transferred to the child.
 * @param count Number of entries in `grants`.
 * @param arguments Packed arguments and environment, or NULL.
 * @param process_handle Receives waitable child-process capability.
 * @param process_id Receives stable child process identifier.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_launch(const void *image, uint32_t length,
                      const AstraLaunchGrant *grants, uint32_t count,
                      const AstraLaunchArguments *arguments,
                      uint32_t *process_handle, uint32_t *process_id);
/**
 * Supplies one borrowed file range at a time. The bytes remain valid until
 * the callback is invoked again; the loader submits them to the kernel before
 * asking for the next range.
 *
 * Once the callbacks and output pointers have been accepted, this function
 * owns the source and invokes `release` exactly once on every return path,
 * including an invalid or truncated image. Release happens before the child
 * is committed, so a source that pins storage or DMA can relinquish it before
 * the child becomes runnable. A release failure aborts the prepared child.
 * @param context Caller-owned launch source.
 * @param offset Requested byte offset.
 * @param length Maximum requested byte count.
 * @param bytes Receives borrowed bytes valid until the next callback.
 * @param moved Receives supplied byte count.
 * @return ASTRA_SYSCALL_* or source-specific failure status.
 */
typedef uint32_t (*AstraLaunchReadAt)(void *context, uint32_t offset,
                                     uint32_t length, const uint8_t **bytes,
                                     uint32_t *moved);
/**
 * Release a streamed launch source exactly once.
 * @param context Caller-owned launch source.
 * @return ASTRA_SYSCALL_* or source-specific failure status.
 */
typedef uint32_t (*AstraLaunchRelease)(void *context);
/**
 * Launch an ELF image supplied incrementally by positioned reads.
 * @param length Complete image byte length.
 * @param read_at Positioned-read callback.
 * @param release Mandatory callback invoked exactly once after acceptance.
 * @param context Context passed to both callbacks.
 * @param grants Capabilities and subset rights transferred to the child.
 * @param count Number of entries in `grants`.
 * @param arguments Packed arguments and environment, or NULL.
 * @param process_handle Receives waitable child-process capability.
 * @param process_id Receives stable child process identifier.
 * @return ASTRA_SYSCALL_* or callback failure status.
 */
uint32_t astra_launch_stream(uint32_t length, AstraLaunchReadAt read_at,
                             AstraLaunchRelease release, void *context,
                             const AstraLaunchGrant *grants, uint32_t count,
                             const AstraLaunchArguments *arguments,
                             uint32_t *process_handle, uint32_t *process_id);
/**
 * Pack argv-style values into a launch argument block.
 * @param arguments Header and offsets initialized on success.
 * @param storage Storage retained until launch or exec completes.
 * @param capacity Bytes available in `storage`.
 * @param source Launch-source value reported to the child.
 * @param count Number of values.
 * @param values Array of NUL-terminated UTF-8 arguments.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_launch_arguments_pack(
    AstraLaunchArguments *arguments, char *storage, uint32_t capacity,
    AstraLaunchSource source, uint32_t count, const char *const *values);
/**
 * Append name/value environment entries to a packed launch block.
 * @param arguments Existing packed argument header.
 * @param storage Its backing storage.
 * @param capacity Total bytes available in `storage`.
 * @param count Number of environment entries.
 * @param names UTF-8 variable names without '='.
 * @param values UTF-8 variable values.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_launch_environment_pack(
    AstraLaunchArguments *arguments, char *storage, uint32_t capacity,
    uint32_t count, const char *const *names, const char *const *values);
/**
 * Pack argv and envp into an in-place exec request.
 * @param request Request header initialized on success.
 * @param storage Backing storage retained until exec completes.
 * @param capacity Bytes available in `storage`.
 * @param source Launch-source value retained across exec.
 * @param argv NULL-terminated UTF-8 argument vector.
 * @param envp NULL-terminated `name=value` UTF-8 environment vector.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_exec_request_pack(AstraExecRequest *request, char *storage,
                                 uint32_t capacity,
                                 AstraLaunchSource source,
                                 char *const argv[], char *const envp[]);
/**
 * Clone the current process and current thread state.
 * @param process_handle Receives waitable child-process capability in parent.
 * @param process_id Receives child process identifier.
 * @return ASTRA_SYSCALL_* status; the ABI distinguishes parent and child.
 */
uint32_t astra_process_clone(uint32_t *process_handle, uint32_t *process_id);
/**
 * Queue a POSIX-style signal for a process.
 * @param process_handle Signal-capable process capability.
 * @param signal Signal number.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_signal(uint32_t process_handle, uint32_t signal);
/**
 * Terminate a process through an administrative capability.
 * @param process_handle Terminate-capable process capability.
 * @param reason Exit reason published to waiters.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_terminate(uint32_t process_handle, uint32_t reason);
/**
 * Suspend every runnable thread of a process.
 * @param process_handle Administrative process capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_suspend(uint32_t process_handle);
/**
 * Resume threads previously suspended through the process capability.
 * @param process_handle Administrative process capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_resume(uint32_t process_handle);
/**
 * Register an Astra process with the POSIX session service.
 * @param service POSIX process-service capability.
 * @param process_handle Registered process capability.
 * @param process_id Stable Astra process identifier.
 * @param flags Registration flags.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_register(uint32_t service,
                                      uint32_t process_handle,
                                      uint32_t process_id, uint32_t flags);
/**
 * Send a signal using POSIX process-selector semantics.
 * @param service POSIX process-service capability.
 * @param selector PID, process-group selector, or POSIX special value.
 * @param signal Signal number; zero checks existence and permission.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_signal(uint32_t service, int32_t selector,
                                    uint32_t signal);
/**
 * Send a signal to a POSIX process group.
 * @param service POSIX process-service capability.
 * @param group Process group identifier.
 * @param signal Signal number.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_signal_group(uint32_t service, int32_t group,
                                          uint32_t signal);
/**
 * Set a process's POSIX process group.
 * @param service POSIX process-service capability.
 * @param process PID, or zero for the caller.
 * @param group Group ID, or zero to use the selected PID.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_setpgid(uint32_t service, int32_t process,
                                     int32_t group);
/**
 * Create a POSIX session led by the caller.
 * @param service POSIX process-service capability.
 * @param reply Receives session and group identifiers.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_setsid(uint32_t service,
                                    AstraPosixProcessReply *reply);
/**
 * Query POSIX session metadata for a process.
 * @param service POSIX process-service capability.
 * @param process PID, or zero for the caller.
 * @param reply Receives PID, process group, session, and flags.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_query(uint32_t service, int32_t process,
                                   AstraPosixProcessReply *reply);
/**
 * Attach the caller's controlling terminal to its POSIX session.
 * @param service POSIX process-service capability.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_tty_attach(uint32_t service);
/**
 * Query the controlling terminal's foreground process group.
 * @param service POSIX process-service capability.
 * @param reply Receives terminal and session metadata.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_tty_foreground(
    uint32_t service, AstraPosixProcessReply *reply);
/**
 * Set the controlling terminal's foreground process group.
 * @param service POSIX process-service capability.
 * @param group Process group in the caller's session.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_tty_set_foreground(uint32_t service,
                                                int32_t group);
/**
 * Deliver a terminal-generated signal to its foreground group.
 * @param service POSIX process-service capability.
 * @param signal Signal number.
 * @return ASTRA_SYSCALL_* or service status.
 */
uint32_t astra_posix_process_tty_signal(uint32_t service, uint32_t signal);
/**
 * Atomically replace the current process image.
 * @param image Complete immutable ELF image.
 * @param length Image byte length.
 * @param request Packed argv, environment, and handoff state.
 * @return ASTRA_SYSCALL_* status; success never returns to the old image.
 */
uint32_t astra_process_exec(const void *image, uint32_t length,
                            const AstraExecRequest *request);

/**
 * Waiting for a child, which is the machine's ordinary wait named for what a
 * launcher does with it. A deadline of zero polls, and that is the form a
 * process hosting services must use: a wait that stops serving the child it is
 * waiting for is a deadlock this architecture makes easy to write.
 *
 * `exit_status` is the child's status and only ever that: a wait that timed out
 * established no status and publishes none.
 * @param handle Wait-capable child-process handle.
 * @param deadline_ns Absolute monotonic deadline, zero to poll, or FOREVER.
 * @param exit_status Receives child exit status only after successful wait.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_process_wait(uint32_t handle, uint64_t deadline_ns,
                            uint32_t *exit_status);

/**
 * Ports: the only way one process reaches another.
 *
 * Creating one yields both ends, and handing an end away is what publishes a
 * service. A send that does not fit answers ASTRA_SYSCALL_WOULD_BLOCK, which is
 * back pressure and not an error -- the port is the queue, and a caller that
 * retries is doing the right thing. A receive with nothing waiting answers the
 * same way.
 *
 * `astra_port_receive` reports the size the message needed even when it refused
 * for want of room, because that number is the point of the refusal.
 * @param message_max Maximum bytes accepted by one message.
 * @param byte_max Total queued message bytes.
 * @param receive_handle Receives the sole receive capability.
 * @param send_handle Receives a transferable send capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_port_create(uint32_t message_max, uint32_t byte_max,
                           uint32_t *receive_handle, uint32_t *send_handle);
/**
 * Send one message and optional transferred capabilities without blocking.
 * @param handle Port send capability.
 * @param message Message bytes.
 * @param size Message byte count.
 * @param handles Capabilities to transfer.
 * @param handle_count Number of transferred capabilities.
 * @return ASTRA_SYSCALL_* status, including WOULD_BLOCK for back pressure.
 */
uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t handle_count);
/**
 * Receive one queued message without blocking.
 * @param handle Port receive capability.
 * @param message Receives message bytes.
 * @param capacity Bytes available in `message`.
 * @param handles Receives transferred handles.
 * @param handle_capacity Number of handle slots available.
 * @param size Receives actual or required message bytes.
 * @param handle_count Receives actual or required handle count.
 * @return ASTRA_SYSCALL_* status, including WOULD_BLOCK when empty.
 */
uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count);
/**
 * Receive a message and its kernel-authenticated sender process ID.
 * @param handle Port receive capability.
 * @param message Receives message bytes.
 * @param capacity Bytes available in `message`.
 * @param handles Receives transferred handles.
 * @param handle_capacity Number of handle slots available.
 * @param size Receives actual or required message bytes.
 * @param handle_count Receives actual or required handle count.
 * @param sender Receives authenticated sender process ID.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_port_receive_from(
    uint32_t handle, void *message, uint32_t capacity, uint32_t *handles,
    uint32_t handle_capacity, uint32_t *size, uint32_t *handle_count,
    uint32_t *sender);

/**
 * Set a per-boot diagnostic event level through its control capability.
 * @param handle Event-control capability.
 * @param subsystem Event subsystem identifier.
 * @param level Maximum enabled event level.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_event_control_set(uint32_t handle, uint32_t subsystem,
                                 uint32_t level);
/**
 * Process queued event-control requests.
 * @param receive Control port receive capability.
 * @param budget Maximum requests to process.
 * @return Requests processed, or encoded failure status per ABI.
 */
uint32_t astra_event_control_pump(uint32_t receive, uint32_t budget);
/**
 * Proxy queued event-control requests to a target capability.
 * @param receive Proxy port receive capability.
 * @param target Event-control target capability.
 * @param budget Maximum requests to process.
 * @return Requests processed, or encoded failure status per ABI.
 */
uint32_t astra_event_control_proxy_pump(uint32_t receive, uint32_t target,
                                       uint32_t budget);

/**
 * The event channel. No handle, no binding and no capability: emitting is
 * universal, because an account of what happened that depends on a right has
 * holes exactly where something went wrong.
 *
 * A status still comes back and is still never acted on here -- it says the
 * call was malformed, not that permission was refused. Diagnostics a program
 * depends on are a program that stops working when the diagnostics do.
 * @param message Structured event message identifier.
 * @param flags Event flags.
 * @param payload Optional event payload.
 * @param length Payload byte count.
 * @return ASTRA_SYSCALL_* status for malformed input or success.
 */
uint32_t astra_event_emit(uint32_t message, uint32_t flags,
                          const void *payload, uint32_t length);

/**
 * What this thread is doing. Begin one where a unit of work starts -- a
 * keystroke reaching the shell, a launch, a boot step -- and every event
 * emitted until the next one is part of that story. Adopting is how a service
 * joins the story it was called from.
 *
 * Activities are flat. No parent, no spans: nesting brings lifetime questions,
 * and a system must not report causality it cannot substantiate.
 * @return New nonzero activity identifier.
 */
uint32_t astra_activity_begin(void);
/**
 * Adopt an incoming activity for subsequent emitted events.
 * @param activity Activity identifier, or zero for none.
 * @return Previously current activity identifier.
 */
uint32_t astra_activity_adopt(uint32_t activity);
/** @return Current thread's activity identifier, or zero for none. */
uint32_t astra_activity_current(void);
/**
 * Replace the current activity while explicitly receiving the prior value.
 * @param activity New activity identifier, or zero for none.
 * @param previous Receives prior activity when non-NULL.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_activity_exchange(uint32_t activity, uint32_t *previous);
/**
 * Reading the stream back: the other half of the reversal above. `cursor` is
 * the sequence already seen and is updated to what to pass next; `lost` counts
 * the records the ring displaced before this call reached them, which a reader
 * must be told rather than left to infer from a history that is mysteriously
 * short.
 *
 * The process handle must carry ASTRA_RIGHT_DEBUG and must name the caller.
 * Emitting needs no capability and reading does, because reading is every
 * process's events at once.
 * @param process_handle Debug-capable handle naming the caller.
 * @param slot Zero-based interrupt-endpoint slot.
 * @param info Receives endpoint state and counters.
 * @param slots Receives total endpoint-slot count.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_irq_endpoint_info(uint32_t process_handle, uint32_t slot,
                                 AstraIrqEndpointInfo *info,
                                 uint32_t *slots);
/**
 * Read structured events from the current process trace ring.
 * @param process_handle Debug-capable handle naming the caller.
 * @param cursor In/out sequence cursor; initialize to zero.
 * @param events Receives at most `capacity` drained records.
 * @param capacity Number of records available.
 * @param copied Receives records written.
 * @param lost Receives records overwritten before they could be read.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_trace_read(uint32_t process_handle, uint32_t *cursor,
                          AstraEventDrained *events, uint32_t capacity,
                          uint32_t *copied, uint32_t *lost);

/**
 * Emit text as a chain of unstructured diagnostic events.
 * @param bytes UTF-8 text bytes.
 * @param length Byte count.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_log_write(const void *bytes, uint32_t length);
/**
 * Emit ephemeral debug text retained only in the in-memory ring.
 * @param bytes UTF-8 text bytes.
 * @param length Byte count.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_log_debug(const void *bytes, uint32_t length);
/**
 * Emit one NUL-terminated UTF-8 diagnostic line.
 * @param text NUL-terminated text.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_log(const char *text);
/**
 * Emit a standardized operation failure.
 * @param operation NUL-terminated operation name.
 * @param status Failure status.
 * @return ASTRA_SYSCALL_* status from diagnostic emission.
 */
uint32_t astra_log_failure(const char *operation, uint32_t status);
/**
 * Format an assertion failure without allocation.
 * @param out Destination UTF-8 buffer.
 * @param capacity Bytes available in `out`.
 * @param file Source filename.
 * @param line Source line.
 * @param expression Failed expression text.
 * @return Bytes written excluding the terminator.
 */
uint32_t astra_assert_message(char *out, uint32_t capacity, const char *file,
                              uint32_t line, const char *expression);

/**
 * Report an assertion and terminate the current process.
 * @param file Source filename.
 * @param line Source line.
 * @param expression Failed expression text.
 */
void astra_assert_failed(const char *file, unsigned int line,
                         const char *expression) __attribute__((noreturn));
/**
 * Native application or service entry point supplied by the program.
 * @param startup Immutable validated startup record.
 * @return Process exit status.
 */
int astra_main(const AstraStartupInfo *startup);

#endif
