#include "events_support.h"

#include <astra/runtime.h>
#include <astra/status.h>

#include <stdio.h>

#define EVENTS_READ_CHUNK 128u

int events_build_path(char *path, size_t capacity, int by_activity,
                      const char *activity, const char *subsystem,
                      const char *level, int previous_boot)
{
    int length;

    if (path == NULL || capacity == 0u || level == NULL)
        return 0;
    if (by_activity)
        length = snprintf(path, capacity, "EVENTS:activity/%s", activity);
    else if (subsystem != NULL)
        length = snprintf(path, capacity, "EVENTS:subsystem/%s/%s",
                          subsystem, level);
    else
        length = snprintf(path, capacity, "EVENTS:boot/%s/%s",
                          previous_boot ? "-1" : "current", level);
    return length >= 0 && (size_t)length < capacity;
}

uint32_t events_tail_from(AstraFile *file, uint32_t lines, uint64_t *result)
{
    uint8_t chunk[EVENTS_READ_CHUNK];
    uint64_t *starts;
    uint64_t offset = 0u;
    uint32_t count = 0u;
    uint32_t oldest = 0u;
    int line_start = 1;

    if (file == NULL || lines == 0u || result == NULL)
        return ASTRA_STATUS_INVALID;
    if (lines > UINT32_MAX / sizeof(*starts))
        return ASTRA_STATUS_LIMIT;
    starts = astra_runtime_allocate((size_t)lines * sizeof(*starts));
    if (starts == NULL)
        return ASTRA_STATUS_LIMIT;
    for (;;) {
        uint32_t moved = 0u;
        uint32_t status = astra_filesystem_read_at(
            file, offset, chunk, sizeof(chunk), &moved);

        if (status != ASTRA_VFS_OK) {
            astra_runtime_deallocate(starts);
            return status;
        }
        if (moved == 0u)
            break;
        for (uint32_t index = 0u; index < moved; ++index) {
            if (line_start) {
                if (count == lines) {
                    starts[oldest] = offset + index;
                    oldest = (oldest + 1u) % lines;
                } else {
                    starts[count++] = offset + index;
                }
                line_start = 0;
            }
            if (chunk[index] == '\n')
                line_start = 1;
        }
        offset += moved;
    }
    *result = count == 0u ? 0u :
              count == lines ? starts[oldest] : starts[0];
    astra_runtime_deallocate(starts);
    return ASTRA_STATUS_OK;
}
