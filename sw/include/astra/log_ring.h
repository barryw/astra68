#ifndef ASTRA_LOG_RING_H
#define ASTRA_LOG_RING_H

/** @file log_ring.h
 * @brief One process's log ring: a bulk ring of fixed slots in a shared area.
 *
 * docs/SYSTEM_LOG.md section 3. The process is the producer, the events
 * service the consumer. Writers inside the process reserve slots with one CAS
 * on a process-local counter, fill them, set each slot's commit word to its
 * position + 1, and advance the shared producer position over committed slots
 * with CAS. No writer waits for another.
 *
 * Doorbell pairing contract: the producer's maybe_notify() reads
 * consumer_position after a seq_cst fence that follows publish()'s CAS on
 * producer_position. For that fence to mean anything, the consumer must
 * store consumer_position (after draining), then fence seq_cst, then re-read
 * producer_position before deciding whether more work already arrived --
 * the same store-then-reload shape, mirrored. Without a matching seq_cst
 * fence on the consumer side, each side's load can miss the other's store
 * (the store-buffering pattern log_ring.c's producer-side fence forbids),
 * and a doorbell can be skipped when it was needed.
 */

#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/object_abi.h>

/* 1024 slots: an ordinary burst (SDL's 512-line scancode table) fits while
 * the service, below applications, waits for the program to yield. The area
 * is created reserved, so pages commit as slots are first touched, and a
 * 256 KiB area takes the same one shared-table span of address space a
 * 64 KiB one did (KERNEL_VM_SHARED_TABLE_SPAN). */
#define ASTRA_LOG_RING_AREA_SIZE    262144u
#define ASTRA_LOG_RING_OFFSET       0u
#define ASTRA_LOG_RING_SLOT_SIZE    252u
#define ASTRA_LOG_RING_CAPACITY     1024u
#define ASTRA_LOG_RING_HEADER_SIZE  64u
#define ASTRA_LOG_SLOT_PAYLOAD      (ASTRA_LOG_RING_SLOT_SIZE - 4u)   /* 248 */
#define ASTRA_LOG_RECORD_HEADER_SIZE 40u
#define ASTRA_LOG_FIRST_TEXT        (ASTRA_LOG_SLOT_PAYLOAD - \
                                     ASTRA_LOG_RECORD_HEADER_SIZE)    /* 208 */
#define ASTRA_LOG_RECORD_SLOTS_MAX  8u
#define ASTRA_LOG_TEXT_MAX          (ASTRA_LOG_FIRST_TEXT + \
                                     (ASTRA_LOG_RECORD_SLOTS_MAX - 1u) * \
                                     ASTRA_LOG_SLOT_PAYLOAD)          /* 1944 */

_Static_assert(ASTRA_LOG_RING_HEADER_SIZE +
               ASTRA_LOG_RING_CAPACITY * ASTRA_LOG_RING_SLOT_SIZE <=
               ASTRA_LOG_RING_AREA_SIZE, "log ring fits its area");

/*
 * Lines this process has dropped, ever: the first producer_reserved word of
 * the ring header, which the kernel never reads. The producer bumps it with a
 * CAS on each drop; every record also carries the count at its writing. The
 * service announces drops before the record that carries them, and from this
 * word when it has read to the end of the ring -- so drops after a program's
 * last line, with no record left to carry them, are still reported.
 */
#define ASTRA_LOG_RING_DROPPED(header) ((header)->producer_reserved[0])

#define ASTRA_LOG_STREAM_LOG    0u
#define ASTRA_LOG_STREAM_STDOUT 1u
#define ASTRA_LOG_STREAM_STDERR 2u
#define ASTRA_LOG_STREAM_KERNEL 3u
#define ASTRA_LOG_STREAM_MAX    3u

#define ASTRA_LOG_FLAG_TRUNCATED 0x1u
#define ASTRA_LOG_FLAG_WALL_VALID 0x2u

/** Decoded record header. Stored in the first slot, native byte order. */
typedef struct AstraLogRecordFields {
    uint32_t sequence;        /* ring position of the record's first slot */
    uint32_t monotonic_high;
    uint32_t monotonic_low;
    uint32_t wall_high;
    uint32_t wall_low;
    int32_t  utc_offset;
    uint32_t zone;
    uint32_t dropped;        /* lines this process dropped, ever */
    uint16_t text_length;
    uint8_t  level;          /* ASTRA_EVENT_LEVEL_* */
    uint8_t  stream;         /* ASTRA_LOG_STREAM_* */
    uint8_t  slots;          /* 1..ASTRA_LOG_RECORD_SLOTS_MAX */
    uint8_t  flags;          /* ASTRA_LOG_FLAG_* */
    uint16_t reserved;
} AstraLogRecordFields;

