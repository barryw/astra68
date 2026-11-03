#include <astra/config_document.h>
#include <astra/config_library.h>

#include <astra/library.h>
#include <astra/runtime.h>
#include <astra/secret_memory.h>
#include <astra/status.h>
#include <astra/vfs_client.h>
#include <astra/vfs_port_transport.h>

#include <stddef.h>
#include <string.h>

ASTRA_DYNAMIC_LIBRARY("config.library.2", 2, 0, 0,
              ASTRA_CONFIG_LIBRARY_ABI_MAJOR,
              ASTRA_CONFIG_LIBRARY_ABI_MINOR,
              "Astra68 contributors", "Copyright 2026 Astra68 contributors");

/* CONFIG is already a provider-rooted startup grant. Its startup-relative
 * prefix has the canonical capability bound; no full pathname scratch is
 * needed for the two fixed document leaves. State lives in its owned area. */
#define CONFIG_ROOT_CAPACITY (ASTRA_CAPABILITY_ROOT_MAX + 1u)
#define CONFIG_PATH_CAPACITY (CONFIG_ROOT_CAPACITY + sizeof(".settings.lock"))

typedef struct ConfigState {
    AstraVfsClient client;
    const AstraConfigSchema *schema;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t text_area;
    char *text;
    uint32_t length;
    char root[CONFIG_ROOT_CAPACITY];
    char path[CONFIG_PATH_CAPACITY];
    char temporary[CONFIG_PATH_CAPACITY];
    char lock_path[CONFIG_PATH_CAPACITY];
} ConfigState;

static int schema_valid(const AstraConfigSchema *schema)
{
    if (schema == NULL || schema->version == 0u || schema->fields == NULL ||
        schema->count == 0u || schema->count > 128u)
        return 0;
    for (uint32_t at = 0u; at < schema->count; ++at) {
        const AstraConfigField *field = &schema->fields[at];
        if (!astra_config_document_key_valid(field->key) ||
            (field->flags & ~ASTRA_CONFIG_FIELD_SENSITIVE) != 0u ||
            ((field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) != 0u &&
             strlen(field->key) > 120u))
            return 0;
        for (uint32_t prior = 0u; prior < at; ++prior)
            if (strcmp(schema->fields[prior].key, field->key) == 0)
                return 0;
    }
    return 1;
}

static int public_schema_valid(const AstraConfigSchema *schema)
{
    if (!schema_valid(schema)) return 0;
    for (uint32_t at = 0u; at < schema->count; ++at)
        if ((schema->fields[at].flags & ASTRA_CONFIG_FIELD_SENSITIVE) != 0u)
            return 0;
    return 1;
}

static const AstraConfigField *schema_field(const ConfigState *state,
                                            const char *key)
{
    if (state == NULL || key == NULL)
        return NULL;
    for (uint32_t at = 0u; at < state->schema->count; ++at)
        if (strcmp(state->schema->fields[at].key, key) == 0)
            return &state->schema->fields[at];
    return NULL;
}

static uint32_t map_status(uint32_t status)
{
    if (status == ASTRA_VFS_OK) return ASTRA_CONFIG_OK;
    if (status == ASTRA_VFS_ERR_NOT_FOUND) return ASTRA_CONFIG_NOT_FOUND;
    if (status == ASTRA_VFS_ERR_ACCESS) return ASTRA_CONFIG_ACCESS;
    if (status == ASTRA_VFS_ERR_BUSY) return ASTRA_CONFIG_BUSY;
    if (status == ASTRA_VFS_ERR_LIMIT || status == ASTRA_VFS_ERR_NO_SPACE)
        return ASTRA_CONFIG_NO_MEMORY;
    return ASTRA_CONFIG_IO;
}

static uint32_t area(uint32_t bytes, uint32_t *handle, void **address)
{
    uint32_t span = 0u;
    uint32_t status;

    *handle = 0u;
    *address = NULL;
    status = astra_rt_area_create(
        bytes == 0u ? 1u : bytes,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
        handle);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_area_map(*handle,
                                   ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                                   address, &span);
    if (status != ASTRA_SYSCALL_OK || span < bytes) {
        if (*address != NULL) (void)astra_rt_area_unmap(*address);
        if (*handle != 0u) (void)astra_close(*handle);
        *address = NULL;
        *handle = 0u;
        return ASTRA_CONFIG_NO_MEMORY;
    }
    return ASTRA_CONFIG_OK;
}

static void release_text(ConfigState *state)
{
    if (state->text != NULL)
        (void)astra_rt_area_unmap(state->text);
    if (state->text_area != 0u)
        (void)astra_close(state->text_area);
    state->text = NULL;
    state->text_area = 0u;
    state->length = 0u;
}

