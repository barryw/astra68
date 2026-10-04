#ifndef ASTRA_SERVICE_MANAGER_ABI_H
#define ASTRA_SERVICE_MANAGER_ABI_H

/**
 * @file service_manager_abi.h
 * @brief Wire protocol between a client and the supervisor's service manager.
 *
 * One message port and one request/reply shape
 * (::AstraServiceManagerRequest / ::AstraServiceManagerReply) carry every
 * operation below: listing configured services, inspecting one, adding or
 * removing one, and controlling a service's or the system's lifecycle.
 * ::AstraServiceDefinition, a service's complete configuration, does not fit
 * inline and instead travels in a shared area attached to the ADD request or
 * the INSPECT reply.
 */

#include <astra/compiler.h>
#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/process.h>
#include <astra/syscall.h>
#include <astra/vfs_service.h>

/** Capability name published by the service manager. */
#define ASTRA_CAPABILITY_SERVICE_MANAGER "SERVICE_MANAGER"

/** `SVCM` protocol identifier carried by every service-manager message. */
#define ASTRA_SERVICE_MANAGER_PROTOCOL UINT32_C(0x5356434d) /* SVCM */
/** Current service-manager wire protocol version. */
#define ASTRA_SERVICE_MANAGER_VERSION 5u

/** Service-manager operation codes, carried in the message header's operation field. */
enum {
    /** List configured services, paging through ::AstraServiceManagerRequest.cursor. */
    ASTRA_SERVICE_MANAGER_LIST = 1u,
    /** Fetch one named service's ::AstraServiceInfo and full ::AstraServiceDefinition. */
    ASTRA_SERVICE_MANAGER_INSPECT,
    /** Add a new service, whose ::AstraServiceDefinition is attached in a shared area. */
    ASTRA_SERVICE_MANAGER_ADD,
    /** Remove a named, stopped service's configuration. */
    ASTRA_SERVICE_MANAGER_REMOVE,
    /** Start a named, stopped service. */
    ASTRA_SERVICE_MANAGER_START,
    /** Stop a named, running service. */
    ASTRA_SERVICE_MANAGER_STOP,
    /** Stop and relaunch a named service. */
    ASTRA_SERVICE_MANAGER_RESTART,
    /** Suspend a named, running service without stopping it. */
    ASTRA_SERVICE_MANAGER_PAUSE,
    /** Resume a named, paused service. */
    ASTRA_SERVICE_MANAGER_RESUME,
    /** Mark a named service to start at boot. */
    ASTRA_SERVICE_MANAGER_ENABLE,
    /** Clear a named service's start-at-boot marking. */
    ASTRA_SERVICE_MANAGER_DISABLE,
    /** Stop every service, in reverse launch order, and halt the system. */
    ASTRA_SERVICE_MANAGER_SHUTDOWN,
    /** Stop every service, in reverse launch order, and reboot the system. */
    ASTRA_SERVICE_MANAGER_SYSTEM_RESTART,
    /** Change a named service's restart policy; AstraServiceManagerRequest.value = AstraServiceRestartPolicy. */
    ASTRA_SERVICE_MANAGER_SET_RESTART,
    /** Operation carried by every ::AstraServiceManagerReply. */
    ASTRA_SERVICE_MANAGER_REPLY
};

/** Lifecycle state of a configured service, reported in ::AstraServiceInfo.state. */
typedef enum AstraServiceState {
    /** Not running, and not in any pending or failed-restart tracking. */
    ASTRA_SERVICE_STATE_STOPPED = 0u,
    /** Launched but has not yet published that it is ready. */
    ASTRA_SERVICE_STATE_STARTING,
    /** Running normally. */
    ASTRA_SERVICE_STATE_READY,
    /** Running but suspended; not currently doing work. */
    ASTRA_SERVICE_STATE_PAUSED,
    /** A stop, restart or removal has been requested and is in progress. */
    ASTRA_SERVICE_STATE_STOPPING,
    /** Exited and is not being restarted (its restart policy was exhausted or is NEVER). */
    ASTRA_SERVICE_STATE_FAILED
} AstraServiceState;

/** When a configured service is launched. */
typedef enum AstraServiceStartPolicy {
    /** Launched automatically during boot. */
    ASTRA_SERVICE_START_BOOT = 1u,
    /** Intended to launch automatically the first time something needs its capability; stored and reported, but the reference supervisor does not yet trigger such a launch. */
    ASTRA_SERVICE_START_ON_DEMAND,
    /** Launched only when explicitly requested (::ASTRA_SERVICE_MANAGER_START). */
    ASTRA_SERVICE_START_MANUAL
} AstraServiceStartPolicy;

