#include <astra/runtime.h>

uint32_t
astra_launch_arguments_pack(AstraLaunchArguments *arguments,
                            AstraLaunchSource source, uint32_t count,
                            const char *const *values)
{
    uint32_t length = 0u;

    if (arguments == NULL || source > ASTRA_LAUNCH_SOURCE_DESKTOP ||
        count > ASTRA_LAUNCH_ARGUMENT_MAX ||
        (count != 0u && values == NULL))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *arguments = (AstraLaunchArguments){0};
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t at = 0u;

        if (values[index] == NULL)
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        while (values[index][at] != '\0') {
            if (length + 2u > ASTRA_LAUNCH_ARGUMENT_BYTES)
                return ASTRA_SYSCALL_RESOURCE_LIMIT;
            arguments->bytes[length++] = values[index][at++];
        }
        arguments->bytes[length++] = '\0';
    }
    arguments->count = (uint16_t)count;
    arguments->length = (uint16_t)length;
    arguments->source = (uint16_t)source;
    return ASTRA_SYSCALL_OK;
}
