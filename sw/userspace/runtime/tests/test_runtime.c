#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/process.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

static uint32_t mock_number;
static uint32_t mock_argument0;

void
astra_syscall5(uint32_t number, uint32_t argument0, uint32_t argument1,
               uint32_t argument2, uint32_t argument3, uint32_t argument4,
               AstraSyscallResult *result)
{
    mock_number = number;
    mock_argument0 = argument0;
    assert(argument1 == 0u);
    assert(argument2 == 0u);
    assert(argument3 == 0u);
    assert(argument4 == 0u);
    result->status = ASTRA_SYSCALL_OK;
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

int
main(void)
{
    test_startup_contract();
    test_memory_primitives();
    test_syscall_wrappers();
    return 0;
}