static int path(char *out, uint32_t capacity, const char *root, const char *leaf)
{
    uint32_t used = 0u;

    while (used < CONFIG_ROOT_CAPACITY && root[used] != '\0') {
        if (used + 1u >= capacity)
            return 0;
        out[used] = root[used];
        ++used;
    }
    if (used == CONFIG_ROOT_CAPACITY || used == 0u || out[0] != '/')
        return 0;
    if (out[used - 1u] != '/') {
        if (used + 1u >= capacity)
            return 0;
        out[used++] = '/';
    }
    while (*leaf != '\0') {
        if (used + 1u >= capacity)
            return 0;
        out[used++] = *leaf++;
    }
    out[used] = '\0';
    return 1;
}

static int root_copy(char *out, const char *root)
{
    uint32_t used = 0u;
    uint32_t source = 0u;

    if (root == NULL)
        return 0;
    /* An empty namespace root means the provider already rooted the grant. */
    if (root[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
        return 1;
    }
    if (root[0] != '/')
        out[used++] = '/';
    while (source < ASTRA_CAPABILITY_ROOT_MAX && root[source] != '\0') {
        if (used + 1u >= CONFIG_ROOT_CAPACITY)
            return 0;
        out[used++] = root[source++];
    }
    if (source == ASTRA_CAPABILITY_ROOT_MAX)
        return 0;
    while (used > 1u && out[used - 1u] == '/')
        --used;
    out[used] = '\0';
    return 1;
}

static uint32_t decimal(char *out, uint64_t value)
{
    char reverse[20];
    uint32_t count = 0u;

    do {
        reverse[count++] = (char)('0' + value % UINT64_C(10));
        value /= UINT64_C(10);
    } while (value != 0u);
    for (uint32_t at = 0u; at < count; ++at)
        out[at] = reverse[count - at - 1u];
    out[count] = '\0';
    return count;
}

static uint32_t initial(ConfigState *state)
{
    static const char prefix[] = "astra-config 1\nschema ";
    char version[11];
    uint32_t version_length = decimal(version, state->schema_version);
    uint32_t length = (uint32_t)(sizeof(prefix) - 1u) + version_length + 1u;
    uint32_t status = area(length + 1u, &state->text_area,
                           (void **)&state->text);

    if (status != ASTRA_CONFIG_OK)
        return status;
    (void)memcpy(state->text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(state->text + sizeof(prefix) - 1u, version, version_length);
    state->text[length - 1u] = '\n';
    state->text[length] = '\0';
    state->length = length;
    return ASTRA_CONFIG_OK;
}

static uint32_t load(ConfigState *state, AstraConfigError *error)
{
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t used = 0u, status;
    char *text = NULL;
    uint32_t text_area = 0u;

    if (error != NULL) {
        error->line = 0u;
        error->version = 0u;
    }
    status = astra_vfs_open(&state->client, state->path,
                            ASTRA_VFS_OPEN_READ, &file, &size, &kind);
    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (state->flags & ASTRA_CONFIG_OPEN_WRITE) != 0u) {
        release_text(state);
        return initial(state);
    }
    if (status != ASTRA_VFS_OK)
        return map_status(status);
    if (kind != ASTRA_VFS_KIND_FILE || size > UINT32_MAX - 1u) {
        (void)astra_vfs_close(&state->client, file);
        return ASTRA_CONFIG_INVALID;
    }
    status = area((uint32_t)size + 1u, &text_area, (void **)&text);
    if (status != ASTRA_CONFIG_OK) {
        (void)astra_vfs_close(&state->client, file);
        return status;
    }
    while (status == ASTRA_CONFIG_OK && used < (uint32_t)size) {
        uint32_t moved = 0u;

        status = astra_vfs_read(
            &state->client, file, used, text + used, (uint32_t)size - used,
            &moved);
        if (status == ASTRA_VFS_OK && moved == 0u)
            status = ASTRA_VFS_ERR_IO;
        used += moved;
    }
    {
        uint32_t close_status = astra_vfs_close(&state->client, file);
        if (status == ASTRA_VFS_OK)
            status = close_status;
    }
    if (status != ASTRA_VFS_OK) {
        if (text != NULL) (void)astra_rt_area_unmap(text);
        if (text_area != 0u) (void)astra_close(text_area);
        return map_status(status);
    }
    text[used] = '\0';
    {
        AstraConfigDocumentError document_error;

        status = astra_config_document_validate(
            text, used, state->schema_version, &document_error);
        if (status != ASTRA_CONFIG_OK) {
            if (error != NULL) {
                error->line = document_error.line;
                error->version = document_error.version;
            }
            (void)astra_rt_area_unmap(text);
            (void)astra_close(text_area);
            return status;
        }
    }
    release_text(state);
    state->text = text;
    state->text_area = text_area;
    state->length = used;
    return ASTRA_CONFIG_OK;
}

static uint32_t open_capability(const AstraStartupInfo *startup,
                                const char *capability_name,
                                const char *owner,
                                const AstraConfigSchema *schema, uint32_t flags,
                                AstraConfig *config, AstraConfigError *error)
{
    const AstraStartupCapability *capability;
    ConfigState *state = NULL;
    uint32_t state_area = 0u, status;

    if (config == NULL || config->_private_state != NULL ||
        config->_private_area != 0u || !astra_startup_validate(startup) ||
        !schema_valid(schema) || flags == 0u ||
        (flags & ~(ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE)) != 0u)
        return ASTRA_CONFIG_INVALID;
    capability = astra_startup_capability(startup, capability_name);
    if (capability == NULL ||
        (capability->flags & ASTRA_CAPABILITY_FLAG_NAMESPACE) == 0u ||
        (capability->flags & ASTRA_CAPABILITY_FLAG_READ) == 0u ||
        ((flags & ASTRA_CONFIG_OPEN_WRITE) != 0u &&
         (capability->flags & ASTRA_CAPABILITY_FLAG_WRITE) == 0u))
        return ASTRA_CONFIG_ACCESS;
    status = area(sizeof(*state), &state_area, (void **)&state);
    if (status != ASTRA_CONFIG_OK)
        return status;
    (void)memset(state, 0, sizeof(*state));
    state->schema_version = schema->version;
    state->schema = schema;
    state->flags = flags;
    if (!root_copy(state->root, capability->root)) {
        status = ASTRA_CONFIG_INVALID;
        goto failed;
    }
    if (owner != NULL) {
        char child[CONFIG_ROOT_CAPACITY];
        if (astra_config_owner_root(state->root, owner, child,
                                    sizeof(child)) != ASTRA_CONFIG_OK) {
            status = ASTRA_CONFIG_INVALID;
            goto failed;
        }
        strcpy(state->root, child);
    }
    if (
        !path(state->path, sizeof(state->path), state->root, "settings.conf") ||
        !path(state->temporary, sizeof(state->temporary), state->root, ".settings.tmp") ||
        !path(state->lock_path, sizeof(state->lock_path), state->root, ".settings.lock")) {
        status = ASTRA_CONFIG_INVALID;
        goto failed;
    }
    status = astra_vfs_client_connect_service(&state->client,
                                              capability->handle);
    if (status != ASTRA_VFS_OK) {
        status = map_status(status);
        goto failed;
    }
    if ((flags & ASTRA_CONFIG_OPEN_WRITE) != 0u) {
        status = astra_vfs_mkdir_mode(&state->client, state->root, 0700u);
        if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS) {
            status = map_status(status);
            goto failed;
        }
    }
    status = load(state, error);
    if (status != ASTRA_CONFIG_OK)
        goto failed;
    config->_private_area = state_area;
    config->_private_state = state;
    return ASTRA_CONFIG_OK;

failed:
    release_text(state);
    if (state->client.transport != NULL)
        (void)astra_vfs_disconnect(&state->client);
    (void)astra_rt_area_unmap(state);
    (void)astra_close(state_area);
    return status;
}

