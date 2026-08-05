#ifndef ASTRA_SUPERVISOR_H
#define ASTRA_SUPERVISOR_H

/*
 * The exit status of the firmware-supplied supervisor image.
 *
 * The kernel cannot see inside a user process, so the supervisor reports what
 * it checked through the only channel every process has: its exit status. The
 * tag proves the code ran at all — a process that never reached user mode
 * exits zero — and the low halfword names the checks that failed. The
 * halfword is deliberate: a byte ran out of room once the block checks
 * arrived, and overflowing bits corrupted the tag rather than the result.
 */
#define ASTRA_SUPERVISOR_STATUS_TAG   0x53560000u /* "SV" + result halfword */
#define ASTRA_SUPERVISOR_STATUS_MASK  0xffff0000u
#define ASTRA_SUPERVISOR_STATUS_OK    ASTRA_SUPERVISOR_STATUS_TAG

/*
 * Stages the initial image reports through ASTRA_SYSCALL_PROGRESS as it comes
 * up. The counter is monotonic, so each stage is strictly greater than the one
 * before it, and the kernel prints the boot line for each.
 */
#define ASTRA_SUPERVISOR_STAGE_SELF_VERIFIED  1u
#define ASTRA_SUPERVISOR_STAGE_BLOCK_LEASED   2u
#define ASTRA_SUPERVISOR_STAGE_BLOCK_ONLINE   3u
#define ASTRA_SUPERVISOR_STAGE_BLOCK_VERIFIED 4u
#define ASTRA_SUPERVISOR_STAGE_VOLUME_FOUND   5u
#define ASTRA_SUPERVISOR_STAGE_VOLUME_MOUNTED 6u
#define ASTRA_SUPERVISOR_STAGE_VOLUME_VERIFIED 7u
#define ASTRA_SUPERVISOR_STAGE_MAX            7u

#define ASTRA_SUPERVISOR_FAIL_STARTUP      (1u << 0)
#define ASTRA_SUPERVISOR_FAIL_QUERY_ABI    (1u << 1)
#define ASTRA_SUPERVISOR_FAIL_ABI_VERSION  (1u << 2)
#define ASTRA_SUPERVISOR_FAIL_SELF_HANDLES (1u << 3)
#define ASTRA_SUPERVISOR_FAIL_CAPABILITIES (1u << 4)
#define ASTRA_SUPERVISOR_FAIL_PROCESS_INFO (1u << 5)
#define ASTRA_SUPERVISOR_FAIL_INFO_CONTENT (1u << 6)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_LEASE  (1u << 7)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_GEOMETRY (1u << 8)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_MEMORY (1u << 9)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_IO     (1u << 10)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_WAIT   (1u << 11)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_TIMEOUT (1u << 12)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_ARM    (1u << 13)
#define ASTRA_SUPERVISOR_FAIL_BLOCK_DRAIN  (1u << 14)
/*
 * The filesystem check gets one aggregate bit because bit 15 is the last the
 * halfword has. Which stage it reached is in the progress counter instead, and
 * that is the better channel anyway: the boot log then shows whether the
 * partition was found, the volume mounted, or the read-back failed, rather than
 * one bit meaning "something about ext4".
 */
#define ASTRA_SUPERVISOR_FAIL_VOLUME       (1u << 15)

#endif
