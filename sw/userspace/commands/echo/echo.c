/*
 * `echo` -- the way a value gets looked at.
 *
 * It exists because `$?` does. A status the shell no longer prints has to be
 * readable somehow, and the readable form of any value on a machine with a
 * shell is `echo $name`. Without it the exit status would be available and
 * unobservable, which is the same as absent.
 *
 * It is a program and not a builtin for the reason everything else here is:
 * a builtin cannot be redirected, cannot be replaced, and is not available to
 * anything but the shell carrying it. `echo hello > file` has to work.
 */

#include <astra/program.h>

#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("echo", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
main(int argc, char **argv)
{
    int first = 1;
    int newline = 1;

    /*
     * `-n` and nothing else. The shell does the quoting and the expansion, so
     * by the time a word arrives here it is already exactly what it should
     * print -- there is nothing left for this program to interpret, and every
     * escape sequence `echo` grew elsewhere is a thing it interprets wrongly.
     */
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        first = 2;
    }
    for (int index = first; index < argc; ++index)
        if ((index != first && putchar(' ') == EOF) ||
            fputs(argv[index], stdout) == EOF)
            return 1;
    if (newline && putchar('\n') == EOF)
        return 1;
    return fflush(stdout) == 0 ? 0 : 1;
}
