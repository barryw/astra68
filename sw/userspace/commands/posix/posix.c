#define _XOPEN_SOURCE 700

/*
 * `posix` -- the POSIX file and directory layer, checked against itself.
 *
 * `hello` proved picolibc's stdio reaches a stream capability. This proves the
 * other half: that `open`, `read`, `write`, `lseek`, `stat`, `fstat`,
 * `opendir`, `readdir`, `mkdir`, `unlink`, `chdir` and `getcwd` do what a
 * ported program will assume they do. The slash root is the process's granted
 * assign table, the current directory is a slash path through that table, and
 * a descriptor is an index into a table this process owns.
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
#include <astra/network.h>
#include <astra/posix_descriptor.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>

/*
 * picolibc does not put `sbrk` in a header for this configuration, and the
 * heap check below is the one place that wants it by name rather than through
 * `malloc`. Declared rather than worked around, so the prototype the compiler
 * checks against is the one the POSIX layer actually defines.
 */
extern void *sbrk(intptr_t increment);
extern char **environ;

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
    FAIL_CWD_SPELLING = 48,
    FAIL_ROOT_STAT = 49,
    FAIL_ROOT_OPEN = 50,
    FAIL_ROOT_READ = 51,
    FAIL_EXCLUSIVE_EXISTING = 52,
    FAIL_EXCLUSIVE_CREATE = 53,
    FAIL_FSYNC = 54,
    FAIL_FTRUNCATE = 55,
    FAIL_APPEND = 56,
    FAIL_OPEN_BUDGET = 57,
    FAIL_ISATTY = 58,
    FAIL_TCGETATTR = 59,
    FAIL_TIOCGWINSZ = 60,
    FAIL_TCSETATTR_RAW = 61,
    FAIL_TCGETATTR_RAW = 62,
    FAIL_TCSETATTR_FLAGS = 63,
    FAIL_TCSETATTR_RESTORE = 64,
    FAIL_TTY_RAW_READ = 65,
    FAIL_TTY_RAW_SEQUENCE = 66,
    FAIL_TTY_QUERY_WRITE = 67,
    FAIL_TTY_QUERY_REPLY = 68,
    FAIL_ARGUMENT_COUNT = 69,
    FAIL_ARGUMENT_VALUE = 70,
    FAIL_PIPE_CREATE = 71,
    FAIL_PIPE_WRITE = 72,
    FAIL_PIPE_DUP = 73,
    FAIL_PIPE_READ = 74,
    FAIL_PIPE_CONTENT = 75,
    FAIL_PIPE_CLOSE = 76,
    FAIL_PIPE_EOF = 77,
    FAIL_DNS = 78,
    FAIL_UDP = 79,
    FAIL_TCP = 80,
    FAIL_SOCKET_EXEC = 81,
};

#define DIRECTORY "posixcheck"
#define NAME "notes.txt"
#define RENAMED "renamed.txt"
#define EXCLUSIVE "exclusive.tmp"
#define DURABLE "durable.tmp"
#define BODY "astra posix layer\n"
/* Where the tail check seeks to, and what it must find from there. */
#define TAIL_AT 6
#define TAIL "posix layer\n"

static int
complain(int code, const char *what)
{
    char report[160];

    (void)fprintf(stderr, "posix: %s failed: %s\n", what, strerror(errno));
    (void)fflush(stderr);
    (void)snprintf(report, sizeof(report), "posix: %s failed: %s",
                   what, strerror(errno));
    (void)astra_log(report);
    return code;
}

static int
complain_gai(int code, const char *what, int error)
{
    char report[160];
    const char *reason = error == EAI_SYSTEM ? strerror(errno) :
                         gai_strerror(error);

    (void)fprintf(stderr, "posix: %s failed: %s\n", what, reason);
    (void)fflush(stderr);
    (void)snprintf(report, sizeof(report), "posix: %s failed: %s",
                   what, reason);
    (void)astra_log(report);
    return code;
}

static int
network_child(void)
{
    char reply[4];

    if (send(3, "ping", 4u, 0) != 4)
        return complain(FAIL_SOCKET_EXEC, "TCP exec child send");
    if (recv(3, reply, sizeof(reply), MSG_WAITALL) != 4)
        return complain(FAIL_SOCKET_EXEC, "TCP exec child receive");
    if (memcmp(reply, "pong", 4u) != 0) {
        errno = EPROTO;
        return complain(FAIL_SOCKET_EXEC, "TCP exec child reply");
    }
    if (close(3) != 0)
        return complain(FAIL_SOCKET_EXEC, "TCP exec child close");
    return 0;
}

