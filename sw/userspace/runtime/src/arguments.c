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

uint32_t
astra_launch_environment_pack(AstraLaunchArguments *arguments, char *storage,
                              uint32_t capacity, uint32_t count,
                              const char *const *names,
                              const char *const *values)
{
    uint32_t length = 0u;

    if (arguments == NULL || count > ASTRA_LAUNCH_ENVIRONMENT_MAX ||
        capacity > ASTRA_LAUNCH_ENVIRONMENT_BYTES ||
        (count != 0u && (storage == NULL || names == NULL || values == NULL)))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    arguments->environment_count = 0u;
    arguments->environment_length = 0u;
    arguments->environment_address = 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t name_length = 0u;
        uint32_t value_length = 0u;

        if (names[index] == NULL || values[index] == NULL ||
            names[index][0] == '\0')
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        while (names[index][name_length] != '\0') {
            if (names[index][name_length] == '=')
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            ++name_length;
        }
        while (values[index][value_length] != '\0')
            ++value_length;
        if (length + name_length + value_length + 2u > capacity ||
            length + name_length + value_length + 2u >
                ASTRA_LAUNCH_ENVIRONMENT_BYTES)
            return ASTRA_SYSCALL_RESOURCE_LIMIT;
        for (uint32_t at = 0u; at < name_length; ++at)
            storage[length++] = names[index][at];
        storage[length++] = '=';
        for (uint32_t at = 0u; at < value_length; ++at)
            storage[length++] = values[index][at];
        storage[length++] = '\0';
    }
    arguments->environment_count = (uint16_t)count;
    arguments->environment_length = (uint16_t)length;
    arguments->environment_address = count != 0u ?
        (uint32_t)(uintptr_t)storage : 0u;
    return ASTRA_SYSCALL_OK;
}
