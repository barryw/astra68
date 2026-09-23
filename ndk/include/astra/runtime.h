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
 * Reserve the largest currently available contiguous private VM extent.
 *
 * This is intended for growable arenas such as a contiguous POSIX heap. The
 * kernel returns the largest run that is at least @p minimum_byte_size; pages
 * remain demand committed, so the reservation consumes address space rather
 * than physical memory until touched.
 *
 * @param minimum_byte_size Smallest useful reservation, rounded to a VM slot.
 * @param permissions Requested access permissions.
 * @param address Receives reservation base.
 * @param mapped_span Receives the actual reserved byte span.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_private_reserve_largest(uint32_t minimum_byte_size,
                                         uint32_t permissions,
                                         void **address,
                                         uint32_t *mapped_span);
/**
 * Commit every page touched by a range in a writable private reservation.
 *
 * This is useful when the caller is certain it will immediately fill the
 * complete range. Ordinary allocations remain demand committed.
 *
 * @param address First byte that will be written.
 * @param byte_size Number of bytes that will be written.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_private_commit(void *address, uint32_t byte_size);
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
 * Decide whether the process allocator may grow.
 *
 * A compatibility layer such as POSIX may install one policy callback to
 * enforce its resource contract.  The runtime remains the sole owner of the
 * process heap and calls the policy before extending it.
 *
 * @param current_bytes Current allocator high-water span.
 * @param growth_bytes Requested additional aligned bytes.
 * @param context Opaque context supplied with the callback.
 * @return Nonzero to permit the growth, zero to reject it.
 */
typedef int (*AstraRuntimeGrowthPolicy)(uint32_t current_bytes,
                                        uint32_t growth_bytes,
                                        void *context);

/**
 * Install or clear the process allocator's growth policy.
 * @param policy Policy callback, or NULL to allow all address-space growth.
 * @param context Opaque callback context.
 */
void astra_runtime_set_growth_policy(AstraRuntimeGrowthPolicy policy,
                                     void *context);
/** @return Bytes currently spanned by the process allocator. */
uint32_t astra_runtime_allocation_span(void);
/**
 * Adjust the process allocator break for C-runtime compatibility.
 *
 * This is the canonical low-level growth primitive used by POSIX `sbrk`.
 * General Astra code should use its C allocator instead.
 *
 * @param increment Signed byte adjustment.
 * @return Previous break on growth, new break on shrink, or NULL on failure.
 */
void *astra_runtime_sbrk(intptr_t increment);
/**
 * Allocate storage from the runtime-owned process heap.
 * @param size Minimum usable byte count; zero requests one byte.
 * @return Suitably aligned storage, or NULL when the heap cannot grow.
 */
void *astra_runtime_allocate(size_t size);
/**
 * Allocate at least @p size bytes at a power-of-two address alignment.
 *
 * The returned block belongs to the same allocator as ordinary runtime
 * allocations and must be released with astra_runtime_deallocate().
 *
 * @param alignment Required nonzero power-of-two alignment.
 * @param size Minimum usable byte count; zero requests one byte.
 * @return Aligned storage, or NULL for invalid alignment or exhaustion.
 */
void *astra_runtime_allocate_aligned(size_t alignment, size_t size);
/**
 * Release runtime-owned storage.
 * @param pointer Storage returned by a runtime allocation function, or NULL.
 */
void astra_runtime_deallocate(void *pointer);
/**
 * Allocate a zero-filled array, rejecting size overflow.
 * @param count Number of elements.
 * @param size Bytes per element.
 * @return Zero-filled storage, or NULL on overflow or exhaustion.
 */
void *astra_runtime_callocate(size_t count, size_t size);
/**
 * Resize runtime-owned storage while preserving the common byte prefix.
 * @param pointer Existing runtime allocation, or NULL to allocate.
 * @param size Requested byte count; zero releases @p pointer.
 * @return Resized storage, or NULL for a zero-size request or exhaustion. On
 * exhaustion the original allocation remains valid.
 */
void *astra_runtime_reallocate(void *pointer, size_t size);
/**
 * Report the usable size of runtime-owned storage.
 * @param pointer Runtime allocation to inspect.
 * @return Usable byte size, or zero when @p pointer is not an allocation.
 */
size_t astra_runtime_allocation_size(void *pointer);
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
 * Attach a previously registered shared-library mapping transaction.
 * The caller must relocate the mapping and then commit `load_handle`; closing
 * that handle instead rolls the mapping back.
 * @param reference Validated library reference.
 * @param base Receives mapped base address.
 * @param span Receives mapped byte span.
 * @param load_handle Receives the transaction capability.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_library_attach(const AstraLibraryReference *reference,
                                 uint32_t *base, uint32_t *span,
                                 uint32_t *load_handle);
