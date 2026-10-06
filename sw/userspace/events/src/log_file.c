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

static int
rotate(AstraLogFile *file)
{
    char from[32];
    char to[32];

    numbered(to, file->base, file->keep);
    (void)file->ops.remove(file->ops.context, to);
    for (uint32_t number = file->keep; number > 1u; --number) {
        numbered(from, file->base, number - 1u);
        numbered(to, file->base, number);
        (void)file->ops.rename(file->ops.context, from, to);
    }
    numbered(to, file->base, 1u);
    if (!file->ops.rename(file->ops.context, file->base, to))
        return 0;
    file->bytes = 0u;
    return 1;
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
    /*
     * 0 until this boot's first write. A reopen after reboot must not treat
     * "unknown" as "yesterday": the boot marker already written into the
     * file is the record of the reboot, so the first write this boot rotates
     * only on size, and the next *actual* date change (one this boot
     * observed) rotates as usual.
     */
    file->day = 0u;
    file->bytes = 0u;
    file->ready = ops->size(ops->context, base, &file->bytes);
    return file->ready;
}

int
astra_log_file_append(AstraLogFile *file, const char *bytes, uint32_t length,
                      uint32_t day)
{
    if (file == NULL || !file->ready || length == 0u)
        return file != NULL && file->ready;
    if (file->bytes != 0u &&
        (file->bytes + length > file->max_bytes ||
         (day != 0u && file->day != 0u && day != file->day)) &&
        !rotate(file))
        return 0;
    if (day != 0u)
        file->day = day;
    if (!file->ops.append(file->ops.context, file->base, bytes, length))
        return 0;
    file->bytes += length;
    return 1;
}
