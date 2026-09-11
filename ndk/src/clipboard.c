#include <astra/clipboard.h>

#include <astra/bytes.h>
#include <astra/clipboard_service.h>
#include <astra/port.h>
#include <astra/resource.h>
#include <astra/utf8.h>

#include "internal/status.h"

static int span_valid(uint32_t offset, uint32_t length, uint32_t bytes,
                      uint32_t minimum)
{
    return offset >= minimum && offset <= bytes && length <= bytes - offset;
}

static int type_valid(const char *type, uint32_t length)
{
    if (type == NULL || length == 0u)
        return 0;
    for (uint32_t index = 0u; index < length; ++index)
        if ((uint8_t)type[index] < 0x21u || (uint8_t)type[index] > 0x7eu)
            return 0;
    return 1;
}

static int bytes_equal(const void *left, const void *right, uint32_t length)
{
    const uint8_t *a = left;
    const uint8_t *b = right;

    for (uint32_t index = 0u; index < length; ++index)
        if (a[index] != b[index])
            return 0;
    return 1;
}

static int utf8_type(const char *type, uint32_t length)
{
    static const char name[] = ASTRA_CLIPBOARD_TYPE_UTF8;

    return length == sizeof(name) - 1u &&
           bytes_equal(type, name, sizeof(name) - 1u);
}

