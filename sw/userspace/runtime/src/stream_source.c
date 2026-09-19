#include <astra/runtime.h>
#include <astra/syscall.h>

#include "stream_source.h"

static uint64_t elapsed(uint64_t start)
{
    uint64_t finish = astra_clock_monotonic();

    return finish >= start ? finish - start : 0u;
}

uint32_t astra_stream_read_exact(AstraReadAt read_at, void *context,
                                 uint32_t image_size, uint32_t offset,
                                 uint32_t length, const uint8_t **bytes,
                                 AstraProcessLoadProfile *profile)
{
    uint32_t moved = 0u;
    uint32_t status;
    uint64_t start = 0u;

    if (bytes == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *bytes = NULL;
    if (read_at == NULL || length == 0u || offset > image_size ||
        length > image_size - offset)
        return ASTRA_SYSCALL_IO_ERROR;
    if (profile != NULL) {
        ++profile->source_reads;
        profile->source_bytes += length;
        start = astra_clock_monotonic();
    }
    status = read_at(context, offset, length, bytes, &moved);
    if (profile != NULL)
        profile->source_read_ns += elapsed(start);
    if (status != 0u || *bytes == NULL || moved != length)
        return ASTRA_SYSCALL_IO_ERROR;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_stream_feed(uint32_t load_handle, uint32_t image_size,
                           AstraReadAt read_at, void *context,
                           uint32_t write_syscall, uint32_t offset,
                           uint32_t length,
                           AstraProcessLoadProfile *profile)
{
    while (length != 0u) {
        const uint8_t *bytes;
        AstraSyscallResult result;
        uint32_t status = astra_stream_read_exact(
            read_at, context, image_size, offset, length, &bytes, profile);
        uint64_t start = 0u;

        if (status != ASTRA_SYSCALL_OK)
            return status;
        if (profile != NULL) {
            ++profile->kernel_writes;
            profile->kernel_write_bytes += length;
            start = astra_clock_monotonic();
        }
        astra_syscall5(write_syscall, load_handle, offset,
                       (uint32_t)(uintptr_t)bytes, length, 0u, &result);
        if (profile != NULL)
            profile->kernel_write_ns += elapsed(start);
        if (result.status != ASTRA_SYSCALL_OK)
            return result.status;
        offset = result.value0;
        length = result.value1;
    }
    return ASTRA_SYSCALL_OK;
}
