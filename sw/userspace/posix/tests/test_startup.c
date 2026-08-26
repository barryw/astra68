#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/posix.h>

extern int astra_main(const AstraStartupInfo *startup);

void astra_posix_file_prepare(void)
{
}

char **environ;
static int entered;
static const AstraStartupInfo *seen_startup;
static char *arguments[] = {
    "vim", "-R", "+42", "--cmd", "set number", "--", "WORK:notes.txt",
    NULL
};

void
astra_posix_start(const AstraStartupInfo *startup)
{
    static char *environment[] = { "HOME=WORK:", "TERM=astra-256color", NULL };

    seen_startup = startup;
    environ = environment;
}

int
main(int argc, char **argv, char **envp)
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
        assert(envp == environ);
        assert(strcmp(envp[1], "TERM=astra-256color") == 0);
        return 73;
    }
    {
        AstraStartupInfo startup = {
            .argc = 7u,
            .argv_address = (uint32_t)(uintptr_t)arguments
        };

        entered = 1;
        assert(astra_main(&startup) == 73);
        assert(seen_startup == &startup);
    }
    return 0;
}