uint32_t astra_config_open(const AstraStartupInfo *startup,
                            const AstraConfigSchema *schema, uint32_t flags,
                            AstraConfig *config, AstraConfigError *error)
{
    return open_capability(startup, ASTRA_CONFIG_CAPABILITY, NULL,
                           schema, flags, config, error);
}

uint32_t astra_config_open_system(const AstraStartupInfo *startup,
                                   const AstraConfigSchema *schema, uint32_t flags,
                                   AstraConfig *config, AstraConfigError *error)
{
    return open_capability(startup, ASTRA_CONFIG_SYSTEM_CAPABILITY, NULL,
                           schema, flags, config, error);
}

uint32_t astra_config_open_public(const AstraStartupInfo *startup,
    const char *owner, const AstraConfigSchema *schema,
    AstraConfig *config, AstraConfigError *error)
{
    if (owner == NULL || !public_schema_valid(schema))
        return ASTRA_CONFIG_INVALID;
    return open_capability(startup, ASTRA_CONFIG_PUBLIC_CAPABILITY, owner,
        schema, ASTRA_CONFIG_OPEN_READ, config, error);
}

uint32_t astra_config_open_publish(const AstraStartupInfo *startup,
    const AstraConfigSchema *schema, AstraConfig *config,
    AstraConfigError *error)
{
    if (!public_schema_valid(schema))
        return ASTRA_CONFIG_INVALID;
    return open_capability(startup, ASTRA_CONFIG_PUBLISH_CAPABILITY, NULL,
        schema, ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE, config, error);
}

void astra_config_close(AstraConfig *config)
{
    ConfigState *state;
    uint32_t state_area;

    if (config == NULL || config->_private_state == NULL)
        return;
    state = config->_private_state;
    state_area = config->_private_area;
    release_text(state);
    (void)astra_vfs_disconnect(&state->client);
    (void)astra_rt_area_unmap(state);
    (void)astra_close(state_area);
    *config = (AstraConfig)ASTRA_CONFIG_INIT;
}

static ConfigState *state_of(const AstraConfig *config)
{
    return config != NULL && config->_private_state != NULL &&
           config->_private_area != 0u ? config->_private_state : NULL;
}

