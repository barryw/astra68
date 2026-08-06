#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <astra/process.h>
#include <astra/event.h>
#include <astra/event_catalog.h>
#include <astra/event_emit.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

static uint32_t mock_number;
static uint32_t mock_argument0;
static uint32_t mock_argument1;
static uint32_t mock_argument2;
static uint32_t mock_argument3;
static uint32_t mock_argument4;
static uint32_t mock_calls;
static uint32_t mock_status = ASTRA_SYSCALL_OK;

void
astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
               uint32_t argument2, uint32_t argument3, uint32_t argument4,
               AstraSyscallResult *result)
{
    mock_number = number;
    mock_argument0 = argument0;
    mock_argument1 = argument1;
    mock_argument2 = argument2;
    mock_argument3 = argument3;
    mock_argument4 = argument4;
    ++mock_calls;
    /*
     * Every wrapper but the event one and the launch takes a single argument,
     * and the mock has always enforced that. An event carries a message, flags,
     * a buffer and a length; a launch carries an image, its length, a grant
     * array, a count and an argument block -- the one call that fills every
     * register the ABI has.
     */
    if (number != ASTRA_SYSCALL_LOG_WRITE &&
        number != ASTRA_SYSCALL_PROCESS_CREATE &&
        number != ASTRA_SYSCALL_WAIT_ONE) {
        assert(argument1 == 0u);
        assert(argument2 == 0u);
        assert(argument3 == 0u);
    }
    if (number != ASTRA_SYSCALL_PROCESS_CREATE) {
        assert(argument4 == 0u);
    }
    result->status = mock_status;
    result->value0 = ASTRA_SYSCALL_ABI_VERSION;
    result->value1 = 0x11111111u;
    result->value2 = 0x22222222u;
}

static AstraStartupInfo
valid_startup(void)
{
    AstraStartupInfo startup = {0};

    startup.magic = ASTRA_STARTUP_MAGIC;
    startup.abi_version = ASTRA_STARTUP_ABI_VERSION;
    startup.header_size = ASTRA_STARTUP_INFO_SIZE;
    startup.total_size = ASTRA_STARTUP_INFO_SIZE;
    startup.syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION;
    return startup;
}

static void
test_startup_contract(void)
{
    AstraStartupInfo startup = valid_startup();

    assert(astra_startup_validate(&startup));
    assert(!astra_startup_validate(NULL));
    startup.magic = 0u;
    assert(!astra_startup_validate(&startup));
    startup = valid_startup();
    startup.capability_count = ASTRA_STARTUP_CAPABILITY_MAX + 1u;
    assert(!astra_startup_validate(&startup));
    startup = valid_startup();
    startup.argc = 1u;
    assert(!astra_startup_validate(&startup));
    startup.argv_address = 0x1000u;
    assert(astra_startup_validate(&startup));
    startup.reserved[1] = 1u;
    assert(!astra_startup_validate(&startup));
}

