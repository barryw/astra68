#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <supervisor.h>

#include <astra/runtime.h>
#include <astra/syscall.h>

#define PROCESS_HANDLE 0x11111111u
#define THREAD_HANDLE 0x22222222u

static void
valid_startup(AstraStartupInfo *result)
{
    AstraStartupInfo startup;

    memset(&startup, 0, sizeof(startup));
    startup.magic = ASTRA_STARTUP_MAGIC;
    startup.abi_version = ASTRA_STARTUP_ABI_VERSION;
    startup.program_entry = 0x00100000u;
    startup.header_size = ASTRA_STARTUP_INFO_SIZE;
    startup.total_size = ASTRA_STARTUP_INFO_SIZE +
                         (2u * ASTRA_STARTUP_CAPABILITY_SIZE);
    startup.syscall_abi_version = ASTRA_SYSCALL_ABI_VERSION;
    startup.process_handle = PROCESS_HANDLE;
    startup.thread_handle = THREAD_HANDLE;
    startup.capability_count = 2u;
    startup.capabilities_address =
        (uint32_t)(uintptr_t)result + ASTRA_STARTUP_INFO_SIZE;
    *result = startup;
}

static void
valid_capabilities(AstraStartupCapability *capabilities)
{
    memset(capabilities, 0, 2u * sizeof(*capabilities));
    astra_capability_name_set(capabilities[0].name,
                              ASTRA_CAPABILITY_PROCESS);
    capabilities[0].handle = PROCESS_HANDLE;
    capabilities[0].rights = 1u;
    astra_capability_name_set(capabilities[1].name,
                              ASTRA_CAPABILITY_THREAD);
    capabilities[1].handle = THREAD_HANDLE;
    capabilities[1].rights = 1u;
}

static SupervisorProbe
valid_probe(void)
{
    SupervisorProbe probe;

    memset(&probe, 0, sizeof(probe));
    probe.query_status = ASTRA_SYSCALL_OK;
    probe.abi_version = ASTRA_SYSCALL_ABI_VERSION;
    probe.process_handle = PROCESS_HANDLE;
    probe.thread_handle = THREAD_HANDLE;
    probe.info_status = ASTRA_SYSCALL_OK;
    probe.info.size = ASTRA_PROCESS_INFO_SIZE;
    probe.info.id = 0x80000011u;
    probe.info.generation = 1u;
    probe.info.owner = 0x80000011u;
    probe.info.resident_frames = 5u;
    probe.info.thread_count = 1u;
    probe.info.live_threads = 1u;
    probe.info.process_state = 2u;
    return probe;
}

static void
test_accepts_a_correct_launch(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe = valid_probe();

    valid_startup(&startup);
    valid_capabilities(capabilities);
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           ASTRA_SUPERVISOR_STATUS_OK);
}

static void
test_startup_failures_stop_everything(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe = valid_probe();

    valid_startup(&startup);
    valid_capabilities(capabilities);
    assert(supervisor_validate(NULL, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_STARTUP));
    assert(supervisor_validate(&startup, capabilities, NULL) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_STARTUP));

    startup.magic = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_STARTUP));

    /* A launch without self handles is not a launch this service survives. */
    valid_startup(&startup);
    startup.process_handle = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_STARTUP));
    valid_startup(&startup);
    startup.thread_handle = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_STARTUP));
}

static void
test_abi_disagreements(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe;

    valid_startup(&startup);
    valid_capabilities(capabilities);

    probe = valid_probe();
    probe.query_status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_QUERY_ABI));

    probe = valid_probe();
    probe.abi_version = ASTRA_SYSCALL_ABI_VERSION + 1u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_ABI_VERSION));

    probe = valid_probe();
    probe.process_handle = PROCESS_HANDLE + 1u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_SELF_HANDLES));

    probe = valid_probe();
    probe.thread_handle = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_SELF_HANDLES));
}

