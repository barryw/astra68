#define _GNU_SOURCE 1

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int execle(const char *path, const char *first, ...);
int execlpe(const char *file, const char *first, ...);

static int searched;
static char *const *expected_environment;

static int
capture(const char *path, char *const argv[], char *const envp[])
{
    assert(strcmp(path, searched != 0 ? "tool" : "/bin/tool") == 0);
    assert(strcmp(argv[0], "tool") == 0);
    assert(strcmp(argv[1], "one") == 0);
    assert(strcmp(argv[2], "two") == 0);
    assert(argv[3] == NULL);
    assert(envp == expected_environment);
    errno = ENOENT;
    return -1;
}

int
execve(const char *path, char *const argv[], char *const envp[])
{
    assert(searched == 0);
    return capture(path, argv, envp);
}

int
execvpe(const char *file, char *const argv[], char *const envp[])
{
    assert(searched != 0);
    return capture(file, argv, envp);
}

int
main(void)
{
    char *environment[] = {"MODE=test", NULL};

    expected_environment = environment;
    assert(execle("/bin/tool", "tool", "one", "two", NULL,
                  environment) == -1 && errno == ENOENT);
    searched = 1;
    assert(execlpe("tool", "tool", "one", "two", NULL,
                   environment) == -1 && errno == ENOENT);
    return 0;
}
