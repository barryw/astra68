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

#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>

ASTRA_PROGRAM("echo", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    const uint32_t *argv = NULL;
    uint32_t first = 1u;
    int newline = 1;

    astra_posix_start(startup);
    if (startup == NULL)
        return 1;
    if (startup->argc != 0u && startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    /*
     * `-n` and nothing else. The shell does the quoting and the expansion, so
     * by the time a word arrives here it is already exactly what it should
     * print -- there is nothing left for this program to interpret, and every
     * escape sequence `echo` grew elsewhere is a thing it interprets wrongly.
     */
    if (argv != NULL && startup->argc > 1u) {
        const char *word = (const char *)(uintptr_t)argv[1];

        if (word != NULL && word[0] == '-' && word[1] == 'n' &&
            word[2] == '\0') {
            newline = 0;
            first = 2u;
        }
    }
    for (uint32_t index = first; argv != NULL && index < startup->argc;
         ++index) {
        const char *word = (const char *)(uintptr_t)argv[index];

        if (index != first)
            (void)fputc(' ', stdout);
        (void)fputs(word != NULL ? word : "", stdout);
    }
    if (newline)
        (void)fputc('\n', stdout);
    if (fflush(stdout) != 0)
        return 1;
    return 0;
}
