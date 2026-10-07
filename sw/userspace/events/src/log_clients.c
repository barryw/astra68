/*
 * Every process's ring, and the kernel ring's text lines, drained into one
 * time-ordered batch. The merge is per drain pass: records within a pass are
 * ordered by monotonic time across processes and the kernel ring, and passes
 * follow one another, so the file is in order to within one pass
 * (docs/SYSTEM_LOG.md section 5).
 */

#include <astra/log_clients.h>

#include <astra/event.h>
#include <astra/log_format.h>
#include <astra/log_ring.h>
#include <astra/message_abi.h>
#include <astra/object_abi.h>

#include <stddef.h>

/* A marker's text: '[' + up to ten digits + " lines dropped]". */
#define DROP_TEXT_MAX 26u
/* Every record reserves at least ASTRA_LOG_LINE_BYTES(0), so no pass can
 * accept more records than this. */
#define DRAIN_RECORDS (ASTRA_LOG_DRAIN_OUT_BYTES / ASTRA_LOG_LINE_BYTES(0u))
/* The smallest `out` a pass accepts: the longest record and its marker. */
#define DRAIN_OUT_MIN (ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX) + \
                       ASTRA_LOG_LINE_BYTES(DROP_TEXT_MAX))

/* The first sweep gives every live client one record before the cap. */
_Static_assert(ASTRA_LOG_CLIENTS_MAX <= DRAIN_RECORDS,
               "one pass's record cap holds a record from every client");

_Static_assert(ASTRA_LOG_DRAIN_OUT_BYTES >=
               ASTRA_LOG_LINE_BYTES(ASTRA_LOG_TEXT_MAX) +
               ASTRA_LOG_LINE_BYTES(DROP_TEXT_MAX),
               "one pass always fits the longest record and its drop marker");

typedef struct Pending {
    AstraLogRecordFields fields;
    AstraLogClient *client;
    const AstraLogLine *line; /* a caller's kernel-ring line, or NULL */
    uint32_t offset;        /* text's start in `arena` */
    uint32_t length;
    uint32_t dropped_delta; /* drops to announce before this record */
    uint32_t marker_only;   /* the ring's end: the announcement alone */
} Pending;

/*
 * Text is copied once, into an arena no larger than the output budget: a
 * record's raw text is never longer than the formatted line it reserved, so
 * whatever passes the reservation has room here too.
 */
static char arena[ASTRA_LOG_DRAIN_OUT_BYTES];
static Pending batch[DRAIN_RECORDS];
static Pending *order[DRAIN_RECORDS];

static uint64_t
monotonic(const AstraLogRecordFields *f)
{
    return ((uint64_t)f->monotonic_high << 32) | f->monotonic_low;
}

static uint64_t
pending_time(const Pending *p)
{
    return p->line != NULL ? p->line->monotonic_ns : monotonic(&p->fields);
}

AstraLogClient *
astra_log_clients_add(AstraLogClients *clients, uint32_t pid,
                      const AstraLogSetup *setup, uint32_t setup_size,
                      volatile const void *ring, uint32_t mapped_size,
                      uint32_t consumer_handle, uint32_t area_handle)
{
    const volatile AstraBulkRingHeader *header = ring;
    uint32_t terminated = 0u;
    uint32_t held = 0u;

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
        mapped_size != ASTRA_LOG_RING_AREA_SIZE ||
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
    /* More rings than the runtime ever makes is not a process logging. */
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index)
        if (clients->client[index].used && clients->client[index].pid == pid)
            ++held;
    if (held >= ASTRA_LOG_SETUPS_MAX)
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
        for (uint32_t at = 0u; at < sizeof(client->last); ++at)
            ((uint8_t *)&client->last)[at] = 0u;
        for (uint32_t at = 0u; at < ASTRA_LOG_IDENT_MAX; ++at)
            client->ident[at] = setup->ident[at];
        return client;
    }
    return NULL;
}

/* `out` always has room: the drain reserved ASTRA_LOG_LINE_BYTES for this
 * line before it read the record. */
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
    used += astra_log_format(out + used, capacity - used, &line);
    local_day = astra_log_local_day(&line);
    if (local_day != 0u)
        *day = local_day;
    return used;
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

/* Drops the ring header counts that this table has not announced. Only
 * forward, like a record's count: a smaller value is not a new drop. */
static uint32_t
unannounced(const AstraLogClient *client, uint32_t *dropped)
{
    *dropped = __atomic_load_n(
        &ASTRA_LOG_RING_DROPPED((const volatile AstraBulkRingHeader *)
                                client->ring), __ATOMIC_ACQUIRE);
    return (int32_t)(*dropped - client->last_dropped) > 0 ?
        *dropped - client->last_dropped : 0u;
}

