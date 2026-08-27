#include <astra/config_document.h>
#include <astra/config_library.h>

#include <astra/library.h>
#include <astra/runtime.h>
#include <astra/vfs_client.h>
#include <astra/vfs_port_transport.h>

#include <stddef.h>
#include <string.h>

ASTRA_LIBRARY("config.library", 1, 0, 0,
              ASTRA_CONFIG_LIBRARY_ABI_MAJOR,
              ASTRA_CONFIG_LIBRARY_ABI_MINOR,
              "Astra68 contributors", "Copyright 2026 Astra68 contributors");

typedef struct ConfigState {
    AstraVfsClient client;
    uint32_t schema_version;
    uint32_t flags;
    uint32_t text_area;
    char *text;
    uint32_t length;
    char root[ASTRA_VFS_PATH_MAX];
    char path[ASTRA_VFS_PATH_MAX];
    char temporary[ASTRA_VFS_PATH_MAX];
} ConfigState;

static uint32_t map_status(uint32_t status)
{
    if (status == ASTRA_VFS_OK) return ASTRA_CONFIG_OK;
    if (status == ASTRA_VFS_ERR_NOT_FOUND) return ASTRA_CONFIG_NOT_FOUND;
    if (status == ASTRA_VFS_ERR_ACCESS) return ASTRA_CONFIG_ACCESS;
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

static int path(char *out, const char *root, const char *leaf)
{
    uint32_t used = 0u;

    while (used < ASTRA_CAPABILITY_ROOT_MAX && root[used] != '\0') {
        if (used + 1u >= ASTRA_VFS_PATH_MAX)
            return 0;
        out[used] = root[used];
        ++used;
    }
    if (used == ASTRA_CAPABILITY_ROOT_MAX || used == 0u || out[0] != '/')
        return 0;
    if (out[used - 1u] != '/') {
        if (used + 1u >= ASTRA_VFS_PATH_MAX)
            return 0;
        out[used++] = '/';
    }
    while (*leaf != '\0') {
        if (used + 1u >= ASTRA_VFS_PATH_MAX)
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

    if (root == NULL || root[0] == '\0')
        return 0;
    if (root[0] != '/')
        out[used++] = '/';
    while (source < ASTRA_CAPABILITY_ROOT_MAX && root[source] != '\0') {
        if (used + 1u >= ASTRA_VFS_PATH_MAX)
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

        status = astra_vfs_port_read_bulk(
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

static uint32_t config_open(const AstraStartupInfo *startup,
                            uint32_t schema_version, uint32_t flags,
                            AstraConfig *config, AstraConfigError *error)
{
    const AstraStartupCapability *capability;
    ConfigState *state = NULL;
    uint32_t state_area = 0u, status;

    if (config == NULL || config->_private_state != NULL ||
        config->_private_area != 0u || !astra_startup_validate(startup) ||
        schema_version == 0u || flags == 0u ||
        (flags & ~(ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE)) != 0u)
        return ASTRA_CONFIG_INVALID;
    capability = astra_startup_capability(startup, ASTRA_CONFIG_CAPABILITY);
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
    state->schema_version = schema_version;
    state->flags = flags;
    if (!root_copy(state->root, capability->root) ||
        !path(state->path, state->root, "settings.conf") ||
        !path(state->temporary, state->root, ".settings.tmp")) {
        status = ASTRA_CONFIG_INVALID;
        goto failed;
    }
    status = astra_vfs_port_connect_lazy(&state->client, capability->handle);
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

static void config_close(AstraConfig *config)
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

static uint32_t config_reload(AstraConfig *config, AstraConfigError *error)
{
    ConfigState *state = state_of(config);

    return state != NULL ? load(state, error) : ASTRA_CONFIG_CLOSED;
}

static uint32_t config_count(const AstraConfig *config, const char *key,
                             uint32_t *count)
{
    const ConfigState *state = state_of(config);

    return state != NULL ? astra_config_document_count(
        state->text, state->length, key, count) : ASTRA_CONFIG_CLOSED;
}

static uint32_t config_get_string(const AstraConfig *config, const char *key,
                                  uint32_t index, char *out,
                                  uint32_t capacity, uint32_t *length)
{
    const ConfigState *state = state_of(config);

    return state != NULL ? astra_config_document_get(
        state->text, state->length, key, index, out, capacity, length) :
        ASTRA_CONFIG_CLOSED;
}

static uint32_t config_get_i64(const AstraConfig *config, const char *key,
                               uint32_t index, int64_t *value)
{
    const ConfigState *state = state_of(config);

    return state != NULL ? astra_config_document_get_i64(
        state->text, state->length, key, index, value) : ASTRA_CONFIG_CLOSED;
}

static uint32_t config_get_u64(const AstraConfig *config, const char *key,
                               uint32_t index, uint64_t *value)
{
    const ConfigState *state = state_of(config);

    return state != NULL ? astra_config_document_get_u64(
        state->text, state->length, key, index, value) : ASTRA_CONFIG_CLOSED;
}

static uint32_t config_get_bool(const AstraConfig *config, const char *key,
                                uint32_t index, int *value)
{
    const ConfigState *state = state_of(config);

    return state != NULL ? astra_config_document_get_bool(
        state->text, state->length, key, index, value) : ASTRA_CONFIG_CLOSED;
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
    status = astra_vfs_unlink(&state->client, state->temporary);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND)
        goto failed;
    status = astra_vfs_open_mode(
        &state->client, state->temporary,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_TRUNCATE,
        0600u, &file, &old_size, &kind);
    while (status == ASTRA_VFS_OK && used < length) {
        uint32_t moved = 0u;

        status = astra_vfs_port_write_bulk(
            &state->client, file, used, text + used, length - used, &moved);
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
    (void)astra_vfs_unlink(&state->client, state->temporary);
    (void)astra_rt_area_unmap(text);
    (void)astra_close(text_area);
    return map_status(status);

denied:
    (void)astra_rt_area_unmap(text);
    (void)astra_close(text_area);
    return ASTRA_CONFIG_ACCESS;
}

static uint32_t change(AstraConfig *config, const char *key, uint32_t index,
                       const char *value, int remove)
{
    ConfigState *state = state_of(config);
    char *text = NULL;
    uint32_t text_area = 0u, required = 0u, status;

    if (state == NULL)
        return ASTRA_CONFIG_CLOSED;
    status = remove ? astra_config_document_remove(
                          state->text, state->length, key, index,
                          NULL, 0u, &required) :
                      astra_config_document_replace(
                          state->text, state->length, key, index, value,
                          NULL, 0u, &required);
    if (status != ASTRA_CONFIG_BUFFER_TOO_SMALL)
        return status;
    status = area(required, &text_area, (void **)&text);
    if (status != ASTRA_CONFIG_OK)
        return status;
    status = remove ? astra_config_document_remove(
                          state->text, state->length, key, index,
                          text, required, &required) :
                      astra_config_document_replace(
                          state->text, state->length, key, index, value,
                          text, required, &required);
    if (status != ASTRA_CONFIG_OK) {
        (void)astra_rt_area_unmap(text);
        (void)astra_close(text_area);
        return status;
    }
    return commit(state, text_area, text, required - 1u);
}

static uint32_t set_string(AstraConfig *config, const char *key,
                           uint32_t index, const char *value)
{
    return value != NULL ? change(config, key, index, value, 0) :
                           ASTRA_CONFIG_INVALID;
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

static uint32_t set_i64(AstraConfig *config, const char *key, uint32_t index,
                        int64_t value)
{
    char number[21];

    (void)signed_decimal(number, value);
    return set_string(config, key, index, number);
}

static uint32_t set_u64(AstraConfig *config, const char *key, uint32_t index,
                        uint64_t value)
{
    char number[21];

    (void)decimal(number, value);
    return set_string(config, key, index, number);
}

static uint32_t set_bool(AstraConfig *config, const char *key, uint32_t index,
                         int value)
{
    return set_string(config, key, index, value != 0 ? "true" : "false");
}

static uint32_t append_string(AstraConfig *config, const char *key,
                              const char *value)
{
    uint32_t count = 0u;
    uint32_t status = config_count(config, key, &count);

    return status == ASTRA_CONFIG_OK ? set_string(config, key, count, value) :
                                      status;
}

static uint32_t append_i64(AstraConfig *config, const char *key,
                           int64_t value)
{
    char number[21];

    (void)signed_decimal(number, value);
    return append_string(config, key, number);
}

static uint32_t append_u64(AstraConfig *config, const char *key,
                           uint64_t value)
{
    char number[21];

    (void)decimal(number, value);
    return append_string(config, key, number);
}

static uint32_t append_bool(AstraConfig *config, const char *key, int value)
{
    return append_string(config, key, value != 0 ? "true" : "false");
}

static uint32_t remove_value(AstraConfig *config, const char *key,
                             uint32_t index)
{
    return change(config, key, index, NULL, 1);
}

const AstraConfigLibraryV1 astra_library_exports ASTRA_LIBRARY_EXPORTS = {
    ASTRA_CONFIG_LIBRARY_ABI_MAJOR,
    ASTRA_CONFIG_LIBRARY_ABI_MINOR,
    sizeof(AstraConfigLibraryV1),
    config_open,
    config_close,
    config_reload,
    config_count,
    config_get_string,
    config_get_i64,
    config_get_u64,
    config_get_bool,
    set_string,
    set_i64,
    set_u64,
    set_bool,
    append_string,
    append_i64,
    append_u64,
    append_bool,
    remove_value,
};
