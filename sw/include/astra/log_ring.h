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
/** Bytes in the shared area that holds the ring. */
#define ASTRA_LOG_RING_AREA_SIZE    262144u
/** Offset of the ring header in its area. */
#define ASTRA_LOG_RING_OFFSET       0u
/** Bytes per slot: a commit word and ::ASTRA_LOG_SLOT_PAYLOAD. */
#define ASTRA_LOG_RING_SLOT_SIZE    252u
/** Slots in the ring. */
#define ASTRA_LOG_RING_CAPACITY     1024u
/** Bytes of AstraBulkRingHeader before the slots. */
#define ASTRA_LOG_RING_HEADER_SIZE  64u
/** Record bytes per slot (248). */
#define ASTRA_LOG_SLOT_PAYLOAD      (ASTRA_LOG_RING_SLOT_SIZE - 4u)
/** Bytes of AstraLogRecordFields at the start of a record's first slot. */
#define ASTRA_LOG_RECORD_HEADER_SIZE 40u
/** Text bytes in a record's first slot (208). */
#define ASTRA_LOG_FIRST_TEXT        (ASTRA_LOG_SLOT_PAYLOAD - \
                                     ASTRA_LOG_RECORD_HEADER_SIZE)
/** Most slots one record spans. */
#define ASTRA_LOG_RECORD_SLOTS_MAX  8u
/** Most text bytes one record carries (1944); longer text is truncated. */
#define ASTRA_LOG_TEXT_MAX          (ASTRA_LOG_FIRST_TEXT + \
                                     (ASTRA_LOG_RECORD_SLOTS_MAX - 1u) * \
                                     ASTRA_LOG_SLOT_PAYLOAD)          /* 1944 */

/** @cond ASTRA_INTERNAL */
_Static_assert(ASTRA_LOG_RING_HEADER_SIZE +
               ASTRA_LOG_RING_CAPACITY * ASTRA_LOG_RING_SLOT_SIZE <=
               ASTRA_LOG_RING_AREA_SIZE, "log ring fits its area");
/** @endcond */

/**
 * Lines dropped from this ring, ever: the first producer_reserved word of the
 * ring header, which the kernel never reads. The count is per ring, not per
 * process: a process that sets up a new ring starts it at zero. The producer
 * bumps it with a CAS on each drop; every record also carries the count at
 * its writing. The service announces drops before the record that carries
 * them, and from this word when it has read to the end of the ring -- so
 * drops after a program's last line, with no record left to carry them, are
 * still reported, as a marker stamped with the last record's time. A ring
 * whose first record never committed has no last record: that marker carries
 * zero time and is written as `boot+0.000`, first in its drain pass.
 *
 * A nonzero reserved word fails the generic bulk-ring validator
 * (system/src/bulk_ring.c requires producer_reserved to be zero), so a log
 * ring is never opened through it: astra_log_consumer_next reads it.
 */
#define ASTRA_LOG_RING_DROPPED(header) ((header)->producer_reserved[0])

/** A line written with astra_log() and its relatives. */
#define ASTRA_LOG_STREAM_LOG    0u
/** A line the program wrote to standard output. */
#define ASTRA_LOG_STREAM_STDOUT 1u
/** A line the program wrote to standard error. */
#define ASTRA_LOG_STREAM_STDERR 2u
/** A line from the kernel event ring. */
#define ASTRA_LOG_STREAM_KERNEL 3u
/** Highest valid ASTRA_LOG_STREAM_* value. */
#define ASTRA_LOG_STREAM_MAX    3u

/** Record flag: the text was cut to ::ASTRA_LOG_TEXT_MAX. */
#define ASTRA_LOG_FLAG_TRUNCATED 0x1u
/** Record flag: the wall-clock fields are valid. */
#define ASTRA_LOG_FLAG_WALL_VALID 0x2u

/** Decoded record header. Stored in the first slot, native byte order. */
typedef struct AstraLogRecordFields {
    uint32_t sequence;        /**< Ring position of the record's first slot. */
    uint32_t monotonic_high;  /**< High word of ns since boot. */
    uint32_t monotonic_low;   /**< Low word of ns since boot. */
    uint32_t wall_high;       /**< High word of ns since the Unix epoch. */
    uint32_t wall_low;        /**< Low word of ns since the Unix epoch. */
    int32_t  utc_offset;      /**< Seconds east of UTC. */
    uint32_t zone;            /**< Packed 4-character zone name. */
    uint32_t dropped;         /**< Lines this ring dropped, ever. */
    uint16_t text_length;     /**< Text bytes after the header. */
    uint8_t  level;           /**< ASTRA_EVENT_LEVEL_*. */
    uint8_t  stream;          /**< ASTRA_LOG_STREAM_*. */
    uint8_t  slots;           /**< 1..::ASTRA_LOG_RECORD_SLOTS_MAX. */
    uint8_t  flags;           /**< ASTRA_LOG_FLAG_*. */
    uint16_t reserved;        /**< Zero. */
} AstraLogRecordFields;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraLogRecordFields) == ASTRA_LOG_RECORD_HEADER_SIZE,
               "log record header ABI");
/** @endcond */

/*
 * Messages to the LOG port. The kernel refuses a port message that does not
 * begin with a valid AstraMessageHeader (sw/kernel/process.c,
 * valid_message_header), so both carry one: protocol ASTRA_LOG_SETUP_MAGIC,
 * version ASTRA_LOG_SETUP_VERSION, and the operation below.
 *
 * SETUP is an AstraLogSetup sent with [consumer endpoint, area]. DOORBELL is
 * the header alone, with no handles.
 */