/**
 * Attach the sole resident library matching an ABI identity.
 * Returns WOULD_BLOCK when no unique resident match exists, allowing the
 * caller to resolve an exact provider reference and retry with
 * astra_rt_library_attach().
 */
uint32_t astra_rt_library_attach_resident(const char *identity,
                                          uint32_t *base, uint32_t *span,
                                          uint32_t *load_handle);
/**
 * Finalize an interpreted process after eager relocation.
 *
 * This one-way call installs the combined initial-exec TLS template for the
 * current and all future threads and seals every kernel-retained RELRO range.
 * It must run after all dependency relocation and before constructors or
 * application-created threads. On success ownership of the template storage
 * transfers to the process: its pages become immutable, must not be freed or
 * reused, and are released with the process address space. On failure the
 * caller retains ownership.
 *
 * @param tls_template Page-aligned, exclusively owned initialized TLS storage,
 *                     or NULL when empty.
 * @param tls_size Exact initialized-plus-zero-fill template size.
 * @param tls_alignment Required power-of-two base alignment; one when empty.
 * @param storage_size Page-rounded exclusive storage span; zero when empty.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_rt_process_dynamic_commit(const void *tls_template,
                                         uint32_t tls_size,
                                         uint32_t tls_alignment,
                                         uint32_t storage_size);
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
 * Query one thread visible through a capability.
 * @param handle Thread capability carrying the QUERY right.
 * @param info ABI-sized structure receiving thread information.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_thread_info(uint32_t handle, AstraThreadInfo *info);
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
 * Read a page of resident shared-library/process mapping records.
 * @param observer Process capability authorizing observation.
 * @param start Zero-based resident-library ordinal to read first.
 * @param records Receives records, or NULL when `capacity` is zero.
 * @param capacity Number of records available.
 * @param moved Receives records written in this page when non-NULL.
 * @param library_count Receives the current total record count.
 * @return ASTRA_SYSCALL_* status. Repeat at `start + moved` until the returned
 * total is reached; the transfer size never limits system residency.
 */
uint32_t astra_library_snapshot(uint32_t observer,
                               uint32_t start,
                               AstraProcLibrarySnapshot *records,
                               uint32_t capacity,
                               uint32_t *moved,
                               uint32_t *library_count);
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

    if (state == NULL || ((uintptr_t)state & (sizeof(*state) - 1u)) != 0u)
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

    if (state == NULL || ((uintptr_t)state & (sizeof(*state) - 1u)) != 0u)
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
 * @param process_id Receives the child PID. The returned handle is the stable
 * process identity and control authority; PIDs may be reused after exit.
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
 * including an invalid or truncated image. Release happens before the object
 * is committed, so a source that pins storage or DMA can relinquish it before
 * publication. A release failure aborts the transaction.
 * @param context Caller-owned immutable source.
 * @param offset Requested byte offset.
 * @param length Maximum requested byte count. A source may return a shorter
 * nonempty prefix in `moved`; executable segment prefixes end on a VM-page
 * boundary unless they complete the requested range.
 * @param bytes Receives borrowed bytes valid until the next callback.
 * @param moved Receives supplied byte count.
 * @return ASTRA_SYSCALL_* or source-specific failure status.
 */
typedef uint32_t (*AstraReadAt)(void *context, uint32_t offset,
                               uint32_t length, const uint8_t **bytes,
                               uint32_t *moved);
/**
 * Release a streamed immutable source exactly once.
 * @param context Caller-owned source.
 * @return ASTRA_SYSCALL_* or source-specific failure status.
 */
typedef uint32_t (*AstraSourceRelease)(void *context);
/**
 * One immutable input to a streamed kernel transaction.
 *
 * The kernel requests exact ranges and never learns a path or filesystem
 * protocol. `release` is invoked exactly once after this source has been
 * accepted or the enclosing transaction aborts.
 */
typedef struct AstraReadSource {
    /** Complete ELF image byte length. */
    uint32_t length;
    /** Positioned reader for exact kernel-requested ranges. */
    AstraReadAt read_at;
    /** Mandatory source-release callback. */
    AstraSourceRelease release;
    /** Opaque value supplied to `read_at` and `release`. */
    void *context;
} AstraReadSource;

/**
 * Open the exact interpreter named by a program's PT_INTERP record.
 *
 * The callback owns provider policy. On success it initializes @p source with
 * an immutable source whose release callback is valid; the executable launcher
 * then owns that source and releases it exactly once.
 *
 * @param context Resolver-owned state.
 * @param identity Exact NUL-terminated interpreter identity.
 * @param source Receives the opened interpreter source.
 * @return ASTRA_SYSCALL_* status.
 */
