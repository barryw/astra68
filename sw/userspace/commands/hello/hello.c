/*
 * `hello` -- the first Astra program whose output goes through a C library.
 *
 * It exists for the same reason `status` does: to prove one link works before
 * anything depends on it. `status` proved a program could be launched and its
 * exit status believed; this proves picolibc's stdio reaches a stream
 * capability, which is the whole claim the POSIX layer makes.
 *
 * It prints with printf on purpose. Every other program on this machine formats
 * numbers by hand -- the supervisor's shell carries a `write_number(uint32_t)`
 * that cannot do width, sign or strings -- and no `ls -l` can be written that
 * way. If this program's output is right, that ends.
 */

#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdio.h>

ASTRA_PROGRAM("hello", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    const uint32_t *argv = NULL;
    char formatted[64];
    int printed;

    astra_posix_start(startup);
    if (startup != NULL && startup->argc != 0u &&
        startup->argv_address != 0u)
        argv = (const uint32_t *)(uintptr_t)startup->argv_address;

    /*
     * The verdict is the exit status, not the text, because the text lands on
     * a screen and the status lands in the trace ring. A machine with no
     * observer still says whether its C library works.
     *
     * snprintf first: it exercises the formatter without needing a stream, so
     * a program launched with no STDOUT still reports a broken libc rather
     * than a missing capability.
     */
    printed = snprintf(formatted, sizeof(formatted),
                       "|%8s|%+d|0x%04x|", "right", -42, 4095);
    if (printed != 21)
        return 10;
    /* Width padding, then the sign, then the zero-padded hex. */
    if (formatted[1] != ' ' || formatted[4] != 'r' || formatted[9] != '|' ||
        formatted[10] != '-' || formatted[14] != '0' ||
        formatted[16] != '0' || formatted[17] != 'f')
        return 11;

    printf("hello from picolibc on m68030\n");
    /* The three things hand-rolled formatting cannot do, in one line. */
    printf("width %s\n", formatted);
    if (argv != NULL)
        for (uint32_t index = 0u; index < startup->argc; ++index)
            printf("argv[%u] = %s\n", index,
                   (const char *)(uintptr_t)argv[index]);
    if (fflush(stdout) != 0)
        return 12;
    return 0;
}
