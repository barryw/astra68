#define _XOPEN_SOURCE 700

/*
 * `posix` -- the POSIX file and directory layer, checked against itself.
 *
 * `hello` proved picolibc's stdio reaches a stream capability. This proves the
 * other half: that `open`, `read`, `write`, `lseek`, `stat`, `fstat`,
 * `opendir`, `readdir`, `mkdir`, `unlink`, `chdir` and `getcwd` do what a
 * ported program will assume they do, over a machine that has none of the
 * things they are defined in terms of -- no root, no current directory, and a
 * descriptor that is an index into a table this process owns.
 *
 * The verdict is the exit status, not the text. Text lands on a screen where
 * nothing reads it back; a status lands in the trace ring, so a boot with no
 * screen still says whether this passed. Each check has its own number, so a
 * failure names the step rather than the file.
 *
 * It leaves nothing behind. Everything it makes, it removes -- a gate that
 * only passes the first time it runs is a gate nobody can run twice.
 */

#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>

/*
 * picolibc does not put `sbrk` in a header for this configuration, and the
 * heap check below is the one place that wants it by name rather than through
 * `malloc`. Declared rather than worked around, so the prototype the compiler
 * checks against is the one the POSIX layer actually defines.
 */
extern void *sbrk(intptr_t increment);

