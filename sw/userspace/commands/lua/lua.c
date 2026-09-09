#include <astra/program.h>

int astra_lua_main(int argc, char **argv);

ASTRA_PROGRAM("lua", 5, 5, 1, "Lua.org, PUC-Rio",
              "Copyright 1994-2026 Lua.org, PUC-Rio");

int
main(int argc, char **argv)
{
    return astra_lua_main(argc, argv);
}