static int
read_terminal_reply(uint8_t *reply, size_t capacity, uint8_t terminator,
                    size_t *length)
{
    *length = 0u;
    while (*length < capacity) {
        ssize_t count = read(STDIN_FILENO, reply + *length, 1u);

        if (count != 1)
            return 0;
        if (reply[(*length)++] == terminator)
            return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    static const char *const expected_arguments[] = {
        "posix", "-R", "+42", "--cmd", "set number", "--",
        "WORK:notes.txt"
    };
    char before[128];
    char after[128];
    char buffer[64];
    struct stat about;
    DIR *directory;
    struct dirent *entry;
    int found = 0;
    int fd;

    if (argc == 2 && strcmp(argv[1], "--network-child") == 0)
        return network_child();

    if (argc != 1 &&
        argc != (int)(sizeof(expected_arguments) /
                      sizeof(expected_arguments[0]))) {
        errno = EINVAL;
        return complain(FAIL_ARGUMENT_COUNT, "startup argument count");
    }
    for (int index = 0; argc != 1 && index < argc; ++index) {
        if (strcmp(argv[index], expected_arguments[index]) != 0) {
            errno = EINVAL;
            return complain(FAIL_ARGUMENT_VALUE, "startup argument value");
        }
    }

    {
        struct termios original;
        struct termios raw;
        struct termios observed;
        struct winsize by_function;
        struct winsize by_ioctl;
        static const char ready[] = "POSIX RAW READY\n";
        static const uint8_t cursor_query[] = "\x1b[6n";
        static const uint8_t attributes_query[] = "\x1b[c";
        static const uint8_t attributes_reply[] = "\x1b[?1;2c";
        uint8_t sequence[3];
        uint8_t reply[32];
        size_t reply_length;
        size_t received = 0u;

        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
            return complain(FAIL_ISATTY, "isatty");
        if (tcgetattr(STDIN_FILENO, &original) != 0 ||
            (original.c_lflag & (ICANON | ECHO)) != (ICANON | ECHO))
            return complain(FAIL_TCGETATTR, "tcgetattr");
        if (tcgetwinsize(STDIN_FILENO, &by_function) != 0 ||
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &by_ioctl) != 0 ||
            by_function.ws_col == 0u || by_function.ws_row == 0u ||
            by_function.ws_col != by_ioctl.ws_col ||
            by_function.ws_row != by_ioctl.ws_row ||
            by_function.ws_xpixel != by_ioctl.ws_xpixel ||
            by_function.ws_ypixel != by_ioctl.ws_ypixel)
            return complain(FAIL_TIOCGWINSZ, "terminal window size");
        raw = original;
        raw.c_iflag &= (tcflag_t)~(ICRNL | IXON);
        raw.c_oflag &= (tcflag_t)~OPOST;
        raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cflag = (raw.c_cflag & (tcflag_t)~CSIZE) | CS8;
        raw.c_cc[VMIN] = 1u;
        raw.c_cc[VTIME] = 0u;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
            return complain(FAIL_TCSETATTR_RAW, "tcsetattr raw");
        if (tcgetattr(STDIN_FILENO, &observed) != 0)
            return complain(FAIL_TCGETATTR_RAW, "tcgetattr raw");
        if ((observed.c_lflag & (ECHO | ICANON | IEXTEN | ISIG)) != 0u) {
            errno = EINVAL;
            return complain(FAIL_TCSETATTR_FLAGS, "termios raw flags");
        }
        if (write(STDOUT_FILENO, cursor_query,
                  sizeof(cursor_query) - 1u) !=
                (ssize_t)(sizeof(cursor_query) - 1u) ||
            !read_terminal_reply(reply, sizeof(reply), 'R', &reply_length))
            return complain(FAIL_TTY_QUERY_WRITE, "cursor query");
        if (reply_length < 6u || reply[0] != 0x1bu || reply[1] != '[' ||
            reply[reply_length - 1u] != 'R')
            return complain(FAIL_TTY_QUERY_REPLY, "cursor reply");
        {
            size_t separator = 2u;

            while (separator + 1u < reply_length &&
                   reply[separator] >= '0' && reply[separator] <= '9')
                ++separator;
            if (separator == 2u || reply[separator] != ';' ||
                separator + 2u >= reply_length)
                return complain(FAIL_TTY_QUERY_REPLY, "cursor reply");
            for (size_t index = separator + 1u;
                 index + 1u < reply_length; ++index)
                if (reply[index] < '0' || reply[index] > '9')
                    return complain(FAIL_TTY_QUERY_REPLY, "cursor reply");
        }
        if (write(STDOUT_FILENO, attributes_query,
                  sizeof(attributes_query) - 1u) !=
                (ssize_t)(sizeof(attributes_query) - 1u) ||
            !read_terminal_reply(reply, sizeof(reply), 'c', &reply_length))
            return complain(FAIL_TTY_QUERY_WRITE, "attributes query");
        if (reply_length != sizeof(attributes_reply) - 1u ||
            memcmp(reply, attributes_reply, reply_length) != 0)
            return complain(FAIL_TTY_QUERY_REPLY, "attributes reply");
        if (write(STDOUT_FILENO, ready, sizeof(ready) - 1u) !=
            (ssize_t)(sizeof(ready) - 1u))
            return complain(FAIL_TTY_RAW_READ, "raw-mode ready");
        while (received < sizeof(sequence)) {
            ssize_t count = read(STDIN_FILENO, sequence + received,
                                 sizeof(sequence) - received);

            if (count <= 0)
                return complain(FAIL_TTY_RAW_READ, "raw terminal read");
            received += (size_t)count;
        }
        if (sequence[0] != 0x1bu || sequence[1] != '[' ||
            sequence[2] != 'A')
            return complain(FAIL_TTY_RAW_SEQUENCE, "raw cursor sequence");
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) != 0)
            return complain(FAIL_TCSETATTR_RESTORE, "tcsetattr restore");
    }

    /*
     * Where a bare name resolves. The first component is an assign granted by
     * the launcher, viewed through the POSIX slash namespace.
     */
    if (getcwd(before, sizeof(before)) == NULL || before[0] == '\0')
        return complain(FAIL_GETCWD, "getcwd");
    if (before[0] != '/')
        return FAIL_CWD_SPELLING;
    if (stat("/", &about) != 0 || !S_ISDIR(about.st_mode))
        return complain(FAIL_ROOT_STAT, "stat root");
    directory = opendir("/");
    if (directory == NULL)
        return complain(FAIL_ROOT_OPEN, "opendir root");
    if (before[1] != '\0') {
        size_t name_length = 0u;

        while (before[name_length + 1u] != '\0' &&
               before[name_length + 1u] != '/')
            ++name_length;
        while ((entry = readdir(directory)) != NULL)
            if (strlen(entry->d_name) == name_length &&
                strncmp(entry->d_name, before + 1, name_length) == 0)
                found = 1;
        if (!found) {
            (void)closedir(directory);
            return FAIL_ROOT_READ;
        }
    }
    (void)closedir(directory);
    found = 0;

    if (mkdir(DIRECTORY, 0755) != 0 && errno != EEXIST)
        return complain(FAIL_MKDIR, "mkdir");

    fd = open(DIRECTORY "/" NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return complain(FAIL_OPEN_WRITE, "open for writing");
    if (write(fd, BODY, sizeof(BODY) - 1u) != (ssize_t)(sizeof(BODY) - 1u))
        return complain(FAIL_WRITE, "write");
    if (close(fd) != 0)
        return complain(FAIL_CLOSE, "close");

    errno = 0;
    fd = open(DIRECTORY "/" NAME, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd >= 0 || errno != EEXIST) {
        if (fd >= 0)
            (void)close(fd);
        return FAIL_EXCLUSIVE_EXISTING;
    }
    fd = open(DIRECTORY "/" EXCLUSIVE,
              O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        return complain(FAIL_EXCLUSIVE_CREATE, "exclusive create");
    if (close(fd) != 0 || unlink(DIRECTORY "/" EXCLUSIVE) != 0)
        return complain(FAIL_EXCLUSIVE_CREATE, "exclusive cleanup");

    fd = open(DIRECTORY "/" DURABLE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, "abc", 3u) != 3 || fsync(fd) != 0)
        return complain(FAIL_FSYNC, "fsync");
    if (ftruncate(fd, 1) != 0 || lseek(fd, 0, SEEK_CUR) != 3 ||
        ftruncate(fd, 5) != 0 || lseek(fd, 0, SEEK_CUR) != 3 || close(fd) != 0 ||
        stat(DIRECTORY "/" DURABLE, &about) != 0 || about.st_size != 5)
        return complain(FAIL_FTRUNCATE, "ftruncate");
    fd = open(DIRECTORY "/" DURABLE, O_RDONLY);
    (void)memset(buffer, 0xff, sizeof(buffer));
    if (fd < 0 || read(fd, buffer, 5u) != 5 || close(fd) != 0 ||
        buffer[0] != 'a' || buffer[1] != 0 || buffer[2] != 0 ||
        buffer[3] != 0 || buffer[4] != 0)
        return complain(FAIL_FTRUNCATE, "ftruncate extension");
    fd = open(DIRECTORY "/" DURABLE, O_WRONLY | O_APPEND);
    if (fd < 0 || lseek(fd, 0, SEEK_SET) != 0 || write(fd, "z", 1u) != 1 ||
        close(fd) != 0 || stat(DIRECTORY "/" DURABLE, &about) != 0 ||
        about.st_size != 6 || unlink(DIRECTORY "/" DURABLE) != 0)
        return complain(FAIL_APPEND, "append");

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

    /* Cross every retired 8/16-entry table through the complete target path. */
    {
        int open_files[64];

        for (uint32_t index = 0u; index < 64u; ++index) {
            open_files[index] = open(DIRECTORY "/" NAME, O_RDONLY);
            if (open_files[index] < 0) {
                while (index != 0u) {
                    --index;
                    (void)close(open_files[index]);
                }
                return complain(FAIL_OPEN_BUDGET, "simultaneous opens");
            }
        }
        for (uint32_t index = 0u; index < 64u; ++index)
            if (close(open_files[index]) != 0)
                return complain(FAIL_OPEN_BUDGET, "simultaneous closes");
    }

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
        static const char payload[] = "astra pipe";
        int ends[2];
        int writer;

        if (pipe(ends) != 0)
            return complain(FAIL_PIPE_CREATE, "pipe");
        writer = dup(ends[1]);
        if (writer < 0)
            return complain(FAIL_PIPE_DUP, "pipe writer dup");
        if (close(ends[1]) != 0)
            return complain(FAIL_PIPE_CLOSE, "pipe writer close");
        if (write(writer, payload, sizeof(payload) - 1u) !=
            (ssize_t)(sizeof(payload) - 1u))
            return complain(FAIL_PIPE_WRITE, "pipe write");
        if (read(ends[0], buffer, 3u) != 3 ||
            read(ends[0], buffer + 3, sizeof(payload) - 4u) !=
                (ssize_t)(sizeof(payload) - 4u))
            return complain(FAIL_PIPE_READ, "pipe read");
        if (memcmp(buffer, payload, sizeof(payload) - 1u) != 0)
            return FAIL_PIPE_CONTENT;
        if (close(writer) != 0)
            return complain(FAIL_PIPE_CLOSE, "last pipe writer close");
        if (read(ends[0], buffer, sizeof(buffer)) != 0)
            return complain(FAIL_PIPE_EOF, "pipe eof");
        if (close(ends[0]) != 0)
            return complain(FAIL_PIPE_CLOSE, "pipe reader close");
    }

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

    {
        struct addrinfo hints = {0};
        struct addrinfo *addresses = NULL;
        const AstraStartupInfo *startup = astra_posix_startup();
        int resolved = 0;

        if (astra_startup_capability(
                startup, ASTRA_CAPABILITY_NETWORK_LISTEN) == NULL) {
            errno = EACCES;
            return complain(FAIL_DNS, "network-listen startup capability");
        }
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        {
            int error = getaddrinfo("localhost", "80", &hints, &addresses);

            if (error != 0)
                return complain_gai(FAIL_DNS, "getaddrinfo localhost",
                                    error);
        }
        for (struct addrinfo *current = addresses; current != NULL;
             current = current->ai_next)
            if ((current->ai_family == AF_INET ||
                 current->ai_family == AF_INET6) &&
                current->ai_socktype == SOCK_STREAM)
                resolved = 1;
        freeaddrinfo(addresses);
        if (!resolved) {
            errno = EHOSTUNREACH;
            return complain(FAIL_DNS, "resolved localhost address");
        }
    }
    {
        struct sockaddr_in address = {0};
        struct sockaddr_in source;
        socklen_t address_size = sizeof(address);
        socklen_t source_size = sizeof(source);
        char packet[4];
        int receiver = socket(AF_INET, SOCK_DGRAM, 0);
        int sender;

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (receiver < 0)
            return complain(FAIL_UDP, "UDP receiver socket");
        sender = socket(AF_INET, SOCK_DGRAM, 0);
        if (sender < 0)
            return complain(FAIL_UDP, "UDP sender socket");
        if (bind(receiver, (struct sockaddr *)&address, sizeof(address)) != 0)
            return complain(FAIL_UDP, "UDP bind");
        if (getsockname(receiver, (struct sockaddr *)&address,
                        &address_size) != 0)
            return complain(FAIL_UDP, "UDP getsockname");
        if (sendto(sender, "udp!", 4u, 0, (struct sockaddr *)&address,
                   address_size) != 4)
            return complain(FAIL_UDP, "UDP sendto");
        if (recvfrom(receiver, packet, sizeof(packet), 0,
                     (struct sockaddr *)&source, &source_size) != 4)
            return complain(FAIL_UDP, "UDP recvfrom");
        if (memcmp(packet, "udp!", 4u) != 0 || source.sin_family != AF_INET) {
            errno = EPROTO;
            return complain(FAIL_UDP, "UDP payload");
        }
        if (close(sender) != 0 || close(receiver) != 0)
            return complain(FAIL_UDP, "UDP close");
    }
    {
        struct sockaddr_in address = {0};
        socklen_t address_size = sizeof(address);
        char request[4];
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        pid_t child;
        int accepted;
        int child_status;

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener < 0 ||
            bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
            getsockname(listener, (struct sockaddr *)&address,
                        &address_size) != 0 || listen(listener, 4) != 0)
            return complain(FAIL_TCP, "TCP listen");
        child = fork();
        if (child < 0)
            return complain(FAIL_TCP, "TCP fork");
        if (child == 0) {
            int client;
            char *child_argv[] = {"posix", "--network-child", NULL};

            (void)close(listener);
            client = socket(AF_INET, SOCK_STREAM, 0);
            if (client < 0)
                _exit(complain(FAIL_TCP, "TCP child socket"));
            if (connect(client, (struct sockaddr *)&address,
                        address_size) != 0)
                _exit(complain(FAIL_TCP, "TCP child connect"));
            if (client != 3 && dup2(client, 3) != 3)
                _exit(complain(FAIL_TCP, "TCP child dup2"));
            if (client != 3 && close(client) != 0)
                _exit(complain(FAIL_TCP, "TCP child close"));
            execve("COMMANDS:posix", child_argv, environ);
            _exit(complain(FAIL_SOCKET_EXEC, "TCP child execve"));
        }
        accepted = accept(listener, NULL, NULL);
        if (accepted < 0)
            return complain(FAIL_SOCKET_EXEC, "TCP parent accept");
        if (recv(accepted, request, sizeof(request), MSG_WAITALL) != 4)
            return complain(FAIL_SOCKET_EXEC, "TCP parent receive");
        if (memcmp(request, "ping", 4u) != 0) {
            errno = EPROTO;
            return complain(FAIL_SOCKET_EXEC, "TCP parent request");
        }
        if (send(accepted, "pong", 4u, 0) != 4)
            return complain(FAIL_SOCKET_EXEC, "TCP parent send");
        if (close(accepted) != 0 || close(listener) != 0)
            return complain(FAIL_SOCKET_EXEC, "TCP parent close");
        if (waitpid(child, &child_status, 0) != child)
            return complain(FAIL_SOCKET_EXEC, "TCP parent waitpid");
        if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
            errno = ECHILD;
            return complain(FAIL_SOCKET_EXEC, "TCP child status");
        }
    }

    printf("POSIX RAW PASS: %s\n", before);
    (void)fflush(stdout);
    return 0;
}
