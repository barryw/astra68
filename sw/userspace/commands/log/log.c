/*
 * `log` -- read the system log (docs/SYSTEM_LOG.md section 6).
 *
 *   log [show] [--boot N] [--last DURATION] [--level L] [--program P]
 *              [--pid HEX] [--stream S] [--grep TEXT]
 *   log stream [the same filters]
 *
 * Haiku has no viewer: its answer is `tail` and `grep` on /var/log/syslog
 * (syslog_daemon's syslog_output.cpp). Those still work here, on
 * EVENTS:log/syslog. This adds what they cannot do across rotations and
 * boots, with the verbs of macOS's log(1), which the spec names.
 *
 * `show` prints matching lines from EVENTS:log/syslog.3, .2, .1 and then
 * syslog, oldest first. `stream` prints the lines appended after it starts,
 * until a line is entered, the way `events --follow` does.
 */

#define _POSIX_C_SOURCE 200809L

#include <astra/program.h>
#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>

#include "log_filter.h"

#include <errno.h>
#include <sys/types.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

ASTRA_PROGRAM("log", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#define LOG_DIRECTORY "/events/log/"
/* The longest formatted line: a 1,944-byte record escaped at four bytes a
 * byte, behind its time, level, ident, pid and stream. */
#define LOG_LINE_MAX 8192u
#define LOG_READ_CHUNK 512u

/* Statically allocated, because a user thread gets one 4 KiB stack. */
static AstraProcessFilesystem process_filesystem =
    ASTRA_PROCESS_FILESYSTEM_INIT;
static uint8_t chunk[LOG_READ_CHUNK];
static char line[LOG_LINE_MAX];
static uint32_t line_used;

static LogFilter filter;
/* --boot: which boot to print, by marker index from the oldest file, or
 * every line when not set. */
static int boot_selected;
static long boot_target;
static long boot_seen;          /* markers met so far, this pass */
static int counting;            /* pass one of --boot: count markers only */

static int
close_with(int status)
{
    astra_process_filesystem_close(&process_filesystem);
    return status;
}

static void
complain(const char *text)
{
    (void)fputs("log: ", stderr);
    (void)fputs(text, stderr);
    (void)fputc('\n', stderr);
}

static void
take_line(void)
{
    line[line_used] = '\0';
    line_used = 0u;
    if (log_filter_is_boot_marker(line))
        ++boot_seen;
    if (counting)
        return;
    if (boot_selected && boot_seen - 1 != boot_target)
        return;
    if (log_filter_match(&filter, line)) {
        (void)fputs(line, stdout);
        (void)fputc('\n', stdout);
    }
}

/*
 * Reads from `offset` to the file's end, handing each complete line to
 * take_line, and returns where it stopped. A line longer than the buffer is
 * cut there; the rest of it is skipped up to its newline.
 */
static uint64_t
feed(AstraFile *file, uint64_t offset)
{
    for (;;) {
        uint32_t moved = 0u;

        if (astra_filesystem_read_at(file, offset, chunk, sizeof(chunk),
                                     &moved) != ASTRA_VFS_OK || moved == 0u)
            return offset;
        for (uint32_t index = 0u; index < moved; ++index) {
            if (chunk[index] == '\n')
                take_line();
            else if (line_used + 1u < sizeof(line))
                line[line_used++] = (char)chunk[index];
        }
        offset += moved;
    }
}

/* Every file, oldest first. */
static void
show_pass(void)
{
    /* EVENTS:log serves ASTRA_EVENTS_LOG_KEEP (3) rotations. */
    static const char *const names[] = {
        LOG_DIRECTORY "syslog.3", LOG_DIRECTORY "syslog.2",
        LOG_DIRECTORY "syslog.1", LOG_DIRECTORY "syslog"
    };

    boot_seen = 0;
    for (uint32_t index = 0u; index < sizeof(names) / sizeof(names[0]);
         ++index) {
        AstraFile file = ASTRA_FILE_INIT;

        if (astra_filesystem_open(&process_filesystem.filesystem,
                                  names[index], ASTRA_VFS_OPEN_READ,
                                  &file) != ASTRA_VFS_OK)
            continue;
        (void)feed(&file, 0u);
        /* The service writes whole lines; a tail without one is still a
         * line. */
        if (line_used != 0u)
            take_line();
        (void)astra_filesystem_close(&file);
    }
}

static int
file_size(AstraFile *file, uint64_t *bytes)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;

    if (astra_filesystem_file_info(file, &info) != ASTRA_VFS_OK)
        return 0;
    *bytes = info.byte_size;
    return 1;
}

/*
 * Lines appended after it starts, until a line is entered. EVENTS: reads
 * the current syslog on every read, so after a rotation the same handle
 * reads the new file, and a size below where this stopped says it rotated.
 *
 * ponytail: a rotation that grows the new file past the old offset between
 * two polls is missed, and its lines with it; it needs 1 MiB in 250 ms.
 */
