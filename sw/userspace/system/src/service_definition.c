#include <astra/service_manager.h>
#include <astra/service_definition_payload.h>
#include <astra/runtime.h>

#include <astra/utf8.h>

#include <string.h>

int astra_service_name_valid(const char *name)
{
    uint32_t length = 0u;

    if (name == NULL || name[0] == '\0' || name[0] == '.')
        return 0;
    while (name[length] != '\0') {
        char value = name[length];

        if (length + 1u >= ASTRA_SERVICE_NAME_MAX ||
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
    if (text == NULL) return 0;
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
    astra_service_definition_destroy(definition);
    if (!bounded_copy(definition->name, sizeof(definition->name), name))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraResult result = astra_service_definition_path_set(definition, executable);
    if (result != ASTRA_OK) return result;
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
        definition->executable == NULL ||
        !terminated_utf8(definition->executable, ASTRA_VFS_PATH_MAX) ||
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
        definition->argument_count > ASTRA_LAUNCH_ARGUMENT_MAX ||
        definition->argument_length == 0u ||
        definition->argument_length > sizeof(definition->arguments) ||
        definition->grant_count > ASTRA_LAUNCH_GRANT_MAX ||
        definition->publication_count > ASTRA_MESSAGE_HANDLES_MAX ||
        definition->dependency_count > ASTRA_LAUNCH_GRANT_MAX ||
        (definition->priority != 0u &&
         (definition->priority < ASTRA_PROCESS_PRIORITY_MIN ||
          definition->priority > ASTRA_PROCESS_PRIORITY_MAX)))
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

void astra_service_definition_destroy(AstraServiceDefinition *definition)
{
    if (definition == NULL) return;
    astra_runtime_deallocate(definition->executable);
    memset(definition, 0, sizeof(*definition));
}
void astra_service_info_destroy(AstraServiceInfo *info)
{
    if (info == NULL) return;
    astra_runtime_deallocate(info->executable);
    memset(info, 0, sizeof(*info));
}
AstraResult astra_service_definition_path_set(AstraServiceDefinition *definition, const char *path)
{
    if (definition == NULL || path == NULL) return ASTRA_ERROR_INVALID_ARGUMENT;
    uint32_t length = 0u;
    while (length < ASTRA_VFS_PATH_MAX && path[length] != '\0') ++length;
    if (length == ASTRA_VFS_PATH_MAX) return ASTRA_ERROR_NAME_TOO_LONG;
    if (length == 0u || !astra_utf8_validate(path, length, 0u)) return ASTRA_ERROR_INVALID_ARGUMENT;
    size_t size = length + 1u;
    char *copy = astra_runtime_allocate(size);
    if (copy == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    memcpy(copy, path, size);
    astra_runtime_deallocate(definition->executable);
    definition->executable = copy;
    return ASTRA_OK;
}
uint32_t astra_service_definition_payload_size(const AstraServiceDefinition *definition)
{
    return astra_service_definition_validate(definition) == ASTRA_OK ?
        (uint32_t)sizeof(AstraServiceDefinitionRecord) + (uint32_t)strlen(definition->executable) + 1u : 0u;
}
AstraResult astra_service_definition_encode(const AstraServiceDefinition *definition, void *bytes, uint32_t capacity)
{
    uint32_t size = astra_service_definition_payload_size(definition);
    if (size == 0u || bytes == NULL || size > capacity) return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraServiceDefinitionRecord *record = bytes;
    memset(record, 0, sizeof(*record));
    record->structure_size = sizeof(*record);
    record->executable_offset = sizeof(*record);
    record->executable_length = size - sizeof(*record);
    record->flags = definition->flags;
    record->start_policy = definition->start_policy;
    record->restart_policy = definition->restart_policy;
    record->argument_count = definition->argument_count;
    record->argument_length = definition->argument_length;
    record->grant_count = definition->grant_count;
    record->publication_count = definition->publication_count;
    record->dependency_count = definition->dependency_count;
    record->priority = definition->priority;
    memcpy(record->name, definition->name, sizeof(record->name));
    memcpy(record->arguments, definition->arguments, sizeof(record->arguments));
    memcpy(record->grants, definition->grants, sizeof(record->grants));
    memcpy(record->publications, definition->publications, sizeof(record->publications));
    memcpy(record->dependencies, definition->dependencies, sizeof(record->dependencies));
    memcpy((char *)bytes + record->executable_offset, definition->executable, record->executable_length);
    return ASTRA_OK;
}
AstraResult astra_service_definition_decode(const void *bytes, uint32_t length, AstraServiceDefinition *definition)
{
    if (bytes == NULL || definition == NULL || length < sizeof(AstraServiceDefinitionRecord) ||
        length > ASTRA_SERVICE_DEFINITION_PAYLOAD_MAX) return ASTRA_ERROR_INVALID_ARGUMENT;
    const AstraServiceDefinitionRecord *record = bytes;
    if (record->structure_size != sizeof(*record) || record->executable_offset != sizeof(*record) ||
        record->executable_length == 0u || record->executable_length > ASTRA_VFS_PATH_MAX ||
        record->executable_length != length - sizeof(*record)) return ASTRA_ERROR_INVALID_ARGUMENT;
    const char *path = (const char *)bytes + record->executable_offset;
    if (path[record->executable_length - 1u] != '\0' ||
        !terminated_utf8(path, record->executable_length) || strlen(path) + 1u != record->executable_length)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    AstraServiceDefinition *candidate = astra_runtime_allocate(sizeof(*candidate));
    if (candidate == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    memset(candidate, 0, sizeof(*candidate));
    candidate->structure_size = sizeof(*candidate);
    candidate->flags = record->flags;
    candidate->start_policy = record->start_policy;
    candidate->restart_policy = record->restart_policy;
    candidate->argument_count = record->argument_count;
    candidate->argument_length = record->argument_length;
    candidate->grant_count = record->grant_count;
    candidate->publication_count = record->publication_count;
    candidate->dependency_count = record->dependency_count;
    candidate->priority = record->priority;
    memcpy(candidate->name, record->name, sizeof(candidate->name));
    memcpy(candidate->arguments, record->arguments, sizeof(candidate->arguments));
    memcpy(candidate->grants, record->grants, sizeof(candidate->grants));
    memcpy(candidate->publications, record->publications, sizeof(candidate->publications));
    memcpy(candidate->dependencies, record->dependencies, sizeof(candidate->dependencies));
    AstraResult result = astra_service_definition_path_set(candidate, path);
    if (result == ASTRA_OK) result = astra_service_definition_validate(candidate);
    if (result == ASTRA_OK) {
        astra_service_definition_destroy(definition);
        memcpy(definition, candidate, sizeof(*definition));
        candidate->executable = NULL;
    }
    astra_service_definition_destroy(candidate);
    astra_runtime_deallocate(candidate);
    return result;
}
void astra_service_info_record(const AstraServiceInfo *info, AstraServiceInfoRecord *record)
{
    memset(record, 0, sizeof(*record));
    memcpy(record->name, info->name, sizeof(record->name));
    record->flags = info->flags;
    record->state = info->state;
    record->start_policy = info->start_policy;
    record->restart_policy = info->restart_policy;
    record->process_id = info->process_id;
    record->generation = info->generation;
    record->exit_status = info->exit_status;
    record->client_count = info->client_count;
}
AstraResult astra_service_info_decode(const AstraServiceInfoRecord *record, const char *path, uint32_t length,
    AstraServiceInfo *info)
{
    if (record == NULL || info == NULL || path == NULL || length == 0u || length > ASTRA_VFS_PATH_MAX ||
        path[length - 1u] != '\0' || !terminated_utf8(path, length) || strlen(path) + 1u != length ||
        record->name[sizeof(record->name) - 1u] != '\0' || !astra_service_name_valid(record->name) ||
        (record->flags & ~ASTRA_SERVICE_FLAG_MASK) != 0u || record->state > ASTRA_SERVICE_STATE_FAILED ||
        record->start_policy < ASTRA_SERVICE_START_BOOT || record->start_policy > ASTRA_SERVICE_START_MANUAL ||
        record->restart_policy > ASTRA_SERVICE_RESTART_ALWAYS) return ASTRA_ERROR_INVALID_ARGUMENT;
    char *copy = astra_runtime_allocate(length);
    if (copy == NULL) return ASTRA_ERROR_OUT_OF_MEMORY;
    memcpy(copy, path, length);
    astra_service_info_destroy(info);
    info->executable = copy;
    memcpy(info->name, record->name, sizeof(info->name));
    info->flags = record->flags;
    info->state = record->state;
    info->start_policy = record->start_policy;
    info->restart_policy = record->restart_policy;
    info->process_id = record->process_id;
    info->generation = record->generation;
    info->exit_status = record->exit_status;
    info->client_count = record->client_count;
    return ASTRA_OK;
}