uint32_t astra_config_reload(AstraConfig *config, AstraConfigError *error)
{
    ConfigState *state = state_of(config);

    return state != NULL ? load(state, error) : ASTRA_CONFIG_CLOSED;
}

static uint32_t sensitive_get_string(ConfigState *state, const char *key,
    uint32_t index, char *out, uint32_t capacity, uint32_t *length);
static uint32_t sensitive_set_string(ConfigState *state, const char *key,
    uint32_t index, const char *value);

uint32_t astra_config_count(const AstraConfig *config, const char *key,
                             uint32_t *count)
{
    const ConfigState *state = state_of(config);

    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (schema_field(state, key) == NULL) return ASTRA_CONFIG_INVALID;
    return astra_config_document_count(state->text, state->length, key, count);
}

uint32_t astra_config_get_string(const AstraConfig *config, const char *key,
                                  uint32_t index, char *out,
                                  uint32_t capacity, uint32_t *length)
{
    const ConfigState *state = state_of(config);

    const AstraConfigField *field = schema_field(state, key);
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (field == NULL) return ASTRA_CONFIG_INVALID;
    return (field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) != 0u ?
        sensitive_get_string((ConfigState *)state, key, index, out, capacity, length) :
        astra_config_document_get(state->text, state->length, key, index,
                                  out, capacity, length);
}

uint32_t astra_config_get_i64(const AstraConfig *config, const char *key,
                               uint32_t index, int64_t *value)
{
    const ConfigState *state = state_of(config);
    const AstraConfigField *field = schema_field(state, key);
    char text[21];
    uint32_t length = 0u, status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (field == NULL) return ASTRA_CONFIG_INVALID;
    if ((field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) == 0u)
        return astra_config_document_get_i64(state->text, state->length,
                                              key, index, value);
    status = astra_config_get_string(config, key, index, text, sizeof(text), &length);
    if (status == ASTRA_CONFIG_OK)
        status = astra_config_value_i64(text, length, value);
    astra_secret_wipe(text, sizeof(text));
    return status == ASTRA_CONFIG_BUFFER_TOO_SMALL ? ASTRA_CONFIG_MALFORMED : status;
}

uint32_t astra_config_get_u64(const AstraConfig *config, const char *key,
                               uint32_t index, uint64_t *value)
{
    const ConfigState *state = state_of(config);
    const AstraConfigField *field = schema_field(state, key);
    char text[21];
    uint32_t length = 0u, status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (field == NULL) return ASTRA_CONFIG_INVALID;
    if ((field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) == 0u)
        return astra_config_document_get_u64(state->text, state->length,
                                              key, index, value);
    status = astra_config_get_string(config, key, index, text, sizeof(text), &length);
    if (status == ASTRA_CONFIG_OK)
        status = astra_config_value_u64(text, length, value);
    astra_secret_wipe(text, sizeof(text));
    return status == ASTRA_CONFIG_BUFFER_TOO_SMALL ? ASTRA_CONFIG_MALFORMED : status;
}

uint32_t astra_config_get_bool(const AstraConfig *config, const char *key,
                                uint32_t index, int *value)
{
    const ConfigState *state = state_of(config);
    const AstraConfigField *field = schema_field(state, key);
    char text[6];
    uint32_t length = 0u, status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (field == NULL) return ASTRA_CONFIG_INVALID;
    if ((field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) == 0u)
        return astra_config_document_get_bool(state->text, state->length,
                                               key, index, value);
    status = astra_config_get_string(config, key, index, text, sizeof(text), &length);
    if (status == ASTRA_CONFIG_OK)
        status = astra_config_value_bool(text, length, value);
    astra_secret_wipe(text, sizeof(text));
    return status == ASTRA_CONFIG_BUFFER_TOO_SMALL ? ASTRA_CONFIG_MALFORMED : status;
}

