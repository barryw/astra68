/* `cat` -- copy files, or standard input, to standard output. */

#include <astra/program.h>
#include <astra/posix.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

ASTRA_PROGRAM("cat", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

enum { CAT_CHUNK = 512u };

static void
say(const char *text)
{
    (void)astra_posix_write_all(STDERR_FILENO, text, strlen(text));
}

static void
complain(const char *path)
{
    int error = errno;

    say("cat: ");
    say(path);
    say(": ");
    say(strerror(error));
    say("\n");
}

static int
emit_descriptor(int descriptor, const char *name)
{
    char chunk[CAT_CHUNK];

    for (;;) {
        ssize_t moved = read(descriptor, chunk, sizeof(chunk));

        if (moved < 0) {
            if (errno == EINTR)
                continue;
            complain(name);
            return 1;
        }
        if (moved == 0)
            return 0;
        if (astra_posix_write_all(STDOUT_FILENO, chunk,
                                  (size_t)moved) != 0) {
            complain("standard output");
            return 1;
        }
    }
}

static int
emit(const char *path)
{
    int descriptor;
    int result;

    if (strcmp(path, "-") == 0)
        return emit_descriptor(STDIN_FILENO, "standard input");
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        complain(path);
        return 1;
    }
    result = emit_descriptor(descriptor, path);
    if (close(descriptor) != 0 && result == 0) {
        complain(path);
        result = 1;
    }
    return result;
}

int
main(int argc, char **argv)
{
    int result = 0;

    if (argc < 2)
        return emit_descriptor(STDIN_FILENO, "standard input");
    for (int index = 1; index < argc; ++index)
        if (emit(argv[index]) != 0)
            result = 1;
    return result;
}
