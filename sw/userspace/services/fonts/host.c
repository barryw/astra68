#include "host.h"

#include <astra/font_worker.h>
#include <astra/runtime.h>
#include <astra/status.h>

#include <stdio.h>
#include <string.h>

#define FONT_TRANSFER_CHUNK 65536u

static void trace_failure(const char *step, uint32_t status)
{
    char text[80];
    int length = snprintf(text, sizeof(text), "fonts %s status=%u", step,
                          status);
    if (length > 0 && (size_t)length < sizeof(text))
        (void)astra_log_debug(text, (uint32_t)length);
}

static uint32_t submit(FontsHost *host)
{
    uint32_t status = astra_host_client_submit(&host->channel);
    if (status != ASTRA_SYSCALL_OK)
        trace_failure("host submit", status);
    else if (host->channel.command->status != ASTRA_STATUS_OK)
        trace_failure("host command", host->channel.command->status);
    return status == ASTRA_SYSCALL_OK ? host->channel.command->status :
                                       ASTRA_STATUS_IO;
}

uint32_t fonts_host_begin(FontsHost *host, uint32_t device,
    const void *source, uint32_t source_size, const char *name,
    uint32_t name_size)
{
    AstraHostCommand *command;
    uint32_t status;

    if (host == NULL || device == 0u || source == NULL || source_size == 0u ||
        source_size > ASTRA_FONT_WORKER_SOURCE_MAX || name == NULL ||
        name_size == 0u || name_size > ASTRA_FONT_WORKER_TEXT_MAX)
        return ASTRA_STATUS_INVALID;
    *host = (FontsHost){0};
    status = astra_host_client_open(device, ASTRA_HOST_CAP_FONT_CONVERT,
                                    FONT_TRANSFER_CHUNK, &host->channel);
    if (status != ASTRA_SYSCALL_OK) {
        trace_failure("host channel open", status);
        return status == ASTRA_SYSCALL_UNSUPPORTED ?
            ASTRA_STATUS_UNSUPPORTED : ASTRA_STATUS_IO;
    }
    host->active = 1u;
    command = astra_host_client_prepare(&host->channel,
        ASTRA_HOST_SERVICE_FONTS, ASTRA_HOST_FONT_BEGIN);
    command->value_lo = source_size;
    command->offset_lo = name_size;
    command->data_length = name_size;
    memcpy((void *)(uintptr_t)host->channel.data, name, name_size);
    status = submit(host);
    for (uint32_t at = 0u; status == ASTRA_STATUS_OK && at < source_size;) {
        uint32_t count = source_size - at;
        if (count > FONT_TRANSFER_CHUNK) count = FONT_TRANSFER_CHUNK;
        command = astra_host_client_prepare(&host->channel,
            ASTRA_HOST_SERVICE_FONTS, ASTRA_HOST_FONT_DATA);
        command->offset_lo = at;
        command->data_length = count;
        memcpy((void *)(uintptr_t)host->channel.data,
               (const uint8_t *)source + at, count);
        status = submit(host);
        if (status == ASTRA_STATUS_OK && command->result_value != at + count)
            status = ASTRA_STATUS_PROTOCOL;
        at += count;
    }
    if (status != ASTRA_STATUS_OK) fonts_host_close(host);
    return status;
}

uint32_t fonts_host_convert(FontsHost *host, uint32_t face,
    void **afnt, uint32_t *afnt_size, uint32_t *faces)
{
    AstraHostCommand *command;
    uint32_t status, length;
    uint8_t *output;

    if (host == NULL || !host->active || afnt == NULL ||
        afnt_size == NULL || faces == NULL ||
        face >= ASTRA_FONT_WORKER_FACES_MAX)
        return ASTRA_STATUS_INVALID;
    *afnt = NULL;
    *afnt_size = 0u;
    *faces = 0u;
    command = astra_host_client_prepare(&host->channel,
        ASTRA_HOST_SERVICE_FONTS, ASTRA_HOST_FONT_CONVERT);
    command->value_lo = face;
    status = submit(host);
    if (status != ASTRA_STATUS_OK) return status;
    length = command->result_value;
    *faces = command->value_hi;
    if (length > ASTRA_FONT_WORKER_SOURCE_MAX) return ASTRA_STATUS_LIMIT;
    if (length == 0u || *faces == 0u ||
        *faces > ASTRA_FONT_WORKER_FACES_MAX || face >= *faces)
        return ASTRA_STATUS_PROTOCOL;
    output = astra_runtime_allocate(length);
    if (output == NULL) return ASTRA_STATUS_LIMIT;
    for (uint32_t at = 0u; status == ASTRA_STATUS_OK && at < length;) {
        uint32_t count = length - at;
        if (count > FONT_TRANSFER_CHUNK) count = FONT_TRANSFER_CHUNK;
        command = astra_host_client_prepare(&host->channel,
            ASTRA_HOST_SERVICE_FONTS, ASTRA_HOST_FONT_READ);
        command->offset_lo = at;
        command->data_capacity = count;
        status = submit(host);
        if (status == ASTRA_STATUS_OK && command->result_length != count)
            status = ASTRA_STATUS_PROTOCOL;
        if (status == ASTRA_STATUS_OK)
            memcpy(output + at,
                   (const void *)(uintptr_t)host->channel.data, count);
        at += count;
    }
    if (status != ASTRA_STATUS_OK) {
        astra_runtime_deallocate(output);
        return status;
    }
    *afnt = output;
    *afnt_size = length;
    return ASTRA_STATUS_OK;
}

void fonts_host_close(FontsHost *host)
{
    if (host == NULL || !host->active) return;
    (void)astra_host_client_prepare(&host->channel,
        ASTRA_HOST_SERVICE_FONTS, ASTRA_HOST_FONT_CLOSE);
    (void)astra_host_client_submit(&host->channel);
    (void)astra_host_client_close(&host->channel);
    host->active = 0u;
}
