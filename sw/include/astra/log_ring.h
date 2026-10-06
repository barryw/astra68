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
 */

#include <stdint.h>

#include <astra/object_abi.h>

#define ASTRA_LOG_RING_AREA_SIZE    65536u
#define ASTRA_LOG_RING_OFFSET       0u
#define ASTRA_LOG_RING_SLOT_SIZE    252u
#define ASTRA_LOG_RING_CAPACITY     256u
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

#define ASTRA_LOG_STREAM_LOG    0u
#define ASTRA_LOG_STREAM_STDOUT 1u
#define ASTRA_LOG_STREAM_STDERR 2u
#define ASTRA_LOG_STREAM_KERNEL 3u
#define ASTRA_LOG_STREAM_MAX    3u

#define ASTRA_LOG_FLAG_TRUNCATED 0x1u
#define ASTRA_LOG_FLAG_WALL_VALID 0x2u

/** Decoded record header. Stored in the first slot, native byte order. */
typedef struct AstraLogRecordFields {
    uint32_t sequence;
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

/* The setup message a process sends the LOG port with [consumer, area]. */
#define ASTRA_CAPABILITY_LOG       "LOG"
#define ASTRA_LOG_SETUP_MAGIC      0x414c4f47u /* "ALOG" */
#define ASTRA_LOG_SETUP_VERSION    1u
#define ASTRA_LOG_IDENT_MAX        32u

typedef struct AstraLogSetup {
    uint32_t magic;
    uint32_t version;
    uint32_t area_size;
    uint32_t ring_offset;
    uint32_t slot_size;
    uint32_t capacity;
    char     ident[ASTRA_LOG_IDENT_MAX];   /* NUL-terminated */
} AstraLogSetup;

/* The doorbell: returns nonzero when the service is gone (PEER_DEAD). */
typedef uint32_t (*AstraLogNotify)(void *context);
#define ASTRA_LOG_DOORBELL_MAGIC   0x414c4442u /* "ALDB", 4-byte message */

/** Process-local producer state. Every field is written only by CAS or by
 * the setup path before `ready` is published. */
typedef struct AstraLogProducer {
    volatile AstraBulkRingHeader *header;
    volatile uint8_t *slots;
    AstraLogNotify notify;
    void *context;
    uint32_t reserved;     /* next position to hand out */
    uint32_t sequence;
    uint32_t dropped;
    uint32_t dead;         /* nonzero once the service is gone */
} AstraLogProducer;

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
uint32_t astra_log_consumer_next(volatile const void *ring_base,
                                 uint32_t *position,
                                 AstraLogRecordFields *fields, char *text,
                                 uint32_t capacity, uint32_t *length);

#endif
