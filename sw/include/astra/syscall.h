#ifndef ASTRA_SYSCALL_H
#define ASTRA_SYSCALL_H

#define ASTRA_SYSCALL_TRAP 15
#define ASTRA_SYSCALL_VECTOR 47
#define ASTRA_SYSCALL_ABI_VERSION 0x00010005

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

typedef struct AstraIrqRecord {
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t status;
    uint32_t sequence;
} AstraIrqRecord;

_Static_assert(sizeof(AstraIrqRecord) == ASTRA_IRQ_RECORD_SIZE,
               "IRQ record ABI size changed");

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