static void
test_capability_table(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe = valid_probe();
    const uint32_t expected =
        ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_CAPABILITIES;

    valid_startup(&startup);
    valid_capabilities(capabilities);
    assert(supervisor_validate(&startup, NULL, &probe) == expected);

    valid_startup(&startup);
    startup.capability_count = 1u;
    startup.total_size = ASTRA_STARTUP_INFO_SIZE + ASTRA_STARTUP_CAPABILITY_SIZE;
    assert(supervisor_validate(&startup, capabilities, &probe) == expected);

    valid_startup(&startup);
    valid_capabilities(capabilities);
    capabilities[0].handle = PROCESS_HANDLE + 1u;
    assert(supervisor_validate(&startup, capabilities, &probe) == expected);

    valid_capabilities(capabilities);
    capabilities[1].rights = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) == expected);

    valid_capabilities(capabilities);
    astra_capability_name_set(capabilities[1].name,
                              ASTRA_CAPABILITY_PROCESS);
    assert(supervisor_validate(&startup, capabilities, &probe) == expected);
}

static void
test_process_view(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe;
    const uint32_t content =
        ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_INFO_CONTENT;

    valid_startup(&startup);
    valid_capabilities(capabilities);

    probe = valid_probe();
    probe.info_status = ASTRA_SYSCALL_INVALID_HANDLE;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG |
            ASTRA_SUPERVISOR_FAIL_PROCESS_INFO));

    probe = valid_probe();
    probe.info.size = ASTRA_PROCESS_INFO_SIZE - 4u;
    assert(supervisor_validate(&startup, capabilities, &probe) == content);

    probe = valid_probe();
    probe.info.resident_frames = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) == content);

    probe = valid_probe();
    probe.info.live_threads = 2u;
    assert(supervisor_validate(&startup, capabilities, &probe) == content);

    probe = valid_probe();
    probe.info.exit_reason = 1u;
    assert(supervisor_validate(&startup, capabilities, &probe) == content);

    probe = valid_probe();
    probe.info.id = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) == content);
}

static void
test_failures_accumulate(void)
{
    AstraStartupInfo startup;
    AstraStartupCapability capabilities[2];
    SupervisorProbe probe = valid_probe();

    valid_startup(&startup);
    valid_capabilities(capabilities);
    capabilities[0].rights = 0u;
    probe.abi_version = 0u;
    probe.info.owner = 0u;
    assert(supervisor_validate(&startup, capabilities, &probe) ==
           (ASTRA_SUPERVISOR_STATUS_TAG | ASTRA_SUPERVISOR_FAIL_ABI_VERSION |
            ASTRA_SUPERVISOR_FAIL_CAPABILITIES |
            ASTRA_SUPERVISOR_FAIL_INFO_CONTENT));
}


/*
 * Capability names are bounded strings now. The comparison has to refuse a
 * field with no NUL in it: reading one as a name would run off the record.
 */
static void
test_capability_names_are_bounded_strings(void)
{
    char unterminated[ASTRA_CAPABILITY_NAME_MAX];
    char field[ASTRA_CAPABILITY_NAME_MAX];

    assert(astra_capability_name_equal("PROCESS", "PROCESS"));
    assert(!astra_capability_name_equal("PROCESS", "PROC"));
    assert(!astra_capability_name_equal("PROC", "PROCESS"));
    assert(!astra_capability_name_equal("", "PROCESS"));
    assert(!astra_capability_name_equal(NULL, "PROCESS"));
    assert(!astra_capability_name_equal("PROCESS", NULL));

    memset(unterminated, 'A', sizeof(unterminated));
    assert(!astra_capability_name_equal(unterminated, "AAAA"));

    /* Setting a name fills the rest of the field, so no stale bytes remain. */
    memset(field, 'Z', sizeof(field));
    astra_capability_name_set(field, "WORK");
    assert(astra_capability_name_equal(field, "WORK"));
    assert(field[sizeof(field) - 1u] == '\0');

    /* A name longer than the field is truncated and stays terminated. */
    astra_capability_name_set(field, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    assert(field[ASTRA_CAPABILITY_NAME_MAX - 1u] == '\0');
    assert(astra_capability_name_equal(field, "ABCDEFGHIJKLMNO"));
}

int
main(void)
{
    test_accepts_a_correct_launch();
    test_startup_failures_stop_everything();
    test_abi_disagreements();
    test_capability_table();
    test_process_view();
    test_failures_accumulate();
    test_capability_names_are_bounded_strings();
    puts("SUPERVISOR PASS");
    return 0;
}
