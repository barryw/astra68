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
    puts("SUPERVISOR LOADER STATUS PASS");
    return 0;
}
