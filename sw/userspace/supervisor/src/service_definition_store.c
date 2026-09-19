#include <service_definition_store.h>

#include <loader.h>

#include <astra/config_document.h>
#include <astra/service_manager.h>
#include <astra/status.h>

#include <string.h>

static uint32_t one_string(const char *text, uint32_t length, const char *key,
                           char *out, uint32_t capacity)
{
    uint32_t count = 0u;
    uint32_t value_length = 0u;
    uint32_t status = astra_config_document_count(text, length, key, &count);

    if (status != ASTRA_CONFIG_OK || count != 1u)
        return ASTRA_STATUS_INVALID;
    status = astra_config_document_get(text, length, key, 0u, out, capacity,
                                       &value_length);
    return status == ASTRA_CONFIG_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
}

static uint32_t optional_bool(const char *text, uint32_t length,
                              const char *key, int *value)
{
    uint32_t count = 0u;
    uint32_t status = astra_config_document_count(text, length, key, &count);

    if (status != ASTRA_CONFIG_OK || count > 1u)
        return ASTRA_STATUS_INVALID;
    if (count == 0u)
        return ASTRA_STATUS_OK;
    return astra_config_document_get_bool(text, length, key, 0u, value) ==
        ASTRA_CONFIG_OK ? ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
}

static uint32_t string_list(const char *text, uint32_t length,
                            const char *key, char *out, uint32_t stride,
                            uint32_t capacity, uint32_t *count)
{
    uint32_t status = astra_config_document_count(text, length, key, count);

    if (status != ASTRA_CONFIG_OK || *count > capacity)
        return ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < *count; ++index) {
        uint32_t value_length = 0u;

        status = astra_config_document_get(text, length, key, index,
                                           out + index * stride, stride,
                                           &value_length);
        if (status != ASTRA_CONFIG_OK)
            return ASTRA_STATUS_INVALID;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t arguments(const char *text, uint32_t length,
                          AstraServiceDefinition *definition)
{
    uint32_t count = 0u;
    uint32_t status = astra_config_document_count(
        text, length, "argument", &count);
    uint32_t used = 0u;

    if (status != ASTRA_CONFIG_OK || count == 0u || count > UINT16_MAX)
        return ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t value_length = 0u;

        status = astra_config_document_get(
            text, length, "argument", index, NULL, 0u, &value_length);
        if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL ||
            value_length + 1u > sizeof(definition->arguments) - used)
            return ASTRA_STATUS_INVALID;
        status = astra_config_document_get(
            text, length, "argument", index, definition->arguments + used,
            sizeof(definition->arguments) - used, &value_length);
        if (status != ASTRA_CONFIG_OK)
            return ASTRA_STATUS_INVALID;
        used += value_length + 1u;
    }
    definition->argument_count = (uint16_t)count;
    definition->argument_length = (uint16_t)used;
    return ASTRA_STATUS_OK;
}

