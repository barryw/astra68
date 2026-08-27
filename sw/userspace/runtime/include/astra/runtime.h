#ifndef ASTRA_RUNTIME_H
#define ASTRA_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <astra/block.h>
#include <astra/display.h>
#include <astra/event.h>
#include <astra/input.h>
#include <astra/library.h>
#include <astra/civil.h>
#include <astra/process.h>
#include <astra/network.h>
#include <astra/syscall.h>

/*
 * Exit status of a process that failed an assertion, tagged the same way the
 * supervisor tags its own result: "AS" in the high halfword, the failing
 * source line in the low one.
 */
#define ASTRA_ASSERT_STATUS_TAG 0x41530000u

/*
 * Four answers and a status. D4 joined the set when the clock started
 * returning the zone with the instant: a call that has to answer two things
 * about one moment cannot be split into two calls without straddling the
 * moment.
 */
typedef struct AstraSyscallResult {
    uint32_t status;
    uint32_t value0;
    uint32_t value1;
    uint32_t value2;
    uint32_t value3;
} AstraSyscallResult;

_Static_assert(sizeof(AstraSyscallResult) == 20u,
               "syscall-result layout changed");

int astra_startup_validate(const AstraStartupInfo *startup);
AstraLaunchSource astra_startup_launch_source(
    const AstraStartupInfo *startup);
const char *astra_startup_argument(const AstraStartupInfo *startup,
                                   uint32_t index);
const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *startup, const char *name);
uint32_t astra_service_ready(uint32_t bootstrap, uint32_t status,
                             const uint32_t *handles,
                             uint32_t handle_count);

void astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
                    uint32_t argument2, uint32_t argument3,
                    uint32_t argument4, AstraSyscallResult *result);

uint32_t astra_yield(void);
uint32_t astra_close(uint32_t handle);
uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                                uint32_t *duplicate);
uint32_t astra_rt_event_create(uint32_t flags, uint32_t rights,
                               uint32_t *handle);
uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle);
uint32_t astra_rt_signal(uint32_t handle, uint32_t count,
                         uint32_t *woken);
uint32_t astra_rt_event_reset(uint32_t handle);
typedef struct AstraThreadStart {
    void (*entry)(uint32_t argument);
    uint32_t argument;
} AstraThreadStart;
uint32_t astra_rt_thread_create(const AstraThreadStart *start,
                                uint32_t priority, uint32_t rights,
                                uint32_t *handle, uint32_t *thread_id);
uint32_t astra_rt_area_create(uint32_t byte_size, uint32_t rights,
                           uint32_t *handle);
/* As above, with ASTRA_AREA_CREATE_* -- reserved rather than committed. */
uint32_t astra_rt_area_create_flagged(uint32_t byte_size, uint32_t rights,
                                   uint32_t flags, uint32_t *handle);
/* Hands back the pages of a reserved area; the reservation stays. */
uint32_t astra_rt_area_decommit(void *address, uint32_t byte_size,
                             uint32_t *released_pages);
uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                        void **address, uint32_t *byte_size);
uint32_t astra_rt_area_unmap(void *address);
uint32_t astra_rt_private_reserve(uint32_t byte_size, uint32_t permissions,
                                 void **address, uint32_t *mapped_span);
uint32_t astra_rt_private_decommit(void *address, uint32_t byte_size,
                                  uint32_t *released_pages);
uint32_t astra_rt_signal_configure(void (*trampoline)(int), void *stack_top,
                                   uint32_t blocked, uint32_t *pending,
                                   uint32_t *previous_blocked);
uint32_t astra_rt_interval_timer(uint64_t delay_ns, uint64_t interval_ns,
                                 uint64_t *old_delay_ns,
                                 uint64_t *old_interval_ns);
uint32_t astra_rt_interval_timer_get(uint64_t *delay_ns,
                                     uint64_t *interval_ns);
uint32_t astra_rt_signal_return(void);
uint32_t astra_rt_ring_create(uint32_t area, uint32_t offset,
                              uint32_t element_size, uint32_t capacity,
                              uint32_t flags, uint32_t *producer,
                              uint32_t *consumer);
uint32_t astra_rt_ring_read_try(uint32_t consumer, void *bytes,
                               uint32_t capacity, uint32_t *copied);
uint32_t astra_rt_ring_write_try(uint32_t producer, const void *bytes,
                                uint32_t length, uint32_t flags,
                                uint32_t *written);
uint32_t astra_rt_library_map(const void *image, uint32_t length,
                              uint32_t *base, uint32_t *span);
uint32_t astra_rt_library_attach(const AstraLibraryReference *reference,
                                 uint32_t *base, uint32_t *span);
