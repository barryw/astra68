/*
 * The client half of the storage protocol. Marshalling only: no filesystem
 * knowledge, no transport knowledge, no state beyond the session it agreed.
 */

#include <astra/vfs_client.h>

#include <stddef.h>

static void
begin(AstraVfsClient *client)
{
    AstraVfsRequest *request = &client->request;
    uint32_t index;
    unsigned char *bytes = (unsigned char *)request;

    for (index = 0u; index < (uint32_t)sizeof(*request); ++index) {
        bytes[index] = 0u;
    }
    request->size = (uint16_t)ASTRA_VFS_REQUEST_SIZE;
    request->version = client->version != 0u ? client->version :
                                               ASTRA_VFS_VERSION;
    request->session = client->session;
    /*
     * Taken from the client rather than from a parameter. Correlation nobody
     * has to remember is correlation that is there when it matters: the call
     * sites that would forget one are exactly the ones being debugged.
     *
     * The Kit does not reach for the runtime to read it. A client belongs to
     * one thread doing one thing at a time -- that is what a session is --
     * so whoever owns the client sets this when the work starts, in one place
     * rather than at every call.
     */
    request->activity = client->activity;
}

/*
 * Copies a path into the record and refuses one that will not fit, rather
 * than truncating. A truncated path names a different file, and the service
 * would answer about that one.
 */
static int
set_path(AstraVfsRequest *request, const char *path)
{
    uint32_t index = 0u;

    if (path == NULL) {
        return 0;
    }
    while (path[index] != '\0') {
        if (index + 1u >= ASTRA_VFS_PATH_MAX) {
            return 0;
        }
        request->body.path[index] = (uint8_t)path[index];
        ++index;
    }
    request->body.path[index] = 0u;
    return 1;
}

