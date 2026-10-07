#ifndef ASTRA_COMMANDS_LOG_FILTER_H
#define ASTRA_COMMANDS_LOG_FILTER_H

/*
 * The `log` command's line filter, apart from the program so the host test
 * runs it. A line is one record of the system log (docs/SYSTEM_LOG.md §5):
 *
 *   2026-10-05T14:03:22.117-04:00 notice chocolate-doom[10000011] stdout: text
 *
 * or `boot+S.mmm` in place of the wall time before the clock was valid.
 * Markers (`--- boot ... ---`) are not records.
 */

#include <stdint.h>

typedef struct LogFilter {
    int has_level; uint32_t level;          /* minimum, ASTRA_EVENT_LEVEL_* */
    const char *program;                     /* exact ident */
    int has_pid; uint32_t pid;
    const char *stream;                      /* exact */
    const char *grep;                        /* substring of text */
    int has_since; int64_t since_unix_s;     /* --last */
} LogFilter;

/* Parse "10m", "2h", "30s", "1d". 0 on bad input. */
int log_filter_duration(const char *text, int64_t *seconds);
/* Parse "YYYY-MM-DDTHH:MM:SS.mmm+HH:MM" at the line start to Unix seconds. */
int log_filter_line_time(const char *line, int64_t *unix_seconds);
/* 1 when the formatted line passes the filter. */
int log_filter_match(const LogFilter *filter, const char *line);
/* 1 for "--- boot ... ---" lines. */
int log_filter_is_boot_marker(const char *line);
/* The index of a level name ("debug" .. "error"), or -1. */
int log_filter_level(const char *name);

#endif
