// SPDX-License-Identifier: MIT

/*
 * The cross compiler's static libstdc++ was built against glibc 2.38+
 * headers, which turn strtoul into __isoc23_strtoul. The DE25 runs
 * Ubuntu 22.04's glibc 2.35, which lacks it, so the audio host would not
 * load. Defined here, the static reference resolves at link time. C23's
 * form differs only in also accepting a 0b binary prefix, which nothing
 * the daemon parses uses.
 */

#include <stdlib.h>

unsigned long __isoc23_strtoul(const char *text, char **end, int base);

unsigned long __isoc23_strtoul(const char *text, char **end, int base)
{
    return strtoul(text, end, base);
}