/** Whether the supervisor relaunches a service automatically after it exits. */
typedef enum AstraServiceRestartPolicy {
    /** Never relaunched automatically, regardless of exit status. */
    ASTRA_SERVICE_RESTART_NEVER = 0u,
    /** Relaunched automatically only after a non-OK exit status; a clean exit is left stopped. */
    ASTRA_SERVICE_RESTART_ON_FAULT,
    /** Relaunched automatically after any exit, clean or not. */
    ASTRA_SERVICE_RESTART_ALWAYS
} AstraServiceRestartPolicy;

/** The service is a normal Astra (m68k, kernel-scheduled) process. */
#define ASTRA_SERVICE_RUNS_ASTRA  (1u << 0)
/** The service runs outside the Astra kernel's process model as a paired/companion process. */
#define ASTRA_SERVICE_RUNS_PAIRED (1u << 1)
/** The service should start at boot; ::ASTRA_SERVICE_MANAGER_ENABLE / DISABLE toggle this for a dynamically-added service. */
#define ASTRA_SERVICE_ENABLED     (1u << 2)
/** The service is the machine's, not the user's: it refuses user-issued stop, restart, pause or disable requests. */
#define ASTRA_SERVICE_PROTECTED   (1u << 3)
/** The service may transfer the capabilities it is granted onward to processes it launches in turn. */
#define ASTRA_SERVICE_DELEGATES   (1u << 4)
/*
 * The machine cannot work without it (storage, display): it is PROTECTED
 * and its death halts the system instead of restarting it. Like PROTECTED,
 * only the startup manifest can set it.
 */
/** The service is essential; its death halts the system instead of restarting it. */
#define ASTRA_SERVICE_CRITICAL    (1u << 5)
/** Every bit ::AstraServiceDefinition.flags may legally carry. */
#define ASTRA_SERVICE_FLAG_MASK                                           \
    (ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_RUNS_PAIRED |               \
     ASTRA_SERVICE_ENABLED | ASTRA_SERVICE_PROTECTED |                    \
     ASTRA_SERVICE_DELEGATES | ASTRA_SERVICE_CRITICAL)

/** One capability granted to a service when it is launched. */
typedef struct AstraServiceAuthority {
    /** Capability name to grant. */
    char name[ASTRA_CAPABILITY_NAME_MAX];
    /**
     * READ/WRITE rights granted into the namespace; meaningful only when #is_namespace is set.
     *
     * For a non-namespace grant (#is_namespace is 0) this must be zero: the
     * rights the service actually receives for that one capability are
     * decided by the supervisor, not carried here.
     */
    uint32_t rights;
    /** Nonzero if #name grants every capability under a namespace root rather than one specific capability. */
    uint32_t is_namespace;
} AstraServiceAuthority;

/** One capability a service publishes once it is ready. */
typedef struct AstraServicePublication {
    /** Capability name to publish. */
    char name[ASTRA_CAPABILITY_NAME_MAX];
    /** READ/WRITE rights a client receives when it looks up this published capability. */
    uint32_t rights;
} AstraServicePublication;

/*
 * Full definitions travel in a shared area. Their only collection ceilings
 * are the child startup page and the number of handles one READY message can
 * publish; configured-service count is deliberately not represented here.
 */
/** A service's complete configuration; too large for an inline message, it travels in a shared area. */
typedef struct AstraServiceDefinition {
    /** Must equal `sizeof(AstraServiceDefinition)`; a mismatch rejects the definition outright. */
    uint32_t structure_size;
    /** `ASTRA_SERVICE_*` bits (see ::ASTRA_SERVICE_FLAG_MASK). */
    uint32_t flags;
    /** ::AstraServiceStartPolicy value. */
    uint32_t start_policy;
    /** ::AstraServiceRestartPolicy value. */
    uint32_t restart_policy;
    /** The service's unique name. */
    char name[ASTRA_VFS_NAME_MAX];
    /** Path to the service's executable or application bundle. */
    char executable[ASTRA_VFS_PATH_MAX];
    /** Number of packed NUL-terminated strings in #arguments; entry 0 is #executable. */
    uint16_t argument_count;
    /** Total bytes used in #arguments. */
    uint16_t argument_length;
    /** Packed NUL-terminated argv strings, back to back; the first is #executable. */
    char arguments[ASTRA_LAUNCH_ARGUMENT_BYTES];
    /** Number of entries used in #grants. */
    uint32_t grant_count;
    /** Capabilities granted to the service when it is launched. */
    AstraServiceAuthority grants[ASTRA_LAUNCH_GRANT_MAX];
    /** Number of entries used in #publications. */
    uint32_t publication_count;
    /** Capabilities the service publishes once ready. */
    AstraServicePublication publications[ASTRA_MESSAGE_HANDLES_MAX];
    /** Number of entries used in #dependencies. */
    uint32_t dependency_count;
    /** Names of other services that must already be running before this one may be launched. */
    char dependencies[ASTRA_LAUNCH_GRANT_MAX][ASTRA_CAPABILITY_NAME_MAX];
} AstraServiceDefinition;

