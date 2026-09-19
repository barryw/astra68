/* Launch one installed Astra application through the system launcher. */

#include <astra/application.h>
#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("open", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *launcher;
    AstraHandle process_handle = ASTRA_INVALID_HANDLE;
    uint32_t process_id;
    uint32_t exit_status = 0u;
    uint32_t wait_status;
    int path_index = 1;
    int wait = 0;
    size_t path_length;
    AstraResult result;

    if (argc > 1 && strcmp(argv[1], "--wait") == 0) {
        wait = 1;
        path_index = 2;
    }
    if (argc <= path_index) {
        (void)fputs("open: usage: open [--wait] APPS:Name.app "
                    "[argument ...]\n",
                    stderr);
        return 1;
    }
    if (!astra_startup_validate(startup))
        return 1;
    launcher = astra_startup_capability(
        startup, ASTRA_CAPABILITY_APPLICATION_LAUNCH);
    if (launcher == NULL || (launcher->rights & ASTRA_RIGHT_SIGNAL) == 0u) {
        (void)fputs("open: application launcher unavailable\n", stderr);
        return 1;
    }
    path_length = strlen(argv[path_index]);
    if (path_length > UINT16_MAX) {
        (void)fputs("open: application path is too long\n", stderr);
        return 1;
    }
    if (wait)
        result = astra_application_launch_waitable(
            launcher->handle, argv[path_index], (uint16_t)path_length,
            ASTRA_LAUNCH_SOURCE_SHELL,
            (const char *const *)&argv[path_index + 1],
            (uint16_t)(argc - path_index - 1), &process_handle, &process_id);
    else
        result = astra_application_launch_with_arguments(
            launcher->handle, argv[path_index], (uint16_t)path_length,
            ASTRA_LAUNCH_SOURCE_SHELL,
            (const char *const *)&argv[path_index + 1],
            (uint16_t)(argc - path_index - 1), &process_id);
    if (result != ASTRA_OK) {
        (void)fputs("open: application launch failed\n", stderr);
        return 1;
    }
    if (!wait)
        return 0;
    wait_status = astra_process_wait(process_handle, ASTRA_DEADLINE_FOREVER,
                                     &exit_status);
    result = astra_handle_close(&process_handle);
    if (wait_status != ASTRA_SYSCALL_OK || result != ASTRA_OK ||
        ASTRA_STATUS_IS_VERDICT(exit_status)) {
        (void)fputs("open: application wait failed\n", stderr);
        return 1;
    }
    return (int)exit_status;
}