static int
stream(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint64_t offset = 0u;

    if (astra_filesystem_open(&process_filesystem.filesystem,
                              LOG_DIRECTORY "syslog", ASTRA_VFS_OPEN_READ,
                              &file) != ASTRA_VFS_OK ||
        !file_size(&file, &offset)) {
        complain("no system log");
        return ASTRA_STATUS_NOT_FOUND;
    }
    for (;;) {
        struct pollfd input = {STDIN_FILENO, POLLIN | POLLHUP, 0};
        uint64_t size = 0u;
        int ready;

        if (file_size(&file, &size) && size < offset) {
            offset = 0u;
            line_used = 0u;
        }
        offset = feed(&file, offset);
        if (fflush(stdout) != 0)
            break;
        ready = poll(&input, 1u, 250);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0)
            break;
        /* A line entered, or the end of input; a readiness with nothing to
         * read yet (EAGAIN) is not either. As events --follow does. */
        if (ready > 0) {
            uint8_t typed[1];
            ssize_t length;

            do {
                length = read(STDIN_FILENO, typed, sizeof(typed));
            } while (length < 0 && errno == EINTR);
            if (length >= 0 || errno != EAGAIN)
                break;
        }
    }
    (void)astra_filesystem_close(&file);
    return ASTRA_STATUS_OK;
}

static int
parse_hex(const char *text, uint32_t *value)
{
    uint32_t result = 0u;
    uint32_t count = 0u;

    for (; text[count] != '\0'; ++count) {
        char at = text[count];
        uint32_t nibble;

        if (count == 8u)
            return 0;
        if (at >= '0' && at <= '9')
            nibble = (uint32_t)(at - '0');
        else if (at >= 'a' && at <= 'f')
            nibble = (uint32_t)(at - 'a') + 10u;
        else if (at >= 'A' && at <= 'F')
            nibble = (uint32_t)(at - 'A') + 10u;
        else
            return 0;
        result = result << 4 | nibble;
    }
    *value = result;
    return count != 0u;
}

/* "0", "-1", "-2" ...: a boot counted back from the newest. */
static int
parse_boot(const char *text, long *value)
{
    long result = 0;
    uint32_t at = text[0] == '-' ? 1u : 0u;

    if (text[at] == '\0')
        return 0;
    for (; text[at] != '\0'; ++at) {
        if (text[at] < '0' || text[at] > '9' || result > 100000)
            return 0;
        result = result * 10 + (text[at] - '0');
    }
    *value = text[0] == '-' ? -result : result;
    return *value <= 0;
}

static void
usage(void)
{
    complain("usage: log [show|stream] [--boot N] [--last 10m] "
             "[--level L] [--program P] [--pid HEX] [--stream S] "
             "[--grep TEXT]");
}

int
main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    int streaming = 0;
    int first = 1;
    long boot = 0;
    uint32_t status;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    if (argc > 1 && strcmp(argv[1], "stream") == 0) {
        streaming = 1;
        first = 2;
    } else if (argc > 1 && strcmp(argv[1], "show") == 0) {
        first = 2;
    }
    for (int index = first; index < argc; ++index) {
        const char *word = argv[index];
        const char *value = index + 1 < argc ? argv[index + 1] : NULL;

        if (value == NULL || strncmp(word, "--", 2u) != 0) {
            usage();
            return ASTRA_STATUS_INVALID;
        }
        ++index;
        if (strcmp(word, "--boot") == 0) {
            if (!parse_boot(value, &boot)) {
                complain("--boot takes 0 (this boot), -1, -2 ...");
                return ASTRA_STATUS_INVALID;
            }
            boot_selected = 1;
        } else if (strcmp(word, "--last") == 0) {
            int64_t seconds;
            struct timespec now;

            if (!log_filter_duration(value, &seconds)) {
                complain("--last takes a duration: 30s, 10m, 2h, 1d");
                return ASTRA_STATUS_INVALID;
            }
            if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
                complain("--last needs the wall clock, and it is not set");
                return ASTRA_STATUS_UNSUPPORTED;
            }
            filter.has_since = 1;
            filter.since_unix_s = (int64_t)now.tv_sec - seconds;
        } else if (strcmp(word, "--level") == 0) {
            int level = log_filter_level(value);

            if (level < 0) {
                complain("levels are debug, info, notice, warning, error");
                return ASTRA_STATUS_INVALID;
            }
            filter.has_level = 1;
            filter.level = (uint32_t)level;
        } else if (strcmp(word, "--program") == 0) {
            filter.program = value;
        } else if (strcmp(word, "--pid") == 0) {
            if (!parse_hex(value, &filter.pid)) {
                complain("--pid takes the hex id ps prints");
                return ASTRA_STATUS_INVALID;
            }
            filter.has_pid = 1;
        } else if (strcmp(word, "--stream") == 0) {
            if (strcmp(value, "log") != 0 && strcmp(value, "stdout") != 0 &&
                strcmp(value, "stderr") != 0 &&
                strcmp(value, "kernel") != 0) {
                complain("streams are log, stdout, stderr, kernel");
                return ASTRA_STATUS_INVALID;
            }
            filter.stream = value;
        } else if (strcmp(word, "--grep") == 0) {
            filter.grep = value;
        } else {
            usage();
            return ASTRA_STATUS_INVALID;
        }
    }
    if (streaming && boot_selected) {
        complain("a stream is this boot; --boot is for show");
        return ASTRA_STATUS_INVALID;
    }
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        complain("filesystem unavailable");
        return (int)status;
    }
    if (streaming)
        return close_with(stream());
    if (boot_selected) {
        /* Pass one counts the markers, so -1 can name the one before the
         * last. Lines before the first marker are an older boot whose
         * marker rotated away; they are only shown when there is none. */
        counting = 1;
        show_pass();
        counting = 0;
        boot_target = boot_seen - 1 + boot;
        if (boot_target < (boot_seen == 0 ? -1 : 0)) {
            complain("no such boot in the log");
            return close_with(ASTRA_STATUS_NOT_FOUND);
        }
    }
    show_pass();
    return close_with(fflush(stdout) == 0 ? ASTRA_STATUS_OK :
                                            ASTRA_STATUS_IO);
}
