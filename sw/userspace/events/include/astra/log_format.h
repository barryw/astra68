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

#include <astra/log_ring.h>

/*
 * The most bytes ::astra_log_format can write for a record of a given text
 * length, whatever the record holds -- derived field by field from
 * log_format.c, and held to it exactly by test_log_file.c:
 *
 *   timestamp  33  "YYYY-MM-DDTHH:MM:SS.mmm+HHHHHH:MM": the year stays four
 *                  digits (a 64-bit nanosecond clock ends in 2554, an int32
 *                  offset moves it at most 68 years) and the hours of an
 *                  int32 offset reach six; "boot+" with a 32-bit second count
 *                  is 19
 *   " level "   9  "warning" is the longest level name
 *   ident     124  ASTRA_LOG_IDENT_MAX - 1 bytes, each at most `\xNN`
 *   "[pid] "   11
 *   "stream: "  8  "stdout"/"stderr"/"kernel"
 *   text       4n  each byte at most `\xNN`
 *   " [truncated]\n" 13
 */
#define ASTRA_LOG_LINE_PREFIX_MAX \
    (33u + 9u + 4u * (ASTRA_LOG_IDENT_MAX - 1u) + 11u + 8u)
#define ASTRA_LOG_LINE_BYTES(text_length) \
    (ASTRA_LOG_LINE_PREFIX_MAX + 4u * (uint32_t)(text_length) + 13u)

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
