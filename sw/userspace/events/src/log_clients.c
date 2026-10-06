/*
 * Every process's ring, drained into one time-ordered batch. The merge is per
 * drain pass: records within a pass are ordered by monotonic time across
 * processes, and passes follow one another, so the file is in order to within
 * one pass (docs/SYSTEM_LOG.md section 5).
 */

#include <astra/log_clients.h>

#include <astra/event.h>
#include <astra/log_format.h>
#include <astra/log_ring.h>
#include <astra/message_abi.h>
#include <astra/object_abi.h>

#include <stddef.h>

#define DRAIN_BATCH 128u

typedef struct Pending {
    AstraLogRecordFields fields;
    AstraLogClient *client;
    uint32_t length;
    char text[ASTRA_LOG_TEXT_MAX];
} Pending;

/* 1,944 text bytes * 128 entries: ~250 KiB of BSS. Measured on events.elf in
 * Task 7; see the task 6 report for the exact delta. */
static Pending batch[DRAIN_BATCH];
static Pending *order[DRAIN_BATCH];

static uint64_t
monotonic(const AstraLogRecordFields *f)
{
    return ((uint64_t)f->monotonic_high << 32) | f->monotonic_low;
}

AstraLogClient *
astra_log_clients_add(AstraLogClients *clients, uint32_t pid,
                      const AstraLogSetup *setup, uint32_t setup_size,
                      volatile const void *ring, uint32_t mapped_size,
                      uint32_t consumer_handle, uint32_t area_handle)
{
    const volatile AstraBulkRingHeader *header = ring;
    uint32_t terminated = 0u;

    if (clients == NULL || setup == NULL || ring == NULL ||
        setup_size != sizeof(*setup) ||
        /* Wire header: the kernel only guarantees a port message begins with
         * a valid AstraMessageHeader (log_ring.h); the protocol fields are
         * this service's own to check, the same way every other service's
         * request validator does (see astra_entropy_request_valid). */
        setup->header.total_size != setup_size ||
        setup->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        setup->header.flags != 0u ||
        setup->header.reserved != 0u ||
        setup->header.protocol != ASTRA_LOG_SETUP_MAGIC ||
        setup->header.protocol_version != ASTRA_LOG_SETUP_VERSION ||
        setup->header.operation != ASTRA_LOG_OPERATION_SETUP ||
        /* Payload: this build's fixed ring layout, restated by the process. */
        setup->magic != ASTRA_LOG_SETUP_MAGIC ||
        setup->version != ASTRA_LOG_SETUP_VERSION ||
        setup->area_size != ASTRA_LOG_RING_AREA_SIZE ||
        setup->ring_offset != ASTRA_LOG_RING_OFFSET ||
        setup->slot_size != ASTRA_LOG_RING_SLOT_SIZE ||
        setup->capacity != ASTRA_LOG_RING_CAPACITY ||
        mapped_size < ASTRA_LOG_RING_AREA_SIZE ||
        header->magic != ASTRA_BULK_RING_MAGIC ||
        header->element_size != ASTRA_LOG_RING_SLOT_SIZE ||
        header->capacity != ASTRA_LOG_RING_CAPACITY ||
        header->data_offset != ASTRA_LOG_RING_HEADER_SIZE)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LOG_IDENT_MAX; ++index)
        if (setup->ident[index] == '\0')
            terminated = 1u;
    if (!terminated)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        AstraLogClient *client = &clients->client[index];

        if (client->used)
            continue;
        client->used = 1u;
        client->pid = pid;
        client->consumer_handle = consumer_handle;
        client->area_handle = area_handle;
        client->ring = ring;
        client->position = header->consumer_position;
        client->last_dropped = 0u;
        client->dead = 0u;
        for (uint32_t at = 0u; at < ASTRA_LOG_IDENT_MAX; ++at)
            client->ident[at] = setup->ident[at];
        return client;
    }
    return NULL;
}

