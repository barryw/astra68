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

#include <astra/posix.h>
#include <astra/vfs_process.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("ls", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    LS_BATCH = 8u,
};

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
 * Days into a year, and then a date. There is no `localtime` to call and no
 * timezone to call it with, so this is UTC and says so in the header rather
 * than pretending to know where the machine is.
 */
static void
date_string(int64_t seconds, char *out, size_t capacity)
{
    static const uint16_t cumulative[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    static const char *const months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int64_t days;
    int64_t rest;
    int64_t year = 1970;
    uint32_t month = 0u;
    int leap;

    if (seconds <= 0) {
        (void)snprintf(out, capacity, "%12s", "-");
        return;
    }
    days = seconds / 86400;
    rest = seconds % 86400;
    for (;;) {
        int64_t length = ((year % 4 == 0 && year % 100 != 0) ||
                          year % 400 == 0) ? 366 : 365;

        if (days < length)
            break;
        days -= length;
        ++year;
    }
    leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 1 : 0;
    for (uint32_t index = 0u; index < 12u; ++index) {
        int64_t start = cumulative[index] +
            ((leap != 0 && index >= 2u) ? 1 : 0);

        if (days >= start)
            month = index;
    }
    days -= cumulative[month] + ((leap != 0 && month >= 2u) ? 1 : 0);
    (void)snprintf(out, capacity, "%s %2ld %02ld:%02ld", months[month],
                   (long)(days + 1), (long)(rest / 3600),
                   (long)((rest % 3600) / 60));
}

static int
list(AstraFilesystem *filesystem, const AstraFilesystemLibraryV1 *library,
     const char *path, int long_form, int all)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry entries[LS_BATCH];
    uint32_t status = library->directory_open(filesystem, path, &directory);
    uint32_t total = 0u;

    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "ls: %s: status %u\n", path, status);
        return (int)status;
    }
    for (;;) {
        uint32_t count = 0u;

        status = library->directory_read(&directory, entries, LS_BATCH,
                                         &count);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        for (uint32_t index = 0u; index < count; ++index) {
            const AstraDirectoryEntry *entry = &entries[index];

            /*
             * `.` and `..` are entries like any other and the protocol carries
             * them, but a listing that opens with two of them every time is
             * two lines of noise. -a is how a person asks for them, which is
             * what every other ls means by it.
             */
            if (!all && entry->name[0] == '.')
                continue;
            if (!long_form) {
                printf("%s%s\n", entry->name,
                       entry->kind == ASTRA_VFS_KIND_DIRECTORY ? "/" : "");
            } else {
                char mode[11];
                char when[16];

                mode_string(entry->mode, entry->kind, mode);
                date_string(entry->mtime, when, sizeof(when));
                printf("%s %3u %5u %5u %9lu %s %s\n", mode,
                       (unsigned)entry->nlink, (unsigned)entry->uid,
                       (unsigned)entry->gid,
                       (unsigned long)entry->byte_size, when, entry->name);
            }
            ++total;
        }
    }
    library->directory_close(&directory);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND) {
        (void)fprintf(stderr, "ls: %s: status %u after %u entries\n", path,
                      status, total);
        return (int)status;
    }
    return 0;
}

int
astra_main(const AstraStartupInfo *startup)
{
    AstraProcessFilesystem process_filesystem =
        ASTRA_PROCESS_FILESYSTEM_INIT;
    char typed[ASTRA_VFS_PATH_MAX];
    const uint32_t *argv = NULL;
    const char *path = NULL;
    int all = 0;
    int long_form = 0;
    int result;
    uint32_t status;

    astra_posix_start(startup);
    if (startup == NULL)
        return ASTRA_STATUS_INVALID;
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
                    (void)fprintf(stderr, "ls: unknown option -%c\n",
                                  word[at]);
                    return ASTRA_STATUS_INVALID;
                }
            }
        } else if (path == NULL) {
            path = word;
        }
    }
    status = astra_process_filesystem_open(&process_filesystem, startup);
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "ls: no filesystem: status %u\n", status);
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
    status = astra_process_path(&process_filesystem, path, typed,
                                sizeof(typed));
    if (status != ASTRA_VFS_OK) {
        (void)fprintf(stderr, "ls: %s: status %u\n",
                      path != NULL ? path : "", status);
        astra_process_filesystem_close(&process_filesystem);
        return (int)status;
    }
    result = list(&process_filesystem.filesystem,
                  process_filesystem.library, typed, long_form, all);
    astra_process_filesystem_close(&process_filesystem);
    (void)fflush(stdout);
    return result;
}
