#define _POSIX_C_SOURCE 200809L

#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/runtime.h>

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;
extern int main(int argc, char **argv);

/* Requiring this symbol forces the standard-main adapter into an image. */
const uint32_t astra_posix_entry_contract = 1u;

static char **
copy_startup_vectors(const AstraStartupInfo *startup, char ***environment)
{
    const char *const *source_arguments = (const char *const *)(uintptr_t)
        startup->argv_address;
    const char *const *source_environment = (const char *const *)(uintptr_t)
        startup->environment_address;
    size_t pointer_count;
    size_t total;
    char **arguments;
    char **environ_copy;
    char *text;

    if (startup->argc > ASTRA_LAUNCH_ARGUMENT_MAX ||
        startup->environment_count > ASTRA_LAUNCH_ENVIRONMENT_MAX)
        return NULL;
    pointer_count = (size_t)startup->argc + 1u +
                    (size_t)startup->environment_count + 1u;
    total = pointer_count * sizeof(char *);
    for (uint32_t index = 0u; index < startup->argc; ++index) {
        size_t length = strlen(source_arguments[index]) + 1u;

        if (length > SIZE_MAX - total)
            return NULL;
        total += length;
    }
    for (uint32_t index = 0u; index < startup->environment_count; ++index) {
        size_t length = strlen(source_environment[index]) + 1u;

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
        size_t length = strlen(source_arguments[index]) + 1u;

        arguments[index] = text;
        memcpy(text, source_arguments[index], length);
        text += length;
    }
    arguments[startup->argc] = NULL;
    for (uint32_t index = 0u; index < startup->environment_count; ++index) {
        size_t length = strlen(source_environment[index]) + 1u;

        environ_copy[index] = text;
        memcpy(text, source_environment[index], length);
        text += length;
    }
    environ_copy[startup->environment_count] = NULL;
    *environment = environ_copy;
    return arguments;
}

/* Standard C/POSIX entry for unmodified applications. Native Astra programs
 * define astra_main themselves, so this archive member is not selected. */
int
astra_main(const AstraStartupInfo *startup)
{
    char **argv;
    char **environment;

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
    return main((int)startup->argc, argv);
}
