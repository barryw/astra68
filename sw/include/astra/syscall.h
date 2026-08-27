#ifndef ASTRA_SYSCALL_H
#define ASTRA_SYSCALL_H

#include <astra/message_abi.h>
#include <astra/limits.h>

#define ASTRA_SYSCALL_TRAP 15
#define ASTRA_SYSCALL_VECTOR 47
#define ASTRA_SYSCALL_ABI_VERSION 0x00010024

#define ASTRA_SYSCALL_QUERY_ABI 0
#define ASTRA_SYSCALL_PROGRESS  1
#define ASTRA_SYSCALL_YIELD     2
#define ASTRA_SYSCALL_EXIT      3
#define ASTRA_SYSCALL_CLOSE     4
#define ASTRA_SYSCALL_CLOCK_MONOTONIC  5
#define ASTRA_SYSCALL_EVENT_CREATE     6
#define ASTRA_SYSCALL_SEMAPHORE_CREATE 7
#define ASTRA_SYSCALL_WAIT_ONE         8
#define ASTRA_SYSCALL_SIGNAL           9
#define ASTRA_SYSCALL_EVENT_RESET      10
#define ASTRA_SYSCALL_CANCEL_WAIT      11
#define ASTRA_SYSCALL_THREAD_CREATE    12
#define ASTRA_SYSCALL_THREAD_EXIT      13
#define ASTRA_SYSCALL_WAIT_MULTIPLE    14
#define ASTRA_SYSCALL_TIMER_CREATE     15
#define ASTRA_SYSCALL_TIMER_SET        16
#define ASTRA_SYSCALL_TIMER_CANCEL     17
#define ASTRA_SYSCALL_PORT_CREATE      18
#define ASTRA_SYSCALL_PORT_SEND_TRY    19
#define ASTRA_SYSCALL_PORT_RECEIVE_TRY 20
#define ASTRA_SYSCALL_HANDLE_DUPLICATE 21
#define ASTRA_SYSCALL_AREA_CREATE      22
#define ASTRA_SYSCALL_AREA_MAP         23
#define ASTRA_SYSCALL_AREA_UNMAP       24
#define ASTRA_SYSCALL_RING_CREATE      25
#define ASTRA_SYSCALL_RING_NOTIFY      26
#define ASTRA_SYSCALL_IRQ_READ         27
#define ASTRA_SYSCALL_IRQ_ACK          28
#define ASTRA_SYSCALL_IRQ_ARM          29
#define ASTRA_SYSCALL_IRQ_MASK         30
#define ASTRA_SYSCALL_IRQ_RECOVER      31
#define ASTRA_SYSCALL_IRQ_REVOKE       32
#define ASTRA_SYSCALL_DEVICE_QUERY     33
#define ASTRA_SYSCALL_DEVICE_RESET     34
#define ASTRA_SYSCALL_DEVICE_REVOKE    35
#define ASTRA_SYSCALL_INPUT_READ_TRY   36
#define ASTRA_SYSCALL_PROCESS_INFO     37
#define ASTRA_SYSCALL_DMA_CREATE       38
#define ASTRA_SYSCALL_BLOCK_QUERY      39
#define ASTRA_SYSCALL_BLOCK_SUBMIT     40
#define ASTRA_SYSCALL_BLOCK_COLLECT    41
#define ASTRA_SYSCALL_CONSOLE_INFO     42
#define ASTRA_SYSCALL_CONSOLE_WRITE    43
#define ASTRA_SYSCALL_LOG_WRITE        44
/*
 * The calling thread's activity: what it is currently doing, for correlation.
 * data[1] of zero begins a fresh one; CURRENT reads it, NONE clears it, and
 * anything else adopts that value. The call returns the current activity in
 * data[1] and the previous activity in data[2].
 *
 * The kernel holds it, per thread, so that every event is stamped without any
 * call site passing one. A machine where correlation is a parameter is a
 * machine where the events that matter are the ones that forgot it.
 */
#define ASTRA_SYSCALL_ACTIVITY         45

/* Reads or clears a thread's activity without allocating a new global id. */
#define ASTRA_ACTIVITY_CURRENT 0xfffffffeu
#define ASTRA_ACTIVITY_NONE 0xffffffffu