static uint32_t authorities(const char *text, uint32_t length,
                            const char *key, AstraServiceAuthority *out,
                            uint32_t capacity, uint32_t *count)
{
    char value[ASTRA_CAPABILITY_NAME_MAX + 4u];
    uint32_t status = astra_config_document_count(text, length, key, count);

    if (status != ASTRA_CONFIG_OK || *count > capacity)
        return ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < *count; ++index) {
        uint32_t value_length = 0u;
        SupervisorManifestGrant grant;

        status = astra_config_document_get(text, length, key, index, value,
                                           sizeof(value), &value_length);
        if (status != ASTRA_CONFIG_OK ||
            !supervisor_manifest_grant(value, &grant))
            return ASTRA_STATUS_INVALID;
        (void)memcpy(out[index].name, grant.name, sizeof(grant.name));
        out[index].rights = grant.rights;
        out[index].is_namespace = grant.is_namespace;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t publications(const char *text, uint32_t length,
                             AstraServicePublication *out,
                             uint32_t capacity, uint32_t *count)
{
    char value[ASTRA_CAPABILITY_NAME_MAX + 4u];
    uint32_t status = astra_config_document_count(
        text, length, "provides", count);

    if (status != ASTRA_CONFIG_OK || *count > capacity)
        return ASTRA_STATUS_INVALID;
    for (uint32_t index = 0u; index < *count; ++index) {
        uint32_t value_length = 0u;

        status = astra_config_document_get(text, length, "provides", index,
                                           value, sizeof(value),
                                           &value_length);
        if (status != ASTRA_CONFIG_OK ||
            !supervisor_manifest_authority(value, out[index].name,
                                           &out[index].rights, 1))
            return ASTRA_STATUS_INVALID;
    }
    return ASTRA_STATUS_OK;
}

uint32_t supervisor_service_definition_parse(
    const char *text, uint32_t length, AstraServiceDefinition *definition,
    uint32_t *error_line)
{
    AstraConfigDocumentError error = {0};
    char policy[16];
    int flag = 0;
    uint32_t status;

    if (definition == NULL)
        return ASTRA_STATUS_INVALID;
    (void)memset(definition, 0, sizeof(*definition));
    definition->structure_size = sizeof(*definition);
    if (error_line != NULL)
        *error_line = 0u;
    status = astra_config_document_validate(
        text, length, SUPERVISOR_SERVICE_DEFINITION_SCHEMA, &error);
    if (status != ASTRA_CONFIG_OK) {
        if (error_line != NULL)
            *error_line = error.line;
        return ASTRA_STATUS_INVALID;
    }
    if (one_string(text, length, "name", definition->name,
                   sizeof(definition->name)) != ASTRA_STATUS_OK ||
        one_string(text, length, "executable", definition->executable,
                   sizeof(definition->executable)) != ASTRA_STATUS_OK ||
        one_string(text, length, "runs", policy, sizeof(policy)) !=
            ASTRA_STATUS_OK)
        return ASTRA_STATUS_INVALID;
    if (strcmp(policy, "astra") == 0)
        definition->flags |= ASTRA_SERVICE_RUNS_ASTRA;
    else if (strcmp(policy, "paired") == 0)
        definition->flags |= ASTRA_SERVICE_RUNS_PAIRED;
    else
        return ASTRA_STATUS_INVALID;
    if (one_string(text, length, "start", policy, sizeof(policy)) !=
            ASTRA_STATUS_OK)
        return ASTRA_STATUS_INVALID;
    definition->start_policy = strcmp(policy, "boot") == 0 ?
        ASTRA_SERVICE_START_BOOT : strcmp(policy, "on-demand") == 0 ?
        ASTRA_SERVICE_START_ON_DEMAND : strcmp(policy, "manual") == 0 ?
        ASTRA_SERVICE_START_MANUAL : 0u;
    if (one_string(text, length, "restart", policy, sizeof(policy)) !=
            ASTRA_STATUS_OK)
        return ASTRA_STATUS_INVALID;
    definition->restart_policy = strcmp(policy, "never") == 0 ?
        ASTRA_SERVICE_RESTART_NEVER : strcmp(policy, "on-fault") == 0 ?
        ASTRA_SERVICE_RESTART_ON_FAULT : strcmp(policy, "always") == 0 ?
        ASTRA_SERVICE_RESTART_ALWAYS : UINT32_MAX;
    status = optional_bool(text, length, "enabled", &flag);
    if (status != ASTRA_STATUS_OK)
        return status;
    if (flag != 0)
        definition->flags |= ASTRA_SERVICE_ENABLED;
    flag = 0;
    status = optional_bool(text, length, "delegates", &flag);
    if (status != ASTRA_STATUS_OK)
        return status;
    if (flag != 0)
        definition->flags |= ASTRA_SERVICE_DELEGATES;
    if (arguments(text, length, definition) != ASTRA_STATUS_OK ||
        authorities(text, length, "grant", definition->grants,
                    ASTRA_LAUNCH_GRANT_MAX, &definition->grant_count) !=
            ASTRA_STATUS_OK ||
        publications(text, length, definition->publications,
                     ASTRA_MESSAGE_HANDLES_MAX,
                     &definition->publication_count) != ASTRA_STATUS_OK ||
        string_list(text, length, "needs",
                    &definition->dependencies[0][0],
                    ASTRA_CAPABILITY_NAME_MAX, ASTRA_LAUNCH_GRANT_MAX,
                    &definition->dependency_count) != ASTRA_STATUS_OK ||
        astra_service_definition_validate(definition) != ASTRA_OK)
        return ASTRA_STATUS_INVALID;
    return ASTRA_STATUS_OK;
}

typedef struct Writer {
    char *out;
    uint32_t capacity;
    uint32_t used;
} Writer;

static void bytes(Writer *writer, const char *value, uint32_t length)
{
    if (writer->out != NULL && writer->used < writer->capacity) {
        uint32_t available = writer->capacity - writer->used;
        uint32_t copied = length < available ? length : available;

        (void)memcpy(writer->out + writer->used, value, copied);
    }
    writer->used += length;
}

static void text(Writer *writer, const char *value)
{
    bytes(writer, value, (uint32_t)strlen(value));
}

static void quoted(Writer *writer, const char *key, const char *value)
{
    text(writer, key);
    text(writer, " \"");
    while (*value != '\0') {
        switch (*value) {
        case '\\': text(writer, "\\\\"); break;
        case '"': text(writer, "\\\""); break;
        case '\n': text(writer, "\\n"); break;
        case '\r': text(writer, "\\r"); break;
        case '\t': text(writer, "\\t"); break;
        default: bytes(writer, value, 1u); break;
        }
        ++value;
    }
    text(writer, "\"\n");
}

static void authority(Writer *writer, const char *key, const char *name,
                      uint32_t rights)
{
    text(writer, key);
    text(writer, " ");
    text(writer, name);
    if (rights != 0u)
        text(writer, rights == ASTRA_RIGHT_READ ? ":r" : ":rw");
    text(writer, "\n");
}

uint32_t supervisor_service_definition_serialize(
    const AstraServiceDefinition *definition, char *out, uint32_t capacity,
    uint32_t *required)
{
    Writer writer = {out, capacity, 0u};
    uint32_t argument = 0u;

    if (required == NULL ||
        astra_service_definition_validate(definition) != ASTRA_OK)
        return ASTRA_STATUS_INVALID;
    text(&writer, "astra-config 1\nschema 1\n");
    quoted(&writer, "name", definition->name);
    quoted(&writer, "executable", definition->executable);
    text(&writer, "runs ");
    text(&writer, (definition->flags & ASTRA_SERVICE_RUNS_PAIRED) != 0u ?
                  "paired\n" : "astra\n");
    text(&writer, "enabled ");
    text(&writer, (definition->flags & ASTRA_SERVICE_ENABLED) != 0u ?
                  "true\n" : "false\n");
    text(&writer, "delegates ");
    text(&writer, (definition->flags & ASTRA_SERVICE_DELEGATES) != 0u ?
                  "true\n" : "false\n");
    text(&writer, "start ");
    text(&writer, definition->start_policy == ASTRA_SERVICE_START_BOOT ?
                  "boot\n" : definition->start_policy ==
                  ASTRA_SERVICE_START_ON_DEMAND ? "on-demand\n" :
                  "manual\n");
    text(&writer, "restart ");
    text(&writer, definition->restart_policy == ASTRA_SERVICE_RESTART_ALWAYS ?
                  "always\n" : definition->restart_policy ==
                  ASTRA_SERVICE_RESTART_ON_FAULT ? "on-fault\n" :
                  "never\n");
    for (uint32_t index = 0u; index < definition->argument_count; ++index) {
        quoted(&writer, "argument", definition->arguments + argument);
        argument += (uint32_t)strlen(definition->arguments + argument) + 1u;
    }
    for (uint32_t index = 0u; index < definition->grant_count; ++index)
        authority(&writer, "grant", definition->grants[index].name,
                  definition->grants[index].rights);
    for (uint32_t index = 0u; index < definition->publication_count; ++index)
        authority(&writer, "provides", definition->publications[index].name,
                  definition->publications[index].rights);
    for (uint32_t index = 0u; index < definition->dependency_count; ++index)
        quoted(&writer, "needs", definition->dependencies[index]);
    *required = writer.used + 1u;
    if (out == NULL || capacity < *required)
        return ASTRA_STATUS_BUFFER_TOO_SMALL;
    out[writer.used] = '\0';
    return ASTRA_STATUS_OK;
}

static const char *service_leaf(const char *path)
{
    const char *leaf = path;

    for (const char *at = path; *at != '\0'; ++at)
        if (*at == ':' || *at == '/')
            leaf = at + 1u;
    return leaf;
}

uint32_t supervisor_service_definition_from_manifest(
    const SupervisorManifestEntry *entry, AstraServiceDefinition *definition)
{
    uint32_t length;

    if (entry == NULL || definition == NULL || entry->resident == 0u)
        return ASTRA_STATUS_INVALID;
    (void)memset(definition, 0, sizeof(*definition));
    definition->structure_size = sizeof(*definition);
    definition->flags = ASTRA_SERVICE_RUNS_ASTRA | ASTRA_SERVICE_ENABLED |
        (entry->required != 0u ? ASTRA_SERVICE_PROTECTED : 0u) |
        (entry->delegates != 0u ? ASTRA_SERVICE_DELEGATES : 0u);
    definition->start_policy = ASTRA_SERVICE_START_BOOT;
    definition->restart_policy = entry->required != 0u ?
        ASTRA_SERVICE_RESTART_ALWAYS : ASTRA_SERVICE_RESTART_ON_FAULT;
    if (strlen(service_leaf(entry->path)) >= sizeof(definition->name) ||
        strlen(entry->path) >= sizeof(definition->executable))
        return ASTRA_STATUS_LIMIT;
    (void)strcpy(definition->name, service_leaf(entry->path));
    (void)strcpy(definition->executable, entry->path);
    length = (uint32_t)strlen(entry->path) + 1u;
    (void)memcpy(definition->arguments, entry->path, length);
    definition->argument_count = 1u;
    definition->argument_length = (uint16_t)length;
    definition->grant_count = entry->grant_count;
    for (uint32_t index = 0u; index < entry->grant_count; ++index) {
        (void)memcpy(definition->grants[index].name,
                     entry->grants[index].name,
                     sizeof(definition->grants[index].name));
        definition->grants[index].rights = entry->grants[index].rights;
        definition->grants[index].is_namespace =
            entry->grants[index].is_namespace;
    }
    definition->publication_count = entry->serves_count;
    for (uint32_t index = 0u; index < entry->serves_count; ++index) {
        (void)memcpy(definition->publications[index].name,
                     entry->serves[index].name,
                     sizeof(definition->publications[index].name));
        definition->publications[index].rights = entry->serves[index].rights;
    }
    return astra_service_definition_validate(definition) == ASTRA_OK ?
        ASTRA_STATUS_OK : ASTRA_STATUS_INVALID;
}

uint32_t supervisor_service_definition_to_manifest(
    const AstraServiceDefinition *definition, SupervisorManifestEntry *entry)
{
    if (entry == NULL ||
        astra_service_definition_validate(definition) != ASTRA_OK ||
        strlen(definition->executable) >= sizeof(entry->path))
        return ASTRA_STATUS_INVALID;
    (void)memset(entry, 0, sizeof(*entry));
    (void)strcpy(entry->path, definition->executable);
    entry->resident = 1u;
    entry->delegates =
        (definition->flags & ASTRA_SERVICE_DELEGATES) != 0u;
    entry->required =
        (definition->flags & ASTRA_SERVICE_PROTECTED) != 0u;
    entry->grant_count = definition->grant_count;
    for (uint32_t index = 0u; index < definition->grant_count; ++index) {
        (void)memcpy(entry->grants[index].name,
                     definition->grants[index].name,
                     sizeof(entry->grants[index].name));
        entry->grants[index].rights = definition->grants[index].rights;
        entry->grants[index].is_namespace =
            definition->grants[index].is_namespace;
    }
    entry->serves_count = definition->publication_count;
    for (uint32_t index = 0u; index < definition->publication_count; ++index) {
        (void)memcpy(entry->serves[index].name,
                     definition->publications[index].name,
                     sizeof(entry->serves[index].name));
        entry->serves[index].rights =
            definition->publications[index].rights;
    }
    return ASTRA_STATUS_OK;
}