/** Snapshot of one configured service's identity, policy and run state. */
typedef struct AstraServiceInfo {
    /** The service's name. */
    char name[ASTRA_VFS_NAME_MAX];
    /** Path to the service's executable or application bundle. */
    char executable[ASTRA_VFS_PATH_MAX];
    /** `ASTRA_SERVICE_*` bits, mirrored from the service's ::AstraServiceDefinition. */
    uint32_t flags;
    /** ::AstraServiceState value. */
    uint32_t state;
    /** Current ::AstraServiceStartPolicy; ::ASTRA_SERVICE_MANAGER_ENABLE / DISABLE may change this at runtime. */
    uint32_t start_policy;
    /** Current ::AstraServiceRestartPolicy. */
    uint32_t restart_policy;
    /** Id of the running process, or zero if the service is not running. */
    uint32_t process_id;
    /** Reserved to distinguish successive runs of the same service; the reference supervisor always reports zero. */
    uint32_t generation;
    /** The `ASTRA_STATUS_*` code (see %status.h) the service last exited with; the reference supervisor always reports zero. */
    uint32_t exit_status;
    /** Number of other registered services currently bound to one of this service's published capabilities. */
    uint32_t client_count;
} AstraServiceInfo;

/** Which list ::AstraServiceListCursor.position is enumerating. */
typedef enum AstraServiceListSource {
    /** Walking the compiled-in boot/startup manifest. */
    ASTRA_SERVICE_LIST_SOURCE_STATIC = 0u,
    /** Walking the on-disk, dynamically-added service-definition store. */
    ASTRA_SERVICE_LIST_SOURCE_DYNAMIC,
    /** Enumeration is finished; there is nothing more to list. */
    ASTRA_SERVICE_LIST_SOURCE_DONE
} AstraServiceListSource;

/* Filesystem positions are opaque 64-bit values; do not encode state in them. */
/** Pagination cursor for ::ASTRA_SERVICE_MANAGER_LIST. */
typedef struct AstraServiceListCursor {
    /** Opaque position within whichever list #source names; carry it back unmodified. */
    uint64_t position;
    /** ::AstraServiceListSource the cursor is currently positioned in. */
    uint32_t source;
    /** Must be zero. */
    uint32_t reserved;
} AstraServiceListCursor;

/** Initializer for a cursor that starts enumeration from the beginning. */
#define ASTRA_SERVICE_LIST_CURSOR_INIT \
    { 0u, ASTRA_SERVICE_LIST_SOURCE_STATIC, 0u }

/** Request sent to the service manager; this struct's `header.operation` selects the operation. */
typedef struct AstraServiceManagerRequest {
    /** Common message header. */
    AstraMessageHeader header;
    /** Pagination cursor; meaningful only for ::ASTRA_SERVICE_MANAGER_LIST. */
    AstraServiceListCursor cursor;
    /** Target service name; empty for ::ASTRA_SERVICE_MANAGER_LIST, SHUTDOWN and SYSTEM_RESTART. */
    char name[ASTRA_VFS_NAME_MAX];
    /** The operation's argument; zero for every operation that takes none, such as an ::AstraServiceRestartPolicy for SET_RESTART. */
    uint32_t value;
    /** Must be zero. */
    uint32_t reserved;
} AstraServiceManagerRequest;

/** Reply to an ::AstraServiceManagerRequest; header.operation is always ::ASTRA_SERVICE_MANAGER_REPLY. */
typedef struct AstraServiceManagerReply {
    /** Common message header. */
    AstraMessageHeader header;
    /** Shared `ASTRA_STATUS_*` outcome code (see %status.h). */
    uint32_t status;
    /** Must be zero. */
    uint32_t reserved;
    /** For ::ASTRA_SERVICE_MANAGER_LIST, the cursor to pass back to continue enumeration. */
    AstraServiceListCursor next_cursor;
    /** The targeted service's info; meaningful when #status is `ASTRA_STATUS_OK` and the request named one service. */
    AstraServiceInfo info;
} AstraServiceManagerReply;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraServiceManagerRequest) <= ASTRA_MESSAGE_SIZE_MAX,
               "service manager request exceeds port ABI");
_Static_assert(sizeof(AstraServiceManagerReply) <= ASTRA_MESSAGE_SIZE_MAX,
               "service manager reply exceeds port ABI");
/** @endcond */

#endif