/*
 * The other half of the reversal: reading the stream back.
 *
 * data[1] is a process handle carrying ASTRA_RIGHT_DEBUG that names the caller;
 * data[2] is the cursor -- the sequence already seen, zero for everything the
 * ring still holds; data[3] is where to put them and data[4] is how many
 * AstraEventDrained records will fit, clamped to ASTRA_TRACE_READ_BATCH_MAX.
 *
 * It returns how many were copied in data[1], the cursor to pass next time in
 * data[2], and in data[3] how many records the caller will never see because
 * the ring displaced them first. That last one is the point: a log that
 * quietly loses records is worse than one that admits it, because everything
 * read after the gap is an assumption.
 *
 * The handle must name the caller. Reading the machine's whole stream is the
 * observer's own authority, and borrowing a debug handle over some third
 * process to obtain it would be laundering authority through a bystander. Only
 * a build with a diagnostic surface puts DEBUG on a process's own handle, which
 * is what makes this a capability rather than a syscall anyone can reach.
 */
#define ASTRA_SYSCALL_TRACE_READ       46

/*
 * Launching a program.
 *
 * data[1] is the ELF image in the caller's own memory and data[2] its length;
 * data[3] is an AstraLaunchGrant array and data[4] how many; data[5] is an
 * AstraLaunchArguments block, or zero for none. The record may point at one
 * bounded packed environment block; the kernel copies it before launching and
 * publishes a conventional null-terminated `environ` vector. It returns a
 * handle to the new process in data[1] -- carrying QUERY, WAIT, TERMINATE and
 * ADMINISTER for priority, and never DEBUG, because having launched something
 * is not authority to inspect its event stream -- and the new process id in
 * data[2].
 *
 * Every grant names a handle the caller already holds, with rights that are a
 * subset of the caller's. A handle it does not hold, or rights wider than its
 * own, fails the whole call rather than being dropped: a child whose namespace
 * is quietly smaller than the line that launched it says would fail later, as a
 * path that does not resolve for a reason nobody can see.
 *
 * There is no fork. Nothing is inherited implicitly, so what a program may
 * touch is what somebody wrote down.
 *
 * AstraLaunchArguments.flags may request ASTRA_LAUNCH_FLAG_ESSENTIAL only
 * when the caller is the firmware-selected initial supervisor. Essential
 * processes retain access to protected memory headroom and the complete
 * priority band. Every ordinary child is capped at NORMAL priority.
 */
#define ASTRA_SYSCALL_PROCESS_CREATE   48
#define ASTRA_SYSCALL_IRQ_ENDPOINT_INFO 49
#define ASTRA_SYSCALL_CONSOLE_CURSOR   50
#define ASTRA_SYSCALL_DISPLAY_SUBMIT   51
#define ASTRA_SYSCALL_DISPLAY_COLLECT  52
#define ASTRA_SYSCALL_LIBRARY_MAP      53
/*
 * Hands the committed pages of a reserved area back, keeping the reservation.
 * data[1] is the address and data[2] the length; it answers with the number of
 * pages actually released in data[1].
 *
 * Only whole pages inside the range go, so an allocator may pass the block it
 * just freed without first working out which pages it has entirely to itself.
 */
#define ASTRA_SYSCALL_AREA_DECOMMIT    54

/*
 * The date: nanoseconds since the Unix epoch, high half in data[1] and low in
 * data[2], the same shape ASTRA_SYSCALL_CLOCK_MONOTONIC answers in.
 *
 * The two clocks are different things and both are needed. Monotonic never
 * goes backwards and is what a timeout is measured against; this one is what
 * a file's timestamp and a person's question are about, and it moves whenever
 * the machine's clock is corrected.
 *
 * A machine that does not know the date answers ASTRA_SYSCALL_UNSUPPORTED
 * rather than zero, because zero is a real instant -- and a program that
 * cannot tell "midnight in 1970" from "no idea" writes the first one into a
 * file and calls it a timestamp.
 */
#define ASTRA_SYSCALL_CLOCK_REALTIME   55
/* Maps an exact library identity already resident in the kernel cache. */
#define ASTRA_SYSCALL_LIBRARY_ATTACH   56
/*
 * Changes the scheduling priority of a process and all its live threads.
 * data[1] is a process handle carrying ASTRA_RIGHT_ADMINISTER; data[2] is a
 * user priority from 1 through 23. The previous priority is returned in
 * data[1]. New threads inherit the new process default.
 */
