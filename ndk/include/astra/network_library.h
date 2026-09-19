/** @file network_library.h @brief Capability-based Network Kit ABI. */
#ifndef ASTRA_NETWORK_LIBRARY_H
#define ASTRA_NETWORK_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#include <astra/network.h>
#include <astra/resource.h>
#include <astra/types.h>

/** Logical name of the Network Kit shared library. */
#define ASTRA_NETWORK_LIBRARY_NAME "network.library"
/** Network Kit ELF ABI major version. */
#define ASTRA_NETWORK_LIBRARY_ABI_MAJOR 1u
/** Network Kit backward-compatible ABI revision. */
#define ASTRA_NETWORK_LIBRARY_ABI_MINOR 1u

/** Open network-service session. */
typedef struct AstraNetworkSession {
    /** @cond ASTRA_INTERNAL */
    AstraHandle _private_control;
    AstraHandle _private_factory;
    AstraHandle _private_area;
    AstraHandle _private_lock;
    AstraHandle _private_reply;
    AstraHandle _private_notify;
    void *_private_shared;
    uint32_t _private_shared_size;
    uint32_t _private_id;
    uint32_t _private_generation;
    uint32_t _private_transaction;
    /** @endcond */
} AstraNetworkSession;

/** Open socket-like network endpoint. */
typedef struct AstraNetworkEndpoint {
    /** @cond ASTRA_INTERNAL */
    AstraNetworkSession *_private_session;
    AstraHandle _private_control;
    AstraHandle _private_readiness;
    uint32_t _private_id;
    uint32_t _private_generation;
    uint16_t _private_family;
    uint8_t _private_type;
    uint8_t _private_protocol;
    /** @endcond */
} AstraNetworkEndpoint;

/** In-flight asynchronous name-resolution request. */
typedef struct AstraNetworkRequest {
    /** @cond ASTRA_INTERNAL */
    AstraNetworkSession *_private_session;
    uint32_t _private_token;
    uint32_t _private_transaction;
    uint32_t _private_generation;
    uint32_t _private_state;
    /** @endcond */
} AstraNetworkRequest;

/** Capability-only session state preserved across atomic exec. */
typedef struct AstraNetworkSessionState {
    uint32_t size; /**< Structure size for ABI validation. */
    AstraHandle control; /**< Session control capability. */
    AstraHandle area; /**< Shared transport-area capability. */
    AstraHandle lock; /**< Shared transport-lock capability. */
    AstraHandle reply; /**< Reply-port receive capability. */
    AstraHandle notify; /**< Readiness notification capability. */
    uint32_t shared_size; /**< Mapped transport bytes. */
    uint32_t id; /**< Service-side session identifier. */
    uint32_t generation; /**< Service-side session generation. */
    uint32_t transaction; /**< Next transaction identifier. */
} AstraNetworkSessionState;

/** Capability-only endpoint state preserved across atomic exec. */
typedef struct AstraNetworkEndpointState {
    uint32_t size; /**< Structure size for ABI validation. */
    AstraHandle control; /**< Endpoint control capability. */
    AstraHandle readiness; /**< Endpoint readiness capability. */
    uint32_t id; /**< Service-side endpoint identifier. */
    uint32_t generation; /**< Service-side endpoint generation. */
    uint16_t family; /**< Address family. */
    uint8_t type; /**< Endpoint type. */
    uint8_t protocol; /**< Transport protocol. */
} AstraNetworkEndpointState;

/** Serialized byte size of AstraNetworkSessionState. */
#define ASTRA_NETWORK_SESSION_STATE_SIZE 40u
/** Serialized byte size of AstraNetworkEndpointState. */
#define ASTRA_NETWORK_ENDPOINT_STATE_SIZE 24u

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraNetworkSessionState) ==
                   ASTRA_NETWORK_SESSION_STATE_SIZE,
               "network session state changed");
_Static_assert(sizeof(AstraNetworkEndpointState) ==
                   ASTRA_NETWORK_ENDPOINT_STATE_SIZE,
               "network endpoint state changed");
/** @endcond */

/** Static initializer for a closed AstraNetworkSession. */
#define ASTRA_NETWORK_SESSION_INIT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
/** Static initializer for a closed AstraNetworkEndpoint. */
#define ASTRA_NETWORK_ENDPOINT_INIT { 0, 0, 0, 0, 0, 0, 0, 0 }
/** Static initializer for an idle AstraNetworkRequest. */
#define ASTRA_NETWORK_REQUEST_INIT { 0, 0, 0, 0, 0 }

