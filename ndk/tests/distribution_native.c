#include <astra/program.h>
#include <astra/runtime.h>

ASTRA_PROGRAM("ndk-native-contract", 0, 1, 0,
              "Astra 68 Project", "Copyright 2026 Astra 68 Project");

int astra_main(const AstraStartupInfo *startup)
{
    return startup == 0;
}