#define ASTRA_SYSCALL_PROCESS_PRIORITY 57
/* COW clone: D1=child handle and D2=child id in the parent, both zero child. */
#define ASTRA_SYSCALL_PROCESS_CLONE    58
/* Kernel-serialized transfers for clone-safe byte-mode bulk rings. */
#define ASTRA_SYSCALL_RING_READ_TRY    59
#define ASTRA_SYSCALL_RING_WRITE_TRY   60
/*
 * Reserves clone-private anonymous address space for the calling process.
 * data[1] is the requested size and data[2] is ASTRA_VM_PRIVATE_*; the result
 * is a root-slot-aligned base in data[1] and the rounded span in data[2]. No
 * RAM is committed until first touch, and every committed page is charged to
 * the process through the ordinary frame quota.
 */
#define ASTRA_SYSCALL_VM_PRIVATE_RESERVE  61
/* Decommits the whole pages inside a private reservation range. */
#define ASTRA_SYSCALL_VM_PRIVATE_DECOMMIT 62
/* Registers the POSIX signal trampoline/stack/mask and returns pending/mask. */
#define ASTRA_SYSCALL_SIGNAL_CONFIGURE 63
/* Arms ITIMER_REAL from relative delay/interval nanoseconds in D1:D2/D3:D4. */
#define ASTRA_SYSCALL_INTERVAL_TIMER 64
/* Restores the context saved by the active signal upcall. */
#define ASTRA_SYSCALL_SIGNAL_RETURN 65
/* Atomically replaces the calling process image; success never returns. */
#define ASTRA_SYSCALL_PROCESS_EXEC 66
/*
 * Transactional executable loading from a userspace file reader.
 *
 * BEGIN consumes only the fixed ELF header and the complete file size, then
 * returns a load handle plus the exact next file range in data[2:3]. WRITE
 * accepts only that range and returns the next one. Once all program headers
 * are accepted, CREATE snapshots grants and arguments and builds a private,
 * non-runnable child. Further WRITE calls fill its validated segment pages.
 * COMMIT publishes the initial thread and returns the child handle and id.
 *
 * Closing the load handle at any point aborts the transaction and releases
 * every partial process resource. The kernel never opens a file or interprets
 * a VFS protocol; userspace supplies only the bytes the kernel requests.
 */
#define ASTRA_SYSCALL_PROCESS_LOAD_BEGIN  67
#define ASTRA_SYSCALL_PROCESS_LOAD_WRITE  68
#define ASTRA_SYSCALL_PROCESS_LOAD_CREATE 69
#define ASTRA_SYSCALL_PROCESS_LOAD_COMMIT 70
/* Protected network broker transport; application protocols stay userspace. */
#define ASTRA_SYSCALL_NETWORK_QUERY        71
#define ASTRA_SYSCALL_NETWORK_EXECUTE      72
/* D1=CLOCK device lease, D2:D3=Unix epoch nanoseconds. */
#define ASTRA_SYSCALL_CLOCK_SET            73

#define ASTRA_VM_PRIVATE_READ  (1u << 0)
#define ASTRA_VM_PRIVATE_WRITE (1u << 1)
/* Complete anonymous window in the 32-bit Astra process address map. */
#define ASTRA_VM_PRIVATE_ADDRESS_SPACE_MAX 0x1f800000u

/*
 * The most one call copies. Small on purpose: a drain is a bounded page and a
 * cursor like every other enumeration here, and the batch is what a kernel
 * stack can hold without asking anyone's permission -- 8 * 56 bytes.
 */
#define ASTRA_TRACE_READ_BATCH_MAX 8u

