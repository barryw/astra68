#include <astra/log_names.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t next_sequence = 100u;

/* The kernel's chain for `name` (sw/kernel/process.c trace_process_start). */
static uint32_t
chain(AstraEventDrained *out, uint32_t pid, const char *name)
{
    uint32_t length = (uint32_t)strlen(name);
    uint32_t offset = 0u;
    uint32_t count = 0u;

    do {
        AstraEventDrained *record = &out[count++];
        uint32_t chunk = length - offset;

        memset(record, 0, sizeof(*record));
        record->sequence = next_sequence++;
        record->process = pid;
        record->message = ASTRA_EVENT_MESSAGE_PROCESS_START;
        record->activity = offset;
        record->flags = ASTRA_EVENT_LEVEL_INFO |
                        ASTRA_EVENT_FLAG_INLINE_STRING;
        if (chunk > ASTRA_EVENT_ARGUMENT_MAX) {
            chunk = ASTRA_EVENT_ARGUMENT_MAX;
            record->flags |= ASTRA_EVENT_FLAG_CONTINUED;
        }
        memcpy(record->payload, name + offset, chunk);
        record->payload_length = (uint16_t)chunk;
        offset += chunk;
    } while (offset < length);
    return count;
}

static int
feed(AstraLogNames *names, const AstraEventDrained *records, uint32_t count)
{
    int changed = 0;

    for (uint32_t index = 0u; index < count; ++index)
        changed |= astra_log_names_record(names, &records[index]);
    return changed;
}

static AstraLogNames names;
static AstraLogNames decoded;

static void
test_chains_name_pids(void)
{
    AstraEventDrained records[4];
    uint32_t count;

    astra_log_names_reset(&names);
    count = chain(records, 7u, "/system/services/events-service");
    assert(count == 2u);
    /* Split across two drains: the head alone changes nothing. */
    assert(!feed(&names, records, 1u));
    assert(astra_log_names_find(&names, 7u) == NULL);
    assert(feed(&names, records + 1, 1u));
    assert(strcmp(astra_log_names_find(&names, 7u), "events-service") == 0);
    /* exec: the latest name wins. */
    count = chain(records, 7u, "/apps/Terminal.app/zsh");
    assert(feed(&names, records, count));
    assert(strcmp(astra_log_names_find(&names, 7u), "zsh") == 0);
    /* An empty name is one record of length 0. */
    count = chain(records, 9u, "");
    assert(count == 1u && feed(&names, records, count));
    assert(strcmp(astra_log_names_find(&names, 9u), "") == 0);
    /* Other records are not names. */
    records[0].message = ASTRA_EVENT_MESSAGE_UNSTRUCTURED;
    records[0].process = 11u;
    assert(!feed(&names, records, 1u));
    assert(astra_log_names_find(&names, 11u) == NULL);
}

/* A tail whose head the ring overwrote is not a name, nor the start of one. */
static void
test_orphaned_tail_is_discarded(void)
{
    AstraEventDrained records[4];
    uint32_t count;

    astra_log_names_reset(&names);
    count = chain(records, 5u, "/system/services/events-service");
    assert(!feed(&names, records + 1, 1u));
    assert(astra_log_names_find(&names, 5u) == NULL);
    /* A head from another pid, then this tail: still not joined. */
    count = chain(records, 6u, "/system/services/display-service");
    chain(records + 2, 5u, "/system/services/events-service");
    assert(!feed(&names, records, 1u));
    assert(!feed(&names, records + 3, 1u));
    assert(astra_log_names_find(&names, 5u) == NULL &&
           astra_log_names_find(&names, 6u) == NULL);
    /* A tail at the wrong offset for the open chain ends it. */
    count = chain(records, 5u, "/system/services/events-service");
    records[1].activity = 2u * ASTRA_EVENT_ARGUMENT_MAX;
    assert(!feed(&names, records, count));
    assert(astra_log_names_find(&names, 5u) == NULL);
}

/* The least recently used name goes first when the table is full. */
static void
test_full_table_forgets_least_recent(void)
{
    AstraEventDrained record[2];

    astra_log_names_reset(&names);
    for (uint32_t pid = 1u; pid <= ASTRA_LOG_NAMES_MAX; ++pid)
        (void)feed(&names, record, chain(record, pid, "/bin/x"));
    assert(astra_log_names_find(&names, 1u) != NULL); /* used: now newest */
    (void)feed(&names, record, chain(record, 1000u, "/bin/y"));
    assert(astra_log_names_find(&names, 2u) == NULL);
    assert(astra_log_names_find(&names, 1u) != NULL);
    assert(strcmp(astra_log_names_find(&names, 1000u), "y") == 0);
}

