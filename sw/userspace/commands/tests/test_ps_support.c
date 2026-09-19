#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/process.h>

#include "../ps/ps_support.h"

int main(void)
{
    AstraProcSnapshot record = {0};
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
    return 0;
}
