#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/process.h>
#include <astra/vfs_service.h>

#include "../ps/ps_support.h"

static AstraProcSnapshot snapshot_storage[40];
static int fail_allocation;

static void *
test_reallocate(void *pointer, size_t size)
{
    (void)pointer;
    if (fail_allocation || size > sizeof(snapshot_storage))
        return NULL;
    return snapshot_storage;
}

int main(void)
{
    AstraProcSnapshot record = {0};
    AstraProcSnapshot *records = NULL;
    char output[512];
    uint32_t length;

    record.process.id = UINT32_MAX;
    record.process.generation = UINT32_MAX;
    record.process.thread_state = 1u;
    record.process.default_priority = ASTRA_PROCESS_PRIORITY_MAX;
    record.process.live_threads = UINT8_MAX;
    record.process.resident_frames = UINT32_MAX;
    record.process.runtime_ns = UINT64_MAX;
    record.process.elapsed_ns = UINT64_MAX;
    record.process.run_count = UINT32_MAX;
    record.process.syscall_count = UINT32_MAX;
    record.process.handle_references = UINT16_MAX;
    memset(record.name, 'n', sizeof(record.name) - 1u);
    record.name[sizeof(record.name) - 1u] = '\0';

    length = astra_ps_format_row(NULL, 0u, &record);
    assert(length > 112u && length < sizeof(output));
    assert(astra_ps_format_row(output, sizeof(output), &record) == length);
    assert(output[length - 1u] == '\n');
    assert(memmem(output, length, record.name, strlen(record.name)) != NULL);

    output[0] = 'z';
    assert(astra_ps_format_row(output, length - 1u, &record) == 0u);
    assert(output[0] == 'z');
    memset(record.name, 'x', sizeof(record.name));
    assert(astra_ps_format_row(output, sizeof(output), &record) == 0u);
    assert(astra_ps_snapshot_allocate(sizeof(snapshot_storage),
                                      test_reallocate, &records) ==
           ASTRA_VFS_OK);
    assert(records == snapshot_storage);
    fail_allocation = 1;
    records = (AstraProcSnapshot *)(uintptr_t)1u;
    assert(astra_ps_snapshot_allocate(sizeof(AstraProcSnapshot),
                                      test_reallocate, &records) ==
           ASTRA_VFS_ERR_LIMIT);
    assert(records == NULL);
    fail_allocation = 0;
    assert(astra_ps_snapshot_allocate(sizeof(AstraProcSnapshot) + 1u,
                                      test_reallocate, &records) ==
           ASTRA_VFS_ERR_PROTOCOL);
    assert(records == NULL);
    return 0;
}
