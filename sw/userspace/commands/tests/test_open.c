#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/application.h>
#include <astra/posix.h>
#include <astra/runtime.h>

static AstraStartupInfo startup;
static AstraStartupCapability launcher = {
    .handle = 7u,
    .rights = ASTRA_RIGHT_SIGNAL,
};
static AstraResult launch_result = ASTRA_OK;
static uint32_t wait_result = ASTRA_SYSCALL_OK;
static uint32_t application_status;
static uint32_t wait_calls;

const AstraStartupInfo *astra_posix_startup(void)
{
    return &startup;
}

int astra_startup_validate(const AstraStartupInfo *candidate)
{
    return candidate == &startup;
}

const AstraStartupCapability *astra_startup_capability(
    const AstraStartupInfo *candidate, const char *name)
{
    assert(candidate == &startup);
    assert(strcmp(name, ASTRA_CAPABILITY_APPLICATION_LAUNCH) == 0);
    return &launcher;
}

AstraResult astra_application_launch_with_arguments(
    AstraHandle handle, const char *path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, uint32_t *process_id)
{
    assert(handle == launcher.handle && source == ASTRA_LAUNCH_SOURCE_SHELL);
    assert(path_length == strlen(path));
    assert(argument_count == 1u && strcmp(arguments[0], "argument") == 0);
    if (launch_result == ASTRA_OK)
        *process_id = 42u;
    return launch_result;
}

AstraResult astra_application_launch_waitable(
    AstraHandle handle, const char *path, uint16_t path_length,
    AstraLaunchSource source, const char *const *arguments,
    uint16_t argument_count, AstraHandle *process_handle,
    uint32_t *process_id)
{
    AstraResult result = astra_application_launch_with_arguments(
        handle, path, path_length, source, arguments, argument_count,
        process_id);

    if (result == ASTRA_OK)
        *process_handle = 55u;
    return result;
}

uint32_t astra_process_wait(uint32_t handle, uint64_t deadline,
                            uint32_t *exit_status)
{
    assert(handle == 55u && deadline == ASTRA_DEADLINE_FOREVER);
    ++wait_calls;
    if (wait_result == ASTRA_SYSCALL_OK)
        *exit_status = application_status;
    return wait_result;
}

AstraResult astra_handle_close(AstraHandle *handle)
{
    assert(handle != NULL && *handle == 55u);
    *handle = ASTRA_INVALID_HANDLE;
    return ASTRA_OK;
}

#define main astra_open_main
#include "../open/open.c"
#undef main

int main(void)
{
    char *plain[] = {"open", "APPS:Test.app", "argument", NULL};
    char *waiting[] = {
        "open", "--wait", "APPS:Test.app", "argument", NULL
    };
    char *missing[] = {"open", NULL};

    assert(astra_open_main(1, missing) == 1);
    assert(astra_open_main(3, plain) == 0);
    assert(wait_calls == 0u);
    launch_result = ASTRA_ERROR_NOT_PRESENT;
    assert(astra_open_main(3, plain) == 1);
    assert(astra_open_main(4, waiting) == 1);
    assert(wait_calls == 0u);
    launch_result = ASTRA_OK;
    application_status = 73u;
    assert(astra_open_main(4, waiting) == 73);
    assert(wait_calls == 1u);
    wait_result = ASTRA_SYSCALL_IO_ERROR;
    assert(astra_open_main(4, waiting) == 1);
    wait_result = ASTRA_SYSCALL_OK;
    application_status = ASTRA_STATUS_FAULTED;
    assert(astra_open_main(4, waiting) == 1);
    return 0;
}
