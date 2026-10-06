/*
 * Append-only text with size-and-midnight rotation: Haiku's syslog_output
 * (src/servers/syslog_daemon/syslog_output.cpp:74-91), with a day boundary as
 * newsyslog and logrotate have. I/O is supplied so the host test and the
 * service run the same code.
 */

#include <astra/log_file.h>

#include <stddef.h>

static void
numbered(char *out, const char *base, uint32_t number)
{
    uint32_t length = 0u;
    char digits[10];
    uint32_t count = 0u;

    while (base[length] != '\0' && length < 20u) {
        out[length] = base[length];
        ++length;
    }
    out[length++] = '.';
    do { digits[count++] = (char)('0' + number % 10u); number /= 10u; }
    while (number != 0u);
    while (count != 0u)
        out[length++] = digits[--count];
    out[length] = '\0';
}

/*
 * Shifts every rotated generation up by one and moves the current base file
 * into slot 1. Stops at the first remove/rename failure and reports it: per
 * ::AstraLogFileOps's contract an absent source is itself success, so a
 * nonzero failure here is a real I/O problem, and the right response is to
 * leave every file exactly where it is -- including the base file, left
 * un-renamed -- rather than cascade a partial shift that could overwrite or
 * lose an older generation.
 */
static int
rotate(AstraLogFile *file)
{
    char from[32];
    char to[32];

    numbered(to, file->base, file->keep);
    if (!file->ops.remove(file->ops.context, to))
        return 0;
    for (uint32_t number = file->keep; number > 1u; --number) {
        numbered(from, file->base, number - 1u);
        numbered(to, file->base, number);
        if (!file->ops.rename(file->ops.context, from, to))
            return 0;
    }
    numbered(to, file->base, 1u);
    if (!file->ops.rename(file->ops.context, file->base, to))
        return 0;
    file->bytes = 0u;
    return 1;
}

/*
 * (Re-)establishes `bytes` from the backing store and marks the file ready,
 * or not, on `ops.size`'s word. Used both by the first open and by a later
 * retry after a transient `size` failure; see the `day` field comment in
 * log_file.h for why it always resets `day` to 0.
 */
static int
establish(AstraLogFile *file)
{
    file->day = 0u;
    file->bytes = 0u;
    file->ready = file->ops.size(file->ops.context, file->base, &file->bytes);
    return file->ready;
}

int
astra_log_file_open(AstraLogFile *file, const AstraLogFileOps *ops,
                    const char *base, uint64_t max_bytes, uint32_t keep)
{
    if (file == NULL || ops == NULL || base == NULL || keep == 0u ||
        max_bytes == 0u)
        return 0;
    file->ops = *ops;
    file->base = base;
    file->max_bytes = max_bytes;
    file->keep = keep;
    return establish(file);
}

uint32_t
astra_log_file_append(AstraLogFile *file, const char *bytes, uint32_t length,
                      uint32_t day)
{
    uint32_t result = ASTRA_LOG_FILE_OK;

    if (file == NULL)
        return ASTRA_LOG_FILE_FAILED;
    /*
     * A `size` failure at open does not disable logging for the rest of the
     * boot: every append while not ready retries establishing the file, so a
     * transient backend error only delays logging until it clears.
     */
    if (!file->ready && !establish(file))
        return ASTRA_LOG_FILE_FAILED;
    if (length == 0u)
        return ASTRA_LOG_FILE_OK;
    if (file->bytes != 0u &&
        (file->bytes + length > file->max_bytes ||
         (day != 0u && file->day != 0u && day != file->day)) &&
        !rotate(file))
        result = ASTRA_LOG_FILE_ROTATE_FAILED;
    if (day != 0u)
        file->day = day;
    if (!file->ops.append(file->ops.context, file->base, bytes, length))
        return ASTRA_LOG_FILE_FAILED;
    file->bytes += length;
    return result;
}