static void
test_memory_primitives(void)
{
    char data[16];
    char copy[16];
    char source[8];
    /* Volatile so the compiler cannot prove the truncation it warns about. */
    volatile size_t truncating = 3u;

    assert(memset(data, 'x', sizeof(data)) == data);
    assert(memcpy(copy, data, sizeof(data)) == copy);
    assert(memcmp(copy, data, sizeof(data)) == 0);
    copy[3] = 'y';
    assert(memcmp(copy, data, sizeof(data)) > 0);
    assert(memmove(copy + 2, copy, 8u) == copy + 2);
    assert(copy[2] == 'x' && copy[5] == 'y');
    assert(strlen("astra") == 5u);

    assert(strcmp("astra", "astra") == 0);
    assert(strcmp("astra", "astrb") < 0);
    assert(strcmp("astrb", "astra") > 0);
    assert(strcmp("astra", "astr") > 0);
    assert(strcmp("", "") == 0);

    assert(strncmp("astra", "astrb", 4u) == 0);
    assert(strncmp("astra", "astrb", 5u) < 0);
    assert(strncmp("astra", "astra", 0u) == 0);
    assert(strncmp("ast", "astra", 5u) < 0);

    /*
     * Short source pads to count; long source is truncated and left
     * unterminated. The source is assembled at run time so the compiler
     * cannot fold these into a builtin and warn about intended truncation.
     */
    memset(data, 'z', sizeof(data));
    source[0] = 'a';
    source[1] = 'b';
    source[2] = '\0';
    assert(strncpy(data, source, 5u) == data);
    assert(data[0] == 'a' && data[1] == 'b');
    assert(data[2] == '\0' && data[3] == '\0' && data[4] == '\0');
    assert(data[5] == 'z');

    source[2] = 't';
    source[3] = 'r';
    source[4] = 'a';
    source[5] = '\0';
    source[0] = 'a';
    source[1] = 's';
    memset(copy, 'z', sizeof(copy));
    assert(strncpy(copy, source, truncating) == copy);
    assert(copy[0] == 'a' && copy[1] == 's' && copy[2] == 't');
    assert(copy[3] == 'z');

    /* strcpy terminates and, unlike strncpy, writes nothing past the NUL. */
    memset(data, 'z', sizeof(data));
    assert(strcpy(data, source) == data);
    assert(memcmp(data, "astra", 6u) == 0);
    assert(data[6] == 'z');
    source[0] = '\0';
    assert(strcpy(data, source) == data);
    assert(data[0] == '\0' && data[1] == 's');
}

typedef void (*QsortFn)(void *, size_t, size_t,
                        int (*)(const void *, const void *));

static int
compare_ints(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;

    return a < b ? -1 : (a > b ? 1 : 0);
}

/*
 * The runtime's qsort is heapsort, chosen because lwext4 sorts directory
 * entries read from a volume Astra did not create. The cases that matter are
 * therefore the adversarial shapes — already sorted, reversed, all equal —
 * not a random array.
 */
static void
test_qsort(void)
{
    int values[64];
    int index;
    /*
     * Called through a plain function pointer for the degenerate cases. The
     * host's <stdlib.h> declares qsort with a nonnull attribute, so a direct
     * call with NULL is rejected by -Wnonnull and reported by UBSan — both
     * reacting to the declaration rather than to the implementation under
     * test. The pointer carries no such attribute, so the guard clause that
     * exists to survive these arguments can actually be given them.
     */
    QsortFn const call = qsort;

    /* Degenerate inputs must not touch memory or crash. */
    call(NULL, 4u, sizeof(int), compare_ints);
    call(values, 0u, sizeof(int), compare_ints);
    call(values, 4u, 0u, compare_ints);
    call(values, 4u, sizeof(int), NULL);

    for (index = 0; index < 64; ++index) {
        values[index] = 63 - index; /* reversed */
    }
    qsort(values, 64u, sizeof(int), compare_ints);
    for (index = 0; index < 64; ++index) {
        assert(values[index] == index);
    }

    /* Already sorted. */
    qsort(values, 64u, sizeof(int), compare_ints);
    for (index = 0; index < 64; ++index) {
        assert(values[index] == index);
    }

    /* All equal: every comparison returns 0. */
    for (index = 0; index < 64; ++index) {
        values[index] = 7;
    }
    qsort(values, 64u, sizeof(int), compare_ints);
    for (index = 0; index < 64; ++index) {
        assert(values[index] == 7);
    }

    /* An odd count with duplicates, so sift_down's child bounds get exercised. */
    for (index = 0; index < 37; ++index) {
        values[index] = (index * 17) % 11;
    }
    qsort(values, 37u, sizeof(int), compare_ints);
    for (index = 1; index < 37; ++index) {
        assert(values[index - 1] <= values[index]);
    }

    /* Two elements, both orders: the smallest array the loop can reorder. */
    values[0] = 2;
    values[1] = 1;
    qsort(values, 2u, sizeof(int), compare_ints);
    assert(values[0] == 1 && values[1] == 2);
    qsort(values, 2u, sizeof(int), compare_ints);
    assert(values[0] == 1 && values[1] == 2);
}

