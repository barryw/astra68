#include <astra/network_library.h>
#include <astra/network_core.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void *mapped_area;
static uint32_t mapped_size;
static uint32_t logged_failure;
static uint32_t open_until_area;
static uint32_t created_area_bytes;
static int mock_exchange;
static int mock_zero_token;
static AstraNetworkRequestMessage last_request;

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    assert(strcmp(operation, "network session open") == 0);
    logged_failure = status;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    if (open_until_area != 0u) {
        assert(messages == 1u && bytes == sizeof(AstraNetworkReplyMessage));
        *receive = 10u;
        *send = 11u;
        return ASTRA_SYSCALL_OK;
    }
    (void)messages; (void)bytes; (void)receive; (void)send;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                                   uint32_t *duplicate)
{
    if (open_until_area != 0u) {
        assert(handle == 11u);
        *duplicate = 16u;
        return ASTRA_SYSCALL_OK;
    }
    (void)handle; (void)rights; (void)duplicate;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_area_create_flagged(uint32_t bytes, uint32_t rights,
                                      uint32_t flags, uint32_t *handle)
{
    assert(open_until_area != 0u);
    created_area_bytes = bytes;
    (void)rights; (void)flags; (void)handle;
    return ASTRA_SYSCALL_RESOURCE_LIMIT;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                           void **address, uint32_t *bytes)
{
    if (handle != 12u || permissions !=
            (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE) ||
        mapped_area == NULL)
        return ASTRA_SYSCALL_INVALID_HANDLE;
    *address = mapped_area;
    *bytes = mapped_size;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_unmap(void *address)
{
    (void)address;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle)
{
    (void)initial; (void)maximum; (void)rights; (void)handle;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *detail)
{
    if (mock_exchange && (handle == 13u || handle == 14u))
        return ASTRA_SYSCALL_OK;
    (void)handle; (void)deadline; (void)detail;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    if (mock_exchange && handle == 13u && count == 1u)
        return ASTRA_SYSCALL_OK;
    (void)handle; (void)count; (void)woken;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t count)
{
    if (mock_exchange) {
        assert(handle == 11u && size == sizeof(last_request));
        assert(handles == NULL && count == 0u);
        (void)memcpy(&last_request, message, size);
        return ASTRA_SYSCALL_OK;
    }
    (void)handle; (void)message; (void)size; (void)handles; (void)count;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count)
{
    if (mock_exchange) {
        AstraNetworkReplyMessage *reply = message;
        static const char hostname[] = "host.example";

        assert(handle == 14u && capacity == sizeof(*reply));
        assert(handles == NULL && handle_capacity == 0u);
        (void)memset(reply, 0, sizeof(*reply));
        astra_message_header_set(
            &reply->header, sizeof(*reply), ASTRA_NETWORK_PROTOCOL,
            ASTRA_NETWORK_VERSION, last_request.header.operation,
            last_request.header.transaction_id);
        reply->session = last_request.session;
        *size = sizeof(*reply);
        if (last_request.header.operation == ASTRA_NETWORK_RESOLVE &&
            last_request.value == 0u) {
            reply->status = ASTRA_NETWORK_IN_PROGRESS;
            reply->value = mock_zero_token ? 0u : 78u;
        } else if (last_request.header.operation ==
                   ASTRA_NETWORK_REVERSE_RESOLVE) {
            if (last_request.value == 0u) {
                reply->status = ASTRA_NETWORK_IN_PROGRESS;
                reply->value = mock_zero_token ? 0u : 77u;
            } else {
                uint8_t *bytes = astra_network_shared_slot_bytes(
                    mapped_area, last_request.slot);

                assert(last_request.value == 77u);
                reply->transferred = sizeof(hostname);
                reply->status = last_request.length < sizeof(hostname) ?
                    ASTRA_NETWORK_BUFFER_TOO_SMALL : ASTRA_NETWORK_OK;
                if (reply->status == ASTRA_NETWORK_OK)
                    (void)memcpy(bytes, hostname, sizeof(hostname));
            }
        } else {
            AstraNetworkInterfaceAddress record = {0};
            uint8_t *bytes = astra_network_shared_slot_bytes(
                mapped_area, last_request.slot);

            assert(last_request.header.operation == ASTRA_NETWORK_INTERFACES);
            reply->transferred = 1u;
            reply->status = last_request.length < sizeof(record) ?
                ASTRA_NETWORK_BUFFER_TOO_SMALL : ASTRA_NETWORK_OK;
            record.address.size = sizeof(record.address);
            record.address.family = ASTRA_NETWORK_FAMILY_IPV4;
            record.address.address[0] = 127u;
            record.address.address[3] = 1u;
            (void)memcpy(record.name, "lo", 3u);
            if (reply->status == ASTRA_NETWORK_OK)
                (void)memcpy(bytes, &record, sizeof(record));
        }
        return ASTRA_SYSCALL_OK;
    }
    (void)handle; (void)message; (void)capacity; (void)handles;
    (void)handle_capacity; (void)size; (void)handle_count;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_close(uint32_t handle)
{
    (void)handle;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraNetworkSession session = ASTRA_NETWORK_SESSION_INIT;
    AstraNetworkEndpoint endpoint = ASTRA_NETWORK_ENDPOINT_INIT;
    AstraNetworkRequest request = ASTRA_NETWORK_REQUEST_INIT;
    AstraNetworkSessionState session_state;
    AstraNetworkEndpointState endpoint_state;
    AstraNetworkAddress reverse_address = {0};
    AstraNetworkInterfaceAddress interface_address = {0};
    char hostname[32];
    uint32_t count;

    assert(astra_network_session_open(0u, &session) == ASTRA_NETWORK_INVALID);
    assert(astra_network_session_open(1u, &session) == ASTRA_NETWORK_UNSUPPORTED);
    assert(logged_failure == ((1u << 16) | ASTRA_SYSCALL_UNSUPPORTED));
    open_until_area = 1u;
    assert(astra_network_session_open(1u, &session) ==
           ASTRA_NETWORK_RESOURCE_LIMIT);
    assert(created_area_bytes == ASTRA_NETWORK_SHARED_BYTES_MAX);
    assert(logged_failure ==
           ((3u << 16) | ASTRA_SYSCALL_RESOURCE_LIMIT));
    assert(session._private_area == 0u && session._private_shared == NULL);
    open_until_area = 0u;
    assert(astra_network_endpoint_open(&session, ASTRA_NETWORK_FAMILY_IPV4,
                                  ASTRA_NETWORK_TYPE_STREAM,
                                  ASTRA_NETWORK_PROTOCOL_TCP, &endpoint) ==
           ASTRA_NETWORK_INVALID);
    assert(astra_network_request_try(&request, NULL, 0u, NULL) ==
           ASTRA_NETWORK_INVALID);
    assert(astra_network_readiness_handle(NULL) == 0u);
    memset(&session_state, 0, sizeof(session_state));
    memset(&endpoint_state, 0, sizeof(endpoint_state));
    assert(astra_network_session_export(&session, &session_state) ==
           ASTRA_NETWORK_INVALID);
    assert(astra_network_session_import(&session_state, &session) ==
           ASTRA_NETWORK_INVALID);
    assert(astra_network_endpoint_export(&endpoint, &endpoint_state) ==
           ASTRA_NETWORK_INVALID);
    assert(astra_network_endpoint_import(&session, &endpoint_state, &endpoint) ==
           ASTRA_NETWORK_INVALID);

    mapped_size = ASTRA_NETWORK_SHARED_METADATA_BYTES +
                  2u * ASTRA_NETWORK_SLOT_BYTES;
    mapped_area = calloc(1u, mapped_size);
    assert(mapped_area != NULL);
    assert(astra_network_shared_initialize(mapped_area, mapped_size, 19u));
    session._private_control = 11u;
    session._private_area = 12u;
    session._private_lock = 13u;
    session._private_reply = 14u;
    session._private_notify = 15u;
    session._private_shared = mapped_area;
    session._private_shared_size = mapped_size;
    session._private_id = 17u;
    session._private_generation = 19u;
    session._private_transaction = 23u;
    assert(astra_network_session_export(&session, &session_state) ==
           ASTRA_NETWORK_OK);
    memset(&session, 0, sizeof(session));
    assert(astra_network_session_import(&session_state, &session) ==
           ASTRA_NETWORK_OK);
    assert(session._private_shared == mapped_area &&
           session._private_transaction == 23u);

    endpoint._private_session = &session;
    endpoint._private_control = 31u;
    endpoint._private_readiness = 32u;
    endpoint._private_id = 33u;
    endpoint._private_generation = 34u;
    endpoint._private_family = ASTRA_NETWORK_FAMILY_IPV6;
    endpoint._private_type = ASTRA_NETWORK_TYPE_DATAGRAM;
    endpoint._private_protocol = ASTRA_NETWORK_PROTOCOL_UDP;
    assert(astra_network_endpoint_export(&endpoint, &endpoint_state) ==
           ASTRA_NETWORK_OK);
    memset(&endpoint, 0, sizeof(endpoint));
    assert(astra_network_endpoint_import(&session, &endpoint_state, &endpoint) ==
           ASTRA_NETWORK_OK);
    assert(endpoint._private_session == &session &&
           endpoint._private_control == 31u &&
           endpoint._private_type == ASTRA_NETWORK_TYPE_DATAGRAM);
    mock_exchange = 1;
    assert(astra_network_resolve_start(&session, "localhost", 0u,
                                       ASTRA_NETWORK_FAMILY_IPV4,
                                       ASTRA_NETWORK_TYPE_STREAM,
                                       ASTRA_NETWORK_PROTOCOL_TCP,
                                       &request) == ASTRA_NETWORK_IN_PROGRESS);
    assert(request._private_state == 1u && request._private_token == 78u);
    mock_zero_token = 1;
    assert(astra_network_resolve_start(&session, "localhost", 0u,
                                       ASTRA_NETWORK_FAMILY_IPV4,
                                       ASTRA_NETWORK_TYPE_STREAM,
                                       ASTRA_NETWORK_PROTOCOL_TCP,
                                       &request) == ASTRA_NETWORK_IO);
    assert(request._private_state == 0u);
    mock_zero_token = 0;
    reverse_address.size = sizeof(reverse_address);
    reverse_address.family = ASTRA_NETWORK_FAMILY_IPV4;
    reverse_address.address[0] = 192u;
    reverse_address.address[3] = 7u;
    assert(astra_network_reverse_start(&session, &reverse_address,
                                       &request) == ASTRA_NETWORK_IN_PROGRESS);
    assert(request._private_state == 2u);
    assert(astra_network_request_try(&request, &reverse_address, 1u,
                                     &count) == ASTRA_NETWORK_INVALID);
    assert(astra_network_reverse_try(&request, NULL, 0u, &count) ==
           ASTRA_NETWORK_BUFFER_TOO_SMALL);
    assert(count == sizeof("host.example") && request._private_state == 2u);
    assert(astra_network_reverse_try(&request, hostname, sizeof(hostname),
                                     &count) == ASTRA_NETWORK_OK);
    assert(strcmp(hostname, "host.example") == 0 &&
           count == sizeof("host.example") && request._private_state == 0u);
    assert(astra_network_reverse_try(&request, hostname, sizeof(hostname),
                                     &count) == ASTRA_NETWORK_INVALID);
    mock_zero_token = 1;
    assert(astra_network_reverse_start(&session, &reverse_address,
                                       &request) == ASTRA_NETWORK_IO);
    assert(request._private_state == 0u);
    mock_zero_token = 0;
    reverse_address.family = ASTRA_NETWORK_FAMILY_UNSPEC;
    assert(astra_network_reverse_start(&session, &reverse_address,
                                       &request) == ASTRA_NETWORK_INVALID);
    assert(astra_network_interfaces(&session, NULL, 0u, &count) ==
           ASTRA_NETWORK_BUFFER_TOO_SMALL && count == 1u);
    assert(astra_network_interfaces(&session, &interface_address, 1u,
                                    &count) == ASTRA_NETWORK_OK);
    assert(count == 1u && strcmp(interface_address.name, "lo") == 0);
    assert(interface_address.address.address[0] == 127u &&
           interface_address.address.address[3] == 1u);
    assert(astra_network_interfaces(&session, NULL, 1u, &count) ==
           ASTRA_NETWORK_INVALID);
    free(mapped_area);
    return 0;
}
