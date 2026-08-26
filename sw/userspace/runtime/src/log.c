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

#include <astra/endian.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/syscall.h>

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
log_write_level(const void *bytes, uint32_t length, uint32_t level)
{
    const uint8_t *text = bytes;
    uint32_t sent = 0u;

    if (bytes == NULL || length == 0u || length > ASTRA_LOG_MAX_BYTES) {
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

uint32_t
astra_log_write(const void *bytes, uint32_t length)
{
    return log_write_level(bytes, length, ASTRA_EVENT_LEVEL_NOTICE);
}

/*
 * The same line, into the ring and no further.
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
    uint32_t length = 0u;

    if (text == NULL) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (text[length] != '\0' && length < ASTRA_LOG_MAX_BYTES) {
        ++length;
    }
    if (length == 0u) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    return astra_log_write(text, length);
}

uint32_t
astra_log_failure(const char *operation, uint32_t status)
{
    char message[64];
    uint32_t length = astra_assert_message(
        message, sizeof(message), operation, status, "failed");

    return astra_log_write(message, length);
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
