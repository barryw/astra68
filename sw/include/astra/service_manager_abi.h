#ifndef ASTRA_SERVICE_MANAGER_ABI_H
#define ASTRA_SERVICE_MANAGER_ABI_H

#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/process.h>
#include <astra/syscall.h>
#include <astra/vfs_service.h>

#define ASTRA_CAPABILITY_SERVICE_MANAGER "SERVICE_MANAGER"

#define ASTRA_SERVICE_MANAGER_PROTOCOL UINT32_C(0x5356434d) /* SVCM */
#define ASTRA_SERVICE_MANAGER_VERSION 2u

enum {
    ASTRA_SERVICE_MANAGER_LIST = 1u,
    ASTRA_SERVICE_MANAGER_INSPECT,
    ASTRA_SERVICE_MANAGER_ADD,
    ASTRA_SERVICE_MANAGER_REMOVE,
    ASTRA_SERVICE_MANAGER_START,
    ASTRA_SERVICE_MANAGER_STOP,
    ASTRA_SERVICE_MANAGER_RESTART,
    ASTRA_SERVICE_MANAGER_PAUSE,
    ASTRA_SERVICE_MANAGER_RESUME,
    ASTRA_SERVICE_MANAGER_ENABLE,
    ASTRA_SERVICE_MANAGER_DISABLE,
    ASTRA_SERVICE_MANAGER_REPLY
};

typedef enum AstraServiceState {
    ASTRA_SERVICE_STATE_STOPPED = 0u,
    ASTRA_SERVICE_STATE_STARTING,
    ASTRA_SERVICE_STATE_READY,
    ASTRA_SERVICE_STATE_PAUSED,
    ASTRA_SERVICE_STATE_STOPPING,
    ASTRA_SERVICE_STATE_FAILED
} AstraServiceState;

typedef enum AstraServiceStartPolicy {
    ASTRA_SERVICE_START_BOOT = 1u,
    ASTRA_SERVICE_START_ON_DEMAND,
    ASTRA_SERVICE_START_MANUAL
} AstraServiceStartPolicy;

typedef enum AstraServiceRestartPolicy {
    ASTRA_SERVICE_RESTART_NEVER = 0u,
    ASTRA_SERVICE_RESTART_ON_FAULT,
    ASTRA_SERVICE_RESTART_ALWAYS
} AstraServiceRestartPolicy;

#define ASTRA_SERVICE_RUNS_ASTRA  (1u << 0)
#define ASTRA_SERVICE_RUNS_PAIRED (1u << 1)
#define ASTRA_SERVICE_ENABLED     (1u << 2)
#define ASTRA_SERVICE_PROTECTED   (1u << 3)
#define ASTRA_SERVICE_DELEGATES   (1u << 4)
#define ASTRA_SERVICE_FLAG_MASK                                           \
    (ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_RUNS_PAIRED |               \
     ASTRA_SERVICE_ENABLED | ASTRA_SERVICE_PROTECTED |                    \
     ASTRA_SERVICE_DELEGATES)

typedef struct AstraServiceAuthority {
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t rights;
    uint32_t is_namespace;
} AstraServiceAuthority;

typedef struct AstraServicePublication {
    char name[ASTRA_CAPABILITY_NAME_MAX];
    uint32_t rights;
} AstraServicePublication;

/*
 * Full definitions travel in a shared area. Their only collection ceilings
 * are the child startup page and the number of handles one READY message can
 * publish; configured-service count is deliberately not represented here.
 */
typedef struct AstraServiceDefinition {
    uint32_t structure_size;
    uint32_t flags;
    uint32_t start_policy;
    uint32_t restart_policy;
    char name[ASTRA_VFS_NAME_MAX];
    char executable[ASTRA_VFS_PATH_MAX];
    uint16_t argument_count;
    uint16_t argument_length;
    char arguments[ASTRA_LAUNCH_ARGUMENT_BYTES];
    uint32_t grant_count;
    AstraServiceAuthority grants[ASTRA_LAUNCH_GRANT_MAX];
    uint32_t publication_count;
    AstraServicePublication publications[ASTRA_MESSAGE_HANDLES_MAX];
    uint32_t dependency_count;
    char dependencies[ASTRA_LAUNCH_GRANT_MAX][ASTRA_CAPABILITY_NAME_MAX];
} AstraServiceDefinition;

typedef struct AstraServiceInfo {
    char name[ASTRA_VFS_NAME_MAX];
    char executable[ASTRA_VFS_PATH_MAX];
    uint32_t flags;
    uint32_t state;
    uint32_t start_policy;
    uint32_t restart_policy;
    uint32_t process_id;
    uint32_t generation;
    uint32_t exit_status;
    uint32_t client_count;
} AstraServiceInfo;

typedef enum AstraServiceListSource {
    ASTRA_SERVICE_LIST_SOURCE_STATIC = 0u,
    ASTRA_SERVICE_LIST_SOURCE_DYNAMIC,
    ASTRA_SERVICE_LIST_SOURCE_DONE
} AstraServiceListSource;

/* Filesystem positions are opaque 64-bit values; do not encode state in them. */
typedef struct AstraServiceListCursor {
    uint64_t position;
    uint32_t source;
    uint32_t reserved;
} AstraServiceListCursor;

#define ASTRA_SERVICE_LIST_CURSOR_INIT \
    { 0u, ASTRA_SERVICE_LIST_SOURCE_STATIC, 0u }

typedef struct AstraServiceManagerRequest {
    AstraMessageHeader header;
    AstraServiceListCursor cursor;
    char name[ASTRA_VFS_NAME_MAX];
} AstraServiceManagerRequest;

typedef struct AstraServiceManagerReply {
    AstraMessageHeader header;
    uint32_t status;
    uint32_t reserved;
    AstraServiceListCursor next_cursor;
    AstraServiceInfo info;
} AstraServiceManagerReply;

_Static_assert(sizeof(AstraServiceManagerRequest) <= ASTRA_MESSAGE_SIZE_MAX,
               "service manager request exceeds port ABI");
_Static_assert(sizeof(AstraServiceManagerReply) <= ASTRA_MESSAGE_SIZE_MAX,
               "service manager reply exceeds port ABI");

#endif