static uint32_t commit(ConfigState *state, uint32_t text_area, char *text,
                       uint32_t length)
{
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t old_size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t used = 0u, status;

    if ((state->flags & ASTRA_CONFIG_OPEN_WRITE) == 0u)
        goto denied;
    status = astra_vfs_unlink(&state->client, state->temporary, 0u);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND)
        goto failed;
    status = astra_vfs_open_mode(
        &state->client, state->temporary,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_TRUNCATE,
        0600u, &file, &old_size, &kind);
    while (status == ASTRA_VFS_OK && used < length) {
        uint32_t moved = 0u;

        status = astra_vfs_write(
            &state->client, file, used, UINT64_MAX, text + used, length - used, &moved);
        if (status == ASTRA_VFS_OK && moved == 0u)
            status = ASTRA_VFS_ERR_IO;
        used += moved;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_sync(&state->client, file);
    if (file != ASTRA_VFS_FILE_INVALID) {
        uint32_t close_status = astra_vfs_close(&state->client, file);
        if (status == ASTRA_VFS_OK)
            status = close_status;
        file = ASTRA_VFS_FILE_INVALID;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_rename(&state->client, state->temporary,
                                  state->path);
    if (status != ASTRA_VFS_OK)
        goto failed;
    release_text(state);
    state->text_area = text_area;
    state->text = text;
    state->length = length;
    return ASTRA_CONFIG_OK;

failed:
    if (file != ASTRA_VFS_FILE_INVALID)
        (void)astra_vfs_close(&state->client, file);
    (void)astra_vfs_unlink(&state->client, state->temporary, 0u);
    (void)astra_rt_area_unmap(text);
    (void)astra_close(text_area);
    return map_status(status);

denied:
    (void)astra_rt_area_unmap(text);
    (void)astra_close(text_area);
    return ASTRA_CONFIG_ACCESS;
}

static uint32_t transform(ConfigState *state, const char *source,
    uint32_t source_length, const char *key, uint32_t index,
    const char *value, int remove, uint32_t *text_area, char **text,
    uint32_t *text_length)
{
    uint32_t required = 0u, status;

    if (state == NULL)
        return ASTRA_CONFIG_CLOSED;
    if (schema_field(state, key) == NULL)
        return ASTRA_CONFIG_INVALID;
    status = remove ? astra_config_document_remove(
                          source, source_length, key, index,
                          NULL, 0u, &required) :
                      astra_config_document_replace(
                          source, source_length, key, index, value,
                          NULL, 0u, &required);
    if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL)
        return status;
    status = area(required, text_area, (void **)text);
    if (status != ASTRA_CONFIG_OK)
        return status;
    status = remove ? astra_config_document_remove(
                          source, source_length, key, index,
                          *text, required, &required) :
                      astra_config_document_replace(
                          source, source_length, key, index, value,
                          *text, required, &required);
    if (status != ASTRA_CONFIG_OK) {
        (void)astra_rt_area_unmap(*text);
        (void)astra_close(*text_area);
        *text = NULL;
        *text_area = 0u;
        return status;
    }
    *text_length = required - 1u;
    return ASTRA_CONFIG_OK;
}

static uint32_t change(ConfigState *state, const char *key, uint32_t index,
                       const char *value, int remove)
{
    char *text = NULL;
    uint32_t text_area = 0u, length = 0u;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    uint32_t status = transform(state, state->text, state->length, key,
        index, value, remove, &text_area, &text, &length);
    return status == ASTRA_CONFIG_OK ?
        commit(state, text_area, text, length) : status;
}

static uint32_t load_if_unchanged(ConfigState *state)
{
    char *previous = state->text;
    uint32_t previous_area = state->text_area;
    uint32_t previous_length = state->length;
    uint32_t status;

    state->text = NULL;
    state->text_area = 0u;
    state->length = 0u;
    status = load(state, NULL);
    if (status != ASTRA_CONFIG_OK) {
        release_text(state);
        state->text = previous;
        state->text_area = previous_area;
        state->length = previous_length;
        return status;
    }
    int changed = previous_length != state->length ||
        memcmp(previous, state->text, previous_length) != 0;
    (void)astra_rt_area_unmap(previous);
    (void)astra_close(previous_area);
    return changed ? ASTRA_CONFIG_CONFLICT : ASTRA_CONFIG_OK;
}

/* HostFS owns flock state on its open description. A dead writer's close
 * releases it, unlike a shared-memory futex or persistent lock directory. */
static uint32_t begin_write(ConfigState *state, AstraVfsFile *lock_file,
                            int require_unchanged)
{
    uint64_t size = 0u;
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;
    uint32_t status;

    *lock_file = ASTRA_VFS_FILE_INVALID;
    if ((state->flags & ASTRA_CONFIG_OPEN_WRITE) == 0u)
        return ASTRA_CONFIG_ACCESS;
    status = astra_vfs_open_mode(&state->client, state->lock_path,
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_NOFOLLOW, 0600u, lock_file, &size, &kind);
    if (status != ASTRA_VFS_OK)
        return map_status(status);
    if (kind != ASTRA_VFS_KIND_FILE) {
        status = ASTRA_VFS_ERR_INVALID;
        goto failed;
    }
    for (uint32_t attempt = 0u; attempt < 5000u; ++attempt) {
        status = astra_vfs_try_lock(&state->client, *lock_file);
        if (status == ASTRA_VFS_OK) {
            uint32_t loaded = require_unchanged ?
                load_if_unchanged(state) : load(state, NULL);
            if (loaded == ASTRA_CONFIG_OK) return loaded;
            (void)astra_vfs_close(&state->client, *lock_file);
            *lock_file = ASTRA_VFS_FILE_INVALID;
            return loaded;
        }
        if (status != ASTRA_VFS_ERR_BUSY)
            goto failed;
        uint32_t slept = astra_rt_thread_sleep(1000000ull,
            ASTRA_THREAD_SLEEP_RELATIVE, 0u, NULL);
        if (slept != ASTRA_SYSCALL_TIMED_OUT &&
            slept != ASTRA_SYSCALL_CANCELLED) {
            status = ASTRA_VFS_ERR_IO;
            goto failed;
        }
    }
    status = ASTRA_VFS_ERR_BUSY;
failed:
    (void)astra_vfs_close(&state->client, *lock_file);
    *lock_file = ASTRA_VFS_FILE_INVALID;
    return map_status(status);
}

