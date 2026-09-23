#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/application.h>
#include <astra/status.h>

static uint32_t reply_receive;
static uint32_t transaction;
static uint16_t expected_count;
static uint16_t expected_source;
static const char *expected_path;
static const char *const *expected_arguments;
static int reply_has_handle = 1;
static uint32_t process_handle_closes;

uint32_t astra_ndk_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CREATE) {
        assert(d1 == 1u && d2 == sizeof(AstraApplicationLaunchReply));
        reply_receive = 0x100u;
        *out_d1 = reply_receive;
        *out_d2 = reply_receive + 1u;
    } else if (number == ASTRA_SYSCALL_PORT_SEND_TRY) {
        const AstraApplicationLaunchRequest *request =
            (const AstraApplicationLaunchRequest *)d2;
        const uint32_t *handles = (const uint32_t *)d4;

        assert(d1 == 7u && d3 == sizeof(*request) && d5 == 1u);
        assert(handles[0] == reply_receive + 1u);
        assert(request->header.protocol == ASTRA_APPLICATION_PROTOCOL);
        assert(request->header.operation == ASTRA_APPLICATION_LAUNCH);
        assert(request->arguments.count == expected_count);
        assert(request->arguments.source == expected_source);
        assert(request->arguments.flags == 0u);
        assert(__builtin_strcmp(request->arguments.bytes, expected_path) == 0);
        {
            const char *argument = request->arguments.bytes +
                                   __builtin_strlen(expected_path) + 1u;

            for (uint16_t index = 1u; index < expected_count; ++index) {
                assert(__builtin_strcmp(argument,
                                        expected_arguments[index - 1u]) == 0);
                argument += __builtin_strlen(argument) + 1u;
            }
        }
        transaction = request->header.transaction_id;
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        AstraApplicationLaunchReply *reply =
            (AstraApplicationLaunchReply *)d2;
        uint32_t *handles = (uint32_t *)d4;

        assert(d1 == reply_receive && d3 == sizeof(*reply) && d5 == 1u);
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
            handles[0] = 0x200u;
            *out_d2 = 1u;
        }
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE);
        assert(d1 == reply_receive || d1 == 0x200u);
        if (d1 == 0x200u)
            ++process_handle_closes;
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
           ASTRA_ERROR_INVALID_ARGUMENT);
    puts("application launch contract tests passed");
    return 0;
}
