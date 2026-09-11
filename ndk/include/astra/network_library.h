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
/** Network Kit export-table ABI major version. */
#define ASTRA_NETWORK_LIBRARY_ABI_MAJOR 1u
/** Network Kit export-table ABI minor version. */
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

/** Network Kit 1.x immutable export table. */
typedef struct AstraNetworkLibraryV1 {
    uint16_t abi_major; /**< ASTRA_NETWORK_LIBRARY_ABI_MAJOR. */
    uint16_t abi_minor; /**< ASTRA_NETWORK_LIBRARY_ABI_MINOR. */
    uint32_t structure_size; /**< Bytes available in this table. */

    /** Open a session using a network-service factory capability. */
    AstraNetworkStatus (*session_open)(AstraHandle, AstraNetworkSession *);
    /** Close a session and all session-owned transport resources. */
    AstraNetworkStatus (*session_close)(AstraNetworkSession *);
    /** Create an endpoint in a session. */
    AstraNetworkStatus (*endpoint_open)(AstraNetworkSession *, uint16_t,
                                         uint8_t, uint8_t,
                                         AstraNetworkEndpoint *);
    /** Close an endpoint. */
    AstraNetworkStatus (*endpoint_close)(AstraNetworkEndpoint *);
    /** Bind an endpoint to a local address. */
    AstraNetworkStatus (*bind)(AstraNetworkEndpoint *,
                                const AstraNetworkAddress *);
    /** Connect an endpoint to a peer. */
    AstraNetworkStatus (*connect)(AstraNetworkEndpoint *,
                                   const AstraNetworkAddress *);
    /** Begin listening for incoming connections. */
    AstraNetworkStatus (*listen)(AstraNetworkEndpoint *, uint32_t);
    /** Accept one incoming connection. */
    AstraNetworkStatus (*accept)(AstraNetworkEndpoint *,
                                  AstraNetworkEndpoint *,
                                  AstraNetworkAddress *);
    /** Send bytes on a connected endpoint. */
    AstraNetworkStatus (*send)(AstraNetworkEndpoint *, const void *, size_t,
                                uint32_t, size_t *);
    /** Send a datagram to an explicit address. */
    AstraNetworkStatus (*send_to)(AstraNetworkEndpoint *, const void *,
                                   size_t, uint32_t,
                                   const AstraNetworkAddress *, size_t *);
    /** Receive bytes from a connected endpoint. */
    AstraNetworkStatus (*receive)(AstraNetworkEndpoint *, void *, size_t,
                                   uint32_t, size_t *);
    /** Receive a datagram and its source address. */
    AstraNetworkStatus (*receive_from)(AstraNetworkEndpoint *, void *, size_t,
                                        uint32_t, AstraNetworkAddress *,
                                        size_t *);
    /** Shut down endpoint directions selected by flags. */
    AstraNetworkStatus (*shutdown)(AstraNetworkEndpoint *, uint32_t);
    /** Read the bound local address. */
    AstraNetworkStatus (*local_address)(AstraNetworkEndpoint *,
                                         AstraNetworkAddress *);
    /** Read the connected peer address. */
    AstraNetworkStatus (*peer_address)(AstraNetworkEndpoint *,
                                        AstraNetworkAddress *);
    /** Read an endpoint option. */
    AstraNetworkStatus (*get_option)(AstraNetworkEndpoint *, uint32_t,
                                      uint32_t *);
    /** Set an endpoint option. */
    AstraNetworkStatus (*set_option)(AstraNetworkEndpoint *, uint32_t,
                                      uint32_t);
    /** Start asynchronous hostname resolution. */
    AstraNetworkStatus (*resolve_start)(AstraNetworkSession *, const char *,
                                         uint32_t, uint16_t, uint8_t, uint8_t,
                                         AstraNetworkRequest *);
    /** Poll an asynchronous request without waiting. */
    AstraNetworkStatus (*request_try)(AstraNetworkRequest *,
                                       AstraNetworkAddress *, uint32_t,
                                       uint32_t *);
    /** Wait for an asynchronous request until an absolute deadline. */
    AstraNetworkStatus (*request_wait)(AstraNetworkRequest *,
                                        AstraNetworkAddress *, uint32_t,
                                        uint32_t *, uint64_t);
    /** Cancel and release an asynchronous request. */
    AstraNetworkStatus (*request_cancel)(AstraNetworkRequest *);
    /** Borrow the endpoint readiness wait handle. */
    AstraHandle (*readiness_handle)(const AstraNetworkEndpoint *);
    /** Read current endpoint readiness flags. */
    uint32_t (*readiness)(const AstraNetworkEndpoint *);
    /** Export session capabilities for atomic exec. */
    AstraNetworkStatus (*session_export)(const AstraNetworkSession *,
                                         AstraNetworkSessionState *);
    /** Import session capabilities after atomic exec. */
    AstraNetworkStatus (*session_import)(const AstraNetworkSessionState *,
                                         AstraNetworkSession *);
    /** Export endpoint capabilities for atomic exec. */
    AstraNetworkStatus (*endpoint_export)(const AstraNetworkEndpoint *,
                                          AstraNetworkEndpointState *);
    /** Import endpoint capabilities after atomic exec. */
    AstraNetworkStatus (*endpoint_import)(AstraNetworkSession *,
                                          const AstraNetworkEndpointState *,
                                          AstraNetworkEndpoint *);
} AstraNetworkLibraryV1;

#endif