static uint32_t end_write(ConfigState *state, AstraVfsFile lock_file,
                          uint32_t status)
{
    uint32_t closed = astra_vfs_close(&state->client, lock_file);
    return status == ASTRA_CONFIG_OK ? map_status(closed) : status;
}

static uint32_t set_unlocked(ConfigState *state, const char *key,
                             uint32_t index, const char *value)
{
    const AstraConfigField *field = schema_field(state, key);
    return (field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) != 0u ?
        sensitive_set_string(state, key, index, value) :
        change(state, key, index, value, 0);
}

static uint32_t set_string(AstraConfig *config, const char *key,
                           uint32_t index, const char *value,
                           int require_unchanged)
{
    ConfigState *state = state_of(config);
    AstraVfsFile lock_file;
    uint32_t status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (value == NULL || schema_field(state, key) == NULL)
        return ASTRA_CONFIG_INVALID;
    status = begin_write(state, &lock_file, require_unchanged);
    if (status != ASTRA_CONFIG_OK) return status;
    return end_write(state, lock_file,
        set_unlocked(state, key, index, value));
}

uint32_t astra_config_set_string(AstraConfig *config, const char *key,
    uint32_t index, const char *value)
{
    return set_string(config, key, index, value, 0);
}

uint32_t astra_config_set_string_if_unchanged(AstraConfig *config,
    const char *key, uint32_t index, const char *value)
{
    return set_string(config, key, index, value, 1);
}

#define CONFIG_SECRET_MAX 1024u
#define CONFIG_SECRET_OVERHEAD 44u
#define CONFIG_SECRET_ENVELOPE (CONFIG_SECRET_MAX + CONFIG_SECRET_OVERHEAD)

static uint32_t secret_crypt(ConfigState *state, const char *name,
                             uint16_t operation, const uint8_t *input,
                             uint32_t input_length, uint8_t *output,
                             uint32_t output_capacity, uint32_t *output_length)
{
    uint32_t name_bytes = (uint32_t)strlen(name) + 1u;
    uint8_t payload[128u + CONFIG_SECRET_ENVELOPE];
    AstraVfsRequest request = {0};
    AstraVfsReply reply = {0};
    AstraVfsTransfer transfer = {0};
    uint32_t status;

    *output_length = 0u;
    if (name_bytes > 128u || input_length > CONFIG_SECRET_ENVELOPE ||
        output_capacity > CONFIG_SECRET_ENVELOPE)
        return ASTRA_CONFIG_INVALID;
    memcpy(payload, name, name_bytes);
    memcpy(payload + name_bytes, input, input_length);
    request.size = ASTRA_VFS_REQUEST_SIZE;
    request.version = ASTRA_VFS_VERSION;
    request.session = ASTRA_VFS_SESSION_INVALID;
    request.file = ASTRA_VFS_FILE_INVALID;
    request.flags = operation == ASTRA_VFS_CONFIG_SEAL ?
        ASTRA_VFS_CONFIG_SEAL : ASTRA_VFS_CONFIG_OPEN;
    request.offset = name_bytes;
    request.length = input_length;
    request.write_limit = output_capacity;
    transfer.input = payload;
    transfer.input_size = name_bytes + input_length;
    transfer.output = output;
    transfer.output_capacity = output_capacity;
    status = astra_vfs_port_transport(&state->client,
        ASTRA_VFS_OP_CONFIG_CRYPT, &request, &transfer, &reply);
    astra_vfs_port_wipe_area(&state->client,
        name_bytes + input_length > output_capacity ?
            name_bytes + input_length : output_capacity);
    astra_secret_wipe(payload, sizeof(payload));
    if (status != ASTRA_VFS_OK)
        return map_status(status);
    if (reply.status == ASTRA_VFS_ERR_ACCESS)
        return ASTRA_CONFIG_AUTH_FAILED;
    if (reply.status != ASTRA_VFS_OK)
        return map_status(reply.status);
    if (reply.count == 0u || reply.count > output_capacity)
        return ASTRA_CONFIG_IO;
    *output_length = reply.count;
    status = ASTRA_CONFIG_OK;
    return status;
}

