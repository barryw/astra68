#include <astra/service_manager.h>

#include <astra/utf8.h>

#include <string.h>

int astra_service_name_valid(const char *name)
{
    uint32_t length = 0u;

    if (name == NULL || name[0] == '\0' || name[0] == '.')
        return 0;
    while (name[length] != '\0') {
        char value = name[length];

        if (length + 1u >= ASTRA_VFS_NAME_MAX ||
            !((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' ||
              value == '_' || value == '.'))
            return 0;
        ++length;
    }
    return astra_utf8_validate(name, length, 0u);
}

static int capability_name_valid(const char name[ASTRA_CAPABILITY_NAME_MAX])
{
    uint32_t length = 0u;

    while (length < ASTRA_CAPABILITY_NAME_MAX && name[length] != '\0')
        ++length;
    return length != 0u && length < ASTRA_CAPABILITY_NAME_MAX;
}

static int terminated_utf8(const char *text, uint32_t capacity)
{
    for (uint32_t index = 0u; index < capacity; ++index)
        if (text[index] == '\0')
            return index != 0u && astra_utf8_validate(text, index, 0u);
    return 0;
}

static int bounded_copy(char *out, uint32_t capacity, const char *value)
{
    uint32_t length = 0u;

    if (value == NULL)
        return 0;
    while (value[length] != '\0') {
        if (length + 1u >= capacity)
            return 0;
        out[length] = value[length];
        ++length;
    }
    out[length] = '\0';
    return 1;
}

AstraResult astra_service_definition_init(AstraServiceDefinition *definition,
                                           const char *name,
                                           const char *executable,
                                           uint32_t runs_on)
{
    if (definition == NULL || !astra_service_name_valid(name) ||
        (runs_on != ASTRA_SERVICE_RUNS_ASTRA &&
         runs_on != ASTRA_SERVICE_RUNS_PAIRED))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    (void)memset(definition, 0, sizeof(*definition));
    if (!bounded_copy(definition->name, sizeof(definition->name), name) ||
        !bounded_copy(definition->executable,
                      sizeof(definition->executable), executable))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    definition->structure_size = sizeof(*definition);
    definition->flags = runs_on | ASTRA_SERVICE_ENABLED;
    definition->start_policy = ASTRA_SERVICE_START_MANUAL;
    definition->restart_policy = ASTRA_SERVICE_RESTART_NEVER;
    return astra_service_definition_add_argument(definition, executable);
}

AstraResult astra_service_definition_add_argument(
    AstraServiceDefinition *definition, const char *argument)
{
    uint32_t length;

    if (definition == NULL || argument == NULL ||
        definition->structure_size != sizeof(*definition) ||
        definition->argument_count == UINT16_MAX ||
        definition->argument_length > sizeof(definition->arguments))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    length = (uint32_t)strlen(argument);
    if (!astra_utf8_validate(argument, length, 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (length + 1u > sizeof(definition->arguments) -
                      definition->argument_length)
        return ASTRA_ERROR_NO_RESOURCES;
    (void)memcpy(definition->arguments + definition->argument_length,
                 argument, length + 1u);
    definition->argument_length += (uint16_t)(length + 1u);
    ++definition->argument_count;
    return ASTRA_OK;
}

static AstraResult add_authority(char out[ASTRA_CAPABILITY_NAME_MAX],
                                 const char *name)
{
    char checked[ASTRA_CAPABILITY_NAME_MAX] = {0};

    if (!bounded_copy(checked, sizeof(checked), name) ||
        !capability_name_valid(checked))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    (void)memcpy(out, checked, sizeof(checked));
    return ASTRA_OK;
}

AstraResult astra_service_definition_add_grant(
    AstraServiceDefinition *definition, const char *name, uint32_t rights,
    int is_namespace)
{
    AstraResult result;

    if (definition == NULL ||
        definition->structure_size != sizeof(*definition) ||
        definition->grant_count >= ASTRA_LAUNCH_GRANT_MAX ||
        (rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u ||
        (is_namespace != 0) != (rights != 0u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = add_authority(
        definition->grants[definition->grant_count].name, name);
    if (result != ASTRA_OK)
        return result;
    definition->grants[definition->grant_count].rights = rights;
    definition->grants[definition->grant_count].is_namespace =
        is_namespace != 0;
    ++definition->grant_count;
    return ASTRA_OK;
}

AstraResult astra_service_definition_add_publication(
    AstraServiceDefinition *definition, const char *name, uint32_t rights)
{
    AstraResult result;

    if (definition == NULL ||
        definition->structure_size != sizeof(*definition) ||
        definition->publication_count >= ASTRA_MESSAGE_HANDLES_MAX ||
        (rights & ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = add_authority(
        definition->publications[definition->publication_count].name, name);
    if (result != ASTRA_OK)
        return result;
    definition->publications[definition->publication_count].rights = rights;
    ++definition->publication_count;
    return ASTRA_OK;
}

AstraResult astra_service_definition_add_dependency(
    AstraServiceDefinition *definition, const char *name)
{
    AstraResult result;

    if (definition == NULL ||
        definition->structure_size != sizeof(*definition) ||
        definition->dependency_count >= ASTRA_LAUNCH_GRANT_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = add_authority(
        definition->dependencies[definition->dependency_count], name);
    if (result == ASTRA_OK)
        ++definition->dependency_count;
    return result;
}

AstraResult astra_service_definition_validate(
    const AstraServiceDefinition *definition)
{
    uint32_t at = 0u;

    if (definition == NULL ||
        definition->structure_size != sizeof(*definition) ||
        !astra_service_name_valid(definition->name) ||
        !terminated_utf8(definition->executable,
                         sizeof(definition->executable)) ||
        (definition->flags & ~ASTRA_SERVICE_FLAG_MASK) != 0u ||
        (definition->flags &
         (ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_RUNS_PAIRED)) == 0u ||
        (definition->flags &
         (ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_RUNS_PAIRED)) ==
            (ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_RUNS_PAIRED) ||
        definition->start_policy < ASTRA_SERVICE_START_BOOT ||
        definition->start_policy > ASTRA_SERVICE_START_MANUAL ||
        definition->restart_policy > ASTRA_SERVICE_RESTART_ALWAYS ||
        definition->argument_count == 0u ||
        definition->argument_length == 0u ||
        definition->argument_length > sizeof(definition->arguments) ||
        definition->grant_count > ASTRA_LAUNCH_GRANT_MAX ||
        definition->publication_count > ASTRA_MESSAGE_HANDLES_MAX ||
        definition->dependency_count > ASTRA_LAUNCH_GRANT_MAX)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < definition->argument_count; ++index) {
        uint32_t start = at;

        while (at < definition->argument_length &&
               definition->arguments[at] != '\0')
            ++at;
        if (at == definition->argument_length ||
            !astra_utf8_validate(&definition->arguments[start], at - start,
                                 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        ++at;
    }
    if (at != definition->argument_length)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < definition->grant_count; ++index)
        if (!capability_name_valid(definition->grants[index].name) ||
            (definition->grants[index].rights &
             ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u ||
            definition->grants[index].is_namespace > 1u ||
            (definition->grants[index].is_namespace != 0u) !=
                (definition->grants[index].rights != 0u))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < definition->publication_count; ++index)
        if (!capability_name_valid(definition->publications[index].name) ||
            (definition->publications[index].rights &
             ~(ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE)) != 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < definition->dependency_count; ++index)
        if (!capability_name_valid(definition->dependencies[index]))
            return ASTRA_ERROR_INVALID_ARGUMENT;
    return ASTRA_OK;
}
