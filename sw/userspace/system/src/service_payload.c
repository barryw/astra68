#include <astra/service_payload.h>
#include <string.h>

AstraResult astra_service_payload_copy(AstraHandle *payload, void *bytes,
    uint32_t length, uint32_t capacity)
{
    if (payload == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraArea area = ASTRA_AREA_INIT;
    area.handle = *payload;
    *payload = ASTRA_INVALID_HANDLE;
    AstraResult result = ASTRA_ERROR_INVALID_ARGUMENT;
    if (bytes != NULL && length != 0u && length <= capacity) {
        result = astra_area_map(&area, ASTRA_AREA_MAP_READ);
        if (result == ASTRA_OK) {
            if (area.size < length) result = ASTRA_ERROR_INVALID_ARGUMENT;
            else memcpy(bytes, area.address, length);
        }
    }
    AstraResult closed = astra_area_close(&area);
    return result == ASTRA_OK ? closed : result;
}

AstraResult astra_service_payload_finish(AstraArea *area, AstraHandle *payload)
{
    if (area == NULL || payload == NULL || *payload != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_area_unmap(area);
    if (result == ASTRA_OK) result = astra_handle_duplicate(area->handle,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, payload);
    AstraResult closed = astra_area_close(area);
    return result == ASTRA_OK ? closed : result;
}

AstraResult astra_service_payload_create(const void *bytes, uint32_t length,
    AstraHandle *payload)
{
    if (bytes == NULL || payload == NULL || *payload != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraArea area = ASTRA_AREA_INIT;
    AstraResult result = astra_area_create(length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
        ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &area);
    if (result == ASTRA_OK) result = astra_area_map(&area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE);
    if (result == ASTRA_OK) {
        memcpy(area.address, bytes, length);
        return astra_service_payload_finish(&area, payload);
    }
    AstraResult closed = astra_area_close(&area);
    (void)closed;
    return result;
}
