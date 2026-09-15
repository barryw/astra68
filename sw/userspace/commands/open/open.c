/* Launch one installed Astra application through the system launcher. */

#include <astra/application.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("open", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *launcher;
    uint32_t process_id;
    size_t path_length;
    AstraResult result;

    if (argc < 2) {
        (void)fputs("open: usage: open APPS:Name.app [argument ...]\n",
                    stderr);
        return (int)-ASTRA_ERROR_INVALID_ARGUMENT;
    }
    if (!astra_startup_validate(startup))
        return (int)-ASTRA_ERROR_INVALID_ARGUMENT;
    launcher = astra_startup_capability(
        startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    if (launcher == NULL || (launcher->rights & ASTRA_RIGHT_SIGNAL) == 0u) {
        (void)fputs("open: application launcher unavailable\n", stderr);
        return (int)-ASTRA_ERROR_PERMISSION;
    }
    path_length = strlen(argv[1]);
    if (path_length > UINT16_MAX) {
        (void)fputs("open: application path is too long\n", stderr);
        return (int)-ASTRA_ERROR_INVALID_ARGUMENT;
    }
    result = astra_application_launch_with_arguments(
        launcher->handle, argv[1], (uint16_t)path_length,
        ASTRA_LAUNCH_SOURCE_SHELL, (const char *const *)&argv[2],
        (uint16_t)(argc - 2), &process_id);
    if (result != ASTRA_OK)
        (void)fputs("open: application launch failed\n", stderr);
    return result == ASTRA_OK ? 0 : (int)-result;
}