/** Open a session. @param factory Network factory capability. @param session Receives session. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_session_open(AstraHandle factory, AstraNetworkSession *session);
/** Close a session. @param session Open session. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_session_close(AstraNetworkSession *session);
/** Create an endpoint. @param session Session. @param family Family. @param type Type. @param protocol Protocol. @param endpoint Receives endpoint. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_endpoint_open(AstraNetworkSession *session, uint16_t family, uint8_t type, uint8_t protocol, AstraNetworkEndpoint *endpoint);
/** Close an endpoint. @param endpoint Endpoint. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_endpoint_close(AstraNetworkEndpoint *endpoint);
/** Bind an endpoint. @param endpoint Endpoint. @param address Address. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_bind(AstraNetworkEndpoint *endpoint, const AstraNetworkAddress *address);
/** Connect an endpoint. @param endpoint Endpoint. @param address Address. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_connect(AstraNetworkEndpoint *endpoint, const AstraNetworkAddress *address);
/** Listen for connections. @param endpoint Endpoint. @param backlog Backlog. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_listen(AstraNetworkEndpoint *endpoint, uint32_t backlog);
/** Accept a connection. @param listener Listener. @param endpoint Receives endpoint. @param address Receives peer address. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_accept(AstraNetworkEndpoint *listener, AstraNetworkEndpoint *endpoint, AstraNetworkAddress *address);
/** Send bytes. @param endpoint Endpoint. @param bytes Source. @param length Bytes. @param flags Flags. @param moved Receives count. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_send(AstraNetworkEndpoint *endpoint, const void *bytes, size_t length, uint32_t flags, size_t *moved);
/** Send a datagram. @param endpoint Endpoint. @param bytes Source. @param length Bytes. @param flags Flags. @param address Destination. @param moved Receives count. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_send_to(AstraNetworkEndpoint *endpoint, const void *bytes, size_t length, uint32_t flags, const AstraNetworkAddress *address, size_t *moved);
/** Receive bytes. @param endpoint Endpoint. @param bytes Destination. @param capacity Bytes. @param flags Flags. @param moved Receives count. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_receive(AstraNetworkEndpoint *endpoint, void *bytes, size_t capacity, uint32_t flags, size_t *moved);
/** Receive a datagram. @param endpoint Endpoint. @param bytes Destination. @param capacity Bytes. @param flags Flags. @param address Receives source. @param moved Receives count. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_receive_from(AstraNetworkEndpoint *endpoint, void *bytes, size_t capacity, uint32_t flags, AstraNetworkAddress *address, size_t *moved);
/** Shut down directions. @param endpoint Endpoint. @param flags Directions. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_shutdown(AstraNetworkEndpoint *endpoint, uint32_t flags);
/** Read local address. @param endpoint Endpoint. @param address Receives address. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_local_address(AstraNetworkEndpoint *endpoint, AstraNetworkAddress *address);
/** Read peer address. @param endpoint Endpoint. @param address Receives address. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_peer_address(AstraNetworkEndpoint *endpoint, AstraNetworkAddress *address);
/** Read an option. @param endpoint Endpoint. @param option Option. @param value Receives value. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_get_option(AstraNetworkEndpoint *endpoint, uint32_t option, uint32_t *value);
/** Set an option. @param endpoint Endpoint. @param option Option. @param value Value. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_set_option(AstraNetworkEndpoint *endpoint, uint32_t option, uint32_t value);
/** Start name resolution. @param session Session. @param name Name. @param length Name bytes. @param family Family. @param type Type. @param protocol Protocol. @param request Receives request. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_resolve_start(AstraNetworkSession *session, const char *name, uint32_t length, uint16_t family, uint8_t type, uint8_t protocol, AstraNetworkRequest *request);
/** Poll resolution. @param request Request. @param addresses Receives addresses. @param capacity Capacity. @param count Receives count. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_request_try(AstraNetworkRequest *request, AstraNetworkAddress *addresses, uint32_t capacity, uint32_t *count);
/** Wait for resolution. @param request Request. @param addresses Receives addresses. @param capacity Capacity. @param count Receives count. @param deadline Deadline. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_request_wait(AstraNetworkRequest *request, AstraNetworkAddress *addresses, uint32_t capacity, uint32_t *count, uint64_t deadline);
/** Cancel resolution. @param request Request. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_request_cancel(AstraNetworkRequest *request);
/** Borrow readiness handle. @param endpoint Endpoint. @return Wait handle. */
AstraHandle astra_network_readiness_handle(const AstraNetworkEndpoint *endpoint);
/** Read readiness flags. @param endpoint Endpoint. @return Flags. */
uint32_t astra_network_readiness(const AstraNetworkEndpoint *endpoint);
/** Export a session. @param session Session. @param state Receives state. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_session_export(const AstraNetworkSession *session, AstraNetworkSessionState *state);
/** Import a session. @param state State. @param session Receives session. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_session_import(const AstraNetworkSessionState *state, AstraNetworkSession *session);
/** Export an endpoint. @param endpoint Endpoint. @param state Receives state. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_endpoint_export(const AstraNetworkEndpoint *endpoint, AstraNetworkEndpointState *state);
/** Import an endpoint. @param session Session. @param state State. @param endpoint Receives endpoint. @return ASTRA_NETWORK_* status. */
AstraNetworkStatus astra_network_endpoint_import(AstraNetworkSession *session, const AstraNetworkEndpointState *state, AstraNetworkEndpoint *endpoint);

#endif
