#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/event_backend.h>
#include <astra/event_catalog.h>
#include <astra/event_store.h>

#define RECORD_COUNT 64u

static AstraEventStored records[RECORD_COUNT];
static AstraEventStore store;
static AstraEventDescriptor catalog_records[2];
static AstraEventCatalog catalog;
static AstraEventsBackend backend;
static const uint32_t base = 0xE0000000u;

static void
set_text(char *out, uint32_t capacity, const char *text)
{
    uint32_t index = 0u;

    while (text[index] != '\0' && index + 1u < capacity) {
        out[index] = text[index];
        ++index;
    }
    out[index] = '\0';
}

static void
append(uint32_t sequence, uint16_t flags, uint32_t message, uint32_t activity)
{
    AstraEventDrained event;

    memset(&event, 0, sizeof(event));
    event.sequence = sequence;
    event.flags = flags;
    event.message = message;
    event.activity = activity;
    event.process = 0x10000011u;
    event.thread = 16u;
    astra_event_store_append(&store, &event);
}

static void
reset(void)
{
    memset(records, 0, sizeof(records));
    memset(catalog_records, 0, sizeof(catalog_records));
    catalog_records[0].magic = ASTRA_EVENT_DESCRIPTOR_MAGIC;
    catalog_records[0].subsystem = ASTRA_EVENT_SUBSYSTEM_SHELL;
    catalog_records[0].line = 467u;
    set_text(catalog_records[0].file, ASTRA_EVENT_FILE_MAX,
             "src/console_shell.c");
    set_text(catalog_records[0].format, ASTRA_EVENT_FORMAT_MAX,
             "command accepted");
    catalog_records[1].magic = ASTRA_EVENT_DESCRIPTOR_MAGIC;
    catalog_records[1].subsystem = ASTRA_EVENT_SUBSYSTEM_STORAGE;
    catalog_records[1].line = 12u;
    set_text(catalog_records[1].file, ASTRA_EVENT_FILE_MAX, "src/volume.c");
    set_text(catalog_records[1].format, ASTRA_EVENT_FORMAT_MAX,
             "volume refused");

    assert(astra_event_catalog_init(&catalog, catalog_records,
                                    sizeof(catalog_records), base));
    assert(astra_event_store_init(&store, records, RECORD_COUNT, &catalog));
    assert(astra_events_backend_init(&backend, &store, &catalog));
}

/* Reads a whole leaf through the ops, one bounded page at a time. */
static uint32_t
read_all(const char *path, char *out, uint32_t capacity)
{
    const AstraVfsBackendOps *ops = astra_events_backend_ops();
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t total = 0u;

    assert(ops->open(&backend, path, ASTRA_VFS_OPEN_READ, &node, &info) ==
           ASTRA_VFS_OK);
    assert(info.kind == ASTRA_VFS_KIND_FILE);
    /* A leaf has no size before it is read; the contract allows exactly that. */
    assert(info.size == 0u);
    for (;;) {
        uint32_t moved = 0u;
        uint32_t chunk = 24u; /* deliberately smaller than a line */

        if (total + chunk > capacity - 1u) {
            chunk = capacity - 1u - total;
        }
        assert(ops->read(&backend, node, total, &out[total], chunk, &moved) ==
               ASTRA_VFS_OK);
        total += moved;
        if (moved == 0u || total + 1u >= capacity) {
            break;
        }
    }
    out[total] = '\0';
    assert(ops->close(&backend, node) == ASTRA_VFS_OK);
    return total;
}

