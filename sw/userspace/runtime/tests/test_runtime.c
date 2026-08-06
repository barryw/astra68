#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

static uint32_t mock_number;
static uint32_t mock_argument0;
static uint32_t mock_argument1;
static uint32_t mock_argument2;
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
    ++mock_calls;
    /*
     * Every wrapper but the diagnostic one takes a single argument, and the
     * mock has always enforced that. The log carries a buffer and a length,
     * so it is the one call where the rest are allowed to be set.
     */
    if (number != ASTRA_SYSCALL_LOG_WRITE) {
        assert(argument1 == 0u);
        assert(argument2 == 0u);
    }
    assert(argument3 == 0u);
    assert(argument4 == 0u);
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
 * The diagnostic channel, from both ends: what it sends when it is allowed to,
 * and what it refuses without reaching the kernel at all. The refusals matter
 * more than the sends -- a channel that quietly issues a syscall with a bad
 * length or an unbound handle is one the kernel has to defend against.
 */
static void test_log_channel(void)
{
    static const char line[] = "volume mounted";
    char oversized[ASTRA_LOG_MAX_BYTES + 32u];
    AstraStartupInfo startup;
    uint32_t calls;

    /* Validating the startup block is what hands the channel its authority. */
    astra_log_bind(0u);
    startup = valid_startup();
    startup.process_handle = 0x1234u;
    assert(astra_startup_validate(&startup) == 1);
    assert(astra_log_handle() == 0x1234u);

    /* A block that fails validation hands over nothing. */
    astra_log_bind(0u);
    startup.magic = 0u;
    assert(astra_startup_validate(&startup) == 0);
    assert(astra_log_handle() == 0u);

    /* Unbound: no authority has been handed over, so nothing is sent. */
    astra_log_bind(0u);
    assert(astra_log_handle() == 0u);
    calls = mock_calls;
    assert(astra_log_write(line, 4u) == ASTRA_SYSCALL_INVALID_HANDLE);
    assert(mock_calls == calls);

    astra_log_bind(0x600du);
    assert(astra_log_handle() == 0x600du);

    /* Bound: the handle, the buffer and the length reach the syscall. */
    mock_status = ASTRA_SYSCALL_OK;
    assert(astra_log_write(line, sizeof(line) - 1u) == ASTRA_SYSCALL_OK);
    assert(mock_number == ASTRA_SYSCALL_LOG_WRITE);
    assert(mock_argument0 == 0x600du);
    assert(mock_argument1 == (uint32_t)(uintptr_t)line);
    assert(mock_argument2 == sizeof(line) - 1u);

    /* A refusal is returned, not acted on. */
    mock_status = ASTRA_SYSCALL_ACCESS_DENIED;
    assert(astra_log_write(line, 4u) == ASTRA_SYSCALL_ACCESS_DENIED);
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

    /* A string is measured, and one too long is cut rather than refused. */
    assert(astra_log("hello") == ASTRA_SYSCALL_OK);
    assert(mock_argument2 == 5u);
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';
    assert(astra_log(oversized) == ASTRA_SYSCALL_OK);
    assert(mock_argument2 == ASTRA_LOG_MAX_BYTES);
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
    test_log_channel();
    test_assert_message();
    return 0;
}
