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
#include <astra/runtime.h>
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
astra_log_write(const void *bytes, uint32_t length)
{
    const uint8_t *text = bytes;
    uint32_t sent = 0u;

    if (bytes == NULL || length == 0u || length > ASTRA_LOG_MAX_BYTES) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    while (sent < length) {
        uint32_t chunk = length - sent;
        uint32_t flags = ASTRA_EVENT_LEVEL_NOTICE |
                         ASTRA_EVENT_FLAG_INLINE_STRING;
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