static uint32_t
append_line(char *out, uint32_t capacity, uint32_t used,
            const AstraLogRecordFields *f, const AstraLogClient *client,
            const char *text, uint32_t length, uint32_t *day)
{
    AstraLogLine line;
    uint32_t local_day;

    line.monotonic_ns = monotonic(f);
    line.wall_ns = ((uint64_t)f->wall_high << 32) | f->wall_low;
    line.utc_offset = f->utc_offset;
    line.zone = f->zone;
    line.wall_valid = (f->flags & ASTRA_LOG_FLAG_WALL_VALID) != 0u;
    line.level = f->level;
    line.stream = f->stream;
    line.truncated = (f->flags & ASTRA_LOG_FLAG_TRUNCATED) != 0u;
    line.pid = client->pid;
    line.ident = client->ident;
    line.text = text;
    line.length = length;
    local_day = astra_log_local_day(&line);
    if (local_day != 0u)
        *day = local_day;
    if (used >= capacity)
        return used;
    return used + astra_log_format(out + used, capacity - used, &line);
}

static uint32_t
dropped_text(char *out, uint32_t count)
{
    char digits[10];
    uint32_t n = 0u;
    uint32_t used = 0u;
    static const char tail[] = " lines dropped]";

    out[used++] = '[';
    do { digits[n++] = (char)('0' + count % 10u); count /= 10u; }
    while (count != 0u);
    while (n != 0u)
        out[used++] = digits[--n];
    for (uint32_t index = 0u; tail[index] != '\0'; ++index)
        out[used++] = tail[index];
    return used;
}

uint32_t
astra_log_clients_drain(AstraLogClients *clients, char *out,
                        uint32_t capacity, AstraLogConsumed consumed,
                        void *context, uint32_t *day, uint32_t *pending)
{
    uint32_t count = 0u;
    uint32_t used = 0u;

    for (uint32_t index = 0u;
         index < ASTRA_LOG_CLIENTS_MAX && count < DRAIN_BATCH; ++index) {
        AstraLogClient *client = &clients->client[index];
        uint32_t start;

        if (!client->used || client->dead)
            continue;
        start = client->position;
        while (count < DRAIN_BATCH) {
            Pending *p = &batch[count];
            uint32_t status = astra_log_consumer_next(
                client->ring, &client->position, &p->fields, p->text,
                sizeof(p->text), &p->length);

            if (status == ASTRA_LOG_READ_EMPTY)
                break;
            if (status == ASTRA_LOG_READ_CORRUPT) {
                client->dead = 1u;
                break;
            }
            p->client = client;
            order[count] = p;
            ++count;
        }
        if (client->position != start)
            consumed(context, client, client->position);
    }
    /* Insertion sort: a batch is at most DRAIN_BATCH and nearly sorted (each
     * client's own records already arrive in order). Stable, so one
     * process's lines keep their order on equal timestamps. */
    for (uint32_t i = 1u; i < count; ++i) {
        Pending *item = order[i];
        uint32_t j = i;

        while (j != 0u &&
               monotonic(&order[j - 1u]->fields) > monotonic(&item->fields)) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = item;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        Pending *p = order[i];
        AstraLogClient *client = p->client;

        if (p->fields.dropped != client->last_dropped) {
            AstraLogRecordFields note = p->fields;
            char text[32];
            uint32_t length = dropped_text(
                text, p->fields.dropped - client->last_dropped);

            note.level = ASTRA_EVENT_LEVEL_WARNING;
            note.stream = ASTRA_LOG_STREAM_LOG;
            note.flags = (uint8_t)(note.flags & ~ASTRA_LOG_FLAG_TRUNCATED);
            used = append_line(out, capacity, used, &note, client, text,
                               length, day);
            client->last_dropped = p->fields.dropped;
        }
        used = append_line(out, capacity, used, &p->fields, client, p->text,
                           p->length, day);
    }
    /*
     * Doorbell pairing (log_ring.h): every `consumed` call above already
     * stored its client's consumer_position (release). The matching seq_cst
     * fence goes here, once, after every store -- then re-reading each live
     * client's producer_position cannot miss a producer's doorbell decision
     * that raced those stores, the same store-then-reload shape mirrored
     * from the producer's maybe_notify().
     */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *pending = 0u;
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        const AstraLogClient *client = &clients->client[index];
        const volatile AstraBulkRingHeader *header;

        if (!client->used || client->dead)
            continue;
        header = client->ring;
        if (__atomic_load_n(&header->producer_position, __ATOMIC_ACQUIRE) !=
            client->position)
            *pending = 1u;
    }
    return used;
}