/*
 * The event channel. A process that is not holding the display lease has no
 * way to say anything about itself -- the progress counter is a monotonic
 * integer and the exit status is one word -- so a service debugging itself had
 * nothing to say it with.
 *
 * It is not authority. The call takes a message id, flags, and at most
 * ASTRA_EVENT_ARGUMENT_MAX bytes of arguments, and no capability at all: a
 * machine whose account of what happened depends on a right has holes exactly
 * where something went wrong. ASTRA_RIGHT_DEBUG gates *reading* other
 * processes' events, and gates the console sink, which is where the leaking
 * risk actually is.
 *
 * There is no handle either. A process may only speak for itself, and the
 * kernel already knows who is calling, so there is nothing to pass and nothing
 * to get wrong.
 *
 * This is the cap on one line of text, which the runtime splits into a chain
 * of events. One event carries at most ASTRA_EVENT_ARGUMENT_MAX; see
 * astra/event.h.
 */
#define ASTRA_LOG_MAX_BYTES 128u

#define ASTRA_DMA_BUFFER_INFO_SIZE 20u

#define ASTRA_INPUT_READ_BATCH_MAX 16u
#define ASTRA_INPUT_READ_OVERFLOW  (1u << 0)


#define ASTRA_DEVICE_INFO_SIZE 24u
#define ASTRA_DEVICE_STATE_READY      1u
#define ASTRA_DEVICE_STATE_LEASED     2u
#define ASTRA_DEVICE_STATE_QUIESCING  3u
#define ASTRA_DEVICE_STATE_RESETTING  4u
#define ASTRA_DEVICE_STATE_FAILED     5u
#define ASTRA_DEVICE_LEASE_ACTIVE     1u
#define ASTRA_DEVICE_LEASE_REVOKING   2u
#define ASTRA_DEVICE_LEASE_REVOKED    3u

#define ASTRA_SYSCALL_PROCESS_EXIT ASTRA_SYSCALL_EXIT

#define ASTRA_SYSCALL_OK               0
#define ASTRA_SYSCALL_BAD_SYSCALL      1
#define ASTRA_SYSCALL_INVALID_ARGUMENT 2
#define ASTRA_SYSCALL_INVALID_HANDLE   3
#define ASTRA_SYSCALL_ACCESS_DENIED    4
#define ASTRA_SYSCALL_RESOURCE_LIMIT   5
#define ASTRA_SYSCALL_WOULD_BLOCK      6
#define ASTRA_SYSCALL_TIMED_OUT        7
#define ASTRA_SYSCALL_PEER_DEAD        8
#define ASTRA_SYSCALL_BAD_ADDRESS      9
#define ASTRA_SYSCALL_CANCELLED        10
#define ASTRA_SYSCALL_OUT_OF_MEMORY    11
#define ASTRA_SYSCALL_IO_ERROR         12
#define ASTRA_SYSCALL_CLOSED           13
#define ASTRA_SYSCALL_BUFFER_TOO_SMALL 14
/*
 * The machine cannot answer this at all -- not a refusal, not a failure, and
 * not something a retry changes. A machine with no wall clock says this to
 * ASTRA_SYSCALL_CLOCK_REALTIME.
 */
#define ASTRA_SYSCALL_UNSUPPORTED      15

#ifndef ASTRA_RIGHTS_DEFINED
#define ASTRA_RIGHTS_DEFINED 1
#define ASTRA_RIGHT_READ       (1u << 0)
#define ASTRA_RIGHT_WRITE      (1u << 1)
#define ASTRA_RIGHT_MAP        (1u << 2)
#define ASTRA_RIGHT_SIGNAL     (1u << 3)
#define ASTRA_RIGHT_WAIT       (1u << 4)
#define ASTRA_RIGHT_TRANSFER   (1u << 5)
#define ASTRA_RIGHT_ADMINISTER (1u << 6)
#define ASTRA_RIGHT_DEBUG      (1u << 7)
#endif

#define ASTRA_EVENT_MANUAL_RESET       (1u << 0)
#define ASTRA_EVENT_INITIALLY_SIGNALED (1u << 1)

#define ASTRA_DEADLINE_NONE_HI 0x7fffffffu
#define ASTRA_DEADLINE_NONE_LO 0xffffffffu
/* The same deadline as one number, for the callers that take one. */
#define ASTRA_DEADLINE_FOREVER \
    ((((uint64_t)ASTRA_DEADLINE_NONE_HI) << 32) | ASTRA_DEADLINE_NONE_LO)

/* A process cannot hold more waitable objects than handles. */
#define ASTRA_WAIT_MULTIPLE_MAX ASTRA_HANDLE_COUNT_MAX
#define ASTRA_WAIT_INDEX_NONE 0xffffffff

