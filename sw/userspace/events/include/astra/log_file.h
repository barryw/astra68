#ifndef ASTRA_LOG_FILE_H
#define ASTRA_LOG_FILE_H

/**
 * @file log_file.h
 * @brief Append-only text log with size-and-midnight rotation.
 *
 * I/O is supplied through ::AstraLogFileOps so the same rotation logic runs
 * under a host test's in-memory directory and under the events service's
 * real VFS calls (Task 7). Haiku's syslog_output
 * (src/servers/syslog_daemon/syslog_output.cpp) rotates on size; this adds
 * the local-date boundary newsyslog and logrotate also use.
 */

#include <stdint.h>

/** I/O the log file needs, supplied by the caller. */
typedef struct AstraLogFileOps {
    /** Opaque context passed back to every callback. */
    void *context;
    /** Fill `*bytes` with `name`'s current size. Return 0 = absent (treated as empty), nonzero = success. */
    int (*size)(void *context, const char *name, uint64_t *bytes);
    /** Append `length` bytes of `bytes` to `name`, creating it if absent. Return nonzero on success. */
    int (*append)(void *context, const char *name, const void *bytes,
                 uint32_t length);
    /** Rename `from` to `to`, replacing `to` if it exists. Return nonzero on success. */
    int (*rename)(void *context, const char *from, const char *to);
    /** Remove `name` if it exists. Return nonzero on success (including when absent). */
    int (*remove)(void *context, const char *name);
} AstraLogFileOps;

/** One rotating log file's state. */
typedef struct AstraLogFile {
    /** I/O callbacks, copied at open. */
    AstraLogFileOps ops;
    /** Base name, e.g. "syslog"; rotated files are "syslog.1".."syslog.N". */
    const char *base;
    /** Rotate at this many bytes. */
    uint64_t max_bytes;
    /** Rotated files kept, "syslog.1".."syslog.keep". */
    uint32_t keep;
    /** Current size of `base`. */
    uint64_t bytes;
    /**
     * YYYYMMDD of the last write this boot; 0 until the first write. Starts
     * at 0 on every open, including a reopen after reboot, so the first
     * write after a reboot never rotates on a date change it did not see
     * happen -- the boot marker already written into the file is the record
     * of the reboot. The next date change after that does rotate.
     */
    uint32_t day;
    /** Nonzero once `astra_log_file_open` has established `bytes`. */
    int ready;
} AstraLogFile;

/**
 * Open (or re-open) a rotating log file, reading its current size.
 *
 * @param file Output state.
 * @param ops I/O callbacks; copied into `file`.
 * @param base Base file name, e.g. "syslog".
 * @param max_bytes Rotate when a write would cross this size; must be nonzero.
 * @param keep Rotated files kept; must be nonzero.
 * @return Nonzero on success.
 */
int astra_log_file_open(AstraLogFile *file, const AstraLogFileOps *ops,
                        const char *base, uint64_t max_bytes, uint32_t keep);

/**
 * Append one already-formatted batch of lines, rotating first if needed.
 *
 * Rotates before writing when the batch would cross `max_bytes`, or when
 * `day` differs from the day of the last write this boot (and both are
 * nonzero). A batch larger than `max_bytes` by itself is still written
 * whole, into a freshly rotated file.
 *
 * @param file Log file state.
 * @param bytes Bytes to append.
 * @param length Number of bytes in `bytes`.
 * @param day YYYYMMDD this batch falls on, or 0 when unknown (never
 *     triggers a date rotation).
 * @return Nonzero on success.
 */
int astra_log_file_append(AstraLogFile *file, const char *bytes,
                          uint32_t length, uint32_t day);

#endif
