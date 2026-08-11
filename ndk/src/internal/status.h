#ifndef ASTRA_NDK_INTERNAL_STATUS_H
#define ASTRA_NDK_INTERNAL_STATUS_H

#include <astra/status.h>
#include <astra/types.h>

static inline AstraResult astra_internal_service_result(uint32_t status)
{
    switch (status) {
    case ASTRA_STATUS_OK: return ASTRA_OK;
    case ASTRA_STATUS_PROTOCOL:
    case ASTRA_STATUS_INVALID: return ASTRA_ERROR_INVALID_ARGUMENT;
    case ASTRA_STATUS_BAD_HANDLE:
    case ASTRA_STATUS_NOT_FOUND: return ASTRA_ERROR_INVALID_HANDLE;
    case ASTRA_STATUS_ACCESS: return ASTRA_ERROR_PERMISSION;
    case ASTRA_STATUS_LIMIT:
    case ASTRA_STATUS_NO_SPACE: return ASTRA_ERROR_NO_RESOURCES;
    case ASTRA_STATUS_UNSUPPORTED: return ASTRA_ERROR_UNSUPPORTED;
    case ASTRA_STATUS_BUSY: return ASTRA_ERROR_BUSY;
    case ASTRA_STATUS_BUFFER_TOO_SMALL: return ASTRA_ERROR_BUFFER_TOO_SMALL;
    case ASTRA_STATUS_PEER_DEAD: return ASTRA_ERROR_PEER_DEAD;
    default: return ASTRA_ERROR_IO;
    }
}

#endif