/*
 * Read `client`'s records into the batch, in ring order, while each one's
 * worst-case line -- plus a drop marker when it carries new drops -- fits the
 * `budget` still unreserved. Past the first record, a client stops at
 * `quota` bytes of its own. A record that does not fit stays in the ring,
 * unread, so nothing is consumed that this pass cannot write. Returns
 * nonzero when the client was stopped for want of room in the budget.
 *
 * The drop delta is taken here, in ring order, and only moves forward: a
 * multithreaded producer samples `dropped` after reserving its slot, so a
 * record can carry a smaller count than the one before it, and that is not
 * a new drop.
 */
static uint32_t
read_client(AstraLogClient *client, uint32_t quota, uint32_t budget,
            uint32_t *reserved, uint32_t *count, uint32_t *arena_used)
{
    uint32_t taken = 0u;

    for (;;) {
        Pending *p;
        uint32_t position = client->position;
        uint32_t delta;
        uint32_t need;
        uint32_t status;

        /* The record cap is a refusal for room like the budget's, so the
         * next pass starts here. */
        if (*count >= DRAIN_RECORDS)
            return 1u;
        p = &batch[*count];
        status = astra_log_consumer_next(
            client->ring, &position, &p->fields, arena + *arena_used,
            ASTRA_LOG_DRAIN_OUT_BYTES - *arena_used, &p->length);

        if (status == ASTRA_LOG_READ_EMPTY) {
            uint32_t dropped;

            /* The end of the ring: drops no record carries yet -- the
             * program may never write another -- are announced here, after
             * its last line, timed with it. */
            delta = unannounced(client, &dropped);
            if (delta == 0u)
                return 0u;
            need = ASTRA_LOG_LINE_BYTES(DROP_TEXT_MAX);
            if (need > budget - *reserved)
                return 1u;
            client->last_dropped = dropped;
            p->fields = client->last;
            p->client = client;
            p->line = NULL;
            p->offset = *arena_used;
            p->length = 0u;
            p->dropped_delta = delta;
            p->marker_only = 1u;
            *reserved += need;
            order[*count] = p;
            ++*count;
            return 0u;
        }
        if (status == ASTRA_LOG_READ_CORRUPT) {
            client->dead = 1u;
            return 0u;
        }
        delta = (int32_t)(p->fields.dropped - client->last_dropped) > 0 ?
            p->fields.dropped - client->last_dropped : 0u;
        need = ASTRA_LOG_LINE_BYTES(p->fields.text_length) +
            (delta != 0u ? ASTRA_LOG_LINE_BYTES(DROP_TEXT_MAX) : 0u);
        if (need > budget - *reserved)
            return 1u;
        if (taken != 0u && taken + need > quota)
            return 0u;
        /* Accepted: need <= unreserved budget <= free arena, so the whole
         * text was copied (p->length == text_length). */
        client->position = position;
        client->last = p->fields;
        if (delta != 0u)
            client->last_dropped = p->fields.dropped;
        p->client = client;
        p->line = NULL;
        p->offset = *arena_used;
        p->dropped_delta = delta;
        p->marker_only = 0u;
        *arena_used += p->length;
        taken += need;
        *reserved += need;
        order[*count] = p;
        ++*count;
    }
}

/*
 * The caller's kernel-ring lines, in order, into the batch: the first always
 * (the budget holds the longest line), the rest while they stay within half
 * the budget, so ring clients keep the other half.
 */
static void
read_extra(const AstraLogLine *extra, uint32_t extra_count, uint32_t budget,
           uint32_t *reserved, uint32_t *count, uint32_t *taken)
{
    for (*taken = 0u; *taken < extra_count && *count < DRAIN_RECORDS;
         ++*taken) {
        const AstraLogLine *line = &extra[*taken];
        uint32_t length = line->length < ASTRA_LOG_TEXT_MAX ?
            line->length : ASTRA_LOG_TEXT_MAX;
        uint32_t need = ASTRA_LOG_LINE_BYTES(length);
        Pending *p = &batch[*count];

        if (*taken != 0u && *reserved + need > budget / 2u)
            return;
        p->client = NULL;
        p->line = line;
        p->dropped_delta = 0u;
        p->marker_only = 0u;
        *reserved += need;
        order[*count] = p;
        ++*count;
    }
}

