#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

#include <astra/posix.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>

void astra_posix_file_prepare(void)
{
}

void astra_posix_socket_prepare(void)
{
}

extern char **environ;
static int entered;
static int signal_prepared;
static int program_calls;
static int ready_calls;
static int closed_calls;
static int bootstrap_present;
static uint32_t ready_result;
static const AstraStartupInfo *seen_startup;
static const AstraStartupCapability bootstrap = { .handle = 37u };
static char *arguments[] = {
    "vim", "-R", "+42", "--cmd", "set number", "--", "/work/notes.txt",
    NULL
};
static char *environment[] = {
    "HOME=/home", "PWD=/dh0/projects/astra", "SHELL=/commands/zsh",
    "TMPDIR=/ram0/tmp", "PATH=/local/commands:/commands",
    "TERM=astra-256color", "OLDPWD=relative:literal", NULL
};

void
astra_posix_start(const AstraStartupInfo *startup)
{
    seen_startup = startup;
    environ = environment;
}

const AstraStartupCapability *
astra_startup_capability(const AstraStartupInfo *startup, const char *name)
{
    assert(startup == seen_startup);
    assert(strcmp(name, ASTRA_CAPABILITY_SERVICE_READY) == 0);
    return bootstrap_present ? &bootstrap : NULL;
}

uint32_t
astra_service_ready(uint32_t handle, uint32_t status, const uint32_t *handles,
                    uint32_t handle_count)
{
    assert(handle == bootstrap.handle);
    assert(status == ASTRA_STATUS_OK);
    assert(handles == NULL && handle_count == 0u);
    ++ready_calls;
    return ready_result;
}

uint32_t
astra_close(uint32_t handle)
{
    assert(handle == bootstrap.handle);
    ++closed_calls;
    return ASTRA_SYSCALL_OK;
}

int
sigprocmask(int how, const sigset_t *restrict set, sigset_t *restrict previous)
{
    assert(how == SIG_SETMASK);
    assert(set == NULL && previous == NULL);
    ++signal_prepared;
    return 0;
}

int
main(int argc, char **argv)
{
    if (entered != 0) {
        ++program_calls;
        assert(argc == 7);
        assert(strcmp(argv[0], "vim") == 0);
        assert(strcmp(argv[1], "-R") == 0);
        assert(strcmp(argv[2], "+42") == 0);
        assert(strcmp(argv[3], "--cmd") == 0);
        assert(strcmp(argv[4], "set number") == 0);
        assert(strcmp(argv[5], "--") == 0);
        assert(strcmp(argv[6], "/work/notes.txt") == 0);
        assert(argv[7] == NULL);
        assert(strcmp(environ[0], "HOME=/home") == 0);
        assert(strcmp(environ[1], "PWD=/dh0/projects/astra") == 0);
        assert(strcmp(environ[2], "SHELL=/commands/zsh") == 0);
        assert(strcmp(environ[3], "TMPDIR=/ram0/tmp") == 0);
        assert(strcmp(environ[4], "PATH=/local/commands:/commands") == 0);
        assert(strcmp(environ[5], "TERM=astra-256color") == 0);
        assert(strcmp(environ[6], "OLDPWD=relative:literal") == 0);
        assert(environ[7] == NULL);
        assert(argv != arguments);
        assert(environ != environment);
        argv[0][0] = 'V';
        environ[0][5] = 'S';
        assert(strcmp(argv[0], "Vim") == 0);
        assert(strcmp(environ[0], "HOME=Shome") == 0);
        assert(strcmp(arguments[0], "vim") == 0);
        assert(strcmp(environment[0], "HOME=/home") == 0);
        return 73;
    }
    {
        AstraStartupInfo startup = {
            .argc = 7u,
            .argv_address = (uint32_t)(uintptr_t)arguments,
            .environment_count = 7u,
            .environment_address = (uint32_t)(uintptr_t)environment
        };

        entered = 1;
        bootstrap_present = 1;
        assert(astra_posix_enter(&startup, main) == 73);
        assert(seen_startup == &startup);
        assert(signal_prepared == 1);
        assert(program_calls == 1 && ready_calls == 1 && closed_calls == 1);

        ready_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
        assert(astra_posix_enter(&startup, main) == 1);
        assert(program_calls == 1 && ready_calls == 2 && closed_calls == 2);

        bootstrap_present = 0;
        assert(astra_posix_enter(&startup, main) == 73);
        assert(program_calls == 2 && ready_calls == 2 && closed_calls == 2);
    }
    return 0;
}
