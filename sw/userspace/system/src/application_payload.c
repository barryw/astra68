#include <astra/application_payload.h>
#include <astra/area.h>
#include <astra/service_payload.h>
#include <astra/utf8.h>

#include <string.h>

AstraResult astra_application_arguments_validate(
    const AstraApplicationLaunchArguments *arguments, const char *bytes,
    uint32_t capacity)
{
    if (arguments == NULL || bytes == NULL || arguments->count == 0u ||
        arguments->count > ASTRA_APPLICATION_ARGUMENT_MAX || arguments->length == 0u ||
        arguments->length > ASTRA_APPLICATION_ARGUMENT_BYTES || arguments->length > capacity ||
        arguments->flags != 0u || arguments->reserved[0] != 0u || arguments->reserved[1] != 0u ||
        (arguments->source != ASTRA_LAUNCH_SOURCE_SHELL && arguments->source != ASTRA_LAUNCH_SOURCE_DESKTOP))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t consumed = 0u;
    for (uint32_t index = 0u; index < arguments->count; ++index) {
        uint32_t start = consumed;
        while (consumed < arguments->length && bytes[consumed] != '\0') ++consumed;
        if (consumed == arguments->length ||
            (index == 0u && (consumed == start || consumed - start >= ASTRA_APPLICATION_PATH_MAX)) ||
            !astra_utf8_validate(bytes + start, consumed - start, 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        ++consumed;
    }
    return consumed == arguments->length ? ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
}

AstraResult astra_application_payload_copy(
    const AstraApplicationLaunchArguments *arguments, AstraHandle *payload,
    char *bytes, uint32_t capacity)
{
    uint32_t length = arguments != NULL && arguments->length <= ASTRA_APPLICATION_ARGUMENT_BYTES ? arguments->length : 0u;
    AstraResult result = astra_service_payload_copy(payload, bytes, length, capacity);
    if (result == ASTRA_OK)
        result = astra_application_arguments_validate(arguments, bytes, capacity);
    return result;
}
