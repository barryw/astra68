#include <astra/vfs_reader.h>

#include <astra/vfs_port_transport.h>

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

    if (source == NULL || source->client == NULL ||
        source->file == ASTRA_VFS_FILE_INVALID || bytes == NULL ||
        moved == NULL || offset > source->length ||
        length > source->length - offset)
        return ASTRA_VFS_ERR_INVALID;
    return astra_vfs_port_read_borrow(source->client, source->file, offset,
                                      length, bytes, moved);
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
