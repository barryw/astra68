/** @file lua_library.c @brief Identity record for the Lua 5 shared library. */

#include <astra/library.h>

ASTRA_DYNAMIC_LIBRARY(
    "lua.library.5", 5, 5, 1, 5, 5,
    "Lua.org, PUC-Rio", "Copyright 1994-2026 Lua.org, PUC-Rio");
