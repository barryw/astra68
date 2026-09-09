#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

#include <astra/posix.h>

extern int astra_main(const AstraStartupInfo *startup);

void astra_posix_file_prepare(void)
{
}

void astra_posix_socket_prepare(void)
{
}

char **environ;
static int entered;
static int signal_prepared;
static const AstraStartupInfo *seen_startup;
static char *arguments[] = {
    "vim", "-R", "+42", "--cmd", "set number", "--", "WORK:notes.txt",
    NULL
};
static char *environment[] = { "HOME=WORK:", "TERM=astra-256color", NULL };

void
astra_posix_start(const AstraStartupInfo *startup)
{
    seen_startup = startup;
    environ = environment;
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
        assert(argc == 7);
        assert(strcmp(argv[0], "vim") == 0);
        assert(strcmp(argv[1], "-R") == 0);
        assert(strcmp(argv[2], "+42") == 0);
        assert(strcmp(argv[3], "--cmd") == 0);
        assert(strcmp(argv[4], "set number") == 0);
        assert(strcmp(argv[5], "--") == 0);
        assert(strcmp(argv[6], "WORK:notes.txt") == 0);
        assert(argv[7] == NULL);
        assert(strcmp(environ[1], "TERM=astra-256color") == 0);
        assert(argv != arguments);
        assert(environ != environment);
        argv[0][0] = 'V';
        environ[0][5] = 'S';
        assert(strcmp(argv[0], "Vim") == 0);
        assert(strcmp(environ[0], "HOME=SORK:") == 0);
        assert(strcmp(arguments[0], "vim") == 0);
        assert(strcmp(environment[0], "HOME=WORK:") == 0);
        return 73;
    }
    {
        AstraStartupInfo startup = {
            .argc = 7u,
            .argv_address = (uint32_t)(uintptr_t)arguments,
            .environment_count = 2u,
            .environment_address = (uint32_t)(uintptr_t)environment
        };

        entered = 1;
        assert(astra_main(&startup) == 73);
        assert(seen_startup == &startup);
        assert(signal_prepared == 1);
    }
    return 0;
}
