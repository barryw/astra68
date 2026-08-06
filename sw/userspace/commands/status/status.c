/*
 * `status` -- exits with the number it was given, and prints nothing.
 *
 * The first program on this machine that is a file rather than the image the
 * firmware carries. It exists to prove the three things a launch has to get
 * right before anything useful can be launched: that the image is loaded and
 * run at all, that the argument vector arrives, and that the exit status comes
 * back to whoever waited. Printing nothing is the point -- it needs no stream,
 * so a failure here is a failure of the launch and not of the streams beside
 * it.
 *
 * It stays after it has served that purpose. A machine with a program that
 * exits with a chosen status is a machine whose status path can be tested from
 * a prompt, forever, without building anything.
 */

#include <astra/program.h>
#include <astra/runtime.h>

ASTRA_PROGRAM("status", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int
astra_main(const AstraStartupInfo *startup)
{
    const uint32_t *argv;
    const char *word;
    uint32_t value = 0u;
    uint32_t index = 0u;

    /*
     * No argument is zero, which is what a program that did what it was asked
     * says. The startup block itself was already validated by crt0, which
     * exits before reaching here if it was not.
     */
    if (startup == NULL || startup->argc < 2u ||
        startup->argv_address == 0u) {
        return 0;
    }
    argv = (const uint32_t *)(uintptr_t)startup->argv_address;
    word = (const char *)(uintptr_t)argv[1];
    if (word == NULL) {
        return 0;
    }
    /*
     * Decimal digits, and bounded at four of them. Nothing here can reach the
     * verdict bit that says a process never got to answer -- a program must not
     * be able to claim the system's own statuses -- and a fixture that could
     * would be one that lies about what happened to it.
     */
    while (index < 4u && word[index] >= '0' && word[index] <= '9') {
        value = (value * 10u) + (uint32_t)(word[index] - '0');
        ++index;
    }
    return (int)value;
}
