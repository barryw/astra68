#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/application.h>
#include <astra/port.h>
#include <astra/area.h>
#include <astra/application_payload.h>
#include <astra/status.h>
#include <astra/vfs_path.h>

#include "syscall.h"

static uint32_t transaction;
static uint16_t expected_count;
static uint16_t expected_source;
static const char *expected_path;
static const char *const *expected_arguments;
static int reply_has_handle = 1;
static uint32_t process_handle_closes;
static uint32_t area_handle_closes, payload_handle_closes, area_maps, area_unmaps;
static uint32_t payload_size;
static char shared[ASTRA_APPLICATION_ARGUMENT_BYTES];
static AstraResult map_failure;
static int corrupt_on_close;
static int call_unsent;

AstraResult astra_area_create(uint32_t size, uint32_t rights, AstraArea *area)
{
    assert(size > 0u && size <= sizeof(shared));
    assert(rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    assert(area->handle == 0u); payload_size = size; area->handle = 0x100u;
    return ASTRA_OK;
}
AstraResult astra_area_map(AstraArea *area, uint32_t flags)
{
    if (map_failure != ASTRA_OK) return map_failure;
    ++area_maps;
    assert(area->handle == 0x100u || area->handle == 0x101u);
    assert(flags == ASTRA_AREA_MAP_READ || flags == (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
    area->address = shared; area->size = payload_size; area->map_flags = flags;
    return ASTRA_OK;
}
AstraResult astra_area_unmap(AstraArea *area)
{
    assert(area->address == shared); ++area_unmaps;
    area->address = NULL; area->size = area->map_flags = 0u;
    return ASTRA_OK;
}
AstraResult astra_area_close(AstraArea *area)
{
    if (area->address != NULL) { AstraResult result = astra_area_unmap(area); assert(result == ASTRA_OK); }
    if (corrupt_on_close) memset(shared, 0xff, sizeof(shared));
    return astra_handle_close(&area->handle);
}

uint32_t astra_system_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CALL) {
        const AstraCall *call = astra_system_test_call;
        const AstraApplicationLaunchRequest *request = call->request;
        AstraApplicationLaunchReply *reply = call->reply;

        assert(((const AstraPortCall *)d1)->port == 7u);
        assert(d2 == 0u && d3 == 0u && d4 == 0u && d5 == 0u);
        assert(call->request_size == sizeof(*request));
        assert(call->handle_count == 1u && call->reply_index == 1u && call->handles[0] == 0x101u);
        assert(area_handle_closes > 0u && area_maps == area_unmaps);
        if (call_unsent) { *out_d1 = ASTRA_PORT_CALL_UNSENT; return ASTRA_SYSCALL_PEER_DEAD; }
        assert(call->reply_capacity == sizeof(*reply));
        assert(call->reply_handle_capacity == 1u);
        assert(request->header.protocol == ASTRA_APPLICATION_PROTOCOL);
        assert(request->header.operation == ASTRA_APPLICATION_LAUNCH);
        assert(request->arguments.count == expected_count);
        assert(request->arguments.source == expected_source);
        assert(request->arguments.flags == 0u);
        assert(request->arguments.reserved[0] == 0u && request->arguments.reserved[1] == 0u);
        assert(__builtin_strcmp(shared, expected_path) == 0);
        {
            const char *argument = shared +
                                   __builtin_strlen(expected_path) + 1u;

            for (uint16_t index = 1u; index < expected_count; ++index) {
                assert(__builtin_strcmp(argument,
                                        expected_arguments[index - 1u]) == 0);
                argument += __builtin_strlen(argument) + 1u;
            }
        }
        transaction = request->header.transaction_id;
        reply->header.total_size = sizeof(*reply);
        reply->header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        reply->header.protocol = ASTRA_APPLICATION_PROTOCOL;
        reply->header.protocol_version = ASTRA_APPLICATION_VERSION;
        reply->header.operation = ASTRA_APPLICATION_LAUNCHED;
        reply->header.transaction_id = transaction;
        reply->status = ASTRA_STATUS_OK;
        reply->process_id = 42u;
        *out_d1 = sizeof(*reply);
        if (reply_has_handle) {
            call->reply_handles[0] = 0x200u;
            *out_d2 = 1u;
        }
    } else if (number == ASTRA_SYSCALL_HANDLE_DUPLICATE) {
        assert(d1 == 0x100u);
        assert(d2 == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
        *out_d1 = 0x101u;
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        if (d1 == 0x200u) ++process_handle_closes;
        else if (d1 == 0x100u) ++area_handle_closes;
        else { assert(d1 == 0x101u); ++payload_handle_closes; }
    }
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    static const char *const dropped[] = {
        "/work/first.txt", "/work/second.txt"
    };
    static const char *const shell_arguments[] = {"--tab", "Progress"};
    static const char malformed_path[] = {
        'A', 'P', 'P', 'S', ':', (char)0xc0, (char)0x80, '\0'
    };
    static const char malformed_argument[] = {
        'W', 'O', 'R', 'K', ':', (char)0xed, (char)0xa0, (char)0x80, '\0'
    };
    static const char *const malformed_arguments[] = {malformed_argument};
    char large_argument[ASTRA_APPLICATION_ARGUMENT_BYTES];
    const char *large_arguments[] = {large_argument};
    char long_path[ASTRA_APPLICATION_PATH_MAX + 1u];
    AstraHandle process_handle = ASTRA_INVALID_HANDLE;
    uint32_t process_id = 0u;

    assert(astra_application_launch(ASTRA_INVALID_HANDLE,
                                    "/apps/Terminal.app", 18u,
                                    &process_id) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_application_launch(7u, malformed_path,
                                    (uint16_t)(sizeof(malformed_path) - 1u),
                                    &process_id) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    assert(astra_application_launch_with_arguments(
               7u, "/apps/Terminal.app", 18u,
               ASTRA_LAUNCH_SOURCE_DESKTOP, malformed_arguments, 1u,
               &process_id) == ASTRA_ERROR_INVALID_ARGUMENT);
    expected_count = 1u;
    expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
    expected_path = "/apps/Terminal.app";
    expected_arguments = NULL;
    assert(astra_application_launch(7u, "/apps/Terminal.app", 18u,
                                    &process_id) == ASTRA_OK);
    assert(process_id == 42u);
    assert(process_handle_closes == 1u);
    expected_count = 3u;
    expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
    expected_arguments = dropped;
    assert(astra_application_launch_with_arguments(
               7u, "/apps/Terminal.app", 18u,
               ASTRA_LAUNCH_SOURCE_DESKTOP, dropped, 2u, &process_id) ==
           ASTRA_OK);
    assert(process_id == 42u);
    assert(process_handle_closes == 2u);
    expected_count = 3u;
    expected_source = ASTRA_LAUNCH_SOURCE_SHELL;
    expected_path = "/apps/InterfaceGallery.app";
    expected_arguments = shell_arguments;
    assert(astra_application_launch_with_arguments(
               7u, expected_path,
               (uint16_t)__builtin_strlen(expected_path),
               ASTRA_LAUNCH_SOURCE_SHELL, shell_arguments, 2u,
               &process_id) == ASTRA_OK);
    assert(process_id == 42u);
    assert(process_handle_closes == 3u);
    assert(astra_application_launch_waitable(
               7u, expected_path,
               (uint16_t)__builtin_strlen(expected_path),
               ASTRA_LAUNCH_SOURCE_SHELL, shell_arguments, 2u,
               &process_handle, &process_id) == ASTRA_OK);
    assert(process_handle == 0x200u && process_id == 42u);
    assert(astra_handle_close(&process_handle) == ASTRA_OK);
    assert(process_handle == ASTRA_INVALID_HANDLE);
    assert(process_handle_closes == 4u);
    reply_has_handle = 0;
    assert(astra_application_launch_waitable(
               7u, expected_path,
               (uint16_t)__builtin_strlen(expected_path),
               ASTRA_LAUNCH_SOURCE_SHELL, shell_arguments, 2u,
               &process_handle, &process_id) == ASTRA_ERROR_IO);
    assert(process_handle == ASTRA_INVALID_HANDLE && process_id == 0u);
    reply_has_handle = 1;
    for (uint32_t index = 0u; index < 300u; ++index)
        large_argument[index] = 'a';
    large_argument[300] = '\0';
    expected_count = 2u;
    expected_source = ASTRA_LAUNCH_SOURCE_SHELL;
    expected_path = "/apps/Terminal.app";
    expected_arguments = large_arguments;
    assert(astra_application_launch_with_arguments(
               7u, expected_path, 18u, ASTRA_LAUNCH_SOURCE_SHELL,
               large_arguments, 1u, &process_id) == ASTRA_OK);
    assert(process_id == 42u);
    assert(process_handle_closes == 5u);
    for (uint32_t index = 300u; index + 1u < sizeof(large_argument); ++index)
        large_argument[index] = 'a';
    large_argument[sizeof(large_argument) - 1u] = '\0';
    assert(astra_application_launch_with_arguments(
               7u, expected_path, 18u, ASTRA_LAUNCH_SOURCE_SHELL,
               large_arguments, 1u, &process_id) ==
           ASTRA_ERROR_NO_RESOURCES);
    for (uint32_t index = 0u; index < 150u; ++index)
        long_path[index] = 'p';
    long_path[150] = '\0';
    expected_count = 1u;
    expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
    expected_path = long_path;
    expected_arguments = NULL;
    assert(astra_application_launch(7u, long_path, 150u, &process_id) ==
           ASTRA_OK);
    assert(process_id == 42u);
    assert(process_handle_closes == 6u);
    for (uint32_t index = 150u; index < ASTRA_APPLICATION_PATH_MAX; ++index)
        long_path[index] = 'p';
    long_path[ASTRA_APPLICATION_PATH_MAX] = '\0';
    assert(astra_application_launch(
               7u, long_path, ASTRA_APPLICATION_PATH_MAX, &process_id) ==
           ASTRA_ERROR_NAME_TOO_LONG);
    long_path[ASTRA_APPLICATION_PATH_MAX - 1u] = '\0';
    expected_path = long_path;
    assert(astra_application_launch(7u, long_path,
        ASTRA_APPLICATION_PATH_MAX - 1u, &process_id) == ASTRA_OK);
    expected_path = "/apps/Terminal.app"; expected_source = ASTRA_LAUNCH_SOURCE_SHELL;
    expected_count = 2u; expected_arguments = large_arguments;
    memset(large_argument, 'a', ASTRA_APPLICATION_ARGUMENT_BYTES - 20u);
    large_argument[ASTRA_APPLICATION_ARGUMENT_BYTES - 20u] = '\0';
    assert(astra_application_launch_with_arguments(7u, expected_path, 18u,
        ASTRA_LAUNCH_SOURCE_SHELL, large_arguments, 1u, &process_id) == ASTRA_OK);
    assert(payload_size == ASTRA_APPLICATION_ARGUMENT_BYTES);
    large_argument[ASTRA_APPLICATION_ARGUMENT_BYTES - 20u] = 'a';
    large_argument[ASTRA_APPLICATION_ARGUMENT_BYTES - 19u] = '\0';
    assert(astra_application_launch_with_arguments(7u, expected_path, 18u,
        ASTRA_LAUNCH_SOURCE_SHELL, large_arguments, 1u, &process_id) == ASTRA_ERROR_NO_RESOURCES);
    map_failure = ASTRA_ERROR_PERMISSION;
    uint32_t old_area_closes = area_handle_closes;
    assert(astra_application_launch(7u, expected_path, 18u, &process_id) == ASTRA_ERROR_PERMISSION);
    assert(area_handle_closes == old_area_closes + 1u);
    map_failure = ASTRA_OK;
    expected_count = 1u; expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP; expected_arguments = NULL;
    call_unsent = 1;
    uint32_t old_payload_closes = payload_handle_closes;
    assert(astra_application_launch(7u, expected_path, 18u, &process_id) == ASTRA_ERROR_PEER_DEAD);
    assert(payload_handle_closes == old_payload_closes + 1u && process_id == 0u);
    call_unsent = 0;
    {
        const char bytes[] = "/apps/X.app";
        char copy[ASTRA_APPLICATION_ARGUMENT_BYTES];
        AstraApplicationLaunchArguments arguments = {.count = 1u, .length = sizeof(bytes),
            .source = ASTRA_LAUNCH_SOURCE_DESKTOP};
        AstraHandle payload = 0x101u;
        memcpy(shared, bytes, sizeof(bytes)); payload_size = sizeof(bytes);
        corrupt_on_close = 1;
        assert(astra_application_payload_copy(&arguments, &payload, copy, sizeof(copy)) == ASTRA_OK);
        assert(payload == ASTRA_INVALID_HANDLE && strcmp(copy, bytes) == 0);
        corrupt_on_close = 0;
        for (uint32_t defect = 0u; defect < 7u; ++defect) {
            payload = 0x101u;
            memcpy(shared, bytes, sizeof(bytes)); payload_size = sizeof(bytes);
            arguments = (AstraApplicationLaunchArguments){.count = 1u, .length = sizeof(bytes),
                .source = ASTRA_LAUNCH_SOURCE_DESKTOP};
            if (defect == 0u) --payload_size;
            if (defect == 1u) shared[sizeof(bytes) - 1u] = 'x';
            if (defect == 2u) { shared[sizeof(bytes)] = '\0'; ++arguments.length; ++payload_size; }
            if (defect == 3u) shared[1] = (char)0xff;
            if (defect == 4u) arguments.count = 2u;
            if (defect == 5u) arguments.reserved[1] = 1u;
            if (defect == 6u) arguments.source = ASTRA_LAUNCH_SOURCE_SYSTEM;
            old_payload_closes = payload_handle_closes;
            assert(astra_application_payload_copy(&arguments, &payload, copy, sizeof(copy)) == ASTRA_ERROR_INVALID_ARGUMENT);
            assert(payload == ASTRA_INVALID_HANDLE && payload_handle_closes == old_payload_closes + 1u);
        }
        map_failure = ASTRA_ERROR_PERMISSION; payload = 0x101u;
        old_payload_closes = payload_handle_closes;
        assert(astra_application_payload_copy(&arguments, &payload, copy, sizeof(copy)) == ASTRA_ERROR_PERMISSION);
        assert(payload == ASTRA_INVALID_HANDLE && payload_handle_closes == old_payload_closes + 1u);
        map_failure = ASTRA_OK;
    }
    {
        static const char bundle[] = "/apps/DiskSetup.app";
        static const char *const volume_arguments[] = {
            "--target", "18446744073709551615", "42", "7"};
        AstraVolumeReference volume = {UINT64_MAX, 42u, 7u};
        expected_count = 5u;
        expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
        expected_path = bundle;
        expected_arguments = volume_arguments;
        assert(astra_application_launch_for_volume(7u, bundle,
            sizeof(bundle) - 1u, ASTRA_LAUNCH_SOURCE_DESKTOP,
            "--target", &volume, &process_id) == ASTRA_OK);
        assert(process_id == 42u);
        volume.generation = 0u;
        assert(astra_application_launch_for_volume(7u, bundle,
            sizeof(bundle) - 1u, ASTRA_LAUNCH_SOURCE_DESKTOP,
            "--target", &volume, &process_id) ==
            ASTRA_ERROR_INVALID_ARGUMENT);
    }
    {
        static const char bundle[] = "/apps/Inspector.app";
        static const char *const arguments[] = {
            "--volume", "91", "42", "7", "/usb0/folder/file"};
        AstraVolumeReference volume = {91u, 42u, 7u};
        expected_count = 6u;
        expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
        expected_path = bundle;
        expected_arguments = arguments;
        assert(astra_application_launch_for_volume_path(7u, bundle,
            sizeof(bundle) - 1u, ASTRA_LAUNCH_SOURCE_DESKTOP,
            &volume, arguments[4], &process_id) == ASTRA_OK);
        assert(process_id == 42u);
        assert(astra_application_launch_for_volume_path(7u, bundle,
            sizeof(bundle) - 1u, ASTRA_LAUNCH_SOURCE_DESKTOP,
            &volume, "usb0/folder", &process_id) ==
            ASTRA_ERROR_INVALID_ARGUMENT);
        assert(astra_path_beneath("/usb0/folder/file", "/usb0"));
        assert(!astra_path_beneath("/usb01/folder", "/usb0"));
        assert(!astra_path_beneath("/dh0/file", "/usb0"));
        assert(strcmp(astra_path_leaf("/usb0/folder/file"), "file") == 0);
    }
    puts("application launch contract tests passed");
    return 0;
}