/** Capability naming the events service's LOG port. */
#define ASTRA_CAPABILITY_LOG       "LOG"
/** Message protocol and AstraLogSetup::magic, "ALOG". */
#define ASTRA_LOG_SETUP_MAGIC      0x414c4f47u
/** Setup version; 2: 1024 slots, header drop count. */
#define ASTRA_LOG_SETUP_VERSION    2u
/** Bytes of AstraLogSetup::ident, terminator included. */
#define ASTRA_LOG_IDENT_MAX        32u
/** LOG port operation: attach a ring. */
#define ASTRA_LOG_OPERATION_SETUP    1u
/** LOG port operation: the ring has new records. */
#define ASTRA_LOG_OPERATION_DOORBELL 2u
/** The most setups one process image makes: one per ring the live service let
 * go of, or per failed exec, each with a ring of its own that stays mapped
 * (runtime/src/log.c). A dead service ends setup for good, in the
 * process and in any child it forks. The service
 * refuses a setup from a pid already holding this many. */
#define ASTRA_LOG_SETUPS_MAX       8u

/** The SETUP message, sent with [consumer endpoint, area]. */
typedef struct AstraLogSetup {
    AstraMessageHeader header;  /**< Operation ::ASTRA_LOG_OPERATION_SETUP. */
    uint32_t magic;             /**< ::ASTRA_LOG_SETUP_MAGIC. */
    uint32_t version;           /**< ::ASTRA_LOG_SETUP_VERSION. */
    uint32_t area_size;         /**< ::ASTRA_LOG_RING_AREA_SIZE. */
    uint32_t ring_offset;       /**< ::ASTRA_LOG_RING_OFFSET. */
    uint32_t slot_size;         /**< ::ASTRA_LOG_RING_SLOT_SIZE. */
    uint32_t capacity;          /**< ::ASTRA_LOG_RING_CAPACITY. */
    char     ident[ASTRA_LOG_IDENT_MAX];   /**< NUL-terminated program name. */
} AstraLogSetup;

/**
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
    volatile AstraBulkRingHeader *header; /**< The shared ring header. */
    volatile uint8_t *slots;   /**< The first slot. */
    AstraLogNotify notify;     /**< Rings the doorbell. */
    void *context;             /**< Passed to notify. */
    uint32_t reserved;         /**< Next position to hand out. */
    uint32_t dead;             /**< Nonzero once the service is gone. */
} AstraLogProducer;

/** A dropped line rings the doorbell on the first drop and every 64th after:
 * a service that died while the ring was full is found within 64 lines, at a
 * cost of one non-blocking send per 64 drops. */
#define ASTRA_LOG_DROP_DOORBELL_INTERVAL 64u

/** Write result: the record is in the ring. */
#define ASTRA_LOG_WRITE_OK      0u
/** Write result: the ring was full; the line was dropped and counted. */
#define ASTRA_LOG_WRITE_DROPPED 1u
/** Write result: the service is gone. */
#define ASTRA_LOG_WRITE_DEAD    2u

/** Read result: a record was returned. */
#define ASTRA_LOG_READ_OK       0u
/** Read result: no committed record at the position. */
#define ASTRA_LOG_READ_EMPTY    1u
/** Read result: the ring holds an invalid record. */
#define ASTRA_LOG_READ_CORRUPT  2u

/**
 * Prepare a producer for the ring at `ring_base`.
 * @param producer State to initialise.
 * @param ring_base The ring header in the shared area.
 * @param notify Doorbell, or NULL.
 * @param context Passed to `notify`.
 */
void astra_log_producer_init(AstraLogProducer *producer,
                             volatile void *ring_base, AstraLogNotify notify,
                             void *context);
/**
 * @param text_length Text bytes, at most ::ASTRA_LOG_TEXT_MAX are kept.
 * @return Slots a record with `text_length` bytes of text spans.
 */
uint32_t astra_log_record_slots(uint32_t text_length);
/**
 * Write one record.
 * @param producer The producer.
 * @param fields Record header; the producer fills sequence, dropped,
 *        text_length, slots and the truncation flag.
 * @param text UTF-8 bytes.
 * @param length Byte count.
 * @return ASTRA_LOG_WRITE_*.
 */
uint32_t astra_log_producer_write(AstraLogProducer *producer,
                                  const AstraLogRecordFields *fields,
                                  const char *text, uint32_t length);
/** Before a caller pays for a record's time and fields: ASTRA_LOG_WRITE_OK
 * when the ring has room for `length` bytes of text now; otherwise the line
 * is dropped and counted exactly as astra_log_producer_write drops it
 * (ASTRA_LOG_WRITE_DROPPED), or the producer is dead (ASTRA_LOG_WRITE_DEAD).
 * Room now is not a reservation: the write still checks.
 * @param producer The producer.
 * @param length Text bytes the caller will write.
 * @return ASTRA_LOG_WRITE_*. */
uint32_t astra_log_producer_drop_if_full(AstraLogProducer *producer,
                                         uint32_t length);
/**
 * Read the record at `*position` and advance past it.
 * @param ring_base The ring header in the shared area.
 * @param position Consumer position, advanced on ::ASTRA_LOG_READ_OK.
 * @param fields Receives the record header.
 * @param text Receives the text.
 * @param capacity Bytes available in `text`.
 * @param length Receives the text byte count.
 * @return ASTRA_LOG_READ_*.
 */
uint32_t astra_log_consumer_next(volatile const void *ring_base,
                                 uint32_t *position,
                                 AstraLogRecordFields *fields, char *text,
                                 uint32_t capacity, uint32_t *length);

#endif
