#ifndef ASTRA_POSIX_PROCESS_H
#define ASTRA_POSIX_PROCESS_H

/**
 * @file posix_process.h
 * @brief Wire protocol for the POSIX process-bookkeeping service.
 *
 * `posixd` is the one place that maps an Astra process handle onto a POSIX
 * pid, parent, process group, and session, and that delivers signals by
 * that bookkeeping. POSIX compatibility calls such as `getpid`, `kill`,
 * `setpgid`, `setsid`, and job-control signal delivery exchange this
 * fixed-size request/reply over the service's message port rather than
 * reaching the kernel directly.
 */

#include <astra/compiler.h>
#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/process.h>

/** Capability-table name published for the POSIX process service's port. */
#define ASTRA_CAPABILITY_POSIX_PROCESS "POSIX_PROCESS"
/** Native-big-endian `PPRC` POSIX-process protocol signature. */
#define ASTRA_POSIX_PROCESS_PROTOCOL UINT32_C(0x50505243) /* PPRC */
/** Current POSIX-process protocol wire-format version. */
#define ASTRA_POSIX_PROCESS_VERSION UINT16_C(4)

/**
 * Register a just-created process with the service.
 *
 * Sent with the new process's handle attached as a second message
 * capability. ::AstraPosixProcessRequest::process is the pid being
 * registered -- it must match that handle's own process id -- and
 * ::AstraPosixProcessRequest::flags may carry
 * ::ASTRA_POSIX_PROCESS_NEW_SESSION to start the registrant as the leader of
 * a new session and process group instead of inheriting the caller's.
 */
#define ASTRA_POSIX_PROCESS_REGISTER 1u
/**
 * Deliver a signal to one process or to a process group.
 *
 * ::AstraPosixProcessRequest::process is a pid selector interpreted the way
 * POSIX `kill()` interprets its pid argument: a positive value names one
 * process, zero means the caller's own process group, -1 means every
 * registered process, and a value less than -1 means the group `-process`.
 * ::AstraPosixProcessRequest::value carries the signal number.
 */
#define ASTRA_POSIX_PROCESS_SIGNAL   2u
/**
 * Set the process group of a process.
 *
 * ::AstraPosixProcessRequest::process is the target pid (zero means the
 * caller) and ::AstraPosixProcessRequest::value is the target group id.
 */
#define ASTRA_POSIX_PROCESS_SETPGID  3u
/**
 * Start a new session and process group with the caller as leader.
 *
 * Both request fields are unused and must be zero. The reply reports the
 * caller's new process, parent, group, and session.
 */
#define ASTRA_POSIX_PROCESS_SETSID   4u
/**
 * Look up a process table entry.
 *
 * ::AstraPosixProcessRequest::process selects the subject; zero means the
 * caller. The reply reports that process's process, parent, group, session,
 * and current session member count.
 */
#define ASTRA_POSIX_PROCESS_QUERY    5u
/**
 * Attach the caller as the controlling session of this service's terminal.
 *
 * Both request fields are unused and must be zero. Only a session leader
 * (a process whose session equals its own pid) may attach.
 */
#define ASTRA_POSIX_PROCESS_TTY_ATTACH 6u
/**
 * Read the terminal's current foreground process group.
 *
 * Both request fields are unused and must be zero. The reply's `session`
 * and `group` carry the terminal's controlling session and foreground group.
 */
#define ASTRA_POSIX_PROCESS_TTY_FOREGROUND 7u
/**
 * Set the terminal's foreground process group.
 *
 * ::AstraPosixProcessRequest::process is unused and must be zero;
 * ::AstraPosixProcessRequest::value is the group id to make foreground.
 */
#define ASTRA_POSIX_PROCESS_TTY_SET_FOREGROUND 8u
/**
 * Deliver a signal to the terminal's current foreground process group.
 *
 * ::AstraPosixProcessRequest::process is unused and must be zero;
 * ::AstraPosixProcessRequest::value carries the signal number.
 */
#define ASTRA_POSIX_PROCESS_TTY_SIGNAL 9u
/**
 * Deliver a signal to one process group, scoped to the caller's own session.
 *
 * ::AstraPosixProcessRequest::process is the target group id (must be
 * positive) and ::AstraPosixProcessRequest::value carries the signal number.
 */
