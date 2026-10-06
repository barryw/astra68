/*
 * The diagnostic channel a program has to talk about itself.
 *
 * Before this there was none. A process not holding the display lease could
 * say exactly two things: a monotonic integer through the progress counter,
 * and one halfword of exit status on its way out. Anything with a bug in it
 * was diagnosed by inference.
 *
 * It is not authority any more. Emitting needs no capability at all, because a
 * machine whose account of what happened depends on one has holes exactly
 * where something went wrong; ASTRA_RIGHT_DEBUG gates reading other processes'
 * events instead. Nothing here can be refused for want of a right, and the
 * status that comes back means the call was malformed.
 *
 * A line longer than one event's payload is emitted as a chain rather than a
 * longer record. Splitting here rather than in the kernel keeps the syscall
 * one call for one event, and the record one slot wide.
 */

#include <astra/event.h>
#include <astra/event_emit.h>

#include <astra/address_space.h>
#include <astra/endian.h>
#include <astra/log_ring.h>
#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/time_page.h>

#include <string.h>

#include "log_internal.h"

uint32_t
astra_event_emit(uint32_t message, uint32_t flags, const void *payload,
                 uint32_t length)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_LOG_WRITE, message, flags,
                   (uint32_t)(uintptr_t)payload, length, 0u, &result);
    return result.status;
}

