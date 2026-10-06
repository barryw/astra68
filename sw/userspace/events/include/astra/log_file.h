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

/**
 * I/O the log file needs, supplied by the caller.
 *
 * `size`, `rename` and `remove` each report 0 only for a *real* I/O failure.
 * A rotation routinely renames or removes a generation that does not exist
 * yet (there may be no `syslog.2` after only one rotation), so "there was
 * nothing there" is success, not failure, for every one of these calls.
 */
typedef struct AstraLogFileOps {
    /** Opaque context passed back to every callback. */
    void *context;
    /**
     * Fill `*bytes` with `name`'s current size, or 0 if `name` does not
     * exist. Return nonzero on success -- including "does not exist", which
     * is reported as `*bytes == 0`, not as failure. Return 0 only on an I/O
     * failure, in which case `*bytes` is unspecified.
     */
    int (*size)(void *context, const char *name, uint64_t *bytes);
    /** Append `length` bytes of `bytes` to `name`, creating it if absent. Return nonzero on success. */
    int (*append)(void *context, const char *name, const void *bytes,
                 uint32_t length);
    /**
     * Rename `from` to `to`, replacing `to` if it exists. Return nonzero on
     * success, including when `from` does not exist (nothing to do). Return
     * 0 only on a real I/O failure.
     */
    int (*rename)(void *context, const char *from, const char *to);
    /**
     * Remove `name`. Return nonzero on success, including when `name` does
     * not exist (nothing to do). Return 0 only on a real I/O failure.
     */
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
     * YYYYMMDD of the last write this boot; 0 until the first write. Resets
     * to 0 whenever `bytes`/`ready` are (re-)established -- the first open,
     * and any later recovery from a transient `size` failure -- so the next
     * write never rotates on a date change it did not itself observe. (After
     * a reboot specifically, the boot marker already written into the file
     * is the record of the reboot, so not rotating on an unseen date change
     * is correct, not just safe.)
     */
    uint32_t day;
    /** Nonzero once `bytes` reflects the real file; see ::astra_log_file_open. */
    int ready;
} AstraLogFile;

/** ::astra_log_file_append succeeded: the batch was written. */
#define ASTRA_LOG_FILE_OK            0u
/**
 * Rotation failed, but the batch was still appended to the current (now
 * over-size, or still on yesterday's date) base file rather than dropped.
 * The caller should count this: appending to a file that should have
 * rotated is better than losing an older generation to a partial shift, but
 * it means rotation needs attention.
 */
#define ASTRA_LOG_FILE_ROTATE_FAILED 1u
/** The batch was not written. */
#define ASTRA_LOG_FILE_FAILED        2u

/**
 * Open (or re-open) a rotating log file, reading its current size.
 *
 * @param file Output state.
 * @param ops I/O callbacks; copied into `file`.
 * @param base Base file name, e.g. "syslog".
 * @param max_bytes Rotate when a write would cross this size; must be nonzero.
 * @param keep Rotated files kept; must be nonzero.
 * @return Nonzero on success. 0 if a parameter is invalid, or if `ops.size`
 *     itself failed -- in which case `file` is not ready, and the next call
 *     to ::astra_log_file_append retries establishing it rather than
 *     refusing to log for the rest of the boot.
 */
int astra_log_file_open(AstraLogFile *file, const AstraLogFileOps *ops,
                        const char *base, uint64_t max_bytes, uint32_t keep);

/**
 * Append one already-formatted batch of lines, rotating first if needed.
 *
 * Rotates before writing when the batch would cross `max_bytes`, or when
 * `day` differs from the day of the last write this boot (and both are
 * nonzero). A batch larger than `max_bytes` by itself is still written
 * whole, into a freshly rotated file. If rotation itself fails partway, the
 * batch is still appended to the current base file rather than dropped (see
 * ::ASTRA_LOG_FILE_ROTATE_FAILED).
 *
 * If `file` is not ready (construction failed, or a previous `ops.size` call
 * failed), this retries establishing it before giving up, so one transient
 * backend failure does not disable logging for the rest of the boot.
 *
 * @param file Log file state.
 * @param bytes Bytes to append.
 * @param length Number of bytes in `bytes`.
 * @param day YYYYMMDD this batch falls on, or 0 when unknown (never
 *     triggers a date rotation).
 * @return ::ASTRA_LOG_FILE_OK, ::ASTRA_LOG_FILE_ROTATE_FAILED or
 *     ::ASTRA_LOG_FILE_FAILED.
 */
uint32_t astra_log_file_append(AstraLogFile *file, const char *bytes,
                               uint32_t length, uint32_t day);

#endif