static void test_a_leaf_renders_its_events(void)
{
    char text[1024];

    reset();
    append(2u, ASTRA_EVENT_LEVEL_INFO, base, 0x1au);
    append(4u, ASTRA_EVENT_LEVEL_WARNING, base + 128u, 0x1au);

    (void)read_all("/boot/current/all", text, sizeof(text));
    /*
     * The whole claim of the design, in one string: the file, the line and the
     * text come from the catalog and cost nothing per occurrence, and the two
     * events of one story sit together.
     */
    assert(strcmp(text,
                  "seq 2  info  10000011/16 act 0000001a  command accepted "
                  "(src/console_shell.c:467)\n"
                  "seq 4  warning  10000011/16 act 0000001a  volume refused "
                  "(src/volume.c:12)\n") == 0);

    /* A page boundary splits between bytes and never loses one. */
    {
        const AstraVfsBackendOps *ops = astra_events_backend_ops();
        AstraVfsNodeInfo info;
        uintptr_t node = 0u;
        char first[16];
        char second[16];
        uint32_t moved = 0u;

        assert(ops->open(&backend, "/boot/current/all", ASTRA_VFS_OPEN_READ,
                         &node, &info) == ASTRA_VFS_OK);
        assert(ops->read(&backend, node, 0u, first, sizeof(first), &moved) ==
               ASTRA_VFS_OK);
        assert(moved == sizeof(first));
        assert(memcmp(first, text, sizeof(first)) == 0);
        assert(ops->read(&backend, node, sizeof(first), second, sizeof(second),
                         &moved) == ASTRA_VFS_OK);
        assert(moved == sizeof(second));
        assert(memcmp(second, &text[sizeof(first)], sizeof(second)) == 0);
        assert(ops->close(&backend, node) == ASTRA_VFS_OK);
    }
}

/*
 * The same bytes whatever the page size. A page that ends mid-line is the
 * ordinary case -- a read is bounded by the protocol, not by the text -- and
 * the resume memo has to be a line boundary or the rest of that line is
 * silently dropped. This is the test that catches it.
 */
static void test_every_page_size_reads_the_same_file(void)
{
    const AstraVfsBackendOps *ops = astra_events_backend_ops();
    char whole[1024];
    char paged[1024];
    uint32_t length;

    reset();
    for (uint32_t sequence = 1u; sequence <= 6u; ++sequence) {
        append(sequence, ASTRA_EVENT_LEVEL_NOTICE, base, sequence);
    }
    length = read_all("/boot/current/all", whole, sizeof(whole));
    assert(length > 0u);

    for (uint32_t chunk = 1u; chunk <= 40u; ++chunk) {
        AstraVfsNodeInfo info;
        uintptr_t node = 0u;
        uint32_t total = 0u;

        assert(ops->open(&backend, "/boot/current/all", ASTRA_VFS_OPEN_READ,
                         &node, &info) == ASTRA_VFS_OK);
        for (;;) {
            uint32_t moved = 0u;

            assert(ops->read(&backend, node, total, &paged[total], chunk,
                             &moved) == ASTRA_VFS_OK);
            total += moved;
            if (moved == 0u) {
                break;
            }
        }
        assert(ops->close(&backend, node) == ASTRA_VFS_OK);
        assert(total == length);
        assert(memcmp(paged, whole, length) == 0);
    }
}

static void test_a_path_is_the_filter(void)
{
    char text[1024];

    reset();
    append(2u, ASTRA_EVENT_LEVEL_INFO, base, 0x1au);         /* shell */
    append(4u, ASTRA_EVENT_LEVEL_WARNING, base + 128u, 0x2bu); /* storage */

    /* A level narrows it. */
    (void)read_all("/boot/current/warning", text, sizeof(text));
    assert(strstr(text, "volume refused") != NULL);
    assert(strstr(text, "command accepted") == NULL);

    /* An activity is one request across every process that touched it. */
    (void)read_all("/activity/0000001a", text, sizeof(text));
    assert(strstr(text, "command accepted") != NULL);
    assert(strstr(text, "volume refused") == NULL);

    /* A subsystem is a catalog lookup, because that is where it lives. */
    (void)read_all("/subsystem/storage", text, sizeof(text));
    assert(strstr(text, "volume refused") != NULL);
    assert(strstr(text, "command accepted") == NULL);

    /* An activity nothing emitted is empty, not an error: the name is valid. */
    assert(read_all("/activity/deadbeef", text, sizeof(text)) == 0u);

    /* A name that is not one of ours is not found. */
    {
        const AstraVfsBackendOps *ops = astra_events_backend_ops();
        AstraVfsNodeInfo info;
        uintptr_t node = 0u;

        assert(ops->open(&backend, "/activity/nope", ASTRA_VFS_OPEN_READ,
                         &node, &info) == ASTRA_VFS_ERR_NOT_FOUND);
        assert(ops->open(&backend, "/subsystem/nothing", ASTRA_VFS_OPEN_READ,
                         &node, &info) == ASTRA_VFS_ERR_NOT_FOUND);
        assert(ops->open(&backend, "/boot/-1/all", ASTRA_VFS_OPEN_READ, &node,
                         &info) == ASTRA_VFS_ERR_NOT_FOUND);
    }
}

