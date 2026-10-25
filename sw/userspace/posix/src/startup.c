#define _POSIX_C_SOURCE 200809L

#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* POSIX process state is owned here: startup installs the copied environment
 * before user main runs, and exec replaces it atomically with the image. */
char **environ;

static char **
copy_startup_vectors(const AstraStartupInfo *startup, char ***environment)
{
    const uint32_t *source_arguments;
    const uint32_t *source_environment;
    size_t pointer_count;
    size_t total;
    char **arguments;
    char **environ_copy;
    char *text;

    if (!astra_startup_validate(startup))
        return NULL;
    source_arguments = (const uint32_t *)(uintptr_t)startup->argv_address;
    source_environment = (const uint32_t *)(uintptr_t)startup->environment_address;
    pointer_count = (size_t)startup->argc + 1u +
                    (size_t)startup->environment_count + 1u;
    total = pointer_count * sizeof(char *);
    for (uint32_t index = 0u; index < startup->argc; ++index) {
        size_t length = strlen((const char *)(uintptr_t)source_arguments[index]) + 1u;

        if (length > SIZE_MAX - total)
            return NULL;
        total += length;
    }
    for (uint32_t index = 0u; index < startup->environment_count; ++index) {
        size_t length = strlen((const char *)(uintptr_t)source_environment[index]) + 1u;

        if (length > SIZE_MAX - total)
            return NULL;
        total += length;
    }
    arguments = malloc(total);
    if (arguments == NULL)
        return NULL;
    environ_copy = arguments + startup->argc + 1u;
    text = (char *)(void *)(environ_copy + startup->environment_count + 1u);
    for (uint32_t index = 0u; index < startup->argc; ++index) {
        const char *value = (const char *)(uintptr_t)source_arguments[index];
        size_t length = strlen(value) + 1u;

        arguments[index] = text;
        memcpy(text, value, length);
        text += length;
    }
    arguments[startup->argc] = NULL;
    for (uint32_t index = 0u; index < startup->environment_count; ++index) {
        const char *value = (const char *)(uintptr_t)source_environment[index];
        size_t length = strlen(value) + 1u;

        environ_copy[index] = text;
        memcpy(text, value, length);
        text += length;
    }
    environ_copy[startup->environment_count] = NULL;
    *environment = environ_copy;
    return arguments;
}

int
astra_posix_enter(const AstraStartupInfo *startup, AstraPosixMain program)
{
    const AstraStartupCapability *bootstrap;
    char **argv;
    char **environment;

    if (program == NULL)
        return 1;
    astra_posix_file_prepare();
    astra_posix_socket_prepare();
    astra_posix_start(startup);
    /* Register the common user-mode trampoline before any signal can arrive. */
    if (sigprocmask(SIG_SETMASK, NULL, NULL) != 0)
        return 1;
    if (startup == NULL || startup->argc == 0u ||
        startup->argv_address == 0u)
        return 1;
    argv = copy_startup_vectors(startup, &environment);
    if (argv == NULL)
        return 1;
    environ = environment;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap != NULL) {
        uint32_t status = astra_service_ready(bootstrap->handle,
                                              ASTRA_STATUS_OK, NULL, 0u);

        (void)astra_close(bootstrap->handle);
        if (status != ASTRA_SYSCALL_OK)
            return 1;
    }
    return program((int)startup->argc, argv);
}
