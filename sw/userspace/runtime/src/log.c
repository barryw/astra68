/*
 * The diagnostic channel a program has to talk about itself.
 *
 * Before this there was none. A process not holding the display lease could
 * say exactly two things: a monotonic integer through the progress counter,
 * and one halfword of exit status on its way out. Anything with a bug in it
 * was diagnosed by inference.
 *
 * The channel is authority, not a free line: the kernel gates the write on a
 * process handle carrying ASTRA_RIGHT_DEBUG, and a build with no debug surface
 * grants that to nobody. A program therefore has to treat logging as something
 * that can be refused, which is why nothing here retries or panics -- the
 * status comes back and the caller carries on.
 *
 * The handle is remembered at startup rather than passed at every call. It is
 * the same handle for the life of the process, and threading it through every
 * diagnostic site is how diagnostics end up not being written.
 */

#include <astra/runtime.h>
#include <astra/syscall.h>

static uint32_t log_process_handle;

void
astra_log_bind(uint32_t process_handle)
{
    log_process_handle = process_handle;
}

uint32_t
astra_log_handle(void)
{
    return log_process_handle;
}

uint32_t
astra_log_write(const void *bytes, uint32_t length)
{
    AstraSyscallResult result;

    if (bytes == NULL || length == 0u || length > ASTRA_LOG_MAX_BYTES) {
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    }
    /*
     * Refused here rather than in the kernel when nothing has bound a handle:
     * a program that logs before it has validated its startup block is asking
     * with an authority it has not been given yet.
     */
    if (log_process_handle == 0u) {
        return ASTRA_SYSCALL_INVALID_HANDLE;
    }
    astra_syscall5(ASTRA_SYSCALL_LOG_WRITE, log_process_handle,
                   (uint32_t)(uintptr_t)bytes, length, 0u, 0u, &result);
    return result.status;
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
    /*
     * A longer line is cut rather than split. Splitting would interleave with
     * whatever else is writing to the same console, and half a line arriving
     * under another process's prefix is worse than a line that ends early.
     */
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
