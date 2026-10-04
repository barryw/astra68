#include <astra/area.h>

#include "internal/syscall.h"

static int valid_map_flags(uint32_t flags)
{
    return (flags & ASTRA_AREA_MAP_READ) != 0u &&
           (flags & ~(ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE)) == 0u;
}

AstraResult astra_area_create(uint32_t byte_size, uint32_t rights,
                              AstraArea *area)
{
    AstraResult result;
    uint32_t handle;
    uint32_t ignored;

    if (area == 0 || area->handle != ASTRA_INVALID_HANDLE ||
        area->address != 0 || area->size != 0u || area->map_flags != 0u ||
        byte_size == 0u || byte_size > ASTRA_AREA_SIZE_MAX || rights == 0u ||
        (rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                    ASTRA_RIGHT_TRANSFER | ASTRA_RIGHT_ADMINISTER)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_AREA_CREATE, byte_size, rights, 0, 0, 0,
        &handle, &ignored));
    if (result == ASTRA_OK) {
        if (handle == ASTRA_INVALID_HANDLE)
            return ASTRA_ERROR_IO;
        area->handle = handle;
    }
    return result;
}

AstraResult astra_area_map(AstraArea *area, uint32_t map_flags)
{
    AstraResult result;
    uint32_t address;
    uint32_t size;

    if (area == 0 || area->handle == ASTRA_INVALID_HANDLE ||
        area->address != 0 || area->size != 0u || area->map_flags != 0u ||
        !valid_map_flags(map_flags))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_AREA_MAP, area->handle, map_flags, 0, 0, 0,
        &address, &size));
    if (result == ASTRA_OK) {
        if (address == 0u || size == 0u || size > ASTRA_AREA_SIZE_MAX)
            return ASTRA_ERROR_IO;
        area->address = (void *)(uintptr_t)address;
        area->size = size;
        area->map_flags = map_flags;
    }
    return result;
}

AstraResult astra_area_unmap(AstraArea *area)
{
    AstraResult result;
    uint32_t ignored1;
    uint32_t ignored2;

    if (area == 0 || area->address == 0 || area->size == 0u ||
        !valid_map_flags(area->map_flags))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_AREA_UNMAP, (uintptr_t)area->address, 0, 0, 0, 0,
        &ignored1, &ignored2));
    if (result == ASTRA_OK) {
        area->address = 0;
        area->size = 0u;
        area->map_flags = 0u;
    }
    return result;
}

AstraResult astra_area_close(AstraArea *area)
{
    AstraResult first = ASTRA_OK;
    AstraResult result;
    uint32_t attempted = 0u;

    if (area == 0)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (area->address != 0) {
        ++attempted;
        result = astra_area_unmap(area);
        if (result != ASTRA_OK)
            first = result;
    }
    if (area->handle != ASTRA_INVALID_HANDLE) {
        ++attempted;
        result = astra_handle_close(&area->handle);
        if (result != ASTRA_OK && first == ASTRA_OK)
            first = result;
    }
    return attempted == 0u ? ASTRA_ERROR_INVALID_HANDLE : first;
}

void astra_area_cleanup(AstraArea *area)
{
    AstraResult ignored = astra_area_close(area);

    (void)ignored;
}
