#include <loader.h>

#include <assert.h>
#include <stdio.h>

static SupervisorProcessRecord process_storage[64];
static int fail_reallocation;

static void *
test_reallocate(void *pointer, size_t size)
{
    (void)pointer;
    if (fail_reallocation || size > sizeof(process_storage))
        return NULL;
    return process_storage;
}

int main(void)
{
    SupervisorProcessTable table = {0};

    assert(!ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_OK));
    assert(!ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_INVALID));
    assert(!ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_PROGRAM_FIRST));
    assert(ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_FAULTED));
    assert(ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_NO_STARTUP));
    assert(ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_BAD_EXIT));
    assert(ASTRA_STATUS_IS_VERDICT(ASTRA_STATUS_SIGNALLED(9u)));

    assert(supervisor_loader_child_status(ASTRA_STATUS_OK) ==
           ASTRA_STATUS_PEER_DEAD);
    assert(supervisor_loader_child_status(ASTRA_STATUS_INVALID) ==
           ASTRA_STATUS_INVALID);
    assert(supervisor_loader_child_status(ASTRA_STATUS_PROGRAM_FIRST) ==
           ASTRA_STATUS_PROGRAM_FIRST);
    assert(supervisor_loader_child_status(ASTRA_STATUS_FAULTED) ==
           SUPERVISOR_LOADER_FAIL_CHILD);
    assert(supervisor_loader_child_status(ASTRA_STATUS_NO_STARTUP) ==
           SUPERVISOR_LOADER_FAIL_CHILD);
    assert(supervisor_loader_child_status(ASTRA_STATUS_BAD_EXIT) ==
           SUPERVISOR_LOADER_FAIL_CHILD);
    assert(supervisor_process_table_reserve(&table, 40u, test_reallocate));
    assert(table.records == process_storage && table.capacity == 64u);
    fail_reallocation = 1;
    assert(!supervisor_process_table_reserve(&table, 65u,
                                             test_reallocate));
    assert(table.records == process_storage && table.capacity == 64u);
    /* Only the machine's services fail the boot (boot_progress.h). */
    assert(supervisor_boot_service_result(ASTRA_STATUS_OK, 0u) ==
           ASTRA_PROGRESS_SERVICE_READY);
    assert(supervisor_boot_service_result(ASTRA_STATUS_OK, 1u) ==
           ASTRA_PROGRESS_SERVICE_READY);
    assert(supervisor_boot_service_result(ASTRA_STATUS_PEER_DEAD, 1u) ==
           ASTRA_PROGRESS_SERVICE_FAILED);
    assert(supervisor_boot_service_result(ASTRA_STATUS_NOT_FOUND, 1u) ==
           ASTRA_PROGRESS_SERVICE_FAILED);
    assert(supervisor_boot_service_result(ASTRA_STATUS_PEER_DEAD, 0u) ==
           ASTRA_PROGRESS_SERVICE_SKIPPED);
    assert(supervisor_boot_service_result(ASTRA_STATUS_NOT_FOUND, 0u) ==
           ASTRA_PROGRESS_SERVICE_SKIPPED);
    /* A count the record cannot hold fails it, and stays a SERVICES. */
    assert(supervisor_boot_services_command(14u) ==
           (ASTRA_PROGRESS_BOOT_SERVICES | 14u));
    assert(supervisor_boot_services_command(
               ASTRA_BOOT_PROGRESS_SERVICES_MAX) ==
           (ASTRA_PROGRESS_BOOT_SERVICES | ASTRA_BOOT_PROGRESS_SERVICES_MAX));
    for (uint32_t count = ASTRA_BOOT_PROGRESS_SERVICES_MAX + 1u;
         count < 0x10000u; count += 0x0FFFu) {
        uint32_t command = supervisor_boot_services_command(count);

        assert((command & ~ASTRA_PROGRESS_BOOT_ARGUMENT_MASK) ==
               ASTRA_PROGRESS_BOOT_SERVICES);
        assert(astra_boot_progress_state(astra_boot_progress_announce(
                   (uint32_t)ASTRA_BOOT_PROGRESS_FIXED_STEPS <<
                       ASTRA_BOOT_PROGRESS_FIXED_SHIFT,
                   command & ASTRA_PROGRESS_BOOT_ARGUMENT_MASK)) ==
               ASTRA_BOOT_PROGRESS_STATE_FAILED);
    }
    puts("SUPERVISOR LOADER STATUS PASS");
    return 0;
}
