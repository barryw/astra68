#include "../events/events_support.h"

#include <astra/runtime.h>
#include <astra/status.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t input[256];
static uint32_t input_size;
static int fail_allocation;

void *astra_runtime_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

uint32_t astra_filesystem_read_at(AstraFile *file, uint64_t offset,
                                  void *buffer, uint32_t length,
                                  uint32_t *moved)
{
    uint32_t available;

    (void)file;
    if (offset >= input_size) {
        *moved = 0u;
        return ASTRA_VFS_OK;
    }
    available = input_size - (uint32_t)offset;
    if (length > available)
        length = available;
    memcpy(buffer, input + offset, length);
    *moved = length;
    return ASTRA_VFS_OK;
}

int main(void)
{
    AstraFile file = ASTRA_FILE_INIT;
    char path[ASTRA_VFS_PATH_MAX];
    char subsystem[169];
    uint64_t offset = UINT64_MAX;

    memset(subsystem, 's', sizeof(subsystem));
    subsystem[166] = '\0';
    assert(events_build_path(path, sizeof(path), 0, NULL, subsystem,
                             "notice", 0));
    assert(strlen(path) == sizeof(path) - 1u);
    subsystem[166] = 's';
    subsystem[167] = '\0';
    assert(!events_build_path(path, sizeof(path), 0, NULL, subsystem,
                              "notice", 0));

    for (uint32_t index = 0u; index < 100u; ++index) {
        input[index * 2u] = 'x';
        input[index * 2u + 1u] = '\n';
    }
    input_size = 200u;
    assert(events_tail_from(&file, 80u, &offset) == ASTRA_STATUS_OK);
    assert(offset == 40u);
    assert(events_tail_from(&file, 1u, &offset) == ASTRA_STATUS_OK);
    assert(offset == 198u);
    assert(events_tail_from(&file, 0u, &offset) == ASTRA_STATUS_INVALID);
    assert(events_tail_from(&file, UINT32_MAX, &offset) ==
           ASTRA_STATUS_LIMIT);
    fail_allocation = 1;
    assert(events_tail_from(&file, 80u, &offset) == ASTRA_STATUS_LIMIT);
    return 0;
}
