#ifndef ASTRA_STATUS_H
#define ASTRA_STATUS_H

/*
 * What anything on this machine says when it is asked how it went.
 *
 * One vocabulary, three ranges, and one rule underneath all of it: a status
 * names *which* failure, never how bad one is. Severity is the caller's
 * judgment -- "not found" ends a boot sequence and is routine inside a loop --
 * and it belongs on the event record, which carries the context that makes
 * severity mean anything. A status ordered by severity also freezes its own
 * numbering the first time someone writes `status > N`, and then cannot gain a
 * failure in the middle of the range without breaking that reader.
 *
 * Plain integer literals rather than UINT32_C, because crt0 includes this from
 * assembly. Each value is written so that it is already the type it needs.
 */

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
#define ASTRA_STATUS_PROTOCOL         1  /* malformed, or a version mismatch */
#define ASTRA_STATUS_NOT_FOUND        2
#define ASTRA_STATUS_EXISTS           3
#define ASTRA_STATUS_NOT_DIR          4
#define ASTRA_STATUS_IS_DIR           5
#define ASTRA_STATUS_ACCESS           6
#define ASTRA_STATUS_NO_SPACE         7
#define ASTRA_STATUS_INVALID          8
#define ASTRA_STATUS_BAD_HANDLE       9
#define ASTRA_STATUS_LIMIT           10
#define ASTRA_STATUS_IO              11
#define ASTRA_STATUS_NOT_EMPTY       12
#define ASTRA_STATUS_UNSUPPORTED     13
#define ASTRA_STATUS_BUSY            14
#define ASTRA_STATUS_BUFFER_TOO_SMALL 15
/*
 * The service is not there. Distinct from every failure above it, because
 * those are answers a service gave and this is the absence of one: a caller
 * can retry a BUSY and must not retry into a peer that has gone, and something
 * has to be restarted rather than asked again.
 *
 * 16, because 1..15 are on the storage wire and are not renumbered.
 */
#define ASTRA_STATUS_PEER_DEAD       16
#define ASTRA_STATUS_CROSS_DEVICE    17
#define ASTRA_STATUS_LOOP            18
/* 19..31 are unassigned, and are the system's to spend. */
#define ASTRA_STATUS_SYSTEM_MAX      31

/*
 * 32 and above -- the program's own. Nothing but the program that returned one
 * knows what it means, and nothing else may interpret it: a launcher reports
 * the number and stops there. A program with more than one way to fail says so
 * here rather than reaching for a shared code that nearly fits.
 */
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
#define ASTRA_STATUS_VERDICT     0x80000000
#define ASTRA_STATUS_FAULTED     0x80000001  /* killed by its own fault */
#define ASTRA_STATUS_NO_STARTUP  0x80000002  /* its startup block was refused */
#define ASTRA_STATUS_BAD_EXIT    0x80000003  /* it returned a negative status */

#endif