static void test_the_tree_lists_itself(void)
{
    const AstraVfsBackendOps *ops = astra_events_backend_ops();
    AstraVfsNodeInfo info;
    char name[64];
    uint64_t cookie = 0u;
    uint32_t entries = 0u;

    reset();
    append(2u, ASTRA_EVENT_LEVEL_INFO, base, 0x1au);
    append(3u, ASTRA_EVENT_LEVEL_INFO, base, 0x1au);
    append(4u, ASTRA_EVENT_LEVEL_INFO, base + 128u, 0x2bu);

    while (ops->readdir(&backend, "/", cookie, name, sizeof(name), &info,
                        &cookie) == ASTRA_VFS_OK) {
        assert(info.kind == ASTRA_VFS_KIND_DIRECTORY);
        ++entries;
    }
    assert(entries == 3u);

    /* One entry per story, named by its first appearance, listed once. */
    cookie = 0u;
    entries = 0u;
    while (ops->readdir(&backend, "/activity", cookie, name, sizeof(name),
                        &info, &cookie) == ASTRA_VFS_OK) {
        if (entries == 0u) {
            assert(strcmp(name, "0000001a") == 0);
        } else {
            assert(strcmp(name, "0000002b") == 0);
        }
        ++entries;
    }
    assert(entries == 2u);

    /* boot/ holds current/ and nothing it cannot answer for. */
    cookie = 0u;
    entries = 0u;
    while (ops->readdir(&backend, "/boot", cookie, name, sizeof(name), &info,
                        &cookie) == ASTRA_VFS_OK) {
        assert(strcmp(name, "current") == 0);
        ++entries;
    }
    assert(entries == 1u);
}

/*
 * Nothing can rm an event. The rights on the binding refuse a write first;
 * this is the second refusal, because a store whose immutability depends on
 * one check has one check to get wrong.
 */
static void test_every_write_verb_is_refused(void)
{
    const AstraVfsBackendOps *ops = astra_events_backend_ops();
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t moved = 7u;

    reset();
    assert(ops->mkdir(&backend, "/anything") == ASTRA_VFS_ERR_ACCESS);
    assert(ops->unlink(&backend, "/boot/current/all") == ASTRA_VFS_ERR_ACCESS);
    assert(ops->open(&backend, "/boot/current/all",
                     ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, &node,
                     &info) == ASTRA_VFS_ERR_ACCESS);
    assert(ops->open(&backend, "/new", ASTRA_VFS_OPEN_CREATE, &node, &info) ==
           ASTRA_VFS_ERR_ACCESS);
    assert(ops->open(&backend, "/boot/current/all", ASTRA_VFS_OPEN_READ, &node,
                     &info) == ASTRA_VFS_OK);
    assert(ops->write(&backend, node, 0u, "x", 1u, &moved) ==
           ASTRA_VFS_ERR_ACCESS);
    assert(moved == 0u);
    assert(ops->close(&backend, node) == ASTRA_VFS_OK);
}

/* The boot ring is a leaf of its own: the earliest events, after eviction. */
static void test_the_earliest_events_survive(void)
{
    char text[2048];
    uint32_t sequence;

    reset();
    append(1u, ASTRA_EVENT_LEVEL_INFO, base, 0u);
    for (sequence = 2u;
         sequence < store.tiers[ASTRA_EVENT_TIER_DETAIL].capacity * 3u;
         ++sequence) {
        append(sequence, ASTRA_EVENT_LEVEL_INFO, base + 128u, 0u);
    }

    /* Evicted from its tier ... */
    (void)read_all("/boot/current/all", text, sizeof(text));
    assert(strstr(text, "seq 1  ") == NULL);
    /* ... and still in the boot ring, which is what the boot ring is for. */
    (void)read_all("/boot/current/earliest", text, sizeof(text));
    assert(strncmp(text, "seq 1  ", 7) == 0);
}

int main(void)
{
    test_a_leaf_renders_its_events();
    test_every_page_size_reads_the_same_file();
    test_a_path_is_the_filter();
    test_the_tree_lists_itself();
    test_every_write_verb_is_refused();
    test_the_earliest_events_survive();
    puts("astra events backend: PASS");
    return 0;
}
