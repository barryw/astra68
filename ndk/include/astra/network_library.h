#ifndef ASTRA_NETWORK_LIBRARY_H
#define ASTRA_NETWORK_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#include <astra/network.h>
#include <astra/resource.h>
#include <astra/types.h>

#define ASTRA_NETWORK_LIBRARY_NAME "network.library"
#define ASTRA_NETWORK_LIBRARY_ABI_MAJOR 1u
#define ASTRA_NETWORK_LIBRARY_ABI_MINOR 1u

typedef struct AstraNetworkSession {
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
} AstraNetworkSession;

typedef struct AstraNetworkEndpoint {
    AstraNetworkSession *_private_session;
    AstraHandle _private_control;
    AstraHandle _private_readiness;
    uint32_t _private_id;
    uint32_t _private_generation;
    uint16_t _private_family;
    uint8_t _private_type;
    uint8_t _private_protocol;
} AstraNetworkEndpoint;

typedef struct AstraNetworkRequest {
    AstraNetworkSession *_private_session;
    uint32_t _private_token;
    uint32_t _private_transaction;
    uint32_t _private_generation;
    uint32_t _private_state;
} AstraNetworkRequest;

/* Capability-only state used by language runtimes across atomic exec. */
typedef struct AstraNetworkSessionState {
    uint32_t size;
    AstraHandle control;
    AstraHandle area;
    AstraHandle lock;
    AstraHandle reply;
    AstraHandle notify;
    uint32_t shared_size;
    uint32_t id;
    uint32_t generation;
    uint32_t transaction;
} AstraNetworkSessionState;

typedef struct AstraNetworkEndpointState {
    uint32_t size;
    AstraHandle control;
    AstraHandle readiness;
    uint32_t id;
    uint32_t generation;
    uint16_t family;
    uint8_t type;
    uint8_t protocol;
} AstraNetworkEndpointState;

#define ASTRA_NETWORK_SESSION_STATE_SIZE 40u
#define ASTRA_NETWORK_ENDPOINT_STATE_SIZE 24u

_Static_assert(sizeof(AstraNetworkSessionState) ==
                   ASTRA_NETWORK_SESSION_STATE_SIZE,
               "network session state changed");
_Static_assert(sizeof(AstraNetworkEndpointState) ==
                   ASTRA_NETWORK_ENDPOINT_STATE_SIZE,
               "network endpoint state changed");

#define ASTRA_NETWORK_SESSION_INIT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
#define ASTRA_NETWORK_ENDPOINT_INIT { 0, 0, 0, 0, 0, 0, 0, 0 }
#define ASTRA_NETWORK_REQUEST_INIT { 0, 0, 0, 0, 0 }

typedef struct AstraNetworkLibraryV1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t structure_size;

    AstraNetworkStatus (*session_open)(AstraHandle, AstraNetworkSession *);
    AstraNetworkStatus (*session_close)(AstraNetworkSession *);
    AstraNetworkStatus (*endpoint_open)(AstraNetworkSession *, uint16_t,
                                         uint8_t, uint8_t,
                                         AstraNetworkEndpoint *);
    AstraNetworkStatus (*endpoint_close)(AstraNetworkEndpoint *);
    AstraNetworkStatus (*bind)(AstraNetworkEndpoint *,
                                const AstraNetworkAddress *);
    AstraNetworkStatus (*connect)(AstraNetworkEndpoint *,
                                   const AstraNetworkAddress *);
    AstraNetworkStatus (*listen)(AstraNetworkEndpoint *, uint32_t);
    AstraNetworkStatus (*accept)(AstraNetworkEndpoint *,
                                  AstraNetworkEndpoint *,
                                  AstraNetworkAddress *);
    AstraNetworkStatus (*send)(AstraNetworkEndpoint *, const void *, size_t,
                                uint32_t, size_t *);
    AstraNetworkStatus (*send_to)(AstraNetworkEndpoint *, const void *,
                                   size_t, uint32_t,
                                   const AstraNetworkAddress *, size_t *);
    AstraNetworkStatus (*receive)(AstraNetworkEndpoint *, void *, size_t,
                                   uint32_t, size_t *);
    AstraNetworkStatus (*receive_from)(AstraNetworkEndpoint *, void *, size_t,
                                        uint32_t, AstraNetworkAddress *,
                                        size_t *);
    AstraNetworkStatus (*shutdown)(AstraNetworkEndpoint *, uint32_t);
    AstraNetworkStatus (*local_address)(AstraNetworkEndpoint *,
                                         AstraNetworkAddress *);
    AstraNetworkStatus (*peer_address)(AstraNetworkEndpoint *,
                                        AstraNetworkAddress *);
    AstraNetworkStatus (*get_option)(AstraNetworkEndpoint *, uint32_t,
                                      uint32_t *);
    AstraNetworkStatus (*set_option)(AstraNetworkEndpoint *, uint32_t,
                                      uint32_t);
    AstraNetworkStatus (*resolve_start)(AstraNetworkSession *, const char *,
                                         uint32_t, uint16_t, uint8_t, uint8_t,
                                         AstraNetworkRequest *);
    AstraNetworkStatus (*request_try)(AstraNetworkRequest *,
                                       AstraNetworkAddress *, uint32_t,
                                       uint32_t *);
    AstraNetworkStatus (*request_wait)(AstraNetworkRequest *,
                                        AstraNetworkAddress *, uint32_t,
                                        uint32_t *, uint64_t);
    AstraNetworkStatus (*request_cancel)(AstraNetworkRequest *);
    AstraHandle (*readiness_handle)(const AstraNetworkEndpoint *);
    uint32_t (*readiness)(const AstraNetworkEndpoint *);
    AstraNetworkStatus (*session_export)(const AstraNetworkSession *,
                                         AstraNetworkSessionState *);
    AstraNetworkStatus (*session_import)(const AstraNetworkSessionState *,
                                         AstraNetworkSession *);
    AstraNetworkStatus (*endpoint_export)(const AstraNetworkEndpoint *,
                                          AstraNetworkEndpointState *);
    AstraNetworkStatus (*endpoint_import)(AstraNetworkSession *,
                                          const AstraNetworkEndpointState *,
                                          AstraNetworkEndpoint *);
} AstraNetworkLibraryV1;

#endif
