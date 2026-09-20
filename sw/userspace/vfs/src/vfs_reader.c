#include <astra/vfs_reader.h>

#include <astra/address_space.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_provider_index.h>

static int path_append(char *path, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (at < capacity && path[at] != '\0')
        ++at;
    while (*text != '\0') {
        if (at + 1u >= capacity)
            return 0;
        path[at++] = *text++;
    }
    path[at] = '\0';
    return 1;
}

static int path_append_number(char *path, uint32_t capacity, uint16_t value)
{
    char digits[5];
    uint32_t count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        char digit[2] = {digits[--count], '\0'};

        if (!path_append(path, capacity, digit))
            return 0;
    }
    return 1;
}

uint32_t
astra_vfs_read_source_open(AstraVfsReadSource *source,
                           const AstraAssignTable *table, const char *path,
                           AstraVfsAssignClientFn client_for, void *context)
{
    AstraVfsClient *client = NULL;
    AstraVfsFile file = ASTRA_VFS_FILE_INVALID;
    uint64_t size = 0u;
    uint16_t kind = 0u;
    char wire[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (source == NULL || table == NULL || path == NULL ||
        client_for == NULL || source->file != ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_assign_open(
        table, path, ASTRA_RIGHT_READ, ASTRA_VFS_OPEN_READ, client_for,
        context, wire, sizeof(wire), &file, &size, &kind, &client, NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    if (kind != ASTRA_VFS_KIND_FILE || size > UINT32_MAX) {
        (void)astra_vfs_close(client, file);
        return kind != ASTRA_VFS_KIND_FILE ? ASTRA_VFS_ERR_INVALID :
                                             ASTRA_VFS_ERR_LIMIT;
    }
    source->client = client;
    source->file = file;
    source->length = (uint32_t)size;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_read_source_read_at(void *context, uint32_t offset,
                              uint32_t length, const uint8_t **bytes,
                              uint32_t *moved)
{
    AstraVfsReadSource *source = context;
    uint32_t available = 0u;
    uint32_t read_length;
    uint32_t status;

    if (source == NULL || source->client == NULL ||
        source->file == ASTRA_VFS_FILE_INVALID || bytes == NULL ||
        moved == NULL || offset > source->length ||
        length > source->length - offset)
        return ASTRA_VFS_ERR_INVALID;
    *bytes = NULL;
    *moved = 0u;
    read_length = length;
    if (length >= ASTRA_MEMORY_PAGE_SIZE) {
        read_length = ASTRA_VFS_BULK_MAX - offset % ASTRA_VFS_BULK_MAX;
        if (read_length > source->length - offset)
            read_length = source->length - offset;
    }
    status = astra_vfs_port_read_borrow(source->client, source->file, offset,
                                        read_length, bytes, &available);
    if (status != ASTRA_VFS_OK)
        return status;
    if (*bytes == NULL || available < length) {
        *bytes = NULL;
        return ASTRA_VFS_ERR_IO;
    }
    *moved = length;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_read_source_close(void *context)
{
    AstraVfsReadSource *source = context;
    AstraVfsClient *client;
    AstraVfsFile file;

    if (source == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (source->file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_OK;
    client = source->client;
    file = source->file;
    *source = (AstraVfsReadSource)ASTRA_VFS_READ_SOURCE_INIT;
    return astra_vfs_close(client, file);
}

uint32_t astra_vfs_library_resolve(
    const AstraAssignTable *table, const char *assign, const char *identity,
    AstraVfsAssignClientFn client_for, void *context,
    char *binary_path, uint32_t path_capacity,
    AstraLibraryReference *reference)
{
    AstraVfsReadSource index_source = ASTRA_VFS_READ_SOURCE_INIT;
    const uint8_t *record = NULL;
    uint32_t moved = 0u;
    uint32_t status;
    uint32_t close_status;
    uint16_t abi = 0u;
    char name[ASTRA_LIBRARY_NAME_MAX];
    char index_path[ASTRA_VFS_PATH_MAX] = {0};

    if (table == NULL || assign == NULL ||
        identity == NULL || client_for == NULL || reference == NULL ||
        binary_path == NULL || path_capacity == 0u)
        return ASTRA_VFS_ERR_INVALID;
    binary_path[0] = '\0';
    *reference = (AstraLibraryReference){0};
    if (!astra_vfs_provider_identity_parse(identity, name, sizeof(name),
                                            &abi) ||
        !path_append(index_path, sizeof(index_path), assign) ||
        !path_append(index_path, sizeof(index_path), ":.providers/") ||
        !path_append(index_path, sizeof(index_path), name) ||
        !path_append(index_path, sizeof(index_path), ".abi-") ||
        !path_append_number(index_path, sizeof(index_path), abi))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_read_source_open(
        &index_source, table, index_path, client_for, context);
    if (status != ASTRA_VFS_OK)
        return status;
    status = astra_vfs_read_source_read_at(
        &index_source, 0u, index_source.length, &record, &moved);
    if (status == ASTRA_VFS_OK &&
        (moved != index_source.length || record == NULL))
        status = ASTRA_VFS_ERR_IO;
    if (status == ASTRA_VFS_OK &&
        !astra_vfs_provider_index_parse(
            record, moved, assign, name, abi, binary_path,
            path_capacity, reference))
        status = ASTRA_VFS_ERR_PROTOCOL;
    close_status = astra_vfs_read_source_close(&index_source);
    if (status == ASTRA_VFS_OK && close_status != ASTRA_VFS_OK)
        status = close_status;
    if (status != ASTRA_VFS_OK) {
        binary_path[0] = '\0';
        *reference = (AstraLibraryReference){0};
    }
    return status;
}

uint32_t astra_vfs_library_source_open(
    AstraVfsReadSource *source, const AstraAssignTable *table,
    const char *assign, const char *identity,
    AstraVfsAssignClientFn client_for, void *context,
    AstraLibraryReference *reference)
{
    char binary_path[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (source == NULL || source->file != ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_vfs_library_resolve(
        table, assign, identity, client_for, context,
        binary_path, sizeof(binary_path), reference);
    if (status == ASTRA_VFS_OK)
        status = astra_vfs_read_source_open(
            source, table, binary_path, client_for, context);
    if (status != ASTRA_VFS_OK)
        *reference = (AstraLibraryReference){0};
    return status;
}