typedef uint32_t (*AstraInterpreterOpen)(void *context,
                                         const char *identity,
                                         AstraReadSource *source);
/**
 * Prepare personality handoff after executable I/O has completely stopped.
 *
 * Filesystem-backed personalities use this point to snapshot transport state:
 * every program and interpreter read and close has completed, while the old
 * image is still intact. The callback may update @p request but must not retain
 * it past the call.
 *
 * @param context Caller-owned preparation state.
 * @param request Mutable exec request supplied to the kernel next.
 * @return ASTRA_SYSCALL_* status.
 */
typedef uint32_t (*AstraExecPrepare)(void *context,
                                     AstraExecRequest *request);

/** ABI size of AstraProcessLoadProfile. */
#define ASTRA_PROCESS_LOAD_PROFILE_SIZE 104u

/**
 * Measured stages of one streamed process-load transaction.
 *
 * All durations are monotonic nanoseconds measured by the launching thread.
 * Source time covers only positioned-read callbacks; kernel time is split by
 * the transaction operation that consumed it. Counts and byte totals make a
 * timing sample comparable across different ELF layouts.
 */
typedef struct AstraProcessLoadProfile {
    /** Must be ASTRA_PROCESS_LOAD_PROFILE_SIZE on input. */
    uint32_t size;
    /** Reserved; must be zero. */
    uint32_t reserved;
    /** Positioned source callbacks completed or attempted. */
    uint64_t source_reads;
    /** Bytes requested from all executable sources. */
    uint64_t source_bytes;
    /** Kernel image-write calls completed or attempted. */
    uint64_t kernel_writes;
    /** Bytes submitted through kernel image-write calls. */
    uint64_t kernel_write_bytes;
    /** Complete measured transaction duration. */
    uint64_t total_ns;
    /** Time spent in positioned source callbacks. */
    uint64_t source_read_ns;
    /** Time spent beginning and validating the program image. */
    uint64_t kernel_begin_ns;
    /** Time spent registering and validating the interpreter image. */
    uint64_t kernel_interpreter_ns;
    /** Time spent submitting requested image ranges to the kernel. */
    uint64_t kernel_write_ns;
    /** Time spent creating the process, mappings, handles, and first thread. */
    uint64_t kernel_create_ns;
    /** Time spent releasing immutable executable sources. */
    uint64_t source_release_ns;
    /** Time spent committing and making the child runnable. */
    uint64_t kernel_commit_ns;
} AstraProcessLoadProfile;

/** ABI size of AstraExecutableLoadProfile. */
#define ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE 128u

/** Complete profile for the canonical executable-launch path. */
typedef struct AstraExecutableLoadProfile {
    /** Must be ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE on input. */
    uint32_t size;
    /** Reserved; must be zero. */
    uint32_t reserved;
    /** Time spent validating and reading PT_INTERP. */
    uint64_t executable_probe_ns;
    /** Time spent opening the exact interpreter provider. */
    uint64_t interpreter_open_ns;
    /** Atomic kernel load transaction and source-I/O measurements. */
    AstraProcessLoadProfile transaction;
} AstraExecutableLoadProfile;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraProcessLoadProfile) ==
                   ASTRA_PROCESS_LOAD_PROFILE_SIZE,
               "process-load profile ABI size changed");
_Static_assert(sizeof(AstraExecutableLoadProfile) ==
                   ASTRA_EXECUTABLE_LOAD_PROFILE_SIZE,
               "executable-load profile ABI size changed");
/** @endcond */
/**
 * Discover the exact interpreter identity carried by an executable.
 *
 * This is a non-owning probe: it performs positioned reads but never invokes
 * `source->release`.  An empty result identifies a static executable.  The
 * kernel remains the executable-format authority and validates the complete
 * image during the subsequent launch transaction.
 *
 * @param source Open immutable executable source retained by the caller.
 * @param identity Receives a NUL-terminated PT_INTERP identity.
 * @param capacity Bytes available at `identity`, including its terminator.
 * @param identity_length Receives the identity length excluding its NUL, or
 * zero for a static executable.
 * @return ASTRA_SYSCALL_OK, ASTRA_SYSCALL_INVALID_ARGUMENT for an invalid
 * executable or output buffer, or ASTRA_SYSCALL_IO_ERROR for a failed read.
 */
uint32_t astra_executable_interpreter(
    const AstraReadSource *source, char *identity, uint32_t capacity,
    uint32_t *identity_length);