#define ASTRA_POSIX_PROCESS_SIGNAL_GROUP 10u

/** Alias for ::ASTRA_SIGNAL_INTERRUPT, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_INTERRUPT ASTRA_SIGNAL_INTERRUPT
/** Alias for ::ASTRA_SIGNAL_QUIT, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_QUIT      ASTRA_SIGNAL_QUIT
/** Alias for ::ASTRA_SIGNAL_KILL, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_KILL      ASTRA_SIGNAL_KILL
/** Alias for ::ASTRA_SIGNAL_CHILD, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_CHILD     ASTRA_SIGNAL_CHILD
/** Alias for ::ASTRA_SIGNAL_CONTINUE, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_CONTINUE  ASTRA_SIGNAL_CONTINUE
/** Alias for ::ASTRA_SIGNAL_STOP, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_STOP      ASTRA_SIGNAL_STOP
/** Alias for ::ASTRA_SIGNAL_TTY_STOP, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_TTY_STOP  ASTRA_SIGNAL_TTY_STOP
/** Alias for ::ASTRA_SIGNAL_WINDOW, as a POSIX-process signal value. */
#define ASTRA_POSIX_SIGNAL_WINDOW    ASTRA_SIGNAL_WINDOW

/**
 * ::ASTRA_POSIX_PROCESS_REGISTER flag: start the registrant as the leader
 * of a new session and process group, rather than inheriting the caller's.
 */
#define ASTRA_POSIX_PROCESS_NEW_SESSION (1u << 0)
/** Every valid ::ASTRA_POSIX_PROCESS_REGISTER flag. */
#define ASTRA_POSIX_PROCESS_FLAG_MASK ASTRA_POSIX_PROCESS_NEW_SESSION

/**
 * One POSIX-process service request.
 *
 * The same fixed record covers every operation in
 * ::ASTRA_POSIX_PROCESS_REGISTER through ::ASTRA_POSIX_PROCESS_SIGNAL_GROUP;
 * see each operation's own documentation for how `process`, `value`, and
 * `flags` are used.
 */
typedef struct AstraPosixProcessRequest {
    /** Message envelope: protocol, version, operation, and transaction id. */
    AstraMessageHeader header;
    /** Operation-specific pid, group, or selector; see the operation macro. */
    int32_t process;
    /** Operation-specific secondary argument, typically a signal number. */
    int32_t value;
    /** Operation-specific flags; zero except where the operation defines one. */
    uint32_t flags;
    /** Must be zero. */
    uint32_t reserved;
} AstraPosixProcessRequest;

/**
 * The reply to one ::AstraPosixProcessRequest.
 *
 * `process`, `parent`, `group`, `session`, and `session_members` are
 * populated only by ::ASTRA_POSIX_PROCESS_QUERY, ::ASTRA_POSIX_PROCESS_SETSID,
 * and ::ASTRA_POSIX_PROCESS_TTY_FOREGROUND; every other successful operation
 * leaves them zero.
 */
typedef struct AstraPosixProcessReply {
    /** Message envelope, echoing the request's operation and transaction id. */
    AstraMessageHeader header;
    /** An ASTRA_STATUS_* result code (see astra/%status.h). */
    uint32_t status;
    /** The subject process's pid. */
    int32_t process;
    /** The subject process's parent pid. */
    int32_t parent;
    /** The subject process's group id, or the terminal's foreground group. */
    int32_t group;
    /** The subject process's session id, or the terminal's controlling session. */
    int32_t session;
    /** Current member count of `session`. */
    uint32_t session_members;
} AstraPosixProcessReply;

/** Encoded byte size of ::AstraPosixProcessRequest. */
#define ASTRA_POSIX_PROCESS_REQUEST_SIZE 40u
/** Encoded byte size of ::AstraPosixProcessReply. */
#define ASTRA_POSIX_PROCESS_REPLY_SIZE 48u

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraPosixProcessRequest) ==
                   ASTRA_POSIX_PROCESS_REQUEST_SIZE,
               "POSIX process request ABI changed");
_Static_assert(sizeof(AstraPosixProcessReply) ==
                   ASTRA_POSIX_PROCESS_REPLY_SIZE,
               "POSIX process reply ABI changed");
/** @endcond */

#endif
