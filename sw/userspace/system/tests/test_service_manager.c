#include <astra/port.h>
#include <astra/service_manager.h>
#include <astra/service_definition_payload.h>
#include <astra/service_payload.h>
#include <astra/area.h>
#include <astra/runtime.h>
#include <stdlib.h>
#include <astra/status.h>

#include "syscall.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t expected_operation;
static const char *expected_name;
static uint32_t expected_value;
static AstraServiceListCursor expected_cursor;

static char shared[ASTRA_SERVICE_DEFINITION_PAYLOAD_MAX];
static uint32_t shared_size, area_closes, payload_closes, maps, unmaps, outstanding;
static int fail_allocate, fault, unsent;
static const char *executable = "/services/remote-desktop";
static AstraServiceDefinition valid_definition(void);

void *astra_runtime_allocate(size_t size)
{
    if (fail_allocate) return NULL;
    void *bytes = malloc(size); if (bytes != NULL) ++outstanding; return bytes;
}
void astra_runtime_deallocate(void *bytes)
{
    if (bytes != NULL) { assert(outstanding != 0u); --outstanding; free(bytes); }
}
AstraResult astra_area_create(uint32_t size, uint32_t rights, AstraArea *area)
{
    assert(size <= sizeof(shared));
    assert(rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    area->handle = 0x100u; shared_size = size; return ASTRA_OK;
}
AstraResult astra_area_map(AstraArea *area, uint32_t flags)
{
    assert(area->handle == 0x100u || area->handle == 0x102u);
    area->address = shared; area->size = shared_size; area->map_flags = flags; ++maps; return ASTRA_OK;
}
AstraResult astra_area_unmap(AstraArea *area)
{
    assert(area->address == shared); area->address = NULL; area->size = area->map_flags = 0u;
    ++unmaps; return ASTRA_OK;
}
AstraResult astra_area_close(AstraArea *area)
{
    if (area->address != NULL) { AstraResult result = astra_area_unmap(area); assert(result == ASTRA_OK); }
    return astra_handle_close(&area->handle);
}
uint32_t astra_system_test_syscall(uint32_t number, uintptr_t d1, uintptr_t d2,
    uintptr_t d3, uintptr_t d4, uintptr_t d5, uint32_t *out_d1, uint32_t *out_d2)
{
    *out_d1 = *out_d2 = 0u; (void)d3; (void)d4; (void)d5;
    if (number == ASTRA_SYSCALL_HANDLE_DUPLICATE) {
        assert(d1 == 0x100u && d2 == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
        *out_d1 = 0x101u;
    } else if (number == ASTRA_SYSCALL_CLOSE) {
        if (d1 == 0x100u) ++area_closes;
        else { assert(d1 == 0x101u || d1 == 0x102u); ++payload_closes; }
    } else {
        assert(number == ASTRA_SYSCALL_PORT_CALL);
        const AstraCall *call = astra_system_test_call;
        const AstraServiceManagerRequest *request = call->request;
        AstraServiceManagerReply *reply = call->reply;
        assert(((const AstraPortCall *)d1)->port == 9u);
        assert(call->request_size == sizeof(*request) && call->reply_index == 0u);
        assert(call->reply_capacity == sizeof(*reply) && call->reply_handle_capacity == 1u);
        assert(request->header.protocol == ASTRA_SERVICE_MANAGER_PROTOCOL && request->header.protocol_version == 7u);
        assert(request->header.operation == expected_operation && strcmp(request->name, expected_name) == 0);
        assert(memcmp(&request->cursor, &expected_cursor, sizeof(request->cursor)) == 0);
        assert(request->value == expected_value && request->reserved == 0u && request->reserved_payload == 0u);
        assert(call->handle_count == (expected_operation == ASTRA_SERVICE_MANAGER_ADD ? 1u : 0u));
        if (expected_operation == ASTRA_SERVICE_MANAGER_ADD) {
            assert(call->handles[0] == 0x101u && request->payload_size == shared_size && area_closes != 0u);
            AstraServiceDefinition sent = ASTRA_SERVICE_DEFINITION_INIT;
            assert(astra_service_definition_decode(shared, shared_size, &sent) == ASTRA_OK);
            assert(strcmp(sent.executable, executable) == 0 && strcmp(sent.name, expected_name) == 0);
            astra_service_definition_destroy(&sent);
        } else assert(request->payload_size == 0u);
        if (unsent) { *out_d1 = ASTRA_PORT_CALL_UNSENT; return ASTRA_SYSCALL_PEER_DEAD; }
        reply->header.total_size = sizeof(*reply); reply->header.header_size = ASTRA_MESSAGE_HEADER_SIZE;
        reply->header.protocol = ASTRA_SERVICE_MANAGER_PROTOCOL; reply->header.protocol_version = ASTRA_SERVICE_MANAGER_VERSION;
        reply->header.operation = ASTRA_SERVICE_MANAGER_REPLY; reply->header.transaction_id = request->header.transaction_id;
        reply->status = ASTRA_STATUS_OK;
        reply->next_cursor.position = expected_operation == ASTRA_SERVICE_MANAGER_LIST ? UINT64_MAX : 0u;
        reply->next_cursor.source = expected_operation == ASTRA_SERVICE_MANAGER_LIST ? ASTRA_SERVICE_LIST_SOURCE_DYNAMIC : 0u;
        if (expected_operation != ASTRA_SERVICE_MANAGER_SHUTDOWN && expected_operation != ASTRA_SERVICE_MANAGER_SYSTEM_RESTART &&
            expected_operation != ASTRA_SERVICE_MANAGER_REMOVE) {
            strcpy(reply->info.name, "remote-desktop"); reply->info.flags = ASTRA_SERVICE_RUNS_PAIRED | ASTRA_SERVICE_ENABLED;
            reply->info.start_policy = ASTRA_SERVICE_START_MANUAL; reply->info.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
            reply->info.state = ASTRA_SERVICE_STATE_READY; reply->info.process_id = 12u;
            if (expected_operation == ASTRA_SERVICE_MANAGER_INSPECT) {
                AstraServiceDefinition definition = valid_definition();
                shared_size = astra_service_definition_payload_size(&definition);
                assert(astra_service_definition_encode(&definition, shared, sizeof(shared)) == ASTRA_OK);
                astra_service_definition_destroy(&definition); reply->payload_kind = 2u;
            } else { shared_size = (uint32_t)strlen(executable) + 1u; memcpy(shared, executable, shared_size); reply->payload_kind = 1u; }
            reply->payload_size = shared_size; call->reply_handles[0] = 0x102u; *out_d2 = 1u;
            if (fault == 1) --shared_size;
            if (fault == 2) shared[reply->payload_size - 1u] = 'x';
            if (fault == 3) reply->payload_kind = 0u;
            if (fault == 4) ((AstraServiceDefinitionRecord *)(void *)shared)->executable_offset++;
        }
        *out_d1 = sizeof(*reply);
        if (fault == 5) fail_allocate = 1;
    }
    return ASTRA_SYSCALL_OK;
}

static AstraServiceDefinition valid_definition(void)
{
    AstraServiceDefinition definition = ASTRA_SERVICE_DEFINITION_INIT;
    assert(astra_service_definition_init(&definition, "remote-desktop", executable, ASTRA_SERVICE_RUNS_PAIRED) == ASTRA_OK);
    definition.restart_policy = ASTRA_SERVICE_RESTART_ON_FAULT;
    return definition;
}

int main(void)
{
    AstraServiceDefinition definition = valid_definition();
    AstraServiceInfo info = ASTRA_SERVICE_INFO_INIT;
    AstraServiceListCursor cursor = ASTRA_SERVICE_LIST_CURSOR_INIT;
    AstraServiceListCursor next;

    astra_service_definition_destroy(&definition);
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
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.grant_count = ASTRA_LAUNCH_GRANT_MAX + 1u;
    assert(astra_service_definition_add_grant(
               &definition, "NETWORK", 0u, 0) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.publication_count = ASTRA_MESSAGE_HANDLES_MAX + 1u;
    assert(astra_service_definition_add_publication(
               &definition, "REMOTE", 0u) == ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.dependency_count = ASTRA_LAUNCH_GRANT_MAX + 1u;
    assert(astra_service_definition_add_dependency(
               &definition, "NETWORK") == ASTRA_ERROR_INVALID_ARGUMENT);

    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    assert(astra_service_definition_validate(&definition) == ASTRA_OK);
    definition.executable[0] = (char)0xc0;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.argument_length--;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.flags |= ASTRA_SERVICE_RUNS_ASTRA;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    definition.priority = ASTRA_PROCESS_PRIORITY_MAX;
    assert(astra_service_definition_validate(&definition) == ASTRA_OK);
    definition.priority = ASTRA_PROCESS_PRIORITY_MAX + 1u;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    (void)strcpy(definition.name, "../escape");
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    astra_service_definition_destroy(&definition);
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
    astra_service_definition_destroy(&definition);
    definition = valid_definition();
    (void)strcpy(definition.publications[0].name, "REMOTE");
    definition.publication_count = 1u;
    definition.publications[0].rights = ASTRA_RIGHT_SIGNAL;
    assert(astra_service_definition_validate(&definition) ==
           ASTRA_ERROR_INVALID_ARGUMENT);

    expected_operation = ASTRA_SERVICE_MANAGER_LIST;
    expected_name = "";
    expected_cursor.position = UINT64_C(0xffffffffffffffff);
    expected_cursor.source = ASTRA_SERVICE_LIST_SOURCE_DYNAMIC;
    expected_cursor.reserved = 0u;
    cursor = expected_cursor;
    assert(astra_service_list(9u, &cursor, &info, &next) == ASTRA_OK);
    assert(strcmp(info.name, "remote-desktop") == 0 &&
           next.position == UINT64_C(0xffffffffffffffff) &&
           next.source == ASTRA_SERVICE_LIST_SOURCE_DYNAMIC);
    expected_operation = ASTRA_SERVICE_MANAGER_RESTART;
    expected_name = "remote-desktop";
    (void)memset(&expected_cursor, 0, sizeof(expected_cursor));
    assert(astra_service_control(9u, ASTRA_SERVICE_MANAGER_RESTART,
                                 "remote-desktop", &info) == ASTRA_OK);
    assert(info.process_id == 12u);
    assert(astra_service_control(9u, ASTRA_SERVICE_MANAGER_LIST,
                                 "remote-desktop", &info) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    expected_operation = ASTRA_SERVICE_MANAGER_SET_RESTART;
    expected_value = ASTRA_SERVICE_RESTART_NEVER;
    info.process_id = 0u;
    assert(astra_service_set_restart(9u, "remote-desktop",
                                     ASTRA_SERVICE_RESTART_NEVER, &info) ==
           ASTRA_OK);
    assert(info.process_id == 12u);
    expected_value = ASTRA_SERVICE_RESTART_ALWAYS;
    assert(astra_service_set_restart(9u, "remote-desktop",
                                     ASTRA_SERVICE_RESTART_ALWAYS, NULL) ==
           ASTRA_OK);
    assert(astra_service_set_restart(9u, "remote-desktop",
                                     ASTRA_SERVICE_RESTART_ALWAYS + 1u,
                                     NULL) == ASTRA_ERROR_INVALID_ARGUMENT);
    expected_value = 0u;
    expected_operation = ASTRA_SERVICE_MANAGER_SHUTDOWN;
    expected_name = "";
    assert(astra_system_shutdown_request(9u) == ASTRA_OK);
    assert(astra_system_shutdown_request(ASTRA_INVALID_HANDLE) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    expected_operation = ASTRA_SERVICE_MANAGER_SYSTEM_RESTART;
    assert(astra_system_restart_request(9u) == ASTRA_OK);
    assert(astra_system_restart_request(ASTRA_INVALID_HANDLE) ==
           ASTRA_ERROR_INVALID_ARGUMENT);
    expected_operation = ASTRA_SERVICE_MANAGER_INSPECT; expected_name = "remote-desktop";
    assert(astra_service_inspect(9u, expected_name, &info, &definition) == ASTRA_OK);
    assert(strcmp(info.executable, executable) == 0 && strcmp(definition.executable, executable) == 0);
    for (fault = 1; fault <= 4; ++fault) {
        char *old_info = info.executable, *old_definition = definition.executable;
        uint32_t owned = outstanding, closed = payload_closes;
        assert(astra_service_inspect(9u, expected_name, &info, &definition) != ASTRA_OK);
        assert(info.executable == old_info && definition.executable == old_definition && outstanding == owned);
        assert(payload_closes == closed + 1u);
    }
    fault = 0;
    uint32_t owned = outstanding, closed = payload_closes;
    fault = 5;
    assert(astra_service_inspect(9u, expected_name, &info, &definition) == ASTRA_ERROR_OUT_OF_MEMORY);
    fail_allocate = 0;
    fault = 0;
    assert(outstanding == owned && payload_closes == closed + 1u);
    char long_path[ASTRA_VFS_PATH_MAX];
    for (uint32_t at = 0u; at < sizeof(long_path) - 1u; ++at)
        long_path[at] = at % 32u == 31u ? '/' : 'p';
    long_path[sizeof(long_path) - 1u] = '\0'; executable = long_path;
    astra_service_definition_destroy(&definition); definition = valid_definition();
    expected_operation = ASTRA_SERVICE_MANAGER_ADD;
    assert(astra_service_add(9u, &definition) == ASTRA_OK);
    unsent = 1; closed = payload_closes;
    assert(astra_service_add(9u, &definition) == ASTRA_ERROR_PEER_DEAD);
    assert(payload_closes == closed + 1u); unsent = 0;
    expected_operation = ASTRA_SERVICE_MANAGER_INSPECT;
    assert(astra_service_inspect(9u, expected_name, &info, &definition) == ASTRA_OK);
    assert(strlen(info.executable) == ASTRA_VFS_PATH_MAX - 1u && strlen(definition.executable) == ASTRA_VFS_PATH_MAX - 1u);
    char too_long[ASTRA_VFS_PATH_MAX + 1u];
    memset(too_long, 'p', sizeof(too_long) - 1u); too_long[sizeof(too_long) - 1u] = '\0';
    AstraServiceDefinition refused = ASTRA_SERVICE_DEFINITION_INIT;
    assert(astra_service_definition_init(&refused, "remote-desktop", too_long,
        ASTRA_SERVICE_RUNS_PAIRED) == ASTRA_ERROR_NAME_TOO_LONG);
    astra_service_definition_destroy(&refused);
    assert(maps == unmaps);
    astra_service_info_destroy(&info); astra_service_definition_destroy(&definition);
    assert(outstanding == 0u);
    puts("service manager contract tests passed");
    return 0;
}