uint32_t astra_query_abi(uint32_t *abi_version, uint32_t *process_handle,
                         uint32_t *thread_handle);
uint32_t astra_process_info(uint32_t handle, AstraProcessInfo *info);
uint32_t astra_process_priority(uint32_t handle, uint32_t priority,
                                uint32_t *previous_priority);
uint32_t astra_progress(uint32_t value);
uint32_t astra_device_query(uint32_t handle, AstraDeviceInfo *info);
uint32_t astra_device_reset(uint32_t handle);
uint64_t astra_clock_monotonic(void);
static inline uint32_t
astra_elapsed_microseconds(uint64_t from, uint64_t to)
{
    uint64_t elapsed;

    if (to <= from)
        return 0u;
    elapsed = (to - from) / 1000u;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}
uint32_t astra_clock_realtime(uint64_t *nanoseconds);
uint32_t astra_clock_realtime_zone(uint64_t *nanoseconds,
                                   AstraTimeZone *zone);
uint32_t astra_clock_set(uint32_t clock, uint64_t nanoseconds);
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline_ns,
                        uint32_t *detail);
uint32_t astra_wait_multiple(const uint32_t *handles, uint32_t count,
                             uint64_t deadline_ns, uint32_t *index,
                             uint32_t *detail);
uint32_t astra_irq_arm(uint32_t handle);
uint32_t astra_irq_mask(uint32_t handle);
uint32_t astra_irq_read(uint32_t handle, AstraIrqRecord *record,
                        uint32_t *events);
uint32_t astra_irq_ack(uint32_t handle, uint32_t sequence);
uint32_t astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info);
uint32_t astra_block_lease_query(uint32_t device, AstraBlockLeaseInfo *geometry);
uint32_t astra_block_lease_submit(uint32_t device, const AstraBlockRequest *request,
                            uint32_t *block_request);
uint32_t astra_block_lease_collect(uint32_t device, uint32_t block_request,
                                   AstraBlockCompletion *completion);
uint32_t astra_network_lease_query(uint32_t device,
                                   AstraNetworkLeaseInfo *info);
uint32_t astra_network_lease_execute(
    uint32_t device, const AstraNetworkTransportRequest *request,
    uint32_t *executed_commands);
uint32_t astra_console_info(uint32_t device, uint32_t *columns,
                            uint32_t *rows);
uint32_t astra_console_write(uint32_t device, uint32_t cell,
                             const uint8_t *cells, uint32_t count);
uint32_t astra_console_cursor(uint32_t device, uint32_t row,
                              uint32_t column, uint32_t visible);
uint32_t astra_display_submit(uint32_t device,
                              const AstraDisplayFrameRequest *request);
uint32_t astra_display_collect(uint32_t device,
                               AstraDisplayFrameCompletion *completion);
uint32_t astra_input_read(uint32_t device, AstraInputEvent *events,
                          uint32_t capacity, uint32_t *count,
                          uint32_t *flags);
void astra_process_exit(uint32_t status) __attribute__((noreturn));
void astra_thread_exit(uint32_t status) __attribute__((noreturn));

/*
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
 * question. The handle that comes back carries QUERY, WAIT and TERMINATE, and
 * never DEBUG -- having started something is not permission to read its account
 * of itself.
 */
uint32_t astra_launch(const void *image, uint32_t length,
                      const AstraLaunchGrant *grants, uint32_t count,
                      const AstraLaunchArguments *arguments,
                      uint32_t *process_handle, uint32_t *process_id);
/*
 * Supplies one borrowed file range at a time. The bytes remain valid until
 * the callback is invoked again; the loader submits them to the kernel before
 * asking for the next range.
 *
 * Once the callbacks and output pointers have been accepted, this function
 * owns the source and invokes `release` exactly once on every return path,
 * including an invalid or truncated image. Release happens before the child
 * is committed, so a source that pins storage or DMA can relinquish it before
 * the child becomes runnable. A release failure aborts the prepared child.
 */
typedef uint32_t (*AstraLaunchReadAt)(void *context, uint32_t offset,
                                     uint32_t length, const uint8_t **bytes,
                                     uint32_t *moved);
typedef uint32_t (*AstraLaunchRelease)(void *context);
uint32_t astra_launch_stream(uint32_t length, AstraLaunchReadAt read_at,
                             AstraLaunchRelease release, void *context,
                             const AstraLaunchGrant *grants, uint32_t count,
                             const AstraLaunchArguments *arguments,
                             uint32_t *process_handle, uint32_t *process_id);
uint32_t astra_launch_arguments_pack(
    AstraLaunchArguments *arguments, char *storage, uint32_t capacity,
    AstraLaunchSource source, uint32_t count, const char *const *values);