_Static_assert(sizeof(AstraLogRecordFields) == ASTRA_LOG_RECORD_HEADER_SIZE,
               "log record header ABI");

/*
 * Messages to the LOG port. The kernel refuses a port message that does not
 * begin with a valid AstraMessageHeader (sw/kernel/process.c,
 * valid_message_header), so both carry one: protocol ASTRA_LOG_SETUP_MAGIC,
 * version ASTRA_LOG_SETUP_VERSION, and the operation below.
 *
 * SETUP is an AstraLogSetup sent with [consumer endpoint, area]. DOORBELL is
 * the header alone, with no handles.
 */
#define ASTRA_CAPABILITY_LOG       "LOG"
#define ASTRA_LOG_SETUP_MAGIC      0x414c4f47u /* "ALOG" */
#define ASTRA_LOG_SETUP_VERSION    2u /* 2: 1024 slots, header drop count */
#define ASTRA_LOG_IDENT_MAX        32u
#define ASTRA_LOG_OPERATION_SETUP    1u
#define ASTRA_LOG_OPERATION_DOORBELL 2u
/* The most setups one process image makes: one per ring the live service let
 * go of, or per failed exec, each with a ring of its own that stays mapped
 * (runtime/src/log.c). A dead service ends setup for good, in the
 * process and in any child it forks. The service
 * refuses a setup from a pid already holding this many. */
#define ASTRA_LOG_SETUPS_MAX       8u

typedef struct AstraLogSetup {
    AstraMessageHeader header;  /* operation ASTRA_LOG_OPERATION_SETUP */
    uint32_t magic;
    uint32_t version;
    uint32_t area_size;
    uint32_t ring_offset;
    uint32_t slot_size;
    uint32_t capacity;
    char     ident[ASTRA_LOG_IDENT_MAX];   /* NUL-terminated */
} AstraLogSetup;

/*
 * The doorbell: returns nonzero when the service is gone. `dropping` is
 * nonzero on the drop path (a full ring, every
 * ASTRA_LOG_DROP_DOORBELL_INTERVAL drops), where the caller may also check
 * that the service still holds this ring's consumer end: a service that
 * refused or abandoned the ring closes it while its port lives on.
 */
typedef uint32_t (*AstraLogNotify)(void *context, uint32_t dropping);

/** Process-local producer state. Every field is written only by CAS, by an
 * atomic store (`dead`), or by the setup path before the producer is used. */
typedef struct AstraLogProducer {
    volatile AstraBulkRingHeader *header;
    volatile uint8_t *slots;
    AstraLogNotify notify;
    void *context;
    uint32_t reserved;     /* next position to hand out */
    uint32_t dead;         /* nonzero once the service is gone */
} AstraLogProducer;

/* A dropped line rings the doorbell on the first drop and every 64th after:
 * a service that died while the ring was full is found within 64 lines, at a
 * cost of one non-blocking send per 64 drops. */
#define ASTRA_LOG_DROP_DOORBELL_INTERVAL 64u

#define ASTRA_LOG_WRITE_OK      0u
#define ASTRA_LOG_WRITE_DROPPED 1u
#define ASTRA_LOG_WRITE_DEAD    2u

#define ASTRA_LOG_READ_OK       0u
#define ASTRA_LOG_READ_EMPTY    1u
#define ASTRA_LOG_READ_CORRUPT  2u

void astra_log_producer_init(AstraLogProducer *producer,
                             volatile void *ring_base, AstraLogNotify notify,
                             void *context);
uint32_t astra_log_record_slots(uint32_t text_length);
uint32_t astra_log_producer_write(AstraLogProducer *producer,
                                  const AstraLogRecordFields *fields,
                                  const char *text, uint32_t length);
/* Before a caller pays for a record's time and fields: ASTRA_LOG_WRITE_OK
 * when the ring has room for `length` bytes of text now; otherwise the line
 * is dropped and counted exactly as astra_log_producer_write drops it
 * (ASTRA_LOG_WRITE_DROPPED), or the producer is dead (ASTRA_LOG_WRITE_DEAD).
 * Room now is not a reservation: the write still checks. */
uint32_t astra_log_producer_drop_if_full(AstraLogProducer *producer,
                                         uint32_t length);
uint32_t astra_log_consumer_next(volatile const void *ring_base,
                                 uint32_t *position,
                                 AstraLogRecordFields *fields, char *text,
                                 uint32_t capacity, uint32_t *length);

#endif