static void
test_state_round_trip(void)
{
    static uint8_t bytes[ASTRA_LOG_STATE_BYTES_MAX];
    AstraEventDrained records[2];
    const AstraLogResume saved = {
        0x123456789ull, 4242u, {4242u, 0x2au, 0x10203u, 0xA00000001ull}
    };
    AstraLogResume resume;
    uint32_t length;

    astra_log_names_reset(&names);
    (void)feed(&names, records, chain(records, 1u, "/rom/supervisor"));
    astra_log_names_pin(&names);
    (void)feed(&names, records, chain(records, 0x2au, "/services/desktop"));
    length = astra_log_state_encode(&saved, &names, bytes, sizeof(bytes));
    assert(length == 44u + 2u * (12u + ASTRA_LOG_IDENT_MAX) + 4u);
    assert(astra_log_state_encode(&saved, &names, bytes, length) == 0u);
    length = astra_log_state_encode(&saved, &names, bytes, sizeof(bytes));
    assert(astra_log_state_decode(bytes, length, &resume, &decoded));
    assert(resume.boot == saved.boot && resume.sequence == saved.sequence);
    assert(resume.mark.sequence == saved.mark.sequence &&
           resume.mark.process == saved.mark.process &&
           resume.mark.message == saved.mark.message &&
           resume.mark.stamp == saved.mark.stamp);
    assert(strcmp(astra_log_names_find(&decoded, 1u), "supervisor") == 0);
    assert(strcmp(astra_log_names_find(&decoded, 0x2au), "desktop") == 0);
    /* The pin survives a restart. */
    for (uint32_t index = 0u; index < ASTRA_LOG_NAMES_MAX; ++index)
        if (decoded.entry[index].pid != 0u)
            assert(decoded.entry[index].pinned ==
                   (decoded.entry[index].pid == 1u));
    /* A new name after decode is newer than every decoded one. */
    assert(decoded.clock >= 2u);
    /* Torn, short or foreign bytes are refused whole. */
    bytes[50] ^= 1u;
    assert(!astra_log_state_decode(bytes, length, &resume, &decoded));
    assert(astra_log_names_find(&decoded, 1u) == NULL);
    bytes[50] ^= 1u;
    assert(!astra_log_state_decode(bytes, length - 1u, &resume, &decoded));
    assert(!astra_log_state_decode(bytes, 3u, &resume, &decoded));
    bytes[0] ^= 1u;
    assert(!astra_log_state_decode(bytes, length, &resume, &decoded));
}

static int
named(uint32_t pid, const char *name)
{
    const char *found = astra_log_names_find(&names, pid);

    return found != NULL && strcmp(found, name) == 0;
}

/* The boot set outlives any number of short-lived programs after it. */
static void
test_boot_set_is_pinned(void)
{
    AstraEventDrained record[2];

    astra_log_names_reset(&names);
    (void)feed(&names, record, chain(record, 1u, "/rom/supervisor"));
    (void)feed(&names, record, chain(record, 5u, "/services/desktop"));
    astra_log_names_pin(&names);
    for (uint32_t pid = 100u; pid < 300u; ++pid)
        (void)feed(&names, record, chain(record, pid, "/bin/ls"));
    assert(named(1u, "supervisor"));
    assert(named(5u, "desktop"));
    /* The unpinned still rotate, least recent first. */
    assert(astra_log_names_find(&names, 299u) != NULL);
    assert(astra_log_names_find(&names, 100u) == NULL);
    /* A pinned pid renamed by exec stays pinned. */
    (void)feed(&names, record, chain(record, 5u, "/bin/zsh"));
    for (uint32_t pid = 300u; pid < 500u; ++pid)
        (void)feed(&names, record, chain(record, pid, "/bin/ls"));
    assert(named(5u, "zsh"));
}

static void
test_boot_id(void)
{
    /* Booted at 1000.4 s and 1000.6 s: one second apart at most. */
    assert(astra_log_boot_id(1005400000000ull, 5000000000ull, 1) == 1000u);
    assert(astra_log_boot_id(1005600000000ull, 5000000000ull, 1) == 1001u);
    assert(astra_log_boot_id(1005600000000ull, 5000000000ull, 0) == 0u);
}

static AstraEventDrained
stamped(uint32_t sequence, uint64_t stamp)
{
    AstraEventDrained record;

    memset(&record, 0, sizeof(record));
    record.sequence = sequence;
    record.timestamp_high = (uint32_t)(stamp >> 32);
    record.timestamp_low = (uint32_t)stamp;
    return record;
}

/* A resume point with no mark in the ring: the wall-clock rule. */
static int
by_clock(uint64_t saved_boot, uint32_t saved_sequence, uint64_t boot,
         const AstraLogBootScan *scan)
{
    const AstraLogResume saved = {saved_boot, saved_sequence, {0u, 0u, 0u, 0u}};

    return astra_log_state_restart(&saved, boot, scan);
}

