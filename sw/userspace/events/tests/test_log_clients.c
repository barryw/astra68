#include <astra/log_clients.h>
#include <astra/log_ring.h>
#include <astra/event.h>
#include <astra/message_abi.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t ring_a[ASTRA_LOG_RING_AREA_SIZE];
static uint8_t ring_b[ASTRA_LOG_RING_AREA_SIZE];
static AstraLogClients clients;
static uint32_t consumed_calls;

static void
format(uint8_t *area)
{
    AstraBulkRingHeader *header = (void *)area;

    memset(area, 0, ASTRA_LOG_RING_AREA_SIZE);
    header->magic = ASTRA_BULK_RING_MAGIC;
    header->element_size = ASTRA_LOG_RING_SLOT_SIZE;
    header->capacity = ASTRA_LOG_RING_CAPACITY;
    header->data_offset = ASTRA_LOG_RING_HEADER_SIZE;
}

static void
emit(uint8_t *area, AstraLogProducer *p, uint64_t monotonic, const char *text)
{
    AstraLogRecordFields f;

    memset(&f, 0, sizeof(f));
    f.monotonic_high = (uint32_t)(monotonic >> 32);
    f.monotonic_low = (uint32_t)monotonic;
    f.level = ASTRA_EVENT_LEVEL_NOTICE;
    f.stream = ASTRA_LOG_STREAM_STDOUT;
    (void)area;
    assert(astra_log_producer_write(p, &f, text, (uint32_t)strlen(text)) ==
           ASTRA_LOG_WRITE_OK);
}

static void
on_consumed(void *context, AstraLogClient *client, uint32_t position)
{
    (void)context;
    ++consumed_calls;
    __atomic_store_n(
        &((AstraBulkRingHeader *)(uintptr_t)client->ring)->consumer_position,
        position, __ATOMIC_RELEASE);
}

/* Same helper the runtime uses to build a SETUP message (log_setup() in
 * sw/userspace/runtime/src/log.c): astra_message_header_set, then the
 * payload fields. */
static AstraLogSetup
setup(const char *ident)
{
    AstraLogSetup s;

    memset(&s, 0, sizeof(s));
    astra_message_header_set(&s.header, sizeof(s), ASTRA_LOG_SETUP_MAGIC,
                             ASTRA_LOG_SETUP_VERSION, ASTRA_LOG_OPERATION_SETUP,
                             0u);
    s.magic = ASTRA_LOG_SETUP_MAGIC;
    s.version = ASTRA_LOG_SETUP_VERSION;
    s.area_size = ASTRA_LOG_RING_AREA_SIZE;
    s.ring_offset = ASTRA_LOG_RING_OFFSET;
    s.slot_size = ASTRA_LOG_RING_SLOT_SIZE;
    s.capacity = ASTRA_LOG_RING_CAPACITY;
    strcpy(s.ident, ident);
    return s;
}

int
main(void)
{
    AstraLogProducer a, b;
    AstraLogSetup sa = setup("doom"), sb = setup("display");
    char out[16384];
    uint32_t day = 0u;
    uint32_t pending = 1u;
    uint32_t n;

    format(ring_a);
    format(ring_b);
    astra_log_producer_init(&a, ring_a, NULL, NULL);
    astra_log_producer_init(&b, ring_b, NULL, NULL);
    assert(astra_log_clients_add(&clients, 0x10000011u, &sa, sizeof(sa),
                                 ring_a, sizeof(ring_a), 5u, 6u) != NULL);
    assert(astra_log_clients_add(&clients, 0x10000012u, &sb, sizeof(sb),
                                 ring_b, sizeof(ring_b), 7u, 8u) != NULL);

    /* Bad setups are refused. */
    {
        AstraLogSetup bad = sa;
        bad.slot_size = 256u;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad) - 1u,
                                     ring_a, sizeof(ring_a), 9u, 10u) == NULL);
        /* No NUL anywhere in `ident`: genuinely unterminated (setting only
         * the last byte would leave the earlier NUL from "doom" intact, and
         * the scan -- like event_catalog.c's terminated() and every other
         * bounded-string check in this codebase -- accepts a NUL anywhere in
         * the field, not just at the end). */
        bad = sa;
        for (uint32_t at = 0u; at < ASTRA_LOG_IDENT_MAX; ++at)
            bad.ident[at] = 'x';
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        assert(astra_log_clients_add(&clients, 1u, &sa, sizeof(sa), ring_a,
                                     4096u, 9u, 10u) == NULL); /* too small */
        /* A payload that still claims the right magic/version but whose
         * message header lies about the operation (or anything else the
         * kernel does not check) is refused, same as any other service's
         * request validator. */
        bad = sa;
        bad.header.operation = ASTRA_LOG_OPERATION_DOORBELL;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        bad.header.protocol_version = ASTRA_LOG_SETUP_VERSION + 1u;
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
        bad = sa;
        bad.header.total_size = sizeof(bad) - 1u; /* header disagrees with wire size */
        assert(astra_log_clients_add(&clients, 1u, &bad, sizeof(bad), ring_a,
                                     sizeof(ring_a), 9u, 10u) == NULL);
    }

    emit(ring_a, &a, 300u, "a-late");
    emit(ring_b, &b, 100u, "b-early");
    emit(ring_a, &a, 200u, "a-middle");
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day, &pending);
    out[n] = '\0';
    /* Merged by monotonic time across clients, each line once. */
    {
        const char *early = strstr(out, "display[10000012] stdout: b-early");
        const char *middle = strstr(out, "doom[10000011] stdout: a-middle");
        const char *late = strstr(out, "doom[10000011] stdout: a-late");

        assert(early && middle && late && early < middle && middle < late);
    }
    assert(consumed_calls == 2u);
    assert(pending == 0u);

    /* Nothing new: no output, no consumed calls, nothing pending. */
    pending = 1u;
    assert(astra_log_clients_drain(&clients, out, sizeof(out), on_consumed,
                                   NULL, &day, &pending) == 0u);
    assert(consumed_calls == 2u);
    assert(pending == 0u);

    /* Drops are announced once, before the next line. */
    a.dropped = 3u;
    emit(ring_a, &a, 400u, "after-drop");
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day, &pending);
    out[n] = '\0';
    assert(strstr(out, "warning doom[10000011] log: [3 lines dropped]\n"));
    assert(strstr(out, "[3 lines dropped]") < strstr(out, "after-drop"));
    assert(pending == 0u);

    /* More records than one drain pass's DRAIN_BATCH cap (128, log_clients.c)
     * leave data unread: `pending` must say so, and the caller must drain
     * again -- "call again until 0 bytes AND pending == 0" -- to reach it. */
    for (uint32_t i = 0u; i < 150u; ++i)
        emit(ring_a, &a, 1000u + i, "x");
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day, &pending);
    assert(n > 0u);
    assert(pending == 1u); /* 22 of the 150 records are still unread */
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day, &pending);
    out[n] = '\0';
    assert(n > 0u);
    assert(pending == 0u);
    n = astra_log_clients_drain(&clients, out, sizeof(out), on_consumed, NULL,
                                &day, &pending);
    assert(n == 0u && pending == 0u); /* now actually caught up */

    /* A corrupt ring marks the client dead and stops reading it. */
    ((AstraBulkRingHeader *)ring_b)->producer_position = 100000u;
    (void)astra_log_clients_drain(&clients, out, sizeof(out), on_consumed,
                                  NULL, &day, &pending);
    assert(clients.client[1].dead);

    puts("LOG CLIENTS PASS");
    return 0;
}