static uint32_t sensitive_encode(ConfigState *state, const char *key,
    const char *value, char encoded[CONFIG_SECRET_ENVELOPE * 2u + 1u])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t sealed[CONFIG_SECRET_ENVELOPE];
    uint32_t sealed_length = 0u, status;
    uint32_t length = (uint32_t)strlen(value);
    if ((state->flags & ASTRA_CONFIG_OPEN_WRITE) == 0u)
        return ASTRA_CONFIG_ACCESS;
    if (length == 0u || length > CONFIG_SECRET_MAX)
        return ASTRA_CONFIG_INVALID;
    status = secret_crypt(state, key, ASTRA_VFS_CONFIG_SEAL,
        (const uint8_t *)value, length, sealed, sizeof(sealed), &sealed_length);
    if (status == ASTRA_CONFIG_OK) {
        for (uint32_t at = 0u; at < sealed_length; ++at) {
            encoded[at * 2u] = hex[sealed[at] >> 4];
            encoded[at * 2u + 1u] = hex[sealed[at] & 15u];
        }
        encoded[sealed_length * 2u] = '\0';
    }
    astra_secret_wipe(sealed, sizeof(sealed));
    return status;
}

static uint32_t sensitive_set_string(ConfigState *state, const char *key,
                                     uint32_t index, const char *value)
{
    char encoded[CONFIG_SECRET_ENVELOPE * 2u + 1u] = {0};
    uint32_t status = sensitive_encode(state, key, value, encoded);
    if (status == ASTRA_CONFIG_OK)
        status = change(state, key, index, encoded, 0);
    astra_secret_wipe(encoded, sizeof(encoded));
    return status;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static uint32_t sensitive_get_string(ConfigState *state, const char *key,
                                     uint32_t index, char *out,
                                     uint32_t capacity, uint32_t *length)
{
    char encoded[CONFIG_SECRET_ENVELOPE * 2u + 1u];
    uint8_t sealed[CONFIG_SECRET_ENVELOPE];
    uint32_t encoded_length = 0u, plain_length = 0u, status;
    if (length == NULL) return ASTRA_CONFIG_INVALID;
    *length = 0u;
    status = astra_config_document_get(state->text, state->length, key, index,
        encoded, sizeof(encoded), &encoded_length);
    if (status != ASTRA_CONFIG_OK) return status;
    if ((encoded_length & 1u) != 0u ||
        encoded_length <= CONFIG_SECRET_OVERHEAD * 2u) return ASTRA_CONFIG_AUTH_FAILED;
    uint32_t sealed_length = encoded_length / 2u;
    for (uint32_t at = 0u; at < sealed_length; ++at) {
        int high = hex_digit(encoded[at * 2u]);
        int low = hex_digit(encoded[at * 2u + 1u]);
        if (high < 0 || low < 0) {
            status = ASTRA_CONFIG_AUTH_FAILED;
            goto done;
        }
        sealed[at] = (uint8_t)((high << 4) | low);
    }
    *length = sealed_length - CONFIG_SECRET_OVERHEAD;
    if (out == NULL || capacity <= *length) {
        status = ASTRA_CONFIG_BUFFER_TOO_SMALL;
        goto done;
    }
    status = secret_crypt(state, key, ASTRA_VFS_CONFIG_OPEN,
        sealed, sealed_length, (uint8_t *)out, *length, &plain_length);
    if (status == ASTRA_CONFIG_OK && plain_length != *length) {
        astra_secret_wipe(out, plain_length);
        status = ASTRA_CONFIG_AUTH_FAILED;
    }
    if (status == ASTRA_CONFIG_OK) out[*length] = '\0';
done:
    astra_secret_wipe(sealed, sizeof(sealed));
    astra_secret_wipe(encoded, sizeof(encoded));
    return status;
}

static uint32_t signed_decimal(char *out, int64_t value)
{
    uint64_t magnitude;

    if (value >= 0)
        return decimal(out, (uint64_t)value);
    out[0] = '-';
    magnitude = value == INT64_MIN ? (uint64_t)INT64_MAX + UINT64_C(1) :
                                     (uint64_t)-value;
    return 1u + decimal(out + 1u, magnitude);
}

uint32_t astra_config_set_i64(AstraConfig *config, const char *key, uint32_t index,
                        int64_t value)
{
    char number[21];

    (void)signed_decimal(number, value);
    return astra_config_set_string(config, key, index, number);
}

uint32_t astra_config_set_u64(AstraConfig *config, const char *key, uint32_t index,
                        uint64_t value)
{
    char number[21];

    (void)decimal(number, value);
    return astra_config_set_string(config, key, index, number);
}

uint32_t astra_config_set_bool(AstraConfig *config, const char *key, uint32_t index,
                         int value)
{
    return astra_config_set_string(config, key, index, value != 0 ? "true" : "false");
}

static uint32_t append_string(AstraConfig *config, const char *key,
                              const char *value, int require_unchanged)
{
    ConfigState *state = state_of(config);
    AstraVfsFile lock_file;
    uint32_t count = 0u;
    uint32_t status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (value == NULL || schema_field(state, key) == NULL)
        return ASTRA_CONFIG_INVALID;
    status = begin_write(state, &lock_file, require_unchanged);
    if (status != ASTRA_CONFIG_OK) return status;
    status = astra_config_document_count(state->text, state->length, key, &count);
    if (status == ASTRA_CONFIG_OK)
        status = set_unlocked(state, key, count, value);
    return end_write(state, lock_file, status);
}

uint32_t astra_config_append_string(AstraConfig *config, const char *key,
    const char *value)
{
    return append_string(config, key, value, 0);
}

uint32_t astra_config_append_string_if_unchanged(AstraConfig *config,
    const char *key, const char *value)
{
    return append_string(config, key, value, 1);
}

uint32_t astra_config_append_i64(AstraConfig *config, const char *key,
                           int64_t value)
{
    char number[21];

    (void)signed_decimal(number, value);
    return astra_config_append_string(config, key, number);
}

uint32_t astra_config_append_u64(AstraConfig *config, const char *key,
                           uint64_t value)
{
    char number[21];

    (void)decimal(number, value);
    return astra_config_append_string(config, key, number);
}

uint32_t astra_config_append_bool(AstraConfig *config, const char *key, int value)
{
    return astra_config_append_string(config, key, value != 0 ? "true" : "false");
}

uint32_t astra_config_remove(AstraConfig *config, const char *key,
                             uint32_t index)
{
    ConfigState *state = state_of(config);
    AstraVfsFile lock_file;
    uint32_t status;
    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (schema_field(state, key) == NULL) return ASTRA_CONFIG_INVALID;
    status = begin_write(state, &lock_file, 0);
    if (status != ASTRA_CONFIG_OK) return status;
    return end_write(state, lock_file,
        change(state, key, index, NULL, 1));
}

static uint32_t apply_changes(AstraConfig *config,
    const AstraConfigChange *changes, uint32_t count, int require_unchanged)
{
    ConfigState *state = state_of(config);
    AstraVfsFile lock_file;
    char *working_text;
    uint32_t working_area = 0u, working_length, status;

    if (state == NULL) return ASTRA_CONFIG_CLOSED;
    if (changes == NULL || count == 0u || count > 64u)
        return ASTRA_CONFIG_INVALID;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraConfigChange *item = &changes[at];
        if (schema_field(state, item->key) == NULL ||
            (item->operation != ASTRA_CONFIG_CHANGE_SET &&
             item->operation != ASTRA_CONFIG_CHANGE_APPEND &&
             item->operation != ASTRA_CONFIG_CHANGE_REMOVE) ||
            (item->operation == ASTRA_CONFIG_CHANGE_REMOVE ?
                item->value != NULL : item->value == NULL) ||
            (item->operation == ASTRA_CONFIG_CHANGE_APPEND && item->index != 0u))
            return ASTRA_CONFIG_INVALID;
    }
    status = begin_write(state, &lock_file, require_unchanged);
    if (status != ASTRA_CONFIG_OK) return status;
    working_text = state->text;
    working_length = state->length;
    for (uint32_t at = 0u; at < count; ++at) {
        const AstraConfigChange *item = &changes[at];
        const AstraConfigField *field = schema_field(state, item->key);
        char encoded[CONFIG_SECRET_ENVELOPE * 2u + 1u] = {0};
        char *next_text = NULL;
        uint32_t next_area = 0u, next_length = 0u;
        uint32_t index = item->index;
        const char *value = item->value;
        if (item->operation == ASTRA_CONFIG_CHANGE_APPEND)
            status = astra_config_document_count(working_text,
                working_length, item->key, &index);
        if (status == ASTRA_CONFIG_OK && value != NULL &&
            (field->flags & ASTRA_CONFIG_FIELD_SENSITIVE) != 0u) {
            status = sensitive_encode(state, item->key, value, encoded);
            value = encoded;
        }
        if (status == ASTRA_CONFIG_OK)
            status = transform(state, working_text, working_length,
                item->key, index, value,
                item->operation == ASTRA_CONFIG_CHANGE_REMOVE,
                &next_area, &next_text, &next_length);
        astra_secret_wipe(encoded, sizeof(encoded));
        if (status != ASTRA_CONFIG_OK) break;
        if (working_area != 0u) {
            (void)astra_rt_area_unmap(working_text);
            (void)astra_close(working_area);
        }
        working_area = next_area;
        working_text = next_text;
        working_length = next_length;
    }
    if (status == ASTRA_CONFIG_OK) {
        status = commit(state, working_area, working_text, working_length);
        working_area = 0u;
    }
    if (working_area != 0u) {
        (void)astra_rt_area_unmap(working_text);
        (void)astra_close(working_area);
    }
    return end_write(state, lock_file, status);
}

uint32_t astra_config_apply(AstraConfig *config,
    const AstraConfigChange *changes, uint32_t count)
{
    return apply_changes(config, changes, count, 0);
}

uint32_t astra_config_apply_if_unchanged(AstraConfig *config,
    const AstraConfigChange *changes, uint32_t count)
{
    return apply_changes(config, changes, count, 1);
}