uint32_t astra_launch_environment_pack(
    AstraLaunchArguments *arguments, char *storage, uint32_t capacity,
    uint32_t count, const char *const *names, const char *const *values);
uint32_t astra_exec_request_pack(AstraExecRequest *request, char *storage,
                                 uint32_t capacity,
                                 AstraLaunchSource source,
                                 char *const argv[], char *const envp[]);
uint32_t astra_process_clone(uint32_t *process_handle, uint32_t *process_id);
uint32_t astra_process_exec(const void *image, uint32_t length,
                            const AstraExecRequest *request);

/*
 * Waiting for a child, which is the machine's ordinary wait named for what a
 * launcher does with it. A deadline of zero polls, and that is the form a
 * process hosting services must use: a wait that stops serving the child it is
 * waiting for is a deadlock this architecture makes easy to write.
 *
 * `exit_status` is the child's status and only ever that: a wait that timed out
 * established no status and publishes none.
 */
uint32_t astra_process_wait(uint32_t handle, uint64_t deadline_ns,
                            uint32_t *exit_status);

/*
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
 */
uint32_t astra_rt_port_create(uint32_t message_max, uint32_t byte_max,
                           uint32_t *receive_handle, uint32_t *send_handle);
uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t handle_count);
uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count);
/* As above, also returning the kernel-authenticated sender process id. */
uint32_t astra_port_receive_from(
    uint32_t handle, void *message, uint32_t capacity, uint32_t *handles,
    uint32_t handle_capacity, uint32_t *size, uint32_t *handle_count,
    uint32_t *sender);

/* Temporary per-boot event-level control over a dedicated capability. */
uint32_t astra_event_control_set(uint32_t handle, uint32_t subsystem,
                                 uint32_t level);
uint32_t astra_event_control_pump(uint32_t receive, uint32_t budget);
uint32_t astra_event_control_proxy_pump(uint32_t receive, uint32_t target,
                                       uint32_t budget);

/*
 * The event channel. No handle, no binding and no capability: emitting is
 * universal, because an account of what happened that depends on a right has
 * holes exactly where something went wrong.
 *
 * A status still comes back and is still never acted on here -- it says the
 * call was malformed, not that permission was refused. Diagnostics a program
 * depends on are a program that stops working when the diagnostics do.
 */
uint32_t astra_event_emit(uint32_t message, uint32_t flags,
                          const void *payload, uint32_t length);

/*
 * What this thread is doing. Begin one where a unit of work starts -- a
 * keystroke reaching the shell, a launch, a boot step -- and every event
 * emitted until the next one is part of that story. Adopting is how a service
 * joins the story it was called from.
 *
 * Activities are flat. No parent, no spans: nesting brings lifetime questions,
 * and a system must not report causality it cannot substantiate.
 */
uint32_t astra_activity_begin(void);
uint32_t astra_activity_adopt(uint32_t activity);
uint32_t astra_activity_current(void);
uint32_t astra_activity_exchange(uint32_t activity, uint32_t *previous);
/*
 * Reading the stream back: the other half of the reversal above. `cursor` is
 * the sequence already seen and is updated to what to pass next; `lost` counts
 * the records the ring displaced before this call reached them, which a reader
 * must be told rather than left to infer from a history that is mysteriously
 * short.
 *
 * The process handle must carry ASTRA_RIGHT_DEBUG and must name the caller.
 * Emitting needs no capability and reading does, because reading is every
 * process's events at once.
 */
uint32_t astra_irq_endpoint_info(uint32_t process_handle, uint32_t slot,
                                 AstraIrqEndpointInfo *info,
                                 uint32_t *slots);
uint32_t astra_trace_read(uint32_t process_handle, uint32_t *cursor,
                          AstraEventDrained *events, uint32_t capacity,
                          uint32_t *copied, uint32_t *lost);

/* A line of text, as a chain of ASTRA_EVENT_MESSAGE_UNSTRUCTURED events. */
uint32_t astra_log_write(const void *bytes, uint32_t length);
/* Ring only: the event store drops debug records at drain, never storing. */
uint32_t astra_log_debug(const void *bytes, uint32_t length);
uint32_t astra_log(const char *text);
uint32_t astra_log_failure(const char *operation, uint32_t status);
uint32_t astra_assert_message(char *out, uint32_t capacity, const char *file,
                              uint32_t line, const char *expression);

/* Declared identically by the freestanding <assert.h> vendored code sees. */
void astra_assert_failed(const char *file, unsigned int line,
                         const char *expression) __attribute__((noreturn));
void astra_thread_exit(uint32_t status) __attribute__((noreturn));

int astra_main(const AstraStartupInfo *startup);

#endif
