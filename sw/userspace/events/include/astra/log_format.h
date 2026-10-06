#ifndef ASTRA_LOG_FORMAT_H
#define ASTRA_LOG_FORMAT_H

/**
 * @file log_format.h
 * @brief One log record rendered as one text line.
 *
 * `docs/SYSTEM_LOG.md`'s wire format:
 * `YYYY-MM-DDTHH:MM:SS.mmm+HH:MM level ident[pid-hex] stream: text\n`, with
 * control bytes escaped so a program's text can never split its own line or
 * dress up a second line as someone else's. This is pure formatting over a
 * caller-filled ::AstraLogLine; it does no I/O and keeps no state.
 */

#include <stdint.h>

/** One decoded log record, ready to render as one line. */
typedef struct AstraLogLine {
    /** Monotonic nanoseconds since boot; used when `wall_valid` is false. */
    uint64_t monotonic_ns;
    /** Nanoseconds since the Unix epoch; meaningful only when `wall_valid`. */
    uint64_t wall_ns;
    /** Offset in seconds east of UTC in force for `wall_ns`. */
    int32_t utc_offset;
    /** Packed zone name, as ::astra_civil_zone_unpack expects. */
    uint32_t zone;
    /** Nonzero when `wall_ns`/`utc_offset`/`zone` are valid. */
    uint8_t wall_valid;
    /** ASTRA_EVENT_LEVEL_* */
    uint8_t level;
    /** ASTRA_LOG_STREAM_* */
    uint8_t stream;
    /** Nonzero when the record's text was truncated by the ring. */
    uint8_t truncated;
    /** Producing process id. */
    uint32_t pid;
    /** Producing process's identity string; NUL-terminated, may be empty. */
    const char *ident;
    /** Record text; not NUL-terminated, exactly `length` bytes. */
    const char *text;
    /** Length of `text` in bytes. */
    uint32_t length;
} AstraLogLine;

/**
 * Render one line into `out`.
 *
 * @param out Output buffer.
 * @param capacity Size of `out` in bytes.
 * @param line Record to render.
 * @return The number of bytes written, including the trailing `\n`; always
 *     at most `capacity`. A line that does not fit is truncated so the
 *     newline still fits, never left without one.
 */
uint32_t astra_log_format(char *out, uint32_t capacity,
                          const AstraLogLine *line);

/**
 * The local calendar date `line` falls on, as YYYYMMDD, for midnight
 * rotation.
 *
 * @param line Record to date.
 * @return YYYYMMDD in `line`'s zone, or 0 when `line` has no valid wall
 *     time (boot-relative lines never rotate the file by date).
 */
uint32_t astra_log_local_day(const AstraLogLine *line);

#endif