static uint32_t
exchange(AstraVfsClient *client, uint32_t operation)
{
    AstraVfsReply *reply = &client->reply;
    uint32_t status;

    if (client == NULL || client->transport == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    status = client->transport(client->context, operation, &client->request,
                               reply);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    /*
     * A reply whose size is not the one this build compiled against came from
     * a different version of the protocol. Trusting its fields would mean
     * reading whatever now lives at those offsets.
     */
    if (reply->size != (uint16_t)ASTRA_VFS_REPLY_SIZE) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    return reply->status;
}

uint32_t
astra_vfs_connect(AstraVfsClient *client, AstraVfsTransport transport,
                  void *context)
{
    uint32_t status;

    if (client == NULL || transport == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    client->transport = transport;
    client->context = context;
    client->session = ASTRA_VFS_SESSION_INVALID;
    client->version = ASTRA_VFS_VERSION;
    client->port_area_capable = 0u;

    begin(client);
    status = exchange(client, ASTRA_VFS_OP_HELLO);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    /*
     * The service may only choose a version this build can speak. A reply
     * outside the range means the two sides disagree about what the numbers
     * mean, which is not something to proceed through.
     */
    if (client->reply.version < ASTRA_VFS_VERSION_MIN ||
        client->reply.version > ASTRA_VFS_VERSION) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    client->session = client->reply.session;
    client->version = client->reply.version;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_disconnect(AstraVfsClient *client)
{
    uint32_t status;

    if (client == NULL || client->session == ASTRA_VFS_SESSION_INVALID) {
        return ASTRA_VFS_OK;
    }
    begin(client);
    status = exchange(client, ASTRA_VFS_OP_BYE);
    client->session = ASTRA_VFS_SESSION_INVALID;
    return status;
}

uint32_t
astra_vfs_open(AstraVfsClient *client, const char *path, uint32_t flags,
               AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    uint32_t status;

    if (client == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *file = ASTRA_VFS_FILE_INVALID;
    begin(client);
    if (!set_path(&client->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    client->request.flags = flags;
    status = exchange(client, ASTRA_VFS_OP_OPEN);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *file = client->reply.file;
    if (size != NULL) {
        *size = client->reply.node_size;
    }
    if (kind != NULL) {
        *kind = client->reply.kind;
    }
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    begin(client);
    client->request.file = file;
    return exchange(client, ASTRA_VFS_OP_CLOSE);
}

uint32_t
astra_vfs_read(AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
               void *buffer, uint32_t length, uint32_t *moved)
{
    uint32_t status;
    uint32_t index;
    uint8_t *out = buffer;

    if (client == NULL || buffer == NULL || moved == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    begin(client);
    client->request.file = file;
    client->request.offset = offset;
    client->request.length = length;
    status = exchange(client, ASTRA_VFS_OP_READ);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (client->reply.count > length) {
        return ASTRA_VFS_ERR_PROTOCOL; /* a service claiming more than it sent */
    }
    for (index = 0u; index < client->reply.count; ++index) {
        out[index] = client->reply.payload[index];
    }
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_write(AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
                const void *buffer, uint32_t length, uint32_t *moved)
{
    const uint8_t *in = buffer;
    uint32_t status;
    uint32_t index;

    if (client == NULL || buffer == NULL || moved == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    begin(client);
    client->request.file = file;
    client->request.offset = offset;
    client->request.length = length;
    for (index = 0u; index < length; ++index) {
        client->request.body.payload[index] = in[index];
    }
    status = exchange(client, ASTRA_VFS_OP_WRITE);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (client->reply.count > length) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *moved = client->reply.count;
    return ASTRA_VFS_OK;
}

/*
 * Everything a stat knows, in one exchange. `astra_vfs_stat` stays as it was
 * because most callers only ever wanted a size and a kind, and a listing that
 * needs the rest should not make every existing caller carry a struct.
 */
uint32_t
astra_vfs_stat_meta(AstraVfsClient *client, const char *path,
                    AstraVfsDirEntry *meta)
{
    uint32_t status;

    if (client == NULL || meta == NULL)
        return ASTRA_VFS_ERR_INVALID;
    begin(client);
    if (!set_path(&client->request, path))
        return ASTRA_VFS_ERR_INVALID;
    status = exchange(client, ASTRA_VFS_OP_STAT);
    if (status != ASTRA_VFS_OK)
        return status;
    meta->name[0] = '\0';
    meta->size = client->reply.node_size;
    meta->mtime = client->reply.mtime;
    meta->uid = client->reply.uid;
    meta->gid = client->reply.gid;
    meta->kind = client->reply.kind;
    meta->mode = client->reply.mode;
    meta->nlink = client->reply.nlink;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_stat(AstraVfsClient *client, const char *path, uint64_t *size,
               uint16_t *kind)
{
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    begin(client);
    if (!set_path(&client->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    status = exchange(client, ASTRA_VFS_OP_STAT);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (size != NULL) {
        *size = client->reply.node_size;
    }
    if (kind != NULL) {
        *kind = client->reply.kind;
    }
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_readdir(AstraVfsClient *client, const char *path, uint64_t cursor,
                  char *name, uint32_t capacity, uint16_t *kind,
                  uint64_t *next)
{
    uint32_t status;
    uint32_t at;

    if (client == NULL || name == NULL || capacity == 0u || next == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    name[0] = '\0';
    begin(client);
    if (!set_path(&client->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    client->request.offset = cursor;
    status = exchange(client, ASTRA_VFS_OP_READDIR);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *next = client->reply.cursor;
    if (client->reply.count >= capacity || client->reply.count >= ASTRA_VFS_IO_MAX) {
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (at = 0u; at < client->reply.count; ++at) {
        name[at] = (char)client->reply.payload[at];
    }
    name[client->reply.count] = '\0';
    if (kind != NULL) {
        *kind = client->reply.kind;
    }
    return ASTRA_VFS_OK;
}

static uint16_t
take_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t
take_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint64_t
take_be64(const uint8_t *bytes)
{
    return ((uint64_t)take_be32(bytes) << 32) | take_be32(bytes + 4u);
}

uint32_t
astra_vfs_readdir_batch(AstraVfsClient *client, const char *path,
                        uint64_t cursor, AstraVfsDirEntry *entries,
                        uint32_t capacity, uint32_t *count, uint64_t *next)
{
    const uint8_t *payload;
    uint32_t operation = ASTRA_VFS_OP_READDIR_BATCH;
    uint32_t status;
    uint32_t at = 0u;
    uint32_t found = 0u;

    if (client == NULL || entries == NULL || capacity == 0u || count == NULL ||
        next == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *count = 0u;
    *next = cursor;
    if (client->version < UINT16_C(4)) {
        status = astra_vfs_readdir(client, path, cursor, entries[0].name,
                                   sizeof(entries[0].name), &entries[0].kind,
                                   next);
        if (status == ASTRA_VFS_OK)
            *count = 1u;
        return status;
    }
    if (client->version >= UINT16_C(10) && client->port_area_capable != 0u)
        operation = ASTRA_VFS_OP_READDIR_AREA;
    begin(client);
    if (!set_path(&client->request, path))
        return ASTRA_VFS_ERR_INVALID;
    client->request.offset = cursor;
    client->request.length = capacity;
    status = exchange(client, operation);
    if (status != ASTRA_VFS_OK)
        return status;
    if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        if (client->port_area_address == NULL ||
            client->reply.count > client->port_area_size)
            return ASTRA_VFS_ERR_PROTOCOL;
        payload = client->port_area_address;
    } else {
        if (client->reply.count > ASTRA_VFS_IO_MAX)
            return ASTRA_VFS_ERR_PROTOCOL;
        payload = client->reply.payload;
    }
    while (at < client->reply.count) {
        uint32_t length;

        const uint8_t *record = &payload[at];

        if (found == capacity ||
            client->reply.count - at < ASTRA_VFS_DIRENT_HEADER)
            return ASTRA_VFS_ERR_PROTOCOL;
        entries[found].kind = take_be16(record + 0u);
        entries[found].mode = take_be16(record + 2u);
        entries[found].nlink = take_be16(record + 4u);
        length = record[6];
        if (record[7] != 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
        entries[found].uid = take_be32(record + 8u);
        entries[found].gid = take_be32(record + 12u);
        entries[found].size = take_be64(record + 16u);
        entries[found].mtime = (int64_t)take_be64(record + 24u);
        entries[found].reserved = 0u;
        at += ASTRA_VFS_DIRENT_HEADER;
        if (length == 0u || length >= ASTRA_VFS_NAME_MAX ||
            length > client->reply.count - at)
            return ASTRA_VFS_ERR_PROTOCOL;
        for (uint32_t index = 0u; index < length; ++index)
            entries[found].name[index] =
                (char)payload[at + index];
        entries[found].name[length] = '\0';
        at += length;
        ++found;
    }
    if (found == 0u)
        return ASTRA_VFS_ERR_PROTOCOL;
    *count = found;
    *next = client->reply.cursor;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_mkdir(AstraVfsClient *client, const char *path)
{

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    begin(client);
    if (!set_path(&client->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    return exchange(client, ASTRA_VFS_OP_MKDIR);
}

uint32_t
astra_vfs_unlink(AstraVfsClient *client, const char *path)
{

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    begin(client);
    if (!set_path(&client->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    return exchange(client, ASTRA_VFS_OP_UNLINK);
}

uint32_t
astra_vfs_rename(AstraVfsClient *client, const char *from, const char *to)
{
    uint32_t status;

    if (client == NULL || client->version < UINT16_C(11))
        return client == NULL ? ASTRA_VFS_ERR_INVALID :
                                ASTRA_VFS_ERR_UNSUPPORTED;
    begin(client);
    if (!set_path(&client->request, from))
        return ASTRA_VFS_ERR_INVALID;
    status = exchange(client, ASTRA_VFS_OP_RENAME_FROM);
    if (status != ASTRA_VFS_OK)
        return status;
    begin(client);
    if (!set_path(&client->request, to))
        return ASTRA_VFS_ERR_INVALID;
    return exchange(client, ASTRA_VFS_OP_RENAME_TO);
}
