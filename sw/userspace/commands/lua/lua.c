#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>

#include <stdint.h>

int astra_lua_main(int argc, char **argv);

ASTRA_PROGRAM("lua", 5, 5, 1, "Lua.org, PUC-Rio",
              "Copyright 1994-2026 Lua.org, PUC-Rio");

int
astra_main(const AstraStartupInfo *startup)
{
    char **argv;

    astra_posix_start(startup);
    if (startup == NULL || startup->argc == 0u ||
        startup->argv_address == 0u)
        return 1;
    argv = (char **)(uintptr_t)startup->argv_address;
    return astra_lua_main((int)startup->argc, argv);
}