static void
test_boot_scan_and_decision(void)
{
    AstraLogBootScan scan;
    AstraEventDrained record;

    /* One boot in the ring: no boundary. */
    memset(&scan, 0, sizeof(scan));
    for (uint32_t sequence = 1u; sequence <= 50u; ++sequence) {
        record = stamped(sequence, 1000u + sequence);
        astra_log_boot_scan(&scan, &record);
    }
    assert(scan.first == 0u && scan.newest == 50u);
    assert(scan.anchor.sequence == 1u && scan.anchor.stamp == 1001u);
    assert(!scan.found);
    /* A restart in this boot resumes; a later sequence or another boot
     * does not. */
    assert(by_clock(1000u, 30u, 1000u, &scan));
    assert(by_clock(1001u, 30u, 1000u, &scan));
    assert(!by_clock(1002u, 30u, 1000u, &scan));
    assert(!by_clock(1000u, 51u, 1000u, &scan));
    assert(by_clock(1000u, 0u, 1000u, &scan));

    /* A warm reset: the ring carries on, the timebase starts again. */
    for (uint32_t sequence = 51u; sequence <= 60u; ++sequence) {
        record = stamped(sequence, sequence - 50u);
        astra_log_boot_scan(&scan, &record);
    }
    assert(scan.first == 51u && scan.newest == 60u);
    assert(scan.anchor.sequence == 51u && scan.anchor.stamp == 1u);
    /* State from the previous boot, however its clock agrees, is a boot. */
    assert(!by_clock(1000u, 30u, 1000u, &scan));
    assert(by_clock(1000u, 50u, 1000u, &scan));
    assert(by_clock(1000u, 55u, 1000u, &scan));
    /* Sequences wrap. */
    assert(astra_log_sequence_after(1u, 0xFFFFFFFFu));
    assert(!astra_log_sequence_after(0xFFFFFFFFu, 1u));
}

/* Scan a ring of `count` records from `sequence`, stamped from `stamp`,
 * looking for `want`. */
static void
scan_ring(AstraLogBootScan *scan, uint32_t want, uint32_t sequence,
          uint32_t count, uint64_t stamp)
{
    memset(scan, 0, sizeof(*scan));
    scan->want = want;
    for (uint32_t index = 0u; index < count; ++index) {
        AstraEventDrained record = stamped(sequence + index, stamp + index);

        record.process = 7u;
        record.message = 0x1234u;
        astra_log_boot_scan(scan, &record);
    }
}

/* The record at the saved sequence decides, and the wall clock does not. */
static void
test_restart_by_mark(void)
{
    AstraLogBootScan scan;
    AstraEventDrained record = stamped(40u, 5039u);
    AstraLogResume saved;

    record.process = 7u;
    record.message = 0x1234u;
    saved.boot = 1000u;
    saved.sequence = 40u;
    saved.mark = astra_log_mark(&record);
    assert(saved.mark.sequence == 40u && saved.mark.stamp == 5039u &&
           saved.mark.process == 7u && saved.mark.message == 0x1234u);

    /* The clock stepped by an hour since the save; the record is still
     * there, the same one: a restart. */
    scan_ring(&scan, 40u, 1u, 60u, 5000u);
    assert(scan.found && scan.at.stamp == 5039u);
    assert(astra_log_state_restart(&saved, 1000u + 3600u, &scan));
    /* And with no valid clock at all. */
    assert(astra_log_state_restart(&saved, 0u, &scan));

    /* A cold boot without a clock reused the sequence: the record there is
     * another one. A boot, though both boot ids are 0. */
    saved.boot = 0u;
    scan_ring(&scan, 40u, 1u, 60u, 7000u);
    assert(scan.found && scan.at.stamp != saved.mark.stamp);
    assert(!astra_log_state_restart(&saved, 0u, &scan));
    /* Same stamp, another process or message: still another record. */
    scan_ring(&scan, 40u, 1u, 60u, 5000u);
    scan.at.process = 8u;
    assert(!astra_log_state_restart(&saved, 0u, &scan));
    scan_ring(&scan, 40u, 1u, 60u, 5000u);
    scan.at.message = 0x1235u;
    assert(!astra_log_state_restart(&saved, 0u, &scan));

    /* The same record, but a warm reset since: it is an earlier boot's. */
    saved.boot = 1000u;
    scan_ring(&scan, 40u, 1u, 60u, 5000u);
    {
        AstraEventDrained later = stamped(61u, 3u);

        astra_log_boot_scan(&scan, &later);
    }
    assert(scan.found && scan.first == 61u);
    assert(!astra_log_state_restart(&saved, 1000u, &scan));

    /* The ring wrapped past it: the wall-clock rule decides. */
    scan_ring(&scan, 40u, 100u, 60u, 9000u);
    assert(!scan.found);
    saved.sequence = 120u;
    assert(astra_log_state_restart(&saved, 1001u, &scan));
    assert(!astra_log_state_restart(&saved, 1002u, &scan));
}

int
main(void)
{
    test_chains_name_pids();
    test_orphaned_tail_is_discarded();
    test_full_table_forgets_least_recent();
    test_state_round_trip();
    test_boot_set_is_pinned();
    test_restart_by_mark();
    test_boot_id();
    test_boot_scan_and_decision();
    puts("log names tests passed");
    return 0;
}
