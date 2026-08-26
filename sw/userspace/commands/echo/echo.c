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
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("echo", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *standard_output;
    char output[ASTRA_LAUNCH_ARGUMENT_BYTES + ASTRA_LAUNCH_ARGUMENT_MAX + 1u];
    uint32_t out = 0u;
    uint32_t stdout_handle = 0u;
    uint32_t first = 1u;
    int newline = 1;

    if (!astra_startup_validate(startup))
        return 1;
    standard_output = astra_startup_capability(startup, "STDOUT");
    if (standard_output != NULL)
        stdout_handle = standard_output->handle;
    if (stdout_handle == 0u)
        return 1;
    /*
     * `-n` and nothing else. The shell does the quoting and the expansion, so
     * by the time a word arrives here it is already exactly what it should
     * print -- there is nothing left for this program to interpret, and every
     * escape sequence `echo` grew elsewhere is a thing it interprets wrongly.
     */
    if (startup->argc > 1u) {
        const char *word = astra_startup_argument(startup, 1u);

        if (word != NULL && word[0] == '-' && word[1] == 'n' &&
            word[2] == '\0') {
            newline = 0;
            first = 2u;
        }
    }
    for (uint32_t index = first; index < startup->argc; ++index) {
        const char *word = astra_startup_argument(startup, index);

        if (index != first)
            output[out++] = ' ';
        if (word != NULL)
            while (*word != '\0')
                output[out++] = *word++;
    }
    if (newline)
        output[out++] = '\n';
    return astra_stream_write_all(stdout_handle, output, out) ==
           ASTRA_SYSCALL_OK ? 0 : 1;
}
