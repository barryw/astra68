#ifndef ASTRA_SUPERVISOR_H
#define ASTRA_SUPERVISOR_H

/*
 * The exit status of the firmware-supplied supervisor image.
 *
 * The kernel cannot see inside a user process, so the supervisor reports what
 * it checked through the only channel every process has: its exit status. The
 * tag proves the code ran at all — a process that never reached user mode
 * exits zero — and the low byte names the check that failed.
 */
#define ASTRA_SUPERVISOR_STATUS_TAG   0x53565200u /* "SVR" + result byte */
#define ASTRA_SUPERVISOR_STATUS_MASK  0xffffff00u
#define ASTRA_SUPERVISOR_STATUS_OK    ASTRA_SUPERVISOR_STATUS_TAG

#define ASTRA_SUPERVISOR_FAIL_STARTUP      (1u << 0)
#define ASTRA_SUPERVISOR_FAIL_QUERY_ABI    (1u << 1)
#define ASTRA_SUPERVISOR_FAIL_ABI_VERSION  (1u << 2)
#define ASTRA_SUPERVISOR_FAIL_SELF_HANDLES (1u << 3)
#define ASTRA_SUPERVISOR_FAIL_CAPABILITIES (1u << 4)
#define ASTRA_SUPERVISOR_FAIL_PROCESS_INFO (1u << 5)
#define ASTRA_SUPERVISOR_FAIL_INFO_CONTENT (1u << 6)

#endif
