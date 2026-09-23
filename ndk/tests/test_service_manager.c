#include <astra/service_manager.h>
#include <astra/status.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t reply_receive;
static uint32_t transaction;
static uint32_t expected_operation;
static AstraServiceListCursor expected_cursor;

uint32_t astra_ndk_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
                                uintptr_t d3, uintptr_t d4, uintptr_t d5,
                                uint32_t *out_d1, uint32_t *out_d2)
{
    *out_d1 = 0u;
    *out_d2 = 0u;
    if (number == ASTRA_SYSCALL_PORT_CREATE) {
        assert(d1 == 1u && d2 == sizeof(AstraServiceManagerReply));
        reply_receive = 0x200u;
        *out_d1 = reply_receive;
        *out_d2 = reply_receive + 1u;
    } else if (number == ASTRA_SYSCALL_PORT_SEND_TRY) {
        const AstraServiceManagerRequest *request =
            (const AstraServiceManagerRequest *)d2;
        const uint32_t *handles = (const uint32_t *)d4;

        assert(d1 == 9u && d3 == sizeof(*request) && d5 == 1u);
        assert(handles[0] == reply_receive + 1u);
        assert(request->header.protocol == ASTRA_SERVICE_MANAGER_PROTOCOL);
        assert(request->header.operation == expected_operation);
        assert(memcmp(&request->cursor, &expected_cursor,
                      sizeof(request->cursor)) == 0);
        transaction = request->header.transaction_id;
    } else if (number == ASTRA_SYSCALL_PORT_RECEIVE_TRY) {
        AstraServiceManagerReply *reply = (AstraServiceManagerReply *)d2;

        assert(d1 == reply_receive && d3 == sizeof(*reply) && d5 == 0u);
        reply->header.total_size = sizeof(*reply);
        reply->header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        reply->header.protocol = ASTRA_SERVICE_MANAGER_PROTOCOL;
        reply->header.protocol_version = ASTRA_SERVICE_MANAGER_VERSION;
        reply->header.operation = ASTRA_SERVICE_MANAGER_REPLY;
        reply->header.transaction_id = transaction;
        reply->status = ASTRA_STATUS_OK;
        reply->next_cursor.position = UINT64_C(0xffffffffffffffff);
        reply->next_cursor.source = ASTRA_SERVICE_LIST_SOURCE_DYNAMIC;
        (void)strcpy(reply->info.name, "remote-desktop");
        reply->info.state = ASTRA_SERVICE_STATE_READY;
        reply->info.process_id = 12u;
        *out_d1 = sizeof(*reply);
    } else {
        assert(number == ASTRA_SYSCALL_CLOSE && d1 == reply_receive);
    }
    return ASTRA_SYSCALL_OK;
}

static AstraServiceDefinition valid_definition(void)
{
    AstraServiceDefinition definition = {0};
    static const char executable[] = "/services/remote-desktop";

    definition.structure_size = sizeof(definition);
    definition.flags = ASTRA_SERVICE_RUNS_PAIRED | ASTRA_SERVICE_ENABLED;
    definition.start_policy = ASTRA_SERVICE_START_MANUAL;
    definition.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
    (void)strcpy(definition.name, "remote-desktop");
    (void)strcpy(definition.executable, executable);
    definition.argument_count = 1u;
    definition.argument_length = sizeof(executable);
    (void)memcpy(definition.arguments, executable, sizeof(executable));
    return definition;
}

int main(void)
{
    AstraServiceDefinition definition = valid_definition();
    AstraServiceInfo info;
    AstraServiceListCursor cursor = ASTRA_SERVICE_LIST_CURSOR_INIT;
    AstraServiceListCursor next;

    (void)memset(&definition, 0xa5, sizeof(definition));
    assert(astra_service_definition_init(
               &definition, "remote-desktop", "/services/remote-desktop",
               ASTRA_SERVICE_RUNS_PAIRED) == ASTRA_OK);
    assert(definition.argument_count == 1u &&
           strcmp(definition.arguments, definition.executable) == 0);
    assert(astra_service_definition_add_grant(
               &definition, "NETWORK", 0u, 0) == ASTRA_OK);
    assert(astra_service_definition_add_publication(
               &definition, "REMOTE", 0u) == ASTRA_OK);
    assert(astra_service_definition_add_dependency(
               &definition, "NETWORK") == ASTRA_OK);

    definition.argument_length = UINT16_MAX;
    assert(astra_service_definition_add_argument(&definition, "x") ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    definition.grant_count = ASTRA_LAUNCH_GRANT_MAX + 1u;
    assert(astra_service_definition_add_grant(
               &definition, "NETWORK", 0u, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    definition.publication_count = ASTRA_MESSAGE_HANDLES_MAX + 1u;
    assert(astra_service_definition_add_publication(
               &definition, "REMOTE", 0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    definition.dependency_count = ASTRA_LAUNCH_GRANT_MAX + 1u;
    assert(astra_service_definition_add_dependency(
               &definition, "NETWORK") == ASTRA_ERROR_INVALID_ARGUMENT);

    definition = valid_definition();
    assert(astra_service_definition_validate(&definition) == ASTRA_OK);
    definition.executable[0] = (char)0xc0;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    definition.argument_length--;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    definition.flags |= ASTRA_SERVICE_RUNS_ASTRA;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    (void)strcpy(definition.name, "../escape");
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    (void)strcpy(definition.grants[0].name, "NETWORK");
    definition.grant_count = 1u;
    definition.grants[0].rights = ASTRA_RIGHT_SIGNAL;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition.grants[0].rights = ASTRA_RIGHT_READ;
    definition.grants[0].is_namespace = 0u;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    definition = valid_definition();
    (void)strcpy(definition.publications[0].name, "REMOTE");
    definition.publication_count = 1u;
    definition.publications[0].rights = ASTRA_RIGHT_SIGNAL;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    expected_operation = ASTRA_SERVICE_MANAGER_LIST;
    expected_cursor.position = UINT64_C(0xffffffffffffffff);
    expected_cursor.source = ASTRA_SERVICE_LIST_SOURCE_DYNAMIC;
    expected_cursor.reserved = 0u;
    cursor = expected_cursor;
    assert(astra_service_list(9u, &cursor, &info, &next) == ASTRA_OK);
    assert(strcmp(info.name, "remote-desktop") == 0 &&
           next.position == UINT64_C(0xffffffffffffffff) &&
           next.source == ASTRA_SERVICE_LIST_SOURCE_DYNAMIC);
    expected_operation = ASTRA_SERVICE_MANAGER_RESTART;
    (void)memset(&expected_cursor, 0, sizeof(expected_cursor));
    assert(astra_service_control(9u, ASTRA_SERVICE_MANAGER_RESTART,
                                 "remote-desktop", &info) == ASTRA_OK);
    assert(info.process_id == 12u);
    assert(astra_service_control(9u, ASTRA_SERVICE_MANAGER_LIST,
                                 "remote-desktop", &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    puts("service manager contract tests passed");
    return 0;
}