ASTRA_PROGRAM("posix", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum {
    FAIL_GETCWD = 10,
    FAIL_MKDIR = 11,
    FAIL_OPEN_WRITE = 12,
    FAIL_WRITE = 13,
    FAIL_CLOSE = 14,
    FAIL_OPEN_READ = 15,
    FAIL_FSTAT = 16,
    FAIL_READ = 17,
    FAIL_CONTENT = 18,
    FAIL_SEEK = 19,
    FAIL_TAIL = 20,
    FAIL_STAT = 21,
    FAIL_STAT_SIZE = 22,
    FAIL_OPENDIR = 23,
    FAIL_READDIR = 24,
    FAIL_CHDIR = 25,
    FAIL_CWD_MOVED = 26,
    FAIL_BARE_STAT = 27,
    FAIL_UNLINK = 28,
    FAIL_GONE = 29,
    FAIL_CHDIR_BACK = 30,
    FAIL_RMDIR = 31,
    FAIL_MISSING_IS_ENOENT = 32,
    FAIL_MALLOC = 33,
    FAIL_MALLOC_USABLE = 34,
    FAIL_REALLOC = 35,
    FAIL_SBRK_GROW = 36,
    FAIL_SBRK_USABLE = 37,
    FAIL_SBRK_SHRINK = 38,
    FAIL_SBRK_BREAK = 39,
    FAIL_RENAME = 40,
    FAIL_RENAME_SOURCE = 41,
    FAIL_RENAME_TARGET = 42,
    FAIL_SYSTEM_MISSING = 43,
    FAIL_SYSTEM_STATUS = 44,
    FAIL_GETPRIORITY = 45,
    FAIL_NICE = 46,
    FAIL_SETPRIORITY = 47,
};

#define DIRECTORY "posixcheck"
#define NAME "notes.txt"
#define RENAMED "renamed.txt"
#define BODY "astra posix layer\n"
/* Where the tail check seeks to, and what it must find from there. */
#define TAIL_AT 6
#define TAIL "posix layer\n"

static int
complain(int code, const char *what)
{
    (void)fprintf(stderr, "posix: %s failed: %s\n", what, strerror(errno));
    return code;
}

int
astra_main(const AstraStartupInfo *startup)
{
    char before[128];
    char after[128];
    char buffer[64];
    struct stat about;
    DIR *directory;
    struct dirent *entry;
    int found = 0;
    int fd;

    astra_posix_start(startup);

    /*
     * Where a bare name resolves. `CWD:` when a shell launched this, because a
     * shell says where it is standing; `WORK:` when whatever launched it did
     * not. Either way it comes back in the machine's own terms, and what comes
     * out of getcwd has to be something open() would accept.
     */
    if (getcwd(before, sizeof(before)) == NULL || before[0] == '\0')
        return complain(FAIL_GETCWD, "getcwd");

    if (mkdir(DIRECTORY, 0755) != 0 && errno != EEXIST)
        return complain(FAIL_MKDIR, "mkdir");

    fd = open(DIRECTORY "/" NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return complain(FAIL_OPEN_WRITE, "open for writing");
    if (write(fd, BODY, sizeof(BODY) - 1u) != (ssize_t)(sizeof(BODY) - 1u))
        return complain(FAIL_WRITE, "write");
    if (close(fd) != 0)
        return complain(FAIL_CLOSE, "close");

    fd = open(DIRECTORY "/" NAME, O_RDONLY);
    if (fd < 0)
        return complain(FAIL_OPEN_READ, "open for reading");
    /* fstat, because stdio asks it about every file it is handed. */
    if (fstat(fd, &about) != 0 || !S_ISREG(about.st_mode) ||
        about.st_size != (off_t)(sizeof(BODY) - 1u))
        return complain(FAIL_FSTAT, "fstat");
    (void)memset(buffer, 0, sizeof(buffer));
    if (read(fd, buffer, sizeof(buffer) - 1u) !=
        (ssize_t)(sizeof(BODY) - 1u))
        return complain(FAIL_READ, "read");
    if (strcmp(buffer, BODY) != 0) {
        (void)fprintf(stderr, "posix: read back %s\n", buffer);
        return FAIL_CONTENT;
    }
    /* A seek that lands inside the file, and a read that proves where. */
    if (lseek(fd, TAIL_AT, SEEK_SET) != (off_t)TAIL_AT)
        return complain(FAIL_SEEK, "lseek");
    (void)memset(buffer, 0, sizeof(buffer));
    if (read(fd, buffer, sizeof(buffer) - 1u) !=
            (ssize_t)(sizeof(BODY) - 1u - TAIL_AT) ||
        strcmp(buffer, TAIL) != 0) {
        (void)fprintf(stderr, "posix: after seek read %s\n", buffer);
        return FAIL_TAIL;
    }
    (void)close(fd);

    if (stat(DIRECTORY "/" NAME, &about) != 0)
        return complain(FAIL_STAT, "stat");
    if (about.st_size != (off_t)(sizeof(BODY) - 1u) ||
        !S_ISREG(about.st_mode))
        return FAIL_STAT_SIZE;

    directory = opendir(DIRECTORY);
    if (directory == NULL)
        return complain(FAIL_OPENDIR, "opendir");
    while ((entry = readdir(directory)) != NULL)
        if (strcmp(entry->d_name, NAME) == 0)
            found = 1;
    (void)closedir(directory);
    if (!found)
        return FAIL_READDIR;
    if (rename(DIRECTORY "/" NAME, DIRECTORY "/" RENAMED) != 0)
        return complain(FAIL_RENAME, "rename");
    if (stat(DIRECTORY "/" NAME, &about) == 0 || errno != ENOENT)
        return FAIL_RENAME_SOURCE;
    if (stat(DIRECTORY "/" RENAMED, &about) != 0)
        return complain(FAIL_RENAME_TARGET, "renamed target");

    /*
     * Into the directory, and then the same file by a bare name. This is the
     * whole point of the cwd being a real thing rather than a string: the name
     * `notes.txt` meant nothing a moment ago and means a file now.
     */
    if (chdir(DIRECTORY) != 0)
        return complain(FAIL_CHDIR, "chdir");
    if (getcwd(after, sizeof(after)) == NULL || strcmp(after, before) == 0)
        return FAIL_CWD_MOVED;
    if (stat(RENAMED, &about) != 0)
        return complain(FAIL_BARE_STAT, "stat of a bare name");

    if (unlink(RENAMED) != 0)
        return complain(FAIL_UNLINK, "unlink");
    if (stat(RENAMED, &about) == 0)
        return FAIL_GONE;
    /* And the errno for a name that is not there, which callers branch on. */
    if (errno != ENOENT)
        return FAIL_MISSING_IS_ENOENT;

    if (chdir("..") != 0 || getcwd(after, sizeof(after)) == NULL ||
        strcmp(after, before) != 0)
        return complain(FAIL_CHDIR_BACK, "chdir back");
    if (rmdir(DIRECTORY) != 0)
        return complain(FAIL_RMDIR, "rmdir");

    /*
     * And a heap. `malloc` is where a ported program gets everything it does
     * not know the size of in advance, so a machine without one runs nothing
     * but programs written for it. The block is written through before it is
     * trusted: an allocator handing back memory nobody mapped fails here
     * rather than somewhere later that looks like a different bug.
     */
    {
        const size_t block = 4096u;
        char *first = malloc(block);
        char *second;

        if (first == NULL)
            return complain(FAIL_MALLOC, "malloc");
        (void)memset(first, 0x5a, block);
        for (size_t index = 0u; index < block; ++index)
            if ((unsigned char)first[index] != 0x5au)
                return FAIL_MALLOC_USABLE;
        second = realloc(first, block * 2u);
        if (second == NULL)
            return complain(FAIL_REALLOC, "realloc");
        for (size_t index = 0u; index < block; ++index)
            if ((unsigned char)second[index] != 0x5au)
                return FAIL_REALLOC;
        free(second);
        /* And that a freed block comes back, which is what makes it a heap. */
        second = malloc(block);
        if (second == NULL)
            return complain(FAIL_MALLOC, "malloc after free");
        free(second);
    }

    /*
     * The break itself, both ways. Growing is arithmetic over a reservation
     * and the pages arrive on the writes below; shrinking hands them back to
     * the kernel, which is the half that makes releasing memory a behaviour
     * rather than a moved pointer. `malloc` above may never shrink -- its
     * allocator decides that -- so the path is exercised here or nowhere.
     */
    {
        const size_t span = 64u * 1024u;
        char *start = sbrk(0);
        char *grown = sbrk((intptr_t)span);

        if (grown == (void *)-1 || grown != start)
            return complain(FAIL_SBRK_GROW, "sbrk grow");
        (void)memset(grown, 0x3c, span);
        for (size_t index = 0u; index < span; ++index)
            if ((unsigned char)grown[index] != 0x3cu)
                return FAIL_SBRK_USABLE;
        if (sbrk(-(intptr_t)span) == (void *)-1)
            return complain(FAIL_SBRK_SHRINK, "sbrk shrink");
        if (sbrk(0) != start)
            return complain(FAIL_SBRK_BREAK, "sbrk break");
        /* And the reservation survives, so the same span comes back. */
        grown = sbrk((intptr_t)span);
        if (grown == (void *)-1 || grown != start)
            return complain(FAIL_SBRK_GROW, "sbrk regrow");
        (void)memset(grown, 0x7eu, span);
        for (size_t index = 0u; index < span; ++index)
            if ((unsigned char)grown[index] != 0x7eu)
                return FAIL_SBRK_USABLE;
        if (sbrk(-(intptr_t)span) == (void *)-1)
            return complain(FAIL_SBRK_SHRINK, "sbrk shrink again");
    }

    if (system(NULL) == 0)
        return FAIL_SYSTEM_MISSING;
    if (system("status 23") != (23 << 8))
        return complain(FAIL_SYSTEM_STATUS, "system");

    {
        int old_nice;

        errno = 0;
        old_nice = getpriority(PRIO_PROCESS, 0);
        if (old_nice == -1 && errno != 0)
            return complain(FAIL_GETPRIORITY, "getpriority");
        if (nice(1) != old_nice + 1 ||
            getpriority(PRIO_PROCESS, 0) != old_nice + 1)
            return complain(FAIL_NICE, "nice");
        if (setpriority(PRIO_PROCESS, 0, old_nice) != 0 ||
            getpriority(PRIO_PROCESS, 0) != old_nice)
            return complain(FAIL_SETPRIORITY, "setpriority");
    }

    printf("posix: %s\n", before);
    (void)fflush(stdout);
    return 0;
}