#define ASTRA_IRQ_RECORD_SIZE 16u
#define ASTRA_IRQ_ENDPOINT_INFO_SIZE 36u

/*
 * The states an endpoint can be in, as ASTRA_SYSCALL_IRQ_ENDPOINT_INFO reports
 * them. They are the kernel's own, published so that a program reading the
 * surface renders a word rather than a number.
 */
#define ASTRA_IRQ_ENDPOINT_FREE     0u
#define ASTRA_IRQ_ENDPOINT_MASKED   1u
#define ASTRA_IRQ_ENDPOINT_ARMED    2u
#define ASTRA_IRQ_ENDPOINT_PENDING  3u
#define ASTRA_IRQ_ENDPOINT_REVOKING 4u

/*
 * Why an endpoint stopped serving, if it did. These are sticky: an endpoint
 * carrying any of them answers every read with the matching status until
 * something recovers it, which is the whole reason this surface exists -- a
 * quarantined device is otherwise indistinguishable from an idle one.
 */
#define ASTRA_IRQ_ENDPOINT_EVENT_OVERFLOW     (1u << 0)
#define ASTRA_IRQ_ENDPOINT_EVENT_STORM        (1u << 1)
#define ASTRA_IRQ_ENDPOINT_EVENT_DEVICE_ERROR (1u << 2)
#define ASTRA_IRQ_EVENT_OVERFLOW     (1u << 0)
#define ASTRA_IRQ_EVENT_STORM        (1u << 1)
#define ASTRA_IRQ_EVENT_DEVICE_ERROR (1u << 2)

#ifndef ASTRA_AREA_ABI_CONSTANTS_DEFINED
#define ASTRA_AREA_ABI_CONSTANTS_DEFINED 1
#define ASTRA_AREA_SIZE_MAX 0x00400000u
#define ASTRA_AREA_MAP_READ  (1u << 0)
#define ASTRA_AREA_MAP_WRITE (1u << 1)
/*
 * Take the address range and commit nothing. Pages arrive as they are
 * touched, a cluster at a time, and are charged to the owner then. What the
 * program never reads, the machine never spends a frame on.
 */
#define ASTRA_AREA_CREATE_RESERVED (1u << 0)
#endif

#ifndef ASTRA_BULK_RING_ABI_CONSTANTS_DEFINED
#define ASTRA_BULK_RING_ABI_CONSTANTS_DEFINED 1
#define ASTRA_BULK_RING_MAGIC 0x4152494eu
#define ASTRA_BULK_RING_ABI_VERSION 1u
#define ASTRA_BULK_RING_HEADER_SIZE 64u
#define ASTRA_BULK_RING_OFFSET_ALIGNMENT 64u
#define ASTRA_BULK_RING_ELEMENT_SIZE_MIN 4u
#define ASTRA_BULK_RING_ELEMENT_SIZE_MAX 4096u
#define ASTRA_BULK_RING_CAPACITY_MIN 2u
#define ASTRA_BULK_RING_CAPACITY_MAX 1024u
#define ASTRA_BULK_RING_NOTIFY_CORRUPT (1u << 0)
#define ASTRA_BULK_RING_CREATE_KERNEL_COPY (1u << 0)
#define ASTRA_BULK_RING_CREATE_FLAG_MASK ASTRA_BULK_RING_CREATE_KERNEL_COPY
#define ASTRA_BULK_RING_TRANSFER_MAX 4096u
#define ASTRA_BULK_RING_WRITE_ATOMIC (1u << 0)
#define ASTRA_BULK_RING_WRITE_FLAG_MASK ASTRA_BULK_RING_WRITE_ATOMIC
#define ASTRA_BULK_RING_PRODUCER 1u
#define ASTRA_BULK_RING_CONSUMER 2u
#endif

#ifndef __ASSEMBLER__

#include <stdint.h>

/*
 * Every record the syscall boundary copies to or from user memory carries
 * ASTRA_ABI_ALIGNMENT, and the kernel refuses an address that does not hold
 * it. The alignment has to be written down here rather than assumed: the m68k
 * ABI aligns uint32_t to two bytes, so a record built from uint32_t fields is
 * only four-byte aligned by luck of where the linker or the stack happens to
 * put it. A batch buffer that moved from a stack frame into .bss landed on an
 * odd word and every read was refused with INVALID_ARGUMENT -- which the
 * caller saw as "no input", so it looked like a hang rather than a refusal.
 */