static AstraResult representations_size(
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *bytes)
{
    uint64_t total = sizeof(AstraClipboardDocumentHeader) +
                     (uint64_t)count *
                         sizeof(AstraClipboardDocumentRecord);

    if (representations == NULL || count == 0u || bytes == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < count; ++index) {
        const AstraClipboardRepresentation *representation =
            &representations[index];

        if (representation->size < sizeof(*representation) ||
            !astra_words_zero(representation->reserved, 4u) ||
            !type_valid(representation->type, representation->type_length) ||
            (representation->data == NULL &&
             representation->data_length != 0u) ||
            (utf8_type(representation->type,
                       representation->type_length) &&
             !astra_utf8_validate(representation->data,
                                  representation->data_length, 0u)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        total += (uint64_t)representation->type_length +
                 representation->data_length;
        if (total > UINT32_MAX)
            return ASTRA_ERROR_NO_RESOURCES;
    }
    *bytes = (uint32_t)total;
    return ASTRA_OK;
}

AstraResult astra_clipboard_document_write(
    void *document, uint32_t capacity,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *bytes)
{
    AstraClipboardDocumentHeader header = {0};
    uint8_t *output = document;
    uint32_t required = 0u;
    uint32_t offset;
    AstraResult result = representations_size(representations, count,
                                               &required);

    if (result != ASTRA_OK)
        return result;
    *bytes = required;
    if (required > capacity || document == NULL)
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    header.magic = ASTRA_CLIPBOARD_DOCUMENT_MAGIC;
    header.version = ASTRA_CLIPBOARD_DOCUMENT_VERSION;
    header.structure_size = sizeof(header);
    header.total_size = required;
    header.representation_count = count;
    header.records_offset = sizeof(header);
    (void)memcpy(output, &header, sizeof(header));
    offset = sizeof(header) + count * sizeof(AstraClipboardDocumentRecord);
    for (uint32_t index = 0u; index < count; ++index) {
        const AstraClipboardRepresentation *representation =
            &representations[index];
        AstraClipboardDocumentRecord record;

        record.type_offset = offset;
        record.type_length = representation->type_length;
        (void)memcpy(output + offset, representation->type,
                     representation->type_length);
        offset += representation->type_length;
        record.data_offset = offset;
        record.data_length = representation->data_length;
        if (representation->data_length != 0u)
            (void)memcpy(output + offset, representation->data,
                         representation->data_length);
        offset += representation->data_length;
        (void)memcpy(output + sizeof(header) +
                         index * sizeof(record),
                     &record, sizeof(record));
    }
    return ASTRA_OK;
}

AstraResult astra_clipboard_document_validate(const void *document,
                                               uint32_t bytes)
{
    const uint8_t *input = document;
    const AstraClipboardDocumentHeader *header = document;
    uint64_t records_end;

    if (document == NULL || bytes < sizeof(*header) ||
        header->magic != ASTRA_CLIPBOARD_DOCUMENT_MAGIC ||
        header->version != ASTRA_CLIPBOARD_DOCUMENT_VERSION ||
        header->structure_size != sizeof(*header) ||
        header->total_size != bytes || header->representation_count == 0u ||
        header->records_offset != sizeof(*header) ||
        !astra_words_zero(header->reserved, 3u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    records_end = (uint64_t)header->records_offset +
                  (uint64_t)header->representation_count *
                      sizeof(AstraClipboardDocumentRecord);
    if (records_end > bytes)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < header->representation_count; ++index) {
        const AstraClipboardDocumentRecord *record =
            (const AstraClipboardDocumentRecord *)(const void *)(
                input + header->records_offset + index * sizeof(*record));
        const char *type;

        if (!span_valid(record->type_offset, record->type_length, bytes,
                        (uint32_t)records_end) ||
            !span_valid(record->data_offset, record->data_length, bytes,
                        (uint32_t)records_end))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        type = (const char *)(const void *)(input + record->type_offset);
        if (!type_valid(type, record->type_length) ||
            (utf8_type(type, record->type_length) &&
             !astra_utf8_validate(input + record->data_offset,
                                  record->data_length, 0u)))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    }
    return ASTRA_OK;
}

static AstraResult exchange(AstraHandle service, uint32_t operation,
                            uint32_t document_size,
                            AstraHandle document_handle,
                            AstraClipboardReply *reply,
                            AstraHandle *received_handle)
{
    AstraClipboardRequest request = {0};
    AstraPort reply_port = ASTRA_PORT_INIT;
    AstraHandle handles[2] = { ASTRA_INVALID_HANDLE, ASTRA_INVALID_HANDLE };
    uint32_t handle_count = 0u;
    uint32_t received_count = 0u;
    uint32_t reply_size = 0u;
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE || reply == NULL ||
        received_handle == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *received_handle = ASTRA_INVALID_HANDLE;
    result = astra_port_create(1u, sizeof(*reply), &reply_port);
    if (result != ASTRA_OK)
        return result;
    result = astra_message_header_init(
        &request.header, sizeof(request), ASTRA_CLIPBOARD_PROTOCOL,
        ASTRA_CLIPBOARD_PROTOCOL_VERSION, operation, 1u);
    if (result != ASTRA_OK)
        goto done;
    request.document_size = document_size;
    if (document_handle != ASTRA_INVALID_HANDLE)
        handles[handle_count++] = document_handle;
    handles[handle_count++] = reply_port.send;
    result = astra_port_send_until(service, &request, sizeof(request), handles,
                                   handle_count, ASTRA_DEADLINE_INFINITE);
    if (document_handle != ASTRA_INVALID_HANDLE)
        document_handle = handles[0];
    reply_port.send = handles[handle_count - 1u];
    if (result != ASTRA_OK)
        goto done;
    result = astra_port_receive_until(
        reply_port.receive, reply, sizeof(*reply), received_handle, 1u,
        &reply_size, &received_count, ASTRA_DEADLINE_INFINITE);
    if (result != ASTRA_OK)
        goto done;
    if (reply_size != sizeof(*reply) ||
        reply->header.total_size != sizeof(*reply) ||
        reply->header.header_size != ASTRA_MESSAGE_HEADER_SIZE ||
        reply->header.flags != 0u ||
        reply->header.protocol != ASTRA_CLIPBOARD_PROTOCOL ||
        reply->header.protocol_version != ASTRA_CLIPBOARD_PROTOCOL_VERSION ||
        reply->header.operation != ASTRA_CLIPBOARD_OPERATION_REPLY ||
        reply->header.transaction_id != request.header.transaction_id ||
        reply->header.reserved != 0u || reply->reserved != 0u ||
        (reply->status == ASTRA_STATUS_OK &&
         ((operation == ASTRA_CLIPBOARD_OPERATION_GET) !=
          (received_count == 1u))) ||
        (reply->status != ASTRA_STATUS_OK && received_count != 0u)) {
        result = ASTRA_ERROR_IO;
        goto done;
    }
    result = reply->status == ASTRA_STATUS_NOT_FOUND ?
             ASTRA_ERROR_NOT_PRESENT :
             astra_internal_service_result(reply->status);

done:
    if (document_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&document_handle);
        (void)ignored;
    }
    if (result != ASTRA_OK && *received_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(received_handle);
        (void)ignored;
    }
    {
        AstraResult close_result = astra_port_close(&reply_port);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    return result;
}

AstraResult astra_clipboard_write(
    AstraHandle service,
    const AstraClipboardRepresentation *representations, uint32_t count,
    uint32_t *generation)
{
    AstraArea area = ASTRA_AREA_INIT;
    AstraHandle immutable = ASTRA_INVALID_HANDLE;
    AstraHandle ignored = ASTRA_INVALID_HANDLE;
    AstraClipboardReply reply = {0};
    uint32_t bytes = 0u;
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_clipboard_document_write(
        NULL, 0u, representations, count, &bytes);
    if (result != ASTRA_ERROR_BUFFER_TOO_SMALL)
        return result;
    if (bytes > ASTRA_AREA_SIZE_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    result = astra_area_create(
        bytes, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                   ASTRA_RIGHT_TRANSFER, &area);
    if (result != ASTRA_OK)
        return result;
    result = astra_area_map(&area,
                            ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result == ASTRA_OK)
        result = astra_clipboard_document_write(
            area.address, area.size, representations, count, &bytes);
    if (area.address != NULL) {
        AstraResult unmap_result = astra_area_unmap(&area);

        if (result == ASTRA_OK && unmap_result != ASTRA_OK)
            result = unmap_result;
    }
    if (result == ASTRA_OK)
        result = astra_handle_duplicate(
            area.handle,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
            &immutable);
    if (result == ASTRA_OK)
        result = exchange(service, ASTRA_CLIPBOARD_OPERATION_SET, bytes,
                          immutable, &reply, &ignored);
    immutable = ASTRA_INVALID_HANDLE;
    {
        AstraResult close_result = astra_area_close(&area);

        if (result == ASTRA_OK && close_result != ASTRA_OK)
            result = close_result;
    }
    if (result == ASTRA_OK && generation != NULL)
        *generation = reply.generation;
    return result;
}

AstraResult astra_clipboard_read(AstraHandle service,
                                 AstraClipboardItem *item)
{
    AstraClipboardReply reply = {0};
    AstraHandle area = ASTRA_INVALID_HANDLE;
    AstraResult result;

    if (service == ASTRA_INVALID_HANDLE || item == NULL ||
        item->_private_area.handle != ASTRA_INVALID_HANDLE ||
        item->_private_area.address != NULL || item->_private_area.size != 0u ||
        item->_private_area.map_flags != 0u || item->generation != 0u ||
        item->_private_document_size != 0u ||
        !astra_words_zero(item->reserved, 2u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = exchange(service, ASTRA_CLIPBOARD_OPERATION_GET, 0u,
                      ASTRA_INVALID_HANDLE, &reply, &area);
    if (result != ASTRA_OK)
        return result;
    if (reply.document_size == 0u ||
        reply.document_size > ASTRA_AREA_SIZE_MAX) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    item->_private_area.handle = area;
    area = ASTRA_INVALID_HANDLE;
    result = astra_area_map(&item->_private_area, ASTRA_AREA_MAP_READ);
    if (result != ASTRA_OK)
        goto fail;
    if (item->_private_area.size < reply.document_size ||
        astra_clipboard_document_validate(item->_private_area.address,
                                          reply.document_size) != ASTRA_OK) {
        result = ASTRA_ERROR_IO;
        goto fail;
    }
    item->generation = reply.generation;
    item->_private_document_size = reply.document_size;
    return ASTRA_OK;

fail:
    if (area != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_handle_close(&area);
        (void)ignored;
    }
    if (item->_private_area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(&item->_private_area);
        (void)ignored;
    }
    return result;
}

AstraResult astra_clipboard_item_find(
    const AstraClipboardItem *item, const char *type, uint32_t type_length,
    const void **data, uint32_t *data_length)
{
    const uint8_t *document;
    const AstraClipboardDocumentHeader *header;

    if (item == NULL || item->_private_area.address == NULL ||
        item->_private_area.size < item->_private_document_size ||
        item->_private_document_size == 0u || item->generation == 0u ||
        !astra_words_zero(item->reserved, 2u) ||
        !type_valid(type, type_length) || data == NULL ||
        data_length == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    document = item->_private_area.address;
    if (astra_clipboard_document_validate(
            document, item->_private_document_size) != ASTRA_OK)
        return ASTRA_ERROR_IO;
    header = (const AstraClipboardDocumentHeader *)(const void *)document;
    for (uint32_t index = 0u; index < header->representation_count; ++index) {
        const AstraClipboardDocumentRecord *record =
            (const AstraClipboardDocumentRecord *)(const void *)(
                document + header->records_offset + index * sizeof(*record));

        if (record->type_length == type_length &&
            bytes_equal(document + record->type_offset, type, type_length)) {
            *data = document + record->data_offset;
            *data_length = record->data_length;
            return ASTRA_OK;
        }
    }
    return ASTRA_ERROR_NOT_PRESENT;
}

AstraResult astra_clipboard_item_close(AstraClipboardItem *item)
{
    AstraResult result;

    if (item == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_area_close(&item->_private_area);
    if (result == ASTRA_OK) {
        item->generation = 0u;
        item->_private_document_size = 0u;
        item->reserved[0] = 0u;
        item->reserved[1] = 0u;
    }
    return result;
}

AstraResult astra_clipboard_clear(AstraHandle service, uint32_t *generation)
{
    AstraClipboardReply reply = {0};
    AstraHandle ignored = ASTRA_INVALID_HANDLE;
    AstraResult result = exchange(
        service, ASTRA_CLIPBOARD_OPERATION_CLEAR, 0u,
        ASTRA_INVALID_HANDLE, &reply, &ignored);

    if (result == ASTRA_OK && generation != NULL)
        *generation = reply.generation;
    return result;
}