uint32_t
astra_trace_read(uint32_t process_handle, uint32_t *cursor,
                 AstraEventDrained *events, uint32_t capacity,
                 uint32_t *copied, uint32_t *lost)
{
    AstraSyscallResult result;

    if (cursor == NULL || events == NULL || copied == NULL || lost == NULL ||
        capacity == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    *copied = 0u;
    *lost = 0u;
    astra_syscall5(ASTRA_SYSCALL_TRACE_READ, process_handle, *cursor,
                   (uint32_t)(uintptr_t)events, capacity, 0u, &result);
    if (result.status != ASTRA_SYSCALL_OK) {
        return result.status;
    }
    *copied = result.value0;
    *cursor = result.value1;
    *lost = result.value2;
    return ASTRA_SYSCALL_OK;
}

/*
 * What an interrupt endpoint is doing, for a program that has the diagnostic
 * capability. `slots` is filled on every call, including a refused one, so a
 * caller sizes its loop from the machine rather than from a constant.
 */
uint32_t
astra_irq_endpoint_info(uint32_t process_handle, uint32_t slot,
                        AstraIrqEndpointInfo *info, uint32_t *slots)
{
    AstraSyscallResult result;

    if (info == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    astra_syscall5(ASTRA_SYSCALL_IRQ_ENDPOINT_INFO, process_handle, slot,
                   (uint32_t)(uintptr_t)info, 0u, 0u, &result);
    if (slots != NULL) {
        *slots = result.value0;
    }
    return result.status;
}

static uint32_t
append(char *out, uint32_t capacity, uint32_t length, const char *text)
{
    while (text != NULL && *text != '\0' && length + 1u < capacity) {
        out[length++] = *text++;
    }
    return length;
}

static uint32_t
append_number(char *out, uint32_t capacity, uint32_t length, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;

    if (value == 0u) {
        return append(out, capacity, length, "0");
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && length + 1u < capacity) {
        out[length++] = digits[--count];
    }
    return length;
}

static uint32_t
log_write_level(const void *bytes, uint32_t length, uint32_t level)
{
    const uint8_t *text = bytes;
    uint32_t sent = 0u;

    if (bytes == NULL || length == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (sent < length) {
        uint32_t chunk = length - sent;
        uint32_t flags = level | ASTRA_EVENT_FLAG_INLINE_STRING;
        uint32_t status;

        if (chunk > ASTRA_EVENT_ARGUMENT_MAX) {
            chunk = ASTRA_EVENT_ARGUMENT_MAX;
            flags |= ASTRA_EVENT_FLAG_CONTINUED;
        }
        status = astra_event_emit(ASTRA_EVENT_MESSAGE_UNSTRUCTURED, flags,
                                  text + sent, chunk);
        /*
         * A refused chunk ends the line rather than retrying it. The caller
         * gets the status of the first thing that went wrong, and a partial
         * line on the console is honest about having been cut off.
         */
        if (status != ASTRA_SYSCALL_OK) {
            return status;
        }
        sent += chunk;
    }
    return ASTRA_SYSCALL_OK;
}

#ifndef ASTRA_LOG_STARTUP_ADDRESS
#define ASTRA_LOG_STARTUP_ADDRESS ((const AstraStartupInfo *)(uintptr_t) \
                                   ASTRA_STARTUP_ADDRESS_START)
#endif

/*
 * The process's log ring, made on the first line that wants it. Setup is
 * claimed with one CAS; a thread that logs while another is setting up uses
 * LOG_WRITE for that line rather than wait (docs/SYSTEM_LOG.md section 3).
 *
 * log_state is one word: the setup state in the low two bits, and above them
 * the generation, the number of setups that have succeeded. A writer loads it
 * once per line and uses only the producer that generation names, so a writer
 * preempted across a re-setup finishes in the ring it reserved in, and its
 * report of a dead ring can only retire the setup it saw (log_ring_died).
 */
#define LOG_SETUP_IDLE   0u
#define LOG_SETUP_BUSY   1u
#define LOG_SETUP_READY  2u
#define LOG_STATE_MASK   3u
#define LOG_GENERATION_SHIFT 2u
#define LOG_SETUP_RETRY_AFTER 64u
/*
 * ponytail: one producer per successful setup, never reused, so a late writer
 * can only write into the dead ring it reserved in. A dead ring's mapping and
 * producer endpoint stay too (64 KiB and one handle each). A new setup follows
 * only a ring the live service let go of (refused or reaped) or a failed exec;
 * after 8 the process stays on LOG_WRITE for good, which bounds that leak at
 * 512 KiB of reservation.
 */
#define LOG_PRODUCERS    ASTRA_LOG_SETUPS_MAX

static uint32_t log_state;
static uint32_t log_setup_backoff;
/*
 * The service is dead: a setup was refused PEER_DEAD. A restarted service
 * makes a new port that the startup handle will never name, so this latches
 * LOG_WRITE for good, and a forked child keeps it (it holds the same handle).
 * Separate from the generation, which counts rings the live service let go.
 */
static uint32_t log_service_dead;
static AstraLogProducer log_producers[LOG_PRODUCERS];
/* Each producer's mapping, the address astra_rt_area_map returned. */
static void *log_producer_areas[LOG_PRODUCERS];
/* Each producer's own endpoint, its doorbell context: held so the process's
 * death reaches the service, and polled to see the service let go of it. */
static uint32_t log_producer_endpoints[LOG_PRODUCERS];
static uint32_t log_port;
static AstraMessageHeader log_doorbell; /* written by setup before READY */

/*
 * The doorbell. Never blocks: a full queue is WOULD_BLOCK, which means a
 * wake is already pending. PEER_DEAD from the port reports the service gone.
 *
 * On the drop path the ring is full, and that is also what a ring the service
 * refused or reaped looks like: it closed the consumer end, and its port
 * still takes doorbells. So there, and only there (once per
 * ASTRA_LOG_DROP_DOORBELL_INTERVAL drops), the producer endpoint is polled:
 * PEER_DEAD or CLOSED means nobody will ever read this ring.
 */
static uint32_t
log_ring_notify(void *context, uint32_t dropping)
{
    uint32_t status;

    if (astra_port_send(log_port, &log_doorbell, sizeof(log_doorbell), NULL,
                        0u) == ASTRA_SYSCALL_PEER_DEAD)
        return 1u;
    if (!dropping)
        return 0u;
    status = astra_wait_one(*(const uint32_t *)context, 0u /* poll */, NULL);
    return status == ASTRA_SYSCALL_PEER_DEAD ||
           status == ASTRA_SYSCALL_CLOSED ? 1u : 0u;
}

static void
log_ident(char out[ASTRA_LOG_IDENT_MAX])
{
    const char *path = astra_startup_argument(ASTRA_LOG_STARTUP_ADDRESS, 0u);
    const char *name = path;
    uint32_t length = 0u;

    if (path == NULL)
        path = name = "?";
    for (const char *at = path; *at != '\0'; ++at)
        if (*at == '/')
            name = at + 1;
    while (name[length] != '\0' && length + 1u < ASTRA_LOG_IDENT_MAX) {
        out[length] = name[length];
        ++length;
    }
    while (length < ASTRA_LOG_IDENT_MAX)
        out[length++] = '\0';
}

/* A READY state word's producer: generation g names log_producers[g - 1]. */
static AstraLogProducer *
log_producer_of(uint32_t state)
{
    return &log_producers[(state >> LOG_GENERATION_SHIFT) - 1u];
}

/* Returns the READY state word when the ring is ready after this call, else
 * 0 (READY is never 0). */
static uint32_t
log_setup(void)
{
    /*
     * Static, not on the stack, so the 64-bit host fake can read them through
     * the 32-bit syscall ABI. Safe only because the BUSY claim below is
     * exclusive: no other thread can hold BUSY until this one stores IDLE or
     * READY, and log_ring_died cannot release a BUSY claim (it CASes from a
     * READY word).
     */
    static AstraLogSetup setup;
    static uint32_t handles[2];
    const AstraStartupCapability *log;
    uint32_t area = 0u;
    uint32_t reader = 0u;
    uint32_t producer = 0u;
    uint32_t consumer = 0u;
    void *address = NULL;
    uint32_t size = 0u;
    uint32_t state = __atomic_load_n(&log_state, __ATOMIC_ACQUIRE);
    uint32_t generation = state >> LOG_GENERATION_SHIFT;
    uint32_t backoff;
    uint32_t status;

    if ((state & LOG_STATE_MASK) == LOG_SETUP_READY)
        return state;
    if ((state & LOG_STATE_MASK) != LOG_SETUP_IDLE ||
        generation == LOG_PRODUCERS ||
        __atomic_load_n(&log_service_dead, __ATOMIC_RELAXED) != 0u)
        return 0u;
    backoff = __atomic_load_n(&log_setup_backoff, __ATOMIC_RELAXED);
    if (backoff != 0u) {
        /* Racy by design: a lost decrement only retries a little later. */
        __atomic_store_n(&log_setup_backoff, backoff - 1u, __ATOMIC_RELAXED);
        if (backoff != 1u)
            return 0u;
    }
    if (!__atomic_compare_exchange_n(&log_state, &state,
                                     state | LOG_SETUP_BUSY, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return 0u;
    log = astra_startup_capability(ASTRA_LOG_STARTUP_ADDRESS,
                                   ASTRA_CAPABILITY_LOG);
    if (log == NULL || log->handle == 0u)
        goto unavailable;
    if (astra_rt_area_create_flagged(
            ASTRA_LOG_RING_AREA_SIZE,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER,
            ASTRA_AREA_CREATE_RESERVED, &area) != ASTRA_SYSCALL_OK)
        goto failed;
    if (astra_rt_area_map(area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                          &address, &size) != ASTRA_SYSCALL_OK ||
        size < ASTRA_LOG_RING_AREA_SIZE)
        goto failed;
    if (astra_rt_ring_create(area, ASTRA_LOG_RING_OFFSET,
                             ASTRA_LOG_RING_SLOT_SIZE,
                             ASTRA_LOG_RING_CAPACITY, 0u, &producer,
                             &consumer) != ASTRA_SYSCALL_OK)
        goto failed;
    /* The service maps it read-write: it stores consumer_position. The
     * kernel moves only handles that carry TRANSFER. */
    if (astra_rt_handle_duplicate(area, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                                            ASTRA_RIGHT_MAP |
                                            ASTRA_RIGHT_TRANSFER,
                                  &reader) != ASTRA_SYSCALL_OK)
        goto failed;
    astra_message_header_set(&setup.header, sizeof(setup),
                             ASTRA_LOG_SETUP_MAGIC, ASTRA_LOG_SETUP_VERSION,
                             ASTRA_LOG_OPERATION_SETUP, 0u);
    setup.magic = ASTRA_LOG_SETUP_MAGIC;
    setup.version = ASTRA_LOG_SETUP_VERSION;
    setup.area_size = ASTRA_LOG_RING_AREA_SIZE;
    setup.ring_offset = ASTRA_LOG_RING_OFFSET;
    setup.slot_size = ASTRA_LOG_RING_SLOT_SIZE;
    setup.capacity = ASTRA_LOG_RING_CAPACITY;
    log_ident(setup.ident);
    handles[0] = consumer;
    handles[1] = reader;
    /* Never blocks: a full queue is WOULD_BLOCK, and this line falls back. */
    status = astra_port_send(log->handle, &setup, sizeof(setup), handles, 2u);
    if (status != ASTRA_SYSCALL_OK) {
        if (status == ASTRA_SYSCALL_PEER_DEAD)
            __atomic_store_n(&log_service_dead, 1u, __ATOMIC_RELAXED);
        goto failed;
    }
    /* Both handles moved to the service on success. */
    log_producer_endpoints[generation] = producer; /* older ones stay open */
    log_producer_areas[generation] = address;
    if (generation == 0u) {
        /* Once: older rings' writers read these in their doorbells. */
        log_port = log->handle;
        astra_message_header_set(&log_doorbell, sizeof(log_doorbell),
                                 ASTRA_LOG_SETUP_MAGIC,
                                 ASTRA_LOG_SETUP_VERSION,
                                 ASTRA_LOG_OPERATION_DOORBELL, 0u);
    }
    astra_log_producer_init(&log_producers[generation], address,
                            log_ring_notify,
                            &log_producer_endpoints[generation]);
    (void)astra_close(area); /* the mapping keeps the area alive */
    state = ((generation + 1u) << LOG_GENERATION_SHIFT) | LOG_SETUP_READY;
    __atomic_store_n(&log_state, state, __ATOMIC_RELEASE);
    return state;

failed:
    if (reader != 0u)
        (void)astra_close(reader);
    if (consumer != 0u)
        (void)astra_close(consumer);
    if (producer != 0u)
        (void)astra_close(producer);
    if (address != NULL)
        (void)astra_rt_area_unmap(address);
    if (area != 0u)
        (void)astra_close(area);
unavailable:
    __atomic_store_n(&log_setup_backoff, LOG_SETUP_RETRY_AFTER,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&log_state, generation << LOG_GENERATION_SHIFT,
                     __ATOMIC_RELEASE);
    return 0u;
}

/*
 * The ring of the READY word `state` reported DEAD. Only that setup is
 * retired: a CAS from exactly that word to IDLE of the same generation. A
 * stale report (a newer setup is READY, or one is BUSY) changes nothing, and
 * only the winner backs off, so setup is retried on the 64th call after.
 */
static void
log_ring_died(uint32_t state)
{
    uint32_t expected = state;

    if (__atomic_compare_exchange_n(&log_state, &expected,
                                    state & ~LOG_STATE_MASK, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        __atomic_store_n(&log_setup_backoff, LOG_SETUP_RETRY_AFTER,
                         __ATOMIC_RELAXED);
}

/*
 * In a forked child, before it runs anything else (astra_process_clone). The
 * kernel clones only a single-threaded process (sw/kernel/process.c,
 * clone_current_process), so no writer is in a ring. The child inherited the
 * parent's mappings of its rings and the state naming them, but not the
 * producer endpoints, which do not clone: unmap the areas, and forget the
 * endpoints unclosed, because their numbers may name a child handle later.
 * The child's next line sets up a ring of its own, with a fresh budget of
 * setups -- unless the service is dead (log_service_dead): the child holds
 * the same startup handle, so it stays on LOG_WRITE too.
 */
void
astra_log_after_fork_child(void)
{
    for (uint32_t index = 0u; index < LOG_PRODUCERS; ++index) {
        if (log_producer_areas[index] != NULL)
            (void)astra_rt_area_unmap(log_producer_areas[index]);
        log_producer_areas[index] = NULL;
        log_producer_endpoints[index] = 0u;
    }
    memset(log_producers, 0, sizeof(log_producers));
    log_port = 0u;
    log_setup_backoff = 0u;
    __atomic_store_n(&log_state, LOG_SETUP_IDLE, __ATOMIC_RELEASE);
}

/*
 * Just before PROCESS_EXEC (astra_exec_executable_stream). Exec keeps the
 * handle table, so without this the service would never see these rings end:
 * close the producer endpoints, and it sees PEER_DEAD and frees the client.
 * Each producer is marked dead first, so a writer still in one falls back to
 * LOG_WRITE. The mappings stay: the kernel refuses to exec a multi-threaded
 * process only after this point, and such a writer could still be in them.
 * A successful exec unmaps them; a failed one leaves the process IDLE at the
 * same generation, and a line 64 calls later sets up a new ring. The backoff
 * is what keeps a line written in the exec window itself (a signal handler)
 * from setting up a ring the exec is about to discard.
 */
void
astra_log_before_exec(void)
{
    uint32_t state = __atomic_load_n(&log_state, __ATOMIC_ACQUIRE);

    for (uint32_t index = 0u; index < LOG_PRODUCERS; ++index) {
        uint32_t endpoint = log_producer_endpoints[index];

        if (endpoint == 0u)
            continue;
        __atomic_store_n(&log_producers[index].dead, 1u, __ATOMIC_RELEASE);
        log_producer_endpoints[index] = 0u;
        (void)astra_close(endpoint);
    }
    __atomic_store_n(&log_setup_backoff, LOG_SETUP_RETRY_AFTER,
                     __ATOMIC_RELAXED);
    /* From READY only: a BUSY setup is another thread's, and exec will be
     * refused while it runs. */
    if ((state & LOG_STATE_MASK) == LOG_SETUP_READY)
        (void)__atomic_compare_exchange_n(&log_state, &state,
                                          state & ~LOG_STATE_MASK, 0,
                                          __ATOMIC_ACQ_REL,
                                          __ATOMIC_RELAXED);
}

#ifdef ASTRA_LOG_TEST
uint32_t
astra_log_test_state(void)
{
    return log_state;
}

struct AstraLogProducer *
astra_log_test_producer(void)
{
    return (log_state & LOG_STATE_MASK) == LOG_SETUP_READY ?
               log_producer_of(log_state) : NULL;
}

void
astra_log_test_ring_died(uint32_t state)
{
    log_ring_died(state);
}

void
astra_log_test_reset(void)
{
    log_state = LOG_SETUP_IDLE;
    log_setup_backoff = 0u;
    log_service_dead = 0u;
    for (uint32_t index = 0u; index < LOG_PRODUCERS; ++index) {
        log_producer_endpoints[index] = 0u;
        log_producer_areas[index] = NULL;
    }
    memset(log_producers, 0, sizeof(log_producers));
    log_port = 0u;
}
#endif

uint32_t
astra_log_line(uint32_t level, uint32_t stream, const char *text,
               uint32_t length)
{
    AstraLogRecordFields fields;
    AstraTimeSample now;
    uint32_t state;

    if (text == NULL || level > ASTRA_EVENT_LEVEL_ERROR ||
        stream > ASTRA_LOG_STREAM_MAX || stream == ASTRA_LOG_STREAM_KERNEL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    while (length != 0u && (text[length - 1u] == '\n' ||
                            text[length - 1u] == '\r'))
        --length;
    /* An empty line is recorded: a program's blank line is part of its
     * output. Only the kernel-ring fallback cannot carry one.
     * Debug is live commentary, never stored (event_store.c drops it). */
    state = level == ASTRA_EVENT_LEVEL_DEBUG ? 0u : log_setup();
    if (state == 0u)
        return length == 0u ? ASTRA_SYSCALL_OK :
                              log_write_level(text, length, level);
    astra_time_now(&now);
    memset(&fields, 0, sizeof(fields));
    fields.monotonic_high = (uint32_t)(now.monotonic_ns >> 32);
    fields.monotonic_low = (uint32_t)now.monotonic_ns;
    fields.wall_high = (uint32_t)(now.wall_ns >> 32);
    fields.wall_low = (uint32_t)now.wall_ns;
    fields.utc_offset = now.utc_offset;
    fields.zone = now.zone;
    fields.level = (uint8_t)level;
    fields.stream = (uint8_t)stream;
    fields.flags = now.wall_valid ? ASTRA_LOG_FLAG_WALL_VALID : 0u;
    if (astra_log_producer_write(log_producer_of(state), &fields, text,
                                 length) == ASTRA_LOG_WRITE_DEAD) {
        /* The service went away, or let go of this ring: kernel ring until a
         * later setup works. The dead ring stays mapped (LOG_PRODUCERS):
         * other writers may be in it. */
        log_ring_died(state);
        return length == 0u ? ASTRA_SYSCALL_OK :
                              log_write_level(text, length, level);
    }
    return ASTRA_SYSCALL_OK; /* written, or dropped and counted */
}

uint32_t
astra_log_write(const void *bytes, uint32_t length)
{
    if (bytes == NULL || length == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return astra_log_line(ASTRA_EVENT_LEVEL_NOTICE, ASTRA_LOG_STREAM_LOG,
                          bytes, length);
}

/*
 * The same line, into the kernel ring and no further.
 *
 * The event store drops debug at drain -- live subscription only, counted and
 * never kept -- which is exactly the difference between a machine's account of
 * itself and a running commentary on it. A terminal echoing every line it
 * prints is the second: worth reading while it happens, and not worth
 * evicting a refusal from the record to keep.
 */
uint32_t
astra_log_debug(const void *bytes, uint32_t length)
{
    return log_write_level(bytes, length, ASTRA_EVENT_LEVEL_DEBUG);
}

uint32_t
astra_log(const char *text)
{
    size_t length;

    if (text == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    length = strlen(text);
    if (length == 0u || length > UINT32_MAX)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    return astra_log_write(text, (uint32_t)length);
}

uint32_t
astra_log_failure(const char *operation, uint32_t status)
{
    char line[ASTRA_LOG_FIRST_TEXT];
    uint32_t length = 0u;

    length = append(line, sizeof(line), length,
                    operation != NULL ? operation : "?");
    length = append(line, sizeof(line), length, ":");
    length = append_number(line, sizeof(line), length, status);
    length = append(line, sizeof(line), length, ": failed");
    return astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG, line,
                          length);
}

uint32_t
astra_log_assertion(const char *file, uint32_t line, const char *expression)
{
    char text[ASTRA_LOG_FIRST_TEXT];
    uint32_t length = astra_assert_message(text, sizeof(text), file, line,
                                           expression);

    return astra_log_line(ASTRA_EVENT_LEVEL_ERROR, ASTRA_LOG_STREAM_LOG, text,
                          length);
}

/*
 * "file:line: expression", built without a formatter because there is no libc
 * here and an assertion is the worst possible moment to need one.
 *
 * Separate from the failure handler so it can be tested: what a truncated
 * message looks like is exactly the case nobody exercises by accident.
 */
uint32_t
astra_assert_message(char *out, uint32_t capacity, const char *file,
                     uint32_t line, const char *expression)
{
    uint32_t length = 0u;

    if (out == NULL || capacity == 0u) {
        return 0u;
    }
    length = append(out, capacity, length, file != NULL ? file : "?");
    length = append(out, capacity, length, ":");
    length = append_number(out, capacity, length, line);
    length = append(out, capacity, length, ": ");
    length = append(out, capacity, length,
                    expression != NULL ? expression : "?");
    out[length] = '\0';
    return length;
}

/*
 * The lowest level each subsystem emits. Info by default, so `debug` call
 * sites stay compiled in and cost one branch until someone asks for them --
 * which is the point of leaving them in at all.
 */
uint8_t astra_event_levels[ASTRA_EVENT_SUBSYSTEM_MAX] = {
    ASTRA_EVENT_LEVEL_INFO, ASTRA_EVENT_LEVEL_INFO, ASTRA_EVENT_LEVEL_INFO,
    ASTRA_EVENT_LEVEL_INFO, ASTRA_EVENT_LEVEL_INFO, ASTRA_EVENT_LEVEL_INFO,
    ASTRA_EVENT_LEVEL_INFO, ASTRA_EVENT_LEVEL_INFO
};

uint32_t
astra_event_level_set(uint32_t subsystem, uint32_t level)
{
    if (subsystem >= ASTRA_EVENT_SUBSYSTEM_MAX ||
        level > ASTRA_EVENT_LEVEL_ERROR) {
        return ASTRA_STATUS_INVALID;
    }
    astra_event_levels[subsystem] = (uint8_t)level;
    return ASTRA_STATUS_OK;
}

uint32_t
astra_event_pack(uint8_t *out, uint32_t capacity, const uint32_t *values,
                 uint32_t count)
{
    uint32_t at = 0u;

    if (out == NULL || count > ASTRA_EVENT_ARGUMENT_COUNT_MAX ||
        count * 4u > capacity) {
        return 0u;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        astra_store_be32(out + at, values[index]);
        at += 4u;
    }
    return at;
}

uint32_t
astra_event_emit_packed(const AstraEventDescriptor *descriptor, uint32_t level,
                        const uint32_t *values, uint32_t count)
{
    uint8_t payload[ASTRA_EVENT_ARGUMENT_COUNT_MAX * 4u];
    uint32_t length;

    if (descriptor == NULL || count > ASTRA_EVENT_ARGUMENT_COUNT_MAX ||
        (values == NULL) != (count == 0u)) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    length = count == 0u ? 0u :
        astra_event_pack(payload, sizeof(payload), values, count);
    if (count != 0u && length == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    return astra_event_emit((uint32_t)(uintptr_t)descriptor, level,
                            length != 0u ? payload : NULL, length);
}

uint32_t
astra_activity_begin(void)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_ACTIVITY, 0u, 0u, 0u, 0u, 0u, &result);
    return result.status == ASTRA_SYSCALL_OK ? result.value0 : 0u;
}

uint32_t
astra_activity_adopt(uint32_t activity)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_ACTIVITY,
                   activity != 0u ? activity : ASTRA_ACTIVITY_NONE,
                   0u, 0u, 0u, 0u, &result);
    return result.status == ASTRA_SYSCALL_OK ? result.value0 : 0u;
}

uint32_t
astra_activity_current(void)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_ACTIVITY, ASTRA_ACTIVITY_CURRENT,
                   0u, 0u, 0u, 0u, &result);
    return result.status == ASTRA_SYSCALL_OK ? result.value0 : 0u;
}

uint32_t
astra_activity_exchange(uint32_t activity, uint32_t *previous)
{
    AstraSyscallResult result;

    astra_syscall5(ASTRA_SYSCALL_ACTIVITY,
                   activity != 0u ? activity : ASTRA_ACTIVITY_NONE,
                   0u, 0u, 0u, 0u, &result);
    if (previous != NULL)
        *previous = result.status == ASTRA_SYSCALL_OK ? result.value1 : 0u;
    return result.status == ASTRA_SYSCALL_OK ? result.value0 : 0u;
}