#define ASTRA_ABI_ALIGNMENT 4u

typedef struct AstraIrqRecord {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t status;
    uint32_t sequence;
} AstraIrqRecord;

/*
 * Transfer memory a service owns: kernel-allocated, physically contiguous,
 * charged to the caller, and mapped into it read/write. The service never
 * names a physical address; the handle is what it hands to the block engine.
 * Released by ASTRA_SYSCALL_CLOSE like any other handle.
 */
typedef struct AstraDmaBufferInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t handle;
    uint32_t virtual_base;
    uint32_t byte_size;
    uint32_t page_count;
} AstraDmaBufferInfo;

/*
 * What an interrupt endpoint is doing, and whether it is still doing it.
 *
 * A device that quarantines itself goes on looking exactly like a device
 * nobody is using: the handles are still open, the driver is still calling,
 * and every call comes back with an I/O error whose cause is three layers
 * down. This is the surface that tells them apart, and `event_flags` is the
 * field that does it.
 */
typedef struct AstraIrqEndpointInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t owner;          /* process id, or zero when the slot is free */
    uint32_t generation;
    uint32_t delivered;
    uint32_t acknowledged;
    uint32_t dropped;
    uint16_t references;
    uint16_t waiters;
    uint8_t source;
    uint8_t state;           /* ASTRA_IRQ_ENDPOINT_* */
    uint8_t trigger;
    uint8_t ipl;
    uint8_t pending_records;
    uint8_t event_flags;     /* ASTRA_IRQ_ENDPOINT_EVENT_* */
    uint8_t consecutive;
    uint8_t reserved;
} AstraIrqEndpointInfo;

typedef struct AstraDeviceInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t device_id;
    uint32_t class_id;
    uint32_t capabilities;
    uint32_t generation;
    uint8_t device_state;
    uint8_t lease_state;
    uint16_t reserved;
} AstraDeviceInfo;

_Static_assert(sizeof(AstraDeviceInfo) == ASTRA_DEVICE_INFO_SIZE,
               "device-info ABI size changed");

_Static_assert(sizeof(AstraDmaBufferInfo) == ASTRA_DMA_BUFFER_INFO_SIZE,
               "dma-buffer-info ABI size changed");

_Static_assert(sizeof(AstraIrqRecord) == ASTRA_IRQ_RECORD_SIZE,
               "IRQ record ABI size changed");

_Static_assert(sizeof(AstraIrqEndpointInfo) == ASTRA_IRQ_ENDPOINT_INFO_SIZE,
               "IRQ endpoint-info ABI size changed");
_Static_assert(_Alignof(AstraIrqEndpointInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "IRQ endpoint-info must satisfy the syscall alignment rule");

/*
 * The refusal these prevent is silent at the call site, so it is caught at
 * compile time on the target that has the weaker alignment rule rather than
 * at run time on the machine that hangs.
 */
_Static_assert(_Alignof(AstraDeviceInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "device-info must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraDmaBufferInfo) % ASTRA_ABI_ALIGNMENT == 0u,
               "dma-buffer-info must satisfy the syscall alignment rule");
_Static_assert(_Alignof(AstraIrqRecord) % ASTRA_ABI_ALIGNMENT == 0u,
               "IRQ record must satisfy the syscall alignment rule");

#ifndef ASTRA_BULK_RING_HEADER_DEFINED
#define ASTRA_BULK_RING_HEADER_DEFINED 1
typedef struct AstraBulkRingHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t element_size;
    uint32_t capacity;
    uint32_t data_offset;
    uint32_t total_size;
    uint32_t generation;
    uint32_t producer_position;
    uint32_t producer_reserved[3];
    uint32_t consumer_position;
    uint32_t consumer_reserved[3];
} AstraBulkRingHeader;
#endif

_Static_assert(sizeof(AstraBulkRingHeader) == ASTRA_BULK_RING_HEADER_SIZE,
               "bulk-ring ABI header size changed");

#endif

#endif
