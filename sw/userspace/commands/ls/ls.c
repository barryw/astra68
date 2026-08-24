/*
 * `ls` -- a directory listing with the fields a listing is supposed to have.
 *
 * The shell has had a builtin `ls` since there was a shell, and it printed
 * names. Names are what the protocol could carry: a directory entry was a kind
 * and a name, so a long listing was not something the builtin declined to do,
 * it was something nothing on the machine could do. Protocol version 6 carries
 * mode, owner, link count, size and modification time on the entry itself, and
 * this is the program that shows them.
 *
 * It is a program rather than a builtin because a builtin cannot be replaced,
 * cannot be launched by anything but the shell that carries it, and cannot be
 * the thing a script runs. `status` proved a program could be launched;
 * `hello` proved a program could format; this is the first one that is useful.
 *
 * The metadata arrives with the names. That is the whole reason `-l` is
 * affordable: a cross-process round trip costs about 7.5 ms here, so a listing
 * that stat'd each name would spend a third of a second on a forty-entry
 * directory doing nothing but switching address spaces.
 *
 * `-l` long, `-a` including the dot entries, and a path that is either
 * `ASSIGN:path` or a name relative to where the launcher says the prompt is
 * standing.
 */

#include <astra/civil.h>
#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>
#include <astra/vfs_union.h>

ASTRA_PROGRAM("ls", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    LS_BATCH = 32u,
    LS_OUTPUT_BYTES = 2048u,
};

static char output_buffer[LS_OUTPUT_BYTES];
static char output_line[ASTRA_VFS_NAME_MAX + 64u];
static AstraVfsDirEntry entries[LS_BATCH];
static size_t output_used;
static int output_failed;
static uint32_t stdout_handle;
static uint32_t stderr_handle;

static void
copy_bytes(char *out, const char *bytes, size_t length)
{
    for (size_t index = 0u; index < length; ++index)
        out[index] = bytes[index];
}

static int
write_all(uint32_t handle, const char *bytes, size_t length)
{
    return astra_stream_write_all(handle, bytes, (uint32_t)length) ==
           ASTRA_SYSCALL_OK;
}

static int
flush_output(void)
{
    if (!output_failed && output_used != 0u &&
        !write_all(stdout_handle, output_buffer, output_used))
        output_failed = 1;
    output_used = 0u;
    return !output_failed;
}

static int
append_output(const char *bytes, size_t length)
{
    if (length > sizeof(output_buffer) - output_used && !flush_output())
        return 0;
    copy_bytes(&output_buffer[output_used], bytes, length);
    output_used += length;
    return 1;
}

static size_t
append_text(char *out, size_t at, const char *text)
{
    while (*text != '\0')
        out[at++] = *text++;
    return at;
}

static void
mode_string(uint16_t mode, uint16_t kind, char *out)
{
    static const char *const bits = "rwxrwxrwx";

    /*
     * Mode zero means the filesystem does not carry permission bits, not that
     * a file has none. Printing `---------` for that would be a confident lie,
     * so it prints a row of question marks and the reader knows to stop
     * believing this column.
     */
    out[0] = kind == ASTRA_VFS_KIND_DIRECTORY ? 'd' : '-';
    for (uint32_t index = 0u; index < 9u; ++index)
        out[index + 1u] = mode == 0u ? '?' :
            ((mode & (1u << (8u - index))) != 0u ? bits[index] : '-');
    out[10] = '\0';
}

/*
 * The mtime column, in the zone the machine is standing in -- which is what a
 * person reading a listing means by "when". `-` for a file whose filesystem
 * never stamped it, which is not the same as a file stamped at midnight in
 * 1970.
 *
 * The calendar itself is shared with `date` and with the kernel's boot line --
 * see astra/civil.h. This used to be a second implementation of leap years,
 * living here because there was nowhere else to put it.
 */
