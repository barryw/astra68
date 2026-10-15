#ifndef ASTRA_APPLICATION_SERVICE_H
#define ASTRA_APPLICATION_SERVICE_H

/**
 * @file application_service.h
 * @brief Wire protocol for asking the application-launch service to start an installed application bundle.
 *
 * The supervisor's application-launch service receives this request on the
 * ::ASTRA_CAPABILITY_APPLICATION_LAUNCH port, resolves the bundle path to an
 * executable image, and launches it. The reply carries a shared status code
 * and the new process's id; on success it also carries one handle, a
 * waitable handle to the new process.
 */

#include <astra/compiler.h>
#include <stdint.h>

#include <astra/process.h>
#include <astra/syscall.h>
#include <astra/vfs_service.h>

/** Capability name published by the application-launch service. */
#define ASTRA_CAPABILITY_APPLICATION_LAUNCH "APP_LAUNCH"

/** `APPL` protocol identifier carried by every application-launch message. */
#define ASTRA_APPLICATION_PROTOCOL UINT32_C(0x4150504c) /* APPL */
/** Current application-launch wire protocol version. */
#define ASTRA_APPLICATION_VERSION 5u

/** Request operation: launch the bundle named in the request. */
#define ASTRA_APPLICATION_LAUNCH 1u
/** Reply operation: the launch request has been answered. */
#define ASTRA_APPLICATION_LAUNCHED 2u

/**
 * Bytes available for packed argv strings in one launch request.
 *
 * Packed strings travel in one transferred readonly shared area. The native
 * kernel validates the combined argument/environment string budget and
 * publishes aligned pointers and strings in its owned startup payload.
 * Capability records occupy their separate startup page.
 */
#define ASTRA_APPLICATION_ARGUMENT_BYTES ASTRA_LAUNCH_ARGUMENT_BYTES
/** Maximum bundle path length, matching the filesystem's own path limit. */
#define ASTRA_APPLICATION_PATH_MAX ASTRA_VFS_PATH_MAX
/**
 * Conservative upper bound on packed argv entries, including the bundle path.
 *
 * This only bounds admission before packing starts; the caller still fails
 * with `ASTRA_ERROR_NO_RESOURCES` if the actual strings do not fit
 * ::ASTRA_APPLICATION_ARGUMENT_BYTES.
 */
#define ASTRA_APPLICATION_ARGUMENT_MAX ASTRA_LAUNCH_ARGUMENT_MAX

/** Control metadata for packed argv in the transferred shared area. */
typedef struct AstraApplicationLaunchArguments {
    /** Number of packed NUL-terminated strings; entry 0 is the bundle path. */
    uint16_t count;
    /** Exact bytes used in the transferred area. */
    uint16_t length;
    /** ::AstraLaunchSource of the request, narrowed to 16 bits; the service accepts only SHELL or DESKTOP. */
    uint16_t source;
    /** Reserved launch flags; must be zero -- the service refuses a nonzero value. */
    uint16_t flags;
    /** Reserved; both words must be zero. */
    uint32_t reserved[2];
} AstraApplicationLaunchArguments;

/** Request to launch an application bundle. Handle0 is the owned READ|MAP
 * payload area; handle1 is the reply-send capability inserted by port_call. */
typedef struct AstraApplicationLaunchRequest {
    /** Common message header; operation is ::ASTRA_APPLICATION_LAUNCH. */
    AstraMessageHeader header;
    /** Bundle path and argv to launch it with. */
    AstraApplicationLaunchArguments arguments;
} AstraApplicationLaunchRequest;

/** Reply to an ::AstraApplicationLaunchRequest. */
typedef struct AstraApplicationLaunchReply {
    /** Common message header; operation is ::ASTRA_APPLICATION_LAUNCHED. */
    AstraMessageHeader header;
    /** Shared `ASTRA_STATUS_*` outcome code (see %status.h). */
    uint32_t status;
    /** The launched process's id; valid only when #status is `ASTRA_STATUS_OK`. */
    uint32_t process_id;
} AstraApplicationLaunchReply;

/** Fixed wire size of ::AstraApplicationLaunchRequest. */
#define ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE 40u
/** Fixed wire size of ::AstraApplicationLaunchReply. */
#define ASTRA_APPLICATION_LAUNCH_REPLY_SIZE 32u

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraApplicationLaunchRequest) ==
                   ASTRA_APPLICATION_LAUNCH_REQUEST_SIZE,
               "application launch request ABI changed");
_Static_assert(sizeof(AstraApplicationLaunchReply) ==
                   ASTRA_APPLICATION_LAUNCH_REPLY_SIZE,
               "application launch reply ABI changed");
/** @endcond */

#endif
