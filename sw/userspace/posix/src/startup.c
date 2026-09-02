#include <astra/posix.h>
#include <astra/posix_descriptor.h>
#include <astra/runtime.h>

#include <stdint.h>

extern char **environ;
extern int main(int argc, char **argv);

/* Standard C/POSIX entry for unmodified applications. Native Astra programs
 * define astra_main themselves, so this archive member is not selected. */
int
astra_main(const AstraStartupInfo *startup)
{
    char **argv;

    astra_posix_file_prepare();
    astra_posix_socket_prepare();
    astra_posix_start(startup);
    if (startup == NULL || startup->argc == 0u ||
        startup->argv_address == 0u)
        return 1;
    argv = (char **)(uintptr_t)startup->argv_address;
    return main((int)startup->argc, argv);
}
