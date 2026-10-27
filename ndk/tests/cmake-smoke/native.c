#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/status.h>
#include <astra/stream.h>

#if __STDC_HOSTED__ != 0
#error Native programs must compile freestanding
#endif

ASTRA_PROGRAM("ndk-cmake-native", 1, 0, 0,
              "Astra 68 Project", "Copyright 2026 Astra 68 Project");

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output;
    const AstraStartupCapability *ready;
    if (startup == 0)
        return 1;
    (void)astra_log("Astra CMake Native");
    output = astra_startup_capability(startup, "STDOUT");
    if (output != 0 &&
        astra_print(output->handle, "Astra CMake Native\n") != ASTRA_SYSCALL_OK)
        return 2;
    ready = astra_startup_capability(startup, "SERVICE_READY");
    if (ready != 0 && astra_service_ready(ready->handle, ASTRA_STATUS_OK,
                                         0, 0u) != ASTRA_SYSCALL_OK)
        return 3;
    return astra_rt_thread_sleep(UINT64_C(1000000000),
                                 ASTRA_THREAD_SLEEP_RELATIVE, 0u, 0) ==
                   ASTRA_SYSCALL_TIMED_OUT ? 0 : 4;
}