static void
date_string(int64_t seconds, const AstraTimeZone *zone, char *out)
{
    AstraCivilTime civil;
    const char *month;

    if (seconds <= 0 ||
        !astra_civil_from_unix_seconds_zone((uint64_t)seconds, zone,
                                            &civil)) {
        copy_bytes(out, "           -", 13u);
        return;
    }
    month = astra_civil_month_name(civil.month);
    out[0] = month[0];
    out[1] = month[1];
    out[2] = month[2];
    out[3] = ' ';
    out[4] = civil.day >= 10u ? (char)('0' + civil.day / 10u) : ' ';
    out[5] = (char)('0' + civil.day % 10u);
    out[6] = ' ';
    out[7] = (char)('0' + civil.hour / 10u);
    out[8] = (char)('0' + civil.hour % 10u);
    out[9] = ':';
    out[10] = (char)('0' + civil.minute / 10u);
    out[11] = (char)('0' + civil.minute % 10u);
    out[12] = '\0';
}

static size_t
append_unsigned32(char *out, size_t at, uint32_t value, size_t width)
{
    char reversed[10];
    size_t count = 0u;

    do {
        uint32_t quotient = value / 10u;

        reversed[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (width > count) {
        out[at++] = ' ';
        --width;
    }
    while (count != 0u)
        out[at++] = reversed[--count];
    return at;
}

static size_t
append_unsigned64(char *out, size_t at, uint64_t value, size_t width)
{
    char reversed[20];
    size_t count = 0u;

    if (value <= UINT32_MAX)
        return append_unsigned32(out, at, (uint32_t)value, width);

    do {
        uint64_t quotient = value / 10u;

        reversed[count++] = (char)('0' + value - quotient * 10u);
        value = quotient;
    } while (value != 0u);
    while (width > count) {
        out[at++] = ' ';
        --width;
    }
    while (count != 0u)
        out[at++] = reversed[--count];
    return at;
}

static size_t
append_member(char *out, size_t at, uint32_t member)
{
    at = append_text(out, at, "  [");
    at = append_unsigned32(out, at, member, 0u);
    return append_text(out, at, "]");
}

static int
write_long_entry(const AstraVfsDirEntry *entry, uint32_t member,
                 const AstraTimeZone *zone)
{
    size_t at = 10u;

    mode_string(entry->mode, entry->kind, output_line);
    output_line[at++] = ' ';
    at = append_unsigned32(output_line, at, entry->nlink, 3u);
    output_line[at++] = ' ';
    at = append_unsigned32(output_line, at, entry->uid, 5u);
    output_line[at++] = ' ';
    at = append_unsigned32(output_line, at, entry->gid, 5u);
    output_line[at++] = ' ';
    at = append_unsigned64(output_line, at, entry->size, 9u);
    output_line[at++] = ' ';
    date_string(entry->mtime, zone, &output_line[at]);
    at += 12u;
    output_line[at++] = ' ';
    at = append_text(output_line, at, entry->name);
    at = append_member(output_line, at, member);
    output_line[at++] = '\n';
    return append_output(output_line, at);
}

static int
report_status(const char *path, uint32_t status, uint32_t after)
{
    const char *text = astra_vfs_status_text(status);
    size_t at = append_text(output_line, 0u, "ls: ");

    at = append_text(output_line, at, path);
    at = append_text(output_line, at, ": ");
    at = append_text(output_line, at,
                     text != NULL ? text : "operation failed");
    if (after != UINT32_MAX) {
        at = append_text(output_line, at, " after ");
        at = append_unsigned32(output_line, at, after, 0u);
        at = append_text(output_line, at, " entries");
    }
    output_line[at++] = '\n';
    return write_all(stderr_handle, output_line, at);
}

static int
list(const char *path, int long_form, int all, const AstraTimeZone *zone)
{
    AstraVfsUnionDirectory directory = ASTRA_VFS_UNION_DIRECTORY_INIT;
    uint32_t status = astra_vfs_union_directory_open(
        astra_process_vfs_assigns(), path, astra_process_vfs_assign_client,
        NULL, &directory);
    uint32_t total = 0u;

    if (status != ASTRA_VFS_OK) {
        (void)report_status(path, status, UINT32_MAX);
        return (int)status;
    }
    for (;;) {
        uint32_t count = 0u;
        uint32_t member = 0u;

        status = astra_vfs_union_directory_read(&directory, entries, LS_BATCH,
                                                &count, &member);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        for (uint32_t index = 0u; index < count; ++index) {
            const AstraVfsDirEntry *entry = &entries[index];

            /*
             * `.` and `..` are entries like any other and the protocol carries
             * them, but a listing that opens with two of them every time is
             * two lines of noise. -a is how a person asks for them, which is
             * what every other ls means by it.
             */
            if (!all && entry->name[0] == '.')
                continue;
            if (!long_form) {
                size_t length = append_text(output_line, 0u, entry->name);

                if (entry->kind == ASTRA_VFS_KIND_DIRECTORY)
                    output_line[length++] = '/';
                length = append_member(output_line, length, member);
                output_line[length++] = '\n';
                if (!append_output(output_line, length))
                    status = ASTRA_VFS_ERR_IO;
            } else {
                if (!write_long_entry(entry, member, zone))
                    status = ASTRA_VFS_ERR_IO;
            }
            ++total;
        }
        if (status != ASTRA_VFS_OK)
            break;
    }
    astra_vfs_union_directory_close(&directory);
    if (status != ASTRA_VFS_OK) {
        (void)report_status(path, status, total);
        return (int)status;
    }
    return 0;
}

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;
    AstraTimeZone zone = ASTRA_TIME_ZONE_UTC;
    char typed[ASTRA_VFS_PATH_MAX];
    uint64_t now_ns = 0u;
    const uint32_t *argv = NULL;
    const char *path = NULL;
    int all = 0;
    int long_form = 0;
    int result;
    uint32_t status;

    if (startup == NULL || startup->capabilities_address == 0u)
        return ASTRA_STATUS_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    for (uint32_t index = 0u; index < startup->capability_count; ++index) {
        if (astra_capability_name_equal(capabilities[index].name, "STDOUT"))
            stdout_handle = capabilities[index].handle;
        else if (astra_capability_name_equal(capabilities[index].name,
                                             "STDERR"))
            stderr_handle = capabilities[index].handle;
    }
    if (stdout_handle == 0u)
        return ASTRA_STATUS_ACCESS;
    if (stderr_handle == 0u)
        stderr_handle = stdout_handle;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    for (uint32_t index = 1u; argv != NULL && index < startup->argc;
         ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];

        if (word == NULL)
            continue;
        if (word[0] == '-' && word[1] != '\0') {
            for (uint32_t at = 1u; word[at] != '\0'; ++at) {
                if (word[at] == 'l') {
                    long_form = 1;
                } else if (word[at] == 'a') {
                    all = 1;
                } else {
                    char error[] = "ls: unknown option -?\n";

                    error[20] = word[at];
                    (void)write_all(stderr_handle, error,
                                    sizeof(error) - 1u);
                    return ASTRA_STATUS_INVALID;
                }
            }
        } else if (path == NULL) {
            path = word;
        }
    }
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK) {
        (void)report_status("no filesystem", status, UINT32_MAX);
        return (int)status;
    }
    /*
     * There is no root and no current directory, so neither a bare `ls` nor a
     * bare name has a defensible default of its own -- but the launcher has
     * one. CWD: is where the shell says the prompt is standing; WORK: is the
     * answer for a launcher that says nothing about where it is. Listing a
     * name typed without an assign used to answer INVALID, which is how `ls
     * proto` refused a directory `mkdir proto` had just made.
     */
    status = astra_process_path(path, typed, sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        (void)report_status(path != NULL ? path : "", status, UINT32_MAX);
        astra_process_vfs_close();
        return (int)status;
    }
    /*
     * One reading of the clock for the whole listing, so every line is in the
     * same zone even if the listing spans a summer-time change. A machine with
     * no clock lists in UTC, and every date it has to show is a dash anyway.
     */
    if (astra_clock_realtime_zone(&now_ns, &zone) != ASTRA_SYSCALL_OK)
        zone = (AstraTimeZone)ASTRA_TIME_ZONE_UTC;
    result = list(typed, long_form, all, &zone);
    astra_process_vfs_close();
    if (!flush_output() && result == 0)
        result = ASTRA_STATUS_IO;
    return result;
}