uint32_t
astra_log_clients_drain(AstraLogClients *clients, const AstraLogLine *extra,
                        uint32_t extra_count, uint32_t *extra_taken,
                        char *out, uint32_t capacity,
                        AstraLogConsumed consumed, void *context,
                        uint32_t *day, uint32_t *pending)
{
    static uint32_t start[ASTRA_LOG_CLIENTS_MAX];
    uint32_t budget = capacity < ASTRA_LOG_DRAIN_OUT_BYTES ?
        capacity : ASTRA_LOG_DRAIN_OUT_BYTES;
    uint32_t count = 0u;
    uint32_t used = 0u;
    uint32_t reserved = 0u;
    uint32_t arena_used = 0u;
    uint32_t live = 0u;
    uint32_t cut = ASTRA_LOG_CLIENTS_MAX;
    uint32_t cursor = clients->cursor % ASTRA_LOG_CLIENTS_MAX;

    uint32_t taken = 0u;

    *pending = 0u;
    if (extra_taken != NULL)
        *extra_taken = 0u;
    if (capacity < DRAIN_OUT_MIN)
        return 0u;
    if (extra != NULL)
        read_extra(extra, extra_count, budget, &reserved, &count, &taken);
    if (extra_taken != NULL)
        *extra_taken = taken;
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        const AstraLogClient *client = &clients->client[index];

        start[index] = client->position;
        if (client->used && !client->dead)
            ++live;
    }
    /*
     * Two sweeps from the cursor. The first gives every live client an
     * equal share of what the kernel-ring lines left of the budget (and
     * always its first record, if that holds it), so one process flooding
     * its ring cannot crowd out another; the second hands what is left over
     * in the same order. The first client refused for want of room starts
     * the next pass, where at least half the budget is open to it.
     */
    for (uint32_t sweep = 0u; sweep < 2u && live != 0u; ++sweep) {
        uint32_t quota = sweep == 0u ? (budget - reserved) / live :
                                       UINT32_MAX;

        for (uint32_t step = 0u; step < ASTRA_LOG_CLIENTS_MAX; ++step) {
            uint32_t index = (cursor + step) % ASTRA_LOG_CLIENTS_MAX;
            AstraLogClient *client = &clients->client[index];

            if (!client->used || client->dead)
                continue;
            if (read_client(client, quota, budget, &reserved, &count,
                            &arena_used) &&
                cut == ASTRA_LOG_CLIENTS_MAX)
                cut = index;
        }
    }
    if (cut != ASTRA_LOG_CLIENTS_MAX)
        clients->cursor = cut;
    for (uint32_t index = 0u; index < ASTRA_LOG_CLIENTS_MAX; ++index) {
        AstraLogClient *client = &clients->client[index];

        if (client->used && client->position != start[index])
            consumed(context, client, client->position);
    }
    /* Insertion sort: a batch is at most DRAIN_RECORDS and nearly sorted
     * (each client's own records, and the kernel ring's, already arrive in
     * order). Stable, so one source's lines keep their order on equal
     * timestamps. */
    for (uint32_t i = 1u; i < count; ++i) {
        Pending *item = order[i];
        uint32_t j = i;

        while (j != 0u && pending_time(order[j - 1u]) > pending_time(item)) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = item;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        Pending *p = order[i];
        AstraLogClient *client = p->client;

        if (p->line != NULL) {
            AstraLogLine line = *p->line;
            uint32_t local_day;

            if (line.length > ASTRA_LOG_TEXT_MAX)
                line.length = ASTRA_LOG_TEXT_MAX;
            used += astra_log_format(out + used, capacity - used, &line);
            local_day = astra_log_local_day(&line);
            if (local_day != 0u)
                *day = local_day;
            continue;
        }
        if (p->dropped_delta != 0u) {
            AstraLogRecordFields note = p->fields;
            char text[DROP_TEXT_MAX];
            uint32_t length = dropped_text(text, p->dropped_delta);

            note.level = ASTRA_EVENT_LEVEL_WARNING;
            note.stream = ASTRA_LOG_STREAM_LOG;
            note.flags = (uint8_t)(note.flags & ~ASTRA_LOG_FLAG_TRUNCATED);
            used = append_line(out, capacity, used, &note, client, text,
                               length, day);
        }
        if (p->marker_only)
            continue;
        used = append_line(out, capacity, used, &p->fields, client,
                           arena + p->offset, p->length, day);
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

        if (client->used && !client->dead &&
            !astra_log_client_caught_up(client))
            *pending = 1u;
    }
    return used;
}

uint32_t
astra_log_client_caught_up(const AstraLogClient *client)
{
    const volatile AstraBulkRingHeader *header = client->ring;

    uint32_t dropped;

    return __atomic_load_n(&header->producer_position, __ATOMIC_ACQUIRE) ==
               client->position &&
           unannounced(client, &dropped) == 0u;
}
