#include <astra/posix.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service_manager.h>

#include <stdio.h>

ASTRA_PROGRAM("restart", 0, 1, 0, "Astra68 contributors",
              "Copyright 2026 Astra68 contributors");

int main(int argc, char **argv)
{
    const AstraStartupInfo *startup = astra_posix_startup();
    const AstraStartupCapability *manager;

    (void)argv;
    if (argc != 1) {
        (void)fputs("usage: restart\n", stderr);
        return 2;
    }
    if (!astra_startup_validate(startup))
        return 1;
    manager = astra_startup_capability(
        startup, ASTRA_CAPABILITY_SERVICE_MANAGER);
    if (manager == NULL ||
        (manager->rights & ASTRA_RIGHT_SIGNAL) == 0u ||
        astra_system_restart_request(manager->handle) != ASTRA_OK) {
        (void)fputs("restart: request refused\n", stderr);
        return 1;
    }
    return 0;
}