static void
test_syscall_wrappers(void)
{
    uint32_t abi;
    uint32_t process;
    uint32_t thread;

    assert(astra_yield() == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_YIELD);
    assert(astra_close(0x12345678u) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_CLOSE);
    assert(mock_argument0 == 0x12345678u);
    assert(astra_query_abi(&abi, &process, &thread) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_QUERY_ABI);
    assert(abi == ASTRA_SYSCALL_ABI_VERSION);
    assert(process == 0x11111111u);
    assert(thread == 0x22222222u);
}

/*
 * The launch, from the launcher's side. What is checked here is the marshalling
 * and nothing else: every rule about what a launch may hand over is the
 * kernel's, and a wrapper that re-decided any of it would be a second answer to
 * a question that must have one.
 *
 * What the wrapper does owe its caller is that the two outputs are never left
 * carrying whatever was in them before. A launcher that reads a handle out of a
 * refused launch closes a handle somebody else is holding.
 */
static void test_launch(void)
{
    static const uint8_t image[64] = {0u};
    AstraLaunchGrant grants[2];
    AstraLaunchArguments arguments;
    uint32_t handle = 0xdeadbeefu;
    uint32_t id = 0xdeadbeefu;
    uint32_t calls;

    memset(grants, 0, sizeof(grants));
    memset(&arguments, 0, sizeof(arguments));

    mock_status = ASTRA_SYSCALL_OK;
    assert(astra_launch(image, (uint32_t)sizeof(image), grants, 2u, &arguments,
                        &handle, &id) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_PROCESS_CREATE);
    assert(mock_argument0 == (uint32_t)(uintptr_t)image);
    assert(mock_argument1 == (uint32_t)sizeof(image));
    assert(mock_argument2 == (uint32_t)(uintptr_t)grants);
    assert(mock_argument3 == 2u);
    assert(mock_argument4 == (uint32_t)(uintptr_t)&arguments);
    /* data[1] is the handle and data[2] the id, in that order. */
    assert(handle == ASTRA_SYSCALL_ABI_VERSION);
    assert(id == 0x11111111u);

    /* Nothing granted and nothing said: three registers that stay zero. */
    assert(astra_launch(image, (uint32_t)sizeof(image), NULL, 0u, NULL,
                        &handle, &id) == ASTRA_SYSCALL_OK);
    assert(mock_argument2 == 0u);
    assert(mock_argument3 == 0u);
    assert(mock_argument4 == 0u);

    /* A refused launch produces no handle and no id, not a stale pair. */
    mock_status = ASTRA_SYSCALL_ACCESS_DENIED;
    handle = 0xdeadbeefu;
    id = 0xdeadbeefu;
    assert(astra_launch(image, (uint32_t)sizeof(image), grants, 1u, NULL,
                        &handle, &id) == ASTRA_SYSCALL_ACCESS_DENIED);
    assert(handle == 0u && id == 0u);
    mock_status = ASTRA_SYSCALL_OK;

    /* Nowhere to put the answer is refused here rather than in the kernel. */
    calls = mock_calls;
    assert(astra_launch(image, (uint32_t)sizeof(image), NULL, 0u, NULL, NULL,
                        &id) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_launch(image, (uint32_t)sizeof(image), NULL, 0u, NULL, &handle,
                        NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(mock_calls == calls);
}

/*
 * Waiting for a child. The wait itself is the one the machine already had; what
 * this adds is that `exit_status` means the child's status and nothing else --
 * a timed-out wait establishes no status, so it publishes none.
 */
static void test_process_wait(void)
{
    static const uint64_t forever =
        ((uint64_t)ASTRA_DEADLINE_NONE_HI << 32) | ASTRA_DEADLINE_NONE_LO;
    uint32_t status = 0xdeadbeefu;
    uint32_t calls;

    mock_status = ASTRA_SYSCALL_OK;
    assert(astra_process_wait(9u, forever, &status) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_WAIT_ONE);
    assert(mock_argument0 == 9u);
    assert(mock_argument1 == ASTRA_DEADLINE_NONE_HI);
    assert(mock_argument2 == ASTRA_DEADLINE_NONE_LO);
    assert(status == ASTRA_SYSCALL_ABI_VERSION);   /* data[1] is the status */

    /*
     * A child that faulted still has a status, and it is the one thing its
     * launcher has to report. PEER_DEAD is how the wait says which it was.
     */
    mock_status = ASTRA_SYSCALL_PEER_DEAD;
    status = 0xdeadbeefu;
    assert(astra_process_wait(9u, 0u, &status) == ASTRA_SYSCALL_PEER_DEAD);
    assert(status == ASTRA_SYSCALL_ABI_VERSION);

    /* A poll that found the child still running says nothing about a status. */
    mock_status = ASTRA_SYSCALL_TIMED_OUT;
    status = 0xdeadbeefu;
    assert(astra_process_wait(9u, 0u, &status) == ASTRA_SYSCALL_TIMED_OUT);
    assert(status == 0u);
    assert(mock_argument1 == 0u && mock_argument2 == 0u);
    mock_status = ASTRA_SYSCALL_OK;

    calls = mock_calls;
    assert(astra_process_wait(9u, 0u, NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(mock_calls == calls);
}


/*
 * The event channel, from both ends: what it sends, and what it refuses
 * without reaching the kernel at all. The refusals matter more than the sends
 * -- a channel that quietly issues a syscall with a bad length is one the
 * kernel has to defend against.
 */
static void test_event_channel(void)
{
    static const char line[] = "volume mounted";
    char oversized[ASTRA_LOG_MAX_BYTES + 32u];
    uint32_t calls;

    /*
     * No binding and no handle. Emitting is universal now: a process that has
     * validated nothing and holds nothing can still say what it is doing.
     */
    mock_status = ASTRA_SYSCALL_OK;
    assert(astra_log_write(line, sizeof(line) - 1u) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_LOG_WRITE);
    assert(mock_argument0 == ASTRA_EVENT_MESSAGE_UNSTRUCTURED);
    assert(mock_argument1 == (ASTRA_EVENT_LEVEL_NOTICE |
                              ASTRA_EVENT_FLAG_INLINE_STRING));
    assert(mock_argument2 == (uint32_t)(uintptr_t)line);
    assert(mock_argument3 == sizeof(line) - 1u);

    /* A refusal is returned, not acted on. */
    mock_status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    assert(astra_log_write(line, 4u) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    mock_status = ASTRA_SYSCALL_OK;

    /* Bad arguments never become a syscall. */
    calls = mock_calls;
    assert(astra_log_write(NULL, 4u) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log_write(line, 0u) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log_write(line, ASTRA_LOG_MAX_BYTES + 1u) ==
           ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log(NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(astra_log("") == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(mock_calls == calls);

    /*
     * A line longer than one event's payload is a chain, not a longer record,
     * and every chunk but the last says so. Five events for 100 bytes: four
     * full ones and a remainder of four.
     */
    calls = mock_calls;
    assert(astra_log_write(oversized, 100u) == ASTRA_SYSCALL_OK);
    assert(mock_calls == calls + 5u);
    assert(mock_argument3 == 100u - 4u * ASTRA_EVENT_ARGUMENT_MAX);
    assert((mock_argument1 & ASTRA_EVENT_FLAG_CONTINUED) == 0u);

    /* One that fits exactly is one event, and it is not continued. */
    calls = mock_calls;
    assert(astra_log_write(oversized, ASTRA_EVENT_ARGUMENT_MAX) ==
           ASTRA_SYSCALL_OK);
    assert(mock_calls == calls + 1u);
    assert((mock_argument1 & ASTRA_EVENT_FLAG_CONTINUED) == 0u);

    /* A chunk refused mid-line stops the line rather than retrying it. */
    mock_status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    calls = mock_calls;
    assert(astra_log_write(oversized, 100u) == ASTRA_SYSCALL_INVALID_ARGUMENT);
    assert(mock_calls == calls + 1u);
    mock_status = ASTRA_SYSCALL_OK;

    /* A string is measured, and one too long is cut rather than refused. */
    assert(astra_log("hello") == ASTRA_SYSCALL_OK);
    assert(mock_argument3 == 5u);
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';
    assert(astra_log(oversized) == ASTRA_SYSCALL_OK);
}

/*
 * The typed event macro. What matters is not that an enabled call site emits
 * -- it is that a disabled one issues no syscall at all, because that is the
 * entire argument for leaving `debug` call sites compiled into shipping code.
 */
static void test_event_macro(void)
{
    uint32_t calls;

    for (uint32_t index = 0u; index < ASTRA_EVENT_SUBSYSTEM_MAX; ++index)
        astra_event_level_set(index, ASTRA_EVENT_LEVEL_INFO);

    /* The message id is the descriptor's address, not a number anyone typed. */
    mock_status = ASTRA_SYSCALL_OK;
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_WARNING,
                 "write refused, status %u after %u sectors", 5u, 128u);
    assert(mock_number == ASTRA_SYSCALL_LOG_WRITE);
    assert(mock_argument0 > ASTRA_EVENT_MESSAGE_RESERVED_MAX);
    assert(mock_argument1 == ASTRA_EVENT_LEVEL_WARNING);
    assert(mock_argument3 == 8u);

    /*
     * The bytes themselves are checked against the packer directly. A host
     * pointer does not survive the 32-bit syscall ABI, so the mock cannot
     * read what it was handed -- which is a property of the host and not of
     * the machine, and the packer is pure and needs no mock anyway.
     */
    {
        const uint32_t values[2] = {5u, 128u};
        uint8_t packed[16] = {0u};

        assert(astra_event_pack(packed, sizeof(packed), values, 2u) == 8u);
        assert(packed[0] == 0u && packed[3] == 5u);
        assert(packed[6] == 0u && packed[7] == 128u);
        /* Big-endian on the wire, whatever this host happens to be. */
        {
            const uint32_t wide[1] = {0x11223344u};

            assert(astra_event_pack(packed, sizeof(packed), wide, 1u) == 4u);
            assert(packed[0] == 0x11u && packed[1] == 0x22u &&
                   packed[2] == 0x33u && packed[3] == 0x44u);
        }
        /* A buffer that cannot hold them packs nothing rather than some. */
        assert(astra_event_pack(packed, 4u, values, 2u) == 0u);
        assert(astra_event_pack(NULL, 16u, values, 2u) == 0u);
    }

    /* Two call sites are two descriptors, so two different message ids. */
    {
        uint32_t first = mock_argument0;

        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_WARNING,
                     "a different message");
        assert(mock_argument0 != first);
        assert(mock_argument3 == 0u);
    }

    /* Below the subsystem's level: one branch, and no syscall. */
    calls = mock_calls;
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_DEBUG,
                 "not emitted %u", 1u);
    assert(mock_calls == calls);

    /* Raising the level opens it, without rebuilding anything. */
    astra_event_level_set(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_DEBUG);
    ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_DEBUG,
                 "emitted now %u", 1u);
    assert(mock_calls == calls + 1u);

    /* One subsystem's level is not another's. */
    calls = mock_calls;
    ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SHELL, ASTRA_EVENT_LEVEL_DEBUG,
                 "still quiet");
    assert(mock_calls == calls);

    /* A level nobody has is refused rather than stored. */
    astra_event_level_set(ASTRA_EVENT_SUBSYSTEM_VFS,
                          ASTRA_EVENT_LEVEL_ERROR + 1u);
    assert(astra_event_levels[ASTRA_EVENT_SUBSYSTEM_VFS] ==
           ASTRA_EVENT_LEVEL_DEBUG);
    astra_event_level_set(ASTRA_EVENT_SUBSYSTEM_MAX, ASTRA_EVENT_LEVEL_ERROR);

    astra_event_level_set(ASTRA_EVENT_SUBSYSTEM_VFS, ASTRA_EVENT_LEVEL_INFO);
}

/*
 * The descriptor is a wire format the catalog tool walks in fixed steps, so
 * its shape is a contract between C and Python and not an implementation
 * detail either of them may change alone.
 */
static void test_event_descriptor_layout(void)
{
    AstraEventDescriptor descriptor;

    assert(sizeof(descriptor) == ASTRA_EVENT_DESCRIPTOR_SIZE);
    assert((char *)&descriptor.magic - (char *)&descriptor == 0);
    assert((char *)&descriptor.line - (char *)&descriptor == 4);
    assert((char *)&descriptor.subsystem - (char *)&descriptor == 6);
    assert((char *)&descriptor.level - (char *)&descriptor == 7);
    assert((char *)&descriptor.argument_count - (char *)&descriptor == 8);
    assert((char *)descriptor.argument_type - (char *)&descriptor == 9);
    assert((char *)descriptor.file - (char *)&descriptor == 16);
    assert((char *)descriptor.format - (char *)&descriptor == 64);
}

/*
 * The catalog on the machine: a division and a bounds check, and a render that
 * walks one string once. The bytes here are built the way objcopy produces
 * them -- descriptors back to back, this build's own byte order -- because that
 * is exactly what the machine will read.
 */
static void set_format(char *out, const char *text)
{
    uint32_t index = 0u;

    while (text[index] != '\0' && index + 1u < ASTRA_EVENT_FORMAT_MAX) {
        out[index] = text[index];
        ++index;
    }
    out[index] = '\0';
}

static void test_event_catalog(void)
{
    static const uint32_t base = 0xE0000000u;
    /* _Alignas is not C11-portable here; a descriptor array is aligned by its
     * own type, which is what the reader requires. */
    AstraEventDescriptor records[3];
    AstraEventCatalog catalog;
    uint8_t payload[8];
    char text[64];

    memset(records, 0, sizeof(records));
    for (uint32_t index = 0u; index < 3u; ++index) {
        records[index].magic = ASTRA_EVENT_DESCRIPTOR_MAGIC;
        records[index].line = (uint16_t)(10u + index);
        records[index].subsystem = ASTRA_EVENT_SUBSYSTEM_STORAGE;
        records[index].level = ASTRA_EVENT_LEVEL_WARNING;
    }
    records[0].argument_count = 0u;
    set_format(records[0].format, "nothing to say");
    records[1].argument_count = 2u;
    records[1].argument_type[0] = ASTRA_EVENT_ARG_U32;
    records[1].argument_type[1] = ASTRA_EVENT_ARG_S32;
    set_format(records[1].format, "write refused, status %u after %d sectors");
    records[2].argument_count = 1u;
    records[2].argument_type[0] = ASTRA_EVENT_ARG_U32;
    set_format(records[2].format, "100%% of %x");

    assert(astra_event_catalog_init(&catalog, records, sizeof(records), base));
    assert(catalog.count == 3u);

    /* An id is an index: base plus the record's offset, and nothing else. */
    assert(astra_event_catalog_lookup(&catalog, base) == &records[0]);
    assert(astra_event_catalog_lookup(&catalog, base + 128u) == &records[1]);
    assert(astra_event_catalog_lookup(&catalog, base - 4u) == NULL);
    assert(astra_event_catalog_lookup(&catalog, base + 384u) == NULL);
    /* An id that does not land on a record was emitted by another image. */
    assert(astra_event_catalog_lookup(&catalog, base + 64u) == NULL);

    (void)astra_event_catalog_render(&catalog, base, 0u, NULL, 0u, text,
                                     sizeof(text));
    assert(strcmp(text, "nothing to say") == 0);

    assert(astra_event_pack(payload, sizeof(payload),
                            (const uint32_t[]){5u, (uint32_t)-3}, 2u) == 8u);
    (void)astra_event_catalog_render(&catalog, base + 128u, 0u, payload, 8u,
                                     text, sizeof(text));
    assert(strcmp(text, "write refused, status 5 after -3 sectors") == 0);

    /* %% is a literal, and hexadecimal is what it says. */
    assert(astra_event_pack(payload, sizeof(payload),
                            (const uint32_t[]){0xbeefu}, 1u) == 4u);
    (void)astra_event_catalog_render(&catalog, base + 256u, 0u, payload, 4u,
                                     text, sizeof(text));
    assert(strcmp(text, "100% of beef") == 0);

    /*
     * A format wanting a value the occurrence does not carry says so. Reading
     * past the payload would be a fault, and printing a zero would be a
     * measurement somebody believes.
     */
    (void)astra_event_catalog_render(&catalog, base + 128u, 0u, NULL, 0u, text,
                                     sizeof(text));
    assert(strcmp(text, "write refused, status <missing> after <missing> "
                        "sectors") == 0);

    /* An inline string is the payload, and the format is not consulted. */
    (void)astra_event_catalog_render(&catalog, base, ASTRA_EVENT_FLAG_INLINE_STRING,
                                     (const uint8_t *)"raw line", 8u, text,
                                     sizeof(text));
    assert(strcmp(text, "raw line") == 0);

    /* An id no catalog holds renders as itself rather than as nothing. */
    (void)astra_event_catalog_render(&catalog, 0x12345678u, 0u, NULL, 0u, text,
                                     sizeof(text));
    assert(strcmp(text, "message 0x12345678") == 0);

    /* Truncation stays inside the buffer and stays terminated. */
    {
        char tiny[8];

        (void)astra_event_catalog_render(&catalog, base, 0u, NULL, 0u, tiny,
                                         sizeof(tiny));
        assert(tiny[sizeof(tiny) - 1u] == '\0');
        assert(strcmp(tiny, "nothing") == 0);
    }

    /*
     * A file that is not a whole number of descriptors, or whose first record
     * is not one, is refused whole. A catalog that is nearly right renders
     * every event wrongly, which is worse than one that fails once.
     */
    assert(!astra_event_catalog_init(&catalog, records, sizeof(records) - 4u,
                                     base));
    assert(catalog.records == NULL);
    records[0].magic = 0u;
    assert(!astra_event_catalog_init(&catalog, records, sizeof(records), base));
    assert(astra_event_catalog_lookup(&catalog, base) == NULL);
}

/* The assertion message: the half that survives when the channel is refused. */
static void test_assert_message(void)
{
    char message[64];
    char tiny[8];
    uint32_t length;

    length = astra_assert_message(message, sizeof(message), "vfs_client.c",
                                  231u, "client != NULL");
    assert(strcmp(message, "vfs_client.c:231: client != NULL") == 0);
    assert(length == strlen(message));

    /* Zero is a line number like any other, not a missing one. */
    (void)astra_assert_message(message, sizeof(message), "a.c", 0u, "x");
    assert(strcmp(message, "a.c:0: x") == 0);

    /* Missing pieces are marked rather than skipped, so the shape holds. */
    (void)astra_assert_message(message, sizeof(message), NULL, 7u, NULL);
    assert(strcmp(message, "?:7: ?") == 0);

    /* Truncation stays inside the buffer and stays terminated. */
    length = astra_assert_message(tiny, sizeof(tiny), "some_long_name.c", 42u,
                                  "condition");
    assert(length == sizeof(tiny) - 1u);
    assert(tiny[sizeof(tiny) - 1u] == '\0');
    assert(strncmp(tiny, "some_lo", 7) == 0);

    /* No buffer, no message, and no write. */
    assert(astra_assert_message(NULL, sizeof(message), "a.c", 1u, "x") == 0u);
    assert(astra_assert_message(message, 0u, "a.c", 1u, "x") == 0u);
}

int
main(void)
{
    test_startup_contract();
    test_memory_primitives();
    test_qsort();
    test_syscall_wrappers();
    test_launch();
    test_process_wait();
    test_event_channel();
    test_event_macro();
    test_event_descriptor_layout();
    test_event_catalog();
    test_assert_message();
    return 0;
}
