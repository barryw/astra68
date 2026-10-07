/*
 * The log ring's two halves. Pure: no syscalls, so the host test drives both
 * against one buffer. docs/SYSTEM_LOG.md section 3 is the design.
 *
 * Slot layout: [commit word][248 payload]. The first slot's payload is the
 * 40-byte AstraLogRecordFields and 208 bytes of text; continuation slots are
 * text. A slot's commit word is its ring position + 1 once its bytes are
 * written: a value from an earlier lap differs by a multiple of the capacity
 * and never matches, so the words need no clearing.
 */

#include <astra/log_ring.h>

#include <astra/event.h>

#include <stddef.h>

static inline int
cas32(uint32_t *word, uint32_t expected, uint32_t desired)
{
    /* Explicit CAS, never __atomic_exchange_n: runtime.h:785-800. */
    return __atomic_compare_exchange_n(word, &expected, desired, 0,
                                       __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static inline uint32_t
load32(const volatile uint32_t *word)
{
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

static volatile uint8_t *
slot_at(volatile uint8_t *slots, uint32_t position)
{
    return slots + (position & (ASTRA_LOG_RING_CAPACITY - 1u)) *
                   ASTRA_LOG_RING_SLOT_SIZE;
}

static volatile uint32_t *
commit_word(volatile uint8_t *slot)
{
    return (volatile uint32_t *)(void *)slot;
}

uint32_t
astra_log_record_slots(uint32_t text_length)
{
    uint32_t rest;

    if (text_length > ASTRA_LOG_TEXT_MAX)
        text_length = ASTRA_LOG_TEXT_MAX;
    if (text_length <= ASTRA_LOG_FIRST_TEXT)
        return 1u;
    rest = text_length - ASTRA_LOG_FIRST_TEXT;
    return 1u + (rest + ASTRA_LOG_SLOT_PAYLOAD - 1u) / ASTRA_LOG_SLOT_PAYLOAD;
}

void
astra_log_producer_init(AstraLogProducer *producer, volatile void *ring_base,
                        AstraLogNotify notify, void *context)
{
    volatile uint8_t *base = ring_base;

    producer->header = (volatile AstraBulkRingHeader *)(void *)base;
    producer->slots = base + ASTRA_LOG_RING_HEADER_SIZE;
    producer->notify = notify;
    producer->context = context;
    producer->reserved = producer->header->producer_position;
    producer->dead = 0u;
}

/*
 * Plain stores into the ring are enough: the release fence before the commit
 * words (astra_log_producer_write) orders them for the consumer, exactly as
 * it ordered the old byte-at-a-time volatile stores. `to` is a slot payload,
 * which is word-aligned (64-byte header, 252-byte slots, 4-byte commit
 * word); `from` is whatever the caller passed. Long words from any even
 * address: the m68k ABI aligns a long to 2, so GCC emits one move.l for an
 * aligned(2) access, and gives anything weaker byte moves and shifts. An odd
 * source is copied a byte at a time.
 */
typedef uint32_t __attribute__((aligned(2), may_alias)) even_u32;

static void
copy_out(volatile uint8_t *to, const void *from, uint32_t length)
{
    uint8_t *out = (uint8_t *)(uintptr_t)to;
    const uint8_t *in = from;
    uint32_t done = 0u;

    if (((uintptr_t)in & 1u) == 0u) {
        for (; done + 4u <= length; done += 4u)
            *(uint32_t *)(void *)(out + done) =
                *(const even_u32 *)(const void *)(in + done);
    }
    for (; done < length; ++done)
        out[done] = in[done];
}

/*
 * Advance producer_position over every committed slot. Any writer may do it;
 * CAS keeps it monotonic when two do it at once.
 *
 * Scans forward verifying each individual slot's own commit word -- the same
 * per-slot check as always, so a record committed out of order (its
 * continuation slots not yet committed) still halts the scan at the first
 * gap -- but makes exactly one CAS per call, from the position it started at
 * straight to the end of the run of slots it verified. Advancing with one CAS
 * per slot instead would make every interior position of a multi-slot record
 * (its continuation slots, already committed before its first slot per the
 * commit loop below) briefly visible in producer_position between those CAS
 * calls; a concurrent reader landing in that window sees a record whose
 * claimed slot count exceeds what producer_position reports and misreads a
 * record still being published as corrupt.
 */
static void
publish(AstraLogProducer *producer)
{
    uint32_t *position =
        (uint32_t *)(uintptr_t)&producer->header->producer_position;

    for (;;) {
        uint32_t at = __atomic_load_n(position, __ATOMIC_ACQUIRE);
        uint32_t reserved = __atomic_load_n(&producer->reserved,
                                            __ATOMIC_ACQUIRE);
        uint32_t end = at;

        while (end != reserved &&
              load32(commit_word(slot_at(producer->slots, end))) == end + 1u)
            ++end;
        if (end == at)
            return;
        (void)cas32(position, at, end);
    }
}

/* The doorbell itself. Never blocks; a gone service marks the producer
 * dead. `dropping` tells the callback this is the drop path. */
static void
ring_doorbell(AstraLogProducer *producer, uint32_t dropping)
{
    if (producer->notify != NULL &&
        producer->notify(producer->context, dropping) != 0u)
        __atomic_store_n(&producer->dead, 1u, __ATOMIC_RELEASE);
}

/*
 * One doorbell per burst: ring only when this record begins where the
 * consumer stopped, i.e. the ring was empty as far as the service knew. While
 * the service is not running its position does not move, so the rest of a
 * burst rings nothing. Concurrent rings are harmless (a full LOG queue already
 * means a wake is pending). The kernel ring object is never notified: the
 * consumer reads published slots from the header directly, and a consumer
 * position reported past the kernel's producer shadow fails the ring.
 */
static void
maybe_notify(AstraLogProducer *producer, uint32_t first)
{
    if (producer->notify == NULL)
        return;
    /* Seq-cst fence: pairs with the consumer's store-then-reload (store
     * consumer_position, fence seq_cst, re-read producer_position --
     * log_ring.h's doorbell pairing contract) so this doorbell decision
     * cannot store-buffer past the consumer's write, the same way
     * astra_log_producer_write's commit/publish fence forbids it between
     * two producers. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (load32(&producer->header->consumer_position) != first)
        return;
    ring_doorbell(producer, 0u);
}

/*
 * A full ring: count the line, and ring the doorbell on the first drop and
 * every ASTRA_LOG_DROP_DOORBELL_INTERVAL after it. A service that died
 * behind never moves consumer_position, so the burst doorbell would never
 * ring again and every line would drop forever. Each drop count is claimed
 * by exactly one CAS winner, so exactly one writer rings per interval.
 */
static uint32_t
drop(AstraLogProducer *producer)
{
    uint32_t *dropped = (uint32_t *)(uintptr_t)
        &ASTRA_LOG_RING_DROPPED(producer->header);
    uint32_t seen = __atomic_load_n(dropped, __ATOMIC_RELAXED);

    while (!cas32(dropped, seen, seen + 1u))
        seen = __atomic_load_n(dropped, __ATOMIC_RELAXED);
    if (seen % ASTRA_LOG_DROP_DOORBELL_INTERVAL == 0u) {
        ring_doorbell(producer, 1u);
        if (__atomic_load_n(&producer->dead, __ATOMIC_ACQUIRE) != 0u)
            return ASTRA_LOG_WRITE_DEAD; /* caller falls back */
    }
    return ASTRA_LOG_WRITE_DROPPED;
}

uint32_t
astra_log_producer_drop_if_full(AstraLogProducer *producer, uint32_t length)
{
    uint32_t slots;

    if (__atomic_load_n(&producer->dead, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_LOG_WRITE_DEAD;
    slots = astra_log_record_slots(length);
    if (__atomic_load_n(&producer->reserved, __ATOMIC_ACQUIRE) + slots -
            load32(&producer->header->consumer_position) <=
        ASTRA_LOG_RING_CAPACITY)
        return ASTRA_LOG_WRITE_OK;
    return drop(producer);
}

uint32_t
astra_log_producer_write(AstraLogProducer *producer,
                         const AstraLogRecordFields *fields,
                         const char *text, uint32_t length)
{
    AstraLogRecordFields header;
    uint32_t slots;
    uint32_t first;
    uint32_t written = 0u;

    if (__atomic_load_n(&producer->dead, __ATOMIC_ACQUIRE) != 0u)
        return ASTRA_LOG_WRITE_DEAD;
    header = *fields;
    header.flags = (uint8_t)(header.flags & ASTRA_LOG_FLAG_WALL_VALID);
    if (length > ASTRA_LOG_TEXT_MAX) {
        length = ASTRA_LOG_TEXT_MAX;
        header.flags |= ASTRA_LOG_FLAG_TRUNCATED;
    }
    slots = astra_log_record_slots(length);
    for (;;) {
        uint32_t consumer = load32(&producer->header->consumer_position);

        first = __atomic_load_n(&producer->reserved, __ATOMIC_ACQUIRE);
        if (first + slots - consumer > ASTRA_LOG_RING_CAPACITY)
            return drop(producer);
        if (cas32(&producer->reserved, first, first + slots))
            break;
    }
    header.sequence = first;
    header.dropped = __atomic_load_n(
        &ASTRA_LOG_RING_DROPPED(producer->header), __ATOMIC_RELAXED);
    header.text_length = (uint16_t)length;
    header.slots = (uint8_t)slots;
    header.reserved = 0u;
    for (uint32_t index = 0u; index < slots; ++index) {
        volatile uint8_t *payload = slot_at(producer->slots, first + index) +
                                    4u;
        uint32_t room = ASTRA_LOG_SLOT_PAYLOAD;
        uint32_t chunk;

        if (index == 0u) {
            copy_out(payload, &header, sizeof(header));
            payload += sizeof(header);
            room = ASTRA_LOG_FIRST_TEXT;
        }
        chunk = length - written < room ? length - written : room;
        copy_out(payload, text + written, chunk);
        written += chunk;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* Continuation slots first, so the first slot's commit means all of it. */
    for (uint32_t index = slots; index-- != 0u;)
        __atomic_store_n(
            (uint32_t *)(uintptr_t)commit_word(
                slot_at(producer->slots, first + index)),
            first + index + 1u, __ATOMIC_RELEASE);
    /* Seq-cst fence: forbids the store-buffering reordering where this
     * writer's commit-word store and publish()'s load of another writer's
     * commit word could each appear to precede the other writer's matching
     * pair, letting both writers miss each other's commit and leave a slot
     * unpublished forever. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    publish(producer);
    maybe_notify(producer, first);
    return ASTRA_LOG_WRITE_OK;
}

static void
copy_in(void *to, const volatile uint8_t *from, uint32_t length)
{
    uint8_t *bytes = to;

    for (uint32_t index = 0u; index < length; ++index)
        bytes[index] = from[index];
}

uint32_t
astra_log_consumer_next(volatile const void *ring_base, uint32_t *position,
                        AstraLogRecordFields *fields, char *text,
                        uint32_t capacity, uint32_t *length)
{
    volatile const AstraBulkRingHeader *header = ring_base;
    volatile uint8_t *slots =
        (volatile uint8_t *)(uintptr_t)ring_base + ASTRA_LOG_RING_HEADER_SIZE;
    uint32_t produced = __atomic_load_n(&header->producer_position,
                                        __ATOMIC_ACQUIRE);
    uint32_t at = *position;
    uint32_t copied = 0u;

    *length = 0u;
    if (produced - at > ASTRA_LOG_RING_CAPACITY)
        return ASTRA_LOG_READ_CORRUPT;
    if (produced == at)
        return ASTRA_LOG_READ_EMPTY;
    copy_in(fields, slot_at(slots, at) + 4u, sizeof(*fields));
    if (fields->slots == 0u || fields->slots > ASTRA_LOG_RECORD_SLOTS_MAX ||
        fields->slots > produced - at ||
        fields->text_length > ASTRA_LOG_TEXT_MAX ||
        astra_log_record_slots(fields->text_length) != fields->slots ||
        fields->stream > ASTRA_LOG_STREAM_MAX ||
        fields->level > ASTRA_EVENT_LEVEL_ERROR)
        return ASTRA_LOG_READ_CORRUPT;
    if (text == NULL && fields->text_length != 0u)
        return ASTRA_LOG_READ_CORRUPT;
    for (uint32_t index = 0u; index < fields->slots; ++index) {
        const volatile uint8_t *payload = slot_at(slots, at + index) + 4u;
        uint32_t room = ASTRA_LOG_SLOT_PAYLOAD;
        uint32_t chunk;

        if (index == 0u) {
            payload += sizeof(*fields);
            room = ASTRA_LOG_FIRST_TEXT;
        }
        chunk = fields->text_length - copied < room ?
            fields->text_length - copied : room;
        if (copied + chunk > capacity)
            chunk = capacity > copied ? capacity - copied : 0u;
        /* text may be NULL when capacity is 0 (caller wants only fields);
         * guard the pointer arithmetic rather than rely on chunk being 0. */
        if (chunk != 0u)
            copy_in(text + copied, payload, chunk);
        copied += chunk;
    }
    *length = copied;
    *position = at + fields->slots;
    return ASTRA_LOG_READ_OK;
}
