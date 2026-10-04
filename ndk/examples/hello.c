#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("hello", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

/* Write one line to the terminal that launched this program. */
int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");

    if (output == 0)
        return 1;
    return astra_print(output->handle, "Hello from Astra\n") ==
                   ASTRA_SYSCALL_OK ? 0 : 1;
}