/**
 * Launch an ELF image supplied incrementally by positioned reads.
 * @param length Complete image byte length.
 * @param read_at Positioned-read callback.
 * @param release Mandatory callback invoked exactly once after acceptance.
 * @param context Context passed to both callbacks.
 * @param grants Capabilities and subset rights transferred to the child.
 * @param count Number of entries in `grants`.
 * @param arguments Packed arguments and environment, or NULL.
 * @param profile Optional initialized profile receiving resolution and load
 * measurements.
 * @param process_handle Receives waitable child-process capability.
 * @param process_id Receives the child PID. The returned handle is the stable
 * process identity and control authority; PIDs may be reused after exit.
 * @return ASTRA_SYSCALL_* or callback failure status.
 */
uint32_t astra_launch_stream(uint32_t length, AstraReadAt read_at,
                             AstraSourceRelease release, void *context,
                             const AstraLaunchGrant *grants, uint32_t count,
                             const AstraLaunchArguments *arguments,
                             AstraProcessLoadProfile *profile,
                             uint32_t *process_handle, uint32_t *process_id);
/**
 * Launch a dynamically linked program with its exact resolved interpreter.
 *
 * Filesystem lookup and version selection remain in the supervisor. This
 * function only streams the two already-open images into one atomic kernel
 * transaction. Both sources are released exactly once before the child can
 * become runnable, including every failure path.
 *
 * @param program Dynamically linked ET_EXEC image carrying PT_INTERP.
 * @param interpreter Exact ET_DYN interpreter selected for that identity.
 * @param grants Capabilities and subset rights transferred to the child.
 * @param count Number of entries in `grants`.
 * @param arguments Packed arguments and environment, or NULL.
 * @param profile Optional initialized profile receiving measured stages.
 * @param process_handle Receives waitable child-process capability.
 * @param process_id Receives the child PID.
 * @return ASTRA_SYSCALL_* or callback failure status.
 */
uint32_t astra_launch_dynamic_stream(
    const AstraReadSource *program,
    const AstraReadSource *interpreter,
    const AstraLaunchGrant *grants, uint32_t count,
    const AstraLaunchArguments *arguments, AstraProcessLoadProfile *profile,
    uint32_t *process_handle, uint32_t *process_id);

/**
 * Launch one executable through the canonical static-or-dynamic path.
 *
 * The executable's PT_INTERP record is the only selector. Static images are
 * streamed directly; dynamic images are paired atomically with the exact
 * interpreter returned by @p open_interpreter. The function owns @p program
 * on entry and releases every successfully opened source exactly once on all
 * return paths.
 *
 * @param program Open immutable executable source.
 * @param open_interpreter Exact-provider resolver for dynamic images.
 * @param interpreter_context Context passed to @p open_interpreter.
 * @param grants Capabilities and subset rights transferred to the child.
 * @param count Number of entries in @p grants.
 * @param arguments Packed arguments and environment, or NULL.
 * @param profile Optional initialized profile receiving measured stages.
 * @param process_handle Receives waitable child-process capability.
 * @param process_id Receives the child PID.
 * @return ASTRA_SYSCALL_* or source/resolver failure status.
 */
uint32_t astra_launch_executable_stream(
    const AstraReadSource *program,
    AstraInterpreterOpen open_interpreter, void *interpreter_context,
    const AstraLaunchGrant *grants, uint32_t count,
    const AstraLaunchArguments *arguments,
    AstraExecutableLoadProfile *profile,
    uint32_t *process_handle, uint32_t *process_id);
/**
 * Atomically replace the current image through the canonical executable path.
 *
 * The executable's PT_INTERP record is the only selector. Static images are
 * installed directly; dynamic images are paired with the exact interpreter
 * returned by @p open_interpreter. The function owns @p program on entry and
 * releases every successfully opened source exactly once on every failure
 * path. Success does not return to the old image.
 *
 * @param program Open immutable executable source.
 * @param open_interpreter Exact-provider resolver for dynamic images.
 * @param interpreter_context Context passed to @p open_interpreter.
 * @param prepare Optional handoff preparation after all source I/O completes.
 * @param prepare_context Context passed to @p prepare.
 * @param request Packed argv, environment, and personality handoff state.
 * @return ASTRA_SYSCALL_* or source/resolver failure status.
 */
uint32_t astra_exec_executable_stream(
    const AstraReadSource *program,
    AstraInterpreterOpen open_interpreter, void *interpreter_context,
    AstraExecPrepare prepare, void *prepare_context,
    AstraExecRequest *request);
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
 * @param process_id Astra PID registered with the POSIX process service.
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
