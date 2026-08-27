#include <astra/network_library.h>
#include <astra/network_core.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

extern const AstraNetworkLibraryV1 astra_library_exports;

static void *mapped_area;
static uint32_t mapped_size;
static uint32_t logged_failure;

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    assert(strcmp(operation, "network session open") == 0);
    logged_failure = status;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_port_create(uint32_t messages, uint32_t bytes,
                              uint32_t *receive, uint32_t *send)
{
    (void)messages; (void)bytes; (void)receive; (void)send;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_handle_duplicate(uint32_t handle, uint32_t rights,
                                   uint32_t *duplicate)
{
    (void)handle; (void)rights; (void)duplicate;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_area_create_flagged(uint32_t bytes, uint32_t rights,
                                      uint32_t flags, uint32_t *handle)
{
    (void)bytes; (void)rights; (void)flags; (void)handle;
    return ASTRA_SYSCALL_UNSUPPORTED;
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
    (void)handle; (void)deadline; (void)detail;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    (void)handle; (void)count; (void)woken;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_port_send(uint32_t handle, const void *message, uint32_t size,
                         const uint32_t *handles, uint32_t count)
{
    (void)handle; (void)message; (void)size; (void)handles; (void)count;
    return ASTRA_SYSCALL_UNSUPPORTED;
}

uint32_t astra_port_receive(uint32_t handle, void *message, uint32_t capacity,
                            uint32_t *handles, uint32_t handle_capacity,
                            uint32_t *size, uint32_t *handle_count)
{
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
    const AstraNetworkLibraryV1 *library = &astra_library_exports;
    AstraNetworkSession session = ASTRA_NETWORK_SESSION_INIT;
    AstraNetworkEndpoint endpoint = ASTRA_NETWORK_ENDPOINT_INIT;
    AstraNetworkRequest request = ASTRA_NETWORK_REQUEST_INIT;
    AstraNetworkSessionState session_state;
    AstraNetworkEndpointState endpoint_state;

    assert(library->abi_major == ASTRA_NETWORK_LIBRARY_ABI_MAJOR);
    assert(library->abi_minor == ASTRA_NETWORK_LIBRARY_ABI_MINOR);
    assert(library->structure_size == sizeof(*library));
    assert(library->session_open(0u, &session) == ASTRA_NETWORK_INVALID);
    assert(library->session_open(1u, &session) == ASTRA_NETWORK_UNSUPPORTED);
    assert(logged_failure == ((1u << 16) | ASTRA_SYSCALL_UNSUPPORTED));
    assert(library->endpoint_open(&session, ASTRA_NETWORK_FAMILY_IPV4,
                                  ASTRA_NETWORK_TYPE_STREAM,
                                  ASTRA_NETWORK_PROTOCOL_TCP, &endpoint) ==
           ASTRA_NETWORK_INVALID);
    assert(library->request_try(&request, NULL, 0u, NULL) ==
           ASTRA_NETWORK_INVALID);
    assert(library->readiness_handle(NULL) == 0u);
    memset(&session_state, 0, sizeof(session_state));
    memset(&endpoint_state, 0, sizeof(endpoint_state));
    assert(library->session_export(&session, &session_state) ==
           ASTRA_NETWORK_INVALID);
    assert(library->session_import(&session_state, &session) ==
           ASTRA_NETWORK_INVALID);
    assert(library->endpoint_export(&endpoint, &endpoint_state) ==
           ASTRA_NETWORK_INVALID);
    assert(library->endpoint_import(&session, &endpoint_state, &endpoint) ==
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
    assert(library->session_export(&session, &session_state) ==
           ASTRA_NETWORK_OK);
    memset(&session, 0, sizeof(session));
    assert(library->session_import(&session_state, &session) ==
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
    assert(library->endpoint_export(&endpoint, &endpoint_state) ==
           ASTRA_NETWORK_OK);
    memset(&endpoint, 0, sizeof(endpoint));
    assert(library->endpoint_import(&session, &endpoint_state, &endpoint) ==
           ASTRA_NETWORK_OK);
    assert(endpoint._private_session == &session &&
           endpoint._private_control == 31u &&
           endpoint._private_type == ASTRA_NETWORK_TYPE_DATAGRAM);
    free(mapped_area);
    return 0;
}
