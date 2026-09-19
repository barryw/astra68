#define _GNU_SOURCE 1

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

int execvpe(const char *file, char *const argv[], char *const envp[]);

typedef int (*ExecVector)(const char *path, char *const argv[],
                          char *const envp[]);

static int
exec_variadic(const char *path, const char *first, va_list arguments,
              ExecVector invoke)
{
    va_list scan;
    va_list fill;
    char *const *environment;
    char **vector;
    const char *argument;
    size_t count = 0u;
    size_t index = 0u;
    int result;

    va_copy(scan, arguments);
    argument = first;
    while (argument != NULL) {
        if (count == SIZE_MAX / sizeof(*vector) - 1u) {
            va_end(scan);
            errno = E2BIG;
            return -1;
        }
        ++count;
        argument = va_arg(scan, const char *);
    }
    environment = va_arg(scan, char *const *);
    va_end(scan);

    vector = malloc((count + 1u) * sizeof(*vector));
    if (vector == NULL) {
        errno = ENOMEM;
        return -1;
    }
    va_copy(fill, arguments);
    argument = first;
    while (argument != NULL) {
        vector[index++] = (char *)argument;
        argument = va_arg(fill, const char *);
    }
    va_end(fill);
    vector[index] = NULL;
    result = invoke(path, vector, (char *const *)environment);
    free(vector);
    return result;
}

int
execle(const char *path, const char *first, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, first);
    result = exec_variadic(path, first, arguments, execve);
    va_end(arguments);
    return result;
}

int
execlpe(const char *file, const char *first, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, first);
    result = exec_variadic(file, first, arguments, execvpe);
    va_end(arguments);
    return result;
}
