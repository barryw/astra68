#include <astra/resource.h>

#include "internal/syscall.h"

AstraResult astra_handle_close(AstraHandle *handle)
{
    AstraResult result;
    uint32_t ignored_d1;
    uint32_t ignored_d2;

    if (handle == 0 || *handle == ASTRA_INVALID_HANDLE)
        return handle == 0 ? ASTRA_ERROR_INVALID_ARGUMENT :
                             ASTRA_ERROR_INVALID_HANDLE;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_CLOSE, *handle, 0, 0, 0, 0,
        &ignored_d1, &ignored_d2));
    if (result == ASTRA_OK)
        *handle = ASTRA_INVALID_HANDLE;
    return result;
}

AstraResult astra_handle_duplicate(AstraHandle source, uint32_t rights,
                                   AstraHandle *duplicate)
{
    AstraResult result;
    uint32_t created;
    uint32_t ignored;

    if (source == ASTRA_INVALID_HANDLE || rights == 0u || duplicate == 0 ||
        *duplicate != ASTRA_INVALID_HANDLE)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_internal_result(astra_internal_syscall(
        ASTRA_SYSCALL_HANDLE_DUPLICATE, source, rights, 0, 0, 0,
        &created, &ignored));
    if (result == ASTRA_OK) {
        if (created == ASTRA_INVALID_HANDLE)
            return ASTRA_ERROR_IO;
        *duplicate = created;
    }
    return result;
}
