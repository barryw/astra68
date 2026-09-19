#include "date_support.h"

#include <astra/runtime.h>
#include <astra/status.h>

#include <stdint.h>
#include <string.h>

uint32_t date_format_alloc(const AstraCivilTime *civil,
                           const struct tm *rendered, const char *format,
                           char **output, size_t *length)
{
    char *expanded;
    char *buffer;
    size_t format_length;
    size_t capacity = 64u;

    if (civil == NULL || rendered == NULL || format == NULL || output == NULL ||
        length == NULL || *output != NULL)
        return ASTRA_STATUS_INVALID;
    format_length = strlen(format);
    if (format_length > (UINT32_MAX - 1u) / 3u)
        return ASTRA_STATUS_LIMIT;
    expanded = astra_runtime_allocate(format_length * 3u + 1u);
    if (expanded == NULL)
        return ASTRA_STATUS_LIMIT;
    if (astra_civil_expand_zone(format, civil, expanded,
                                (uint32_t)(format_length * 3u + 1u)) == 0u) {
        astra_runtime_deallocate(expanded);
        return ASTRA_STATUS_INVALID;
    }
    buffer = astra_runtime_allocate(capacity);
    if (buffer == NULL) {
        astra_runtime_deallocate(expanded);
        return ASTRA_STATUS_LIMIT;
    }
    for (;;) {
        size_t rendered_length = strftime(buffer, capacity, expanded, rendered);

        if (rendered_length != 0u) {
            astra_runtime_deallocate(expanded);
            *output = buffer;
            *length = rendered_length;
            return ASTRA_STATUS_OK;
        }
        if (capacity > UINT32_MAX / 2u) {
            astra_runtime_deallocate(buffer);
            astra_runtime_deallocate(expanded);
            return ASTRA_STATUS_LIMIT;
        }
        capacity *= 2u;
        {
            char *grown = astra_runtime_reallocate(buffer, capacity);

            if (grown == NULL) {
                astra_runtime_deallocate(buffer);
                astra_runtime_deallocate(expanded);
                return ASTRA_STATUS_LIMIT;
            }
            buffer = grown;
        }
    }
}
