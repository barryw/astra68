#ifndef ASTRA_SYSCALL_H
#define ASTRA_SYSCALL_H

#define ASTRA_SYSCALL_TRAP 15
#define ASTRA_SYSCALL_VECTOR 47
#define ASTRA_SYSCALL_ABI_VERSION 0x0001000b

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
 * data[1] of zero begins a fresh one; anything else adopts that value, which
 * is how a service joins the story it was called from. Both return the
 * thread's current activity.
 *
 * The kernel holds it, per thread, so that every event is stamped without any
 * call site passing one. A machine where correlation is a parameter is a
 * machine where the events that matter are the ones that forgot it.
 */
#define ASTRA_SYSCALL_ACTIVITY         45

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

#define ASTRA_WAIT_MULTIPLE_MAX 16
#define ASTRA_WAIT_INDEX_NONE 0xffffffff

#define ASTRA_IRQ_RECORD_SIZE 16u
#define ASTRA_IRQ_EVENT_OVERFLOW     (1u << 0)
#define ASTRA_IRQ_EVENT_STORM        (1u << 1)
#define ASTRA_IRQ_EVENT_DEVICE_ERROR (1u << 2)

#ifndef ASTRA_AREA_ABI_CONSTANTS_DEFINED
#define ASTRA_AREA_ABI_CONSTANTS_DEFINED 1
#define ASTRA_AREA_SIZE_MAX 0x00010000u
#define ASTRA_AREA_MAP_READ  (1u << 0)
#define ASTRA_AREA_MAP_WRITE (1u << 1)
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
#define ASTRA_BULK_RING_PRODUCER 1u
#define ASTRA_BULK_RING_CONSUMER 2u
#endif

#ifndef ASTRA_MESSAGE_ABI_CONSTANTS_DEFINED
#define ASTRA_MESSAGE_ABI_CONSTANTS_DEFINED 1
#define ASTRA_PORT_MESSAGES_MAX 8u
#define ASTRA_PORT_BYTES_MAX 2240u
#define ASTRA_MESSAGE_HEADER_SIZE 24u
#define ASTRA_MESSAGE_INLINE_MAX 256u
#define ASTRA_MESSAGE_SIZE_MAX \
    (ASTRA_MESSAGE_HEADER_SIZE + ASTRA_MESSAGE_INLINE_MAX)
#define ASTRA_MESSAGE_HANDLES_MAX 8u
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

#ifndef ASTRA_MESSAGE_HEADER_DEFINED
#define ASTRA_MESSAGE_HEADER_DEFINED 1
typedef struct AstraMessageHeader {
    uint32_t total_size;
    uint16_t header_size;
    uint16_t flags;
    uint32_t protocol;
    uint16_t protocol_version;
    uint16_t reserved;
    uint32_t operation;
    uint32_t transaction_id;
} AstraMessageHeader;
#endif

_Static_assert(sizeof(AstraMessageHeader) == ASTRA_MESSAGE_HEADER_SIZE,
               "message ABI header size changed");

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
