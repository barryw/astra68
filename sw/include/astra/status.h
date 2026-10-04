#ifndef ASTRA_STATUS_H
#define ASTRA_STATUS_H

/**
 * @file status.h
 * @brief What anything on this machine says when it is asked how it went.
 *
 * One vocabulary, three ranges, and one rule underneath all of it: a status
 * names *which* failure, never how bad one is. Severity is the caller's
 * judgment -- "not found" ends a boot sequence and is routine inside a loop --
 * and it belongs on the event record, which carries the context that makes
 * severity mean anything. A status ordered by severity also freezes its own
 * numbering the first time someone writes `status > N`, and then cannot gain a
 * failure in the middle of the range without breaking that reader.
 *
 * The three ranges are: 0 (success); 1..31, the shared vocabulary every
 * program on the machine means the same thing by (storage/VFS replies,
 * service-manager and application-launch replies, and POSIX-layer
 * translations all reuse it); and 32 and above, which only the program that
 * returned the value may interpret. A fourth region, the high bit
 * (::ASTRA_STATUS_VERDICT and above), is not a status a program returns at
 * all: it is the kernel's own verdict on how a process ended.
 *
 * Plain integer literals rather than UINT32_C, because crt0 includes this from
 * assembly. Each value is written so that it is already the type it needs.
 */

/** Success. The one value that never stands for a failure. */
#define ASTRA_STATUS_OK 0

/*
 * 1..31 -- the shared vocabulary. Every program on the machine means the same
 * thing by these, which is the whole point of having them: a launcher can say
 * what went wrong without knowing anything about what it launched.
 *
 * 1..15 are the storage protocol's own numbers. They came first, they are on
 * the wire, and they are not renumbered -- the protocol uses this vocabulary
 * rather than carrying a second one that would have to be translated at every
 * boundary and would drift from it at the first disagreement.
 */
/** Malformed request, or a protocol version mismatch. */
#define ASTRA_STATUS_PROTOCOL         1  /* malformed, or a version mismatch */
/** The named object does not exist. */
#define ASTRA_STATUS_NOT_FOUND        2
/** An object already exists where this operation would have created one. */
#define ASTRA_STATUS_EXISTS           3
/** A path component that must be a directory is not one. */
#define ASTRA_STATUS_NOT_DIR          4
/** An operation that forbids a directory was given one. */
#define ASTRA_STATUS_IS_DIR           5
/** The caller lacks the rights this operation requires. */
#define ASTRA_STATUS_ACCESS           6
/** No space is left to satisfy the request. */
#define ASTRA_STATUS_NO_SPACE         7
/** The request is malformed, or its arguments do not make sense together. */
#define ASTRA_STATUS_INVALID          8
/** The handle named in the request is not valid for this operation. */
#define ASTRA_STATUS_BAD_HANDLE       9
/** The request exceeds a fixed capacity. */
#define ASTRA_STATUS_LIMIT           10
/** The underlying device or transport failed. */
#define ASTRA_STATUS_IO              11
/** A directory, or other container, must be empty for this operation and is not. */
#define ASTRA_STATUS_NOT_EMPTY       12
/** The operation is recognized but not implemented here. */
#define ASTRA_STATUS_UNSUPPORTED     13
/** The resource exists but cannot accept this operation right now; safe to retry. */
#define ASTRA_STATUS_BUSY            14
/** The caller's buffer is too small to hold the result. */
#define ASTRA_STATUS_BUFFER_TOO_SMALL 15
/*
 * The service is not there. Distinct from every failure above it, because
 * those are answers a service gave and this is the absence of one: a caller
 * can retry a BUSY and must not retry into a peer that has gone, and something
 * has to be restarted rather than asked again.
 *
 * 16, because 1..15 are on the storage wire and are not renumbered.
 */
/** The peer that would have answered is gone; restart it rather than retrying. */
#define ASTRA_STATUS_PEER_DEAD       16
/** The operation would have crossed from one storage device to another. */
#define ASTRA_STATUS_CROSS_DEVICE    17
/** Following the request would revisit something already visited, such as a symlink cycle. */
#define ASTRA_STATUS_LOOP            18
/** The operation needs to write to storage that is mounted read-only. */
#define ASTRA_STATUS_READ_ONLY       19
/* 20..31 are unassigned, and are the system's to spend. */
/** Highest value in the shared vocabulary; 20..31 are unassigned and reserved for it. */
#define ASTRA_STATUS_SYSTEM_MAX      31

/*
 * 32 and above -- the program's own. Nothing but the program that returned one
 * knows what it means, and nothing else may interpret it: a launcher reports
 * the number and stops there. A program with more than one way to fail says so
 * here rather than reaching for a shared code that nearly fits.
 */
/** First value a program may define for its own, privately-interpreted status. */
#define ASTRA_STATUS_PROGRAM_FIRST   32

/*
 * The high bit is the system's verdict on a process, and a program can never
 * produce one: astra_main returns int, so a value carrying this bit is
 * negative, and crt0 refuses to exit with it.
 *
 * This is what keeps "it failed" apart from "it never got to say". Unix loses
 * that distinction and pays for it with conventions like 127 that any program
 * may also return by accident; here a process that was killed cannot be read
 * as one that succeeded, which it could when the answer was zero.
 */
/** High bit marking a kernel verdict on how a process ended; no program's own return value can ever carry it. */
#define ASTRA_STATUS_VERDICT     0x80000000
/** Verdict: the process was killed by its own fault, such as an illegal access, and never returned a status. */
#define ASTRA_STATUS_FAULTED     0x80000001  /* killed by its own fault */
/** Verdict: the kernel refused the process's startup block. */
#define ASTRA_STATUS_NO_STARTUP  0x80000002  /* its startup block was refused */
/** Verdict: the process returned a status that already carried the verdict bit, which crt0 refuses to pass through. */
#define ASTRA_STATUS_BAD_EXIT    0x80000003  /* it returned a negative status */
/**
 * Test whether a status is a kernel verdict rather than a value the process itself returned.
 *
 * @param status Status value to test.
 * @return Nonzero if @p status carries ::ASTRA_STATUS_VERDICT.
 */
#define ASTRA_STATUS_IS_VERDICT(status) \
    (((status) & ASTRA_STATUS_VERDICT) != 0)
/* Forced process termination with the exact nonzero 1..31 reason attached. */
/** Base verdict value for forced termination by signal; the low 5 bits carry the signal number. */
#define ASTRA_STATUS_SIGNALLED_BASE 0x80000100
/**
 * Build the verdict for a process forcibly terminated by a signal.
 *
 * @param reason Signal number that terminated the process; only the low 5 bits (1..31) are kept.
 * @return The ::ASTRA_STATUS_SIGNALLED_BASE verdict carrying @p reason.
 */
#define ASTRA_STATUS_SIGNALLED(reason) \
    (ASTRA_STATUS_SIGNALLED_BASE | ((reason) & 31))
/**
 * Test whether a status is a forced-termination-by-signal verdict.
 *
 * @param status Status value to test.
 * @return Nonzero if @p status was produced by ::ASTRA_STATUS_SIGNALLED.
 */
#define ASTRA_STATUS_IS_SIGNALLED(status) \
    (((status) & 0xffffffe0) == ASTRA_STATUS_SIGNALLED_BASE)
/**
 * Extract the signal number from a forced-termination verdict.
 *
 * @param status A status for which ::ASTRA_STATUS_IS_SIGNALLED is true.
 * @return The signal number (1..31) that terminated the process.
 */
#define ASTRA_STATUS_SIGNAL_NUMBER(status) ((status) & 31)

#endif
