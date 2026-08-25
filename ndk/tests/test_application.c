#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <astra/application.h>
#include <astra/status.h>

static uint32_t reply_receive;
static uint32_t transaction;
static uint16_t expected_count;
static uint16_t expected_source;

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
        assert(__builtin_strcmp(request->arguments.bytes,
                                "APPS:Terminal.app") == 0);
        if (expected_count == 3u) {
            const char *first = request->arguments.bytes + 18u;
            const char *second = first + __builtin_strlen(first) + 1u;

            assert(__builtin_strcmp(first, "WORK:first.txt") == 0);
            assert(__builtin_strcmp(second, "WORK:second.txt") == 0);
        }
        transaction = request->header.transaction_id;
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        AstraApplicationLaunchReply *reply =
            (AstraApplicationLaunchReply *)d2;

        assert(d1 == reply_receive && d3 == sizeof(*reply) && d5 == 0u);
        reply->header.total_size = sizeof(*reply);
        reply->header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        reply->header.protocol = ASTRA_APPLICATION_PROTOCOL;
        reply->header.protocol_version = ASTRA_APPLICATION_VERSION;
        reply->header.operation = ASTRA_APPLICATION_LAUNCHED;
        reply->header.transaction_id = transaction;
        reply->status = ASTRA_STATUS_OK;
        reply->process_id = 42u;
        *out_d1 = sizeof(*reply);
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE && d1 == reply_receive);
    }
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    static const char *const dropped[] = {
        "WORK:first.txt", "WORK:second.txt"
    };
    uint32_t process_id = 0u;

    assert(astra_application_launch(ASTRA_INVALID_HANDLE,
                                    "APPS:Terminal.app", 17u,
                                    &process_id) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    expected_count = 1u;
    expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
    assert(astra_application_launch(7u, "APPS:Terminal.app", 17u,
                                    &process_id) == ASTRA_OK);
    assert(process_id == 42u);
    expected_count = 3u;
    expected_source = ASTRA_LAUNCH_SOURCE_DESKTOP;
    assert(astra_application_launch_with_arguments(
               7u, "APPS:Terminal.app", 17u,
               ASTRA_LAUNCH_SOURCE_DESKTOP, dropped, 2u, &process_id) ==
           ASTRA_OK);
    assert(process_id == 42u);
    puts("application launch contract tests passed");
    return 0;
}
