/*
 * The client half of the storage protocol. Marshalling only: no filesystem
 * knowledge, no transport knowledge, no state beyond the session it agreed.
 */

#include <astra/vfs_client.h>

#include <astra/endian.h>
#include <astra/vfs_backend.h>

#include <stddef.h>
#include <string.h>

static uint32_t
backend_enter(AstraVfsClient *client, const AstraVfsBackendOps **ops,
              void **context)
{
    uint32_t status;

    if (client->direct_backend_enter == NULL ||
        client->direct_backend_leave == NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = client->direct_backend_enter(client);
    if (status != ASTRA_VFS_OK)
        return status;
    *ops = client->direct_backend_ops;
    *context = client->direct_backend_context;
    if (*ops == NULL || *context == NULL) {
        client->direct_backend_leave(client);
        return ASTRA_VFS_ERR_INVALID;
    }
    return ASTRA_VFS_OK;
}

static void
backend_leave(AstraVfsClient *client)
{
    client->direct_backend_leave(client);
}

static AstraVfsCallState *
call_state(AstraVfsClient *client)
{
    return client->call_acquire != NULL ? client->call_acquire(client) :
                                          &client->call;
}

static void
begin_request(AstraVfsClient *client, AstraVfsRequest *request, uint16_t size)
{
    memset(request, 0, size);
    request->size = size;
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

static AstraVfsCallState *
begin(AstraVfsClient *client)
{
    AstraVfsCallState *call = call_state(client);

    begin_request(client, &call->request,
                  (uint16_t)ASTRA_VFS_REQUEST_SIZE);
    return call;
}

/*
 * Copies a path into the record and refuses one that will not fit, rather
 * than truncating. A truncated path names a different file, and the service
 * would answer about that one.
 */
static int
set_path_bytes(uint8_t destination[ASTRA_VFS_PATH_MAX], const char *path)
{
    uint32_t index = 0u;

    if (path == NULL) {
        return 0;
    }
    while (path[index] != '\0') {
        if (index + 1u >= ASTRA_VFS_PATH_MAX) {
            return 0;
        }
        destination[index] = (uint8_t)path[index];
        ++index;
    }
    destination[index] = 0u;
    return 1;
}

static uint32_t
exchange_request(AstraVfsClient *client, uint32_t operation,
                 const AstraVfsRequest *request, AstraVfsCallState *call)
{
    AstraVfsReply *reply = &call->reply;
    uint32_t status;

    if (client == NULL || client->transport == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    status = client->transport(client->context, operation, request, reply);
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

static int
set_path(AstraVfsRequest *request, const char *path)
{
    return set_path_bytes(request->body.path, path);
}

static uint32_t
exchange(AstraVfsClient *client, AstraVfsCallState *call,
         uint32_t operation)
{
    return exchange_request(client, operation, &call->request, call);
}

uint32_t
astra_vfs_connect(AstraVfsClient *client, AstraVfsTransport transport,
                  void *context)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || transport == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    client->transport = transport;
    client->context = context;
    client->area_payload = NULL;
    client->call_acquire = NULL;
    client->session = ASTRA_VFS_SESSION_INVALID;
    client->version = ASTRA_VFS_VERSION;
    client->port_area_capable = 0u;
    client->direct_backend_ops = NULL;
    client->direct_backend_context = NULL;
    client->direct_backend_enter = NULL;
    client->direct_backend_leave = NULL;

    call = begin(client);
    status = exchange(client, call, ASTRA_VFS_OP_HELLO);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    /*
     * The service may only choose a version this build can speak. A reply
     * outside the range means the two sides disagree about what the numbers
     * mean, which is not something to proceed through.
     */
    if (call->reply.version < ASTRA_VFS_VERSION_MIN ||
        call->reply.version > ASTRA_VFS_VERSION) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    client->session = call->reply.session;
    client->version = call->reply.version;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_disconnect(AstraVfsClient *client)
{
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || client->session == ASTRA_VFS_SESSION_INVALID) {
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    status = exchange(client, call, ASTRA_VFS_OP_BYE);
    client->session = ASTRA_VFS_SESSION_INVALID;
    return status;
}

uint32_t
astra_vfs_open(AstraVfsClient *client, const char *path, uint32_t flags,
               AstraVfsFile *file, uint64_t *size, uint16_t *kind)
{
    return astra_vfs_open_mode(client, path, flags, ASTRA_VFS_MODE_DEFAULT,
                               file, size, kind);
}

uint32_t
astra_vfs_open_mode(AstraVfsClient *client, const char *path, uint32_t flags,
                    uint16_t create_mode, AstraVfsFile *file, uint64_t *size,
                    uint16_t *kind)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    AstraVfsNodeInfo info = {0};
    uintptr_t node = 0u;
    uint32_t status;

    if (client == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
        client->version < UINT16_C(12)) {
        return ASTRA_VFS_ERR_UNSUPPORTED;
    }
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u &&
        client->version < UINT16_C(13)) {
        return ASTRA_VFS_ERR_UNSUPPORTED;
    }
    if (create_mode != ASTRA_VFS_MODE_DEFAULT &&
        ((create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
         (flags & ASTRA_VFS_OPEN_CREATE) == 0u))
        return ASTRA_VFS_ERR_INVALID;
    if (create_mode != ASTRA_VFS_MODE_DEFAULT &&
        client->version < UINT16_C(14))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    *file = ASTRA_VFS_FILE_INVALID;
    if (client->direct_backend_ops != NULL) {
        const uint32_t allowed =
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
            ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
            ASTRA_VFS_OPEN_DIRECTORY | ASTRA_VFS_OPEN_EXCLUSIVE |
            ASTRA_VFS_OPEN_APPEND;

        if (path == NULL || (flags & ~allowed) != 0u ||
            (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u ||
            ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
             (flags & ASTRA_VFS_OPEN_CREATE) == 0u))
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->open(context, path, flags, create_mode, &node, &info);
        if (status == ASTRA_VFS_OK && node > (uintptr_t)UINT32_MAX) {
            (void)ops->close(context, node);
            status = ASTRA_VFS_ERR_PROTOCOL;
        }
        backend_leave(client);
        if (status != ASTRA_VFS_OK)
            return status;
        *file = (AstraVfsFile)node;
        if (size != NULL)
            *size = info.size;
        if (kind != NULL)
            *kind = info.kind;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    if (!set_path(&call->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    call->request.flags = flags;
    call->request.offset = client->version >= UINT16_C(14) ? create_mode : 0u;
    status = exchange(client, call, ASTRA_VFS_OP_OPEN);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *file = call->reply.file;
    if (size != NULL) {
        *size = call->reply.node_size;
    }
    if (kind != NULL) {
        *kind = call->reply.kind;
    }
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->close(context, file);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    return exchange(client, call, ASTRA_VFS_OP_CLOSE);
}

uint32_t
astra_vfs_read(AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
               void *buffer, uint32_t length, uint32_t *moved)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
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
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->read(context, file, offset, buffer, length, moved);
        backend_leave(client);
        return status == ASTRA_VFS_OK && *moved > length ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.offset = offset;
    call->request.length = length;
    status = exchange(client, call, ASTRA_VFS_OP_READ);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (call->reply.count > length) {
        return ASTRA_VFS_ERR_PROTOCOL; /* a service claiming more than it sent */
    }
    for (index = 0u; index < call->reply.count; ++index) {
        out[index] = call->reply.payload[index];
    }
    *moved = call->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_write_position(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, const void *buffer, uint32_t length,
                         uint32_t *moved, uint64_t *position)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    const uint8_t *in = buffer;
    uint32_t status;
    uint32_t index;

    if (client == NULL || buffer == NULL || moved == NULL || position == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    *position = offset;
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->write(context, file, offset, 0u, buffer, length, moved,
                            position);
        backend_leave(client);
        return status == ASTRA_VFS_OK && *moved > length ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.offset = offset;
    call->request.length = length;
    for (index = 0u; index < length; ++index) {
        call->request.body.payload[index] = in[index];
    }
    status = exchange(client, call, ASTRA_VFS_OP_WRITE);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (call->reply.count > length) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    *moved = call->reply.count;
    *position = client->version >= UINT16_C(13) ? call->reply.node_size :
                                                  offset + *moved;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_write(AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
                const void *buffer, uint32_t length, uint32_t *moved)
{
    uint64_t position;

    return astra_vfs_write_position(client, file, offset, buffer, length,
                                    moved, &position);
}

uint32_t
astra_vfs_sync(AstraVfsClient *client, AstraVfsFile file)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(13))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->sync(context, file);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    return exchange(client, call, ASTRA_VFS_OP_SYNC);
}

uint32_t
astra_vfs_truncate(AstraVfsClient *client, AstraVfsFile file, uint64_t size)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(13))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->truncate(context, file, size);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.offset = size;
    return exchange(client, call, ASTRA_VFS_OP_TRUNCATE);
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
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL || meta == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->stat(context, path, &info);
        backend_leave(client);
        if (status != ASTRA_VFS_OK)
            return status;
        meta->name[0] = '\0';
        meta->size = info.size;
        meta->mtime = info.mtime;
        meta->uid = info.uid;
        meta->gid = info.gid;
        meta->kind = info.kind;
        meta->mode = info.mode;
        meta->nlink = info.nlink;
        meta->reserved = 0u;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    status = exchange(client, call, ASTRA_VFS_OP_STAT);
    if (status != ASTRA_VFS_OK)
        return status;
    meta->name[0] = '\0';
    meta->size = call->reply.node_size;
    meta->mtime = call->reply.mtime;
    meta->uid = call->reply.uid;
    meta->gid = call->reply.gid;
    meta->kind = call->reply.kind;
    meta->mode = call->reply.mode;
    meta->nlink = call->reply.nlink;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_stat(AstraVfsClient *client, const char *path, uint64_t *size,
               uint16_t *kind)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->stat(context, path, &info);
        backend_leave(client);
        if (status != ASTRA_VFS_OK)
            return status;
        if (size != NULL)
            *size = info.size;
        if (kind != NULL)
            *kind = info.kind;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    if (!set_path(&call->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    status = exchange(client, call, ASTRA_VFS_OP_STAT);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (size != NULL) {
        *size = call->reply.node_size;
    }
    if (kind != NULL) {
        *kind = call->reply.kind;
    }
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_readdir(AstraVfsClient *client, const char *path, uint64_t cursor,
                  char *name, uint32_t capacity, uint16_t *kind,
                  uint64_t *next)
{
    AstraVfsCallState *call;
    uint32_t status;
    uint32_t at;

    if (client == NULL || name == NULL || capacity == 0u || next == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    name[0] = '\0';
    call = begin(client);
    if (!set_path(&call->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    call->request.offset = cursor;
    status = exchange(client, call, ASTRA_VFS_OP_READDIR);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *next = call->reply.cursor;
    if (call->reply.count >= capacity || call->reply.count >= ASTRA_VFS_IO_MAX) {
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (at = 0u; at < call->reply.count; ++at) {
        name[at] = (char)call->reply.payload[at];
    }
    name[call->reply.count] = '\0';
    if (kind != NULL) {
        *kind = call->reply.kind;
    }
    return ASTRA_VFS_OK;
}

static uint32_t
readdir_batch(AstraVfsClient *client, AstraVfsFile directory,
              const char *path, uint64_t cursor, AstraVfsDirEntry *entries,
              uint32_t capacity, uint32_t *count, uint64_t *next)
{
    AstraVfsCallState *call;
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
    if (directory != ASTRA_VFS_FILE_INVALID &&
        client->version < UINT16_C(17))
        return ASTRA_VFS_ERR_UNSUPPORTED;
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
    call = begin(client);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    call->request.offset = cursor;
    call->request.length = capacity;
    call->request.file = directory;
    status = exchange(client, call, operation);
    if (status != ASTRA_VFS_OK)
        return status;
    if (operation == ASTRA_VFS_OP_READDIR_AREA) {
        uint32_t area_size = 0u;

        payload = client->area_payload != NULL ?
            client->area_payload(client, &area_size) : NULL;
        if (payload == NULL || call->reply.count > area_size)
            return ASTRA_VFS_ERR_PROTOCOL;
    } else {
        if (call->reply.count > ASTRA_VFS_IO_MAX)
            return ASTRA_VFS_ERR_PROTOCOL;
        payload = call->reply.payload;
    }
    while (at < call->reply.count) {
        uint32_t length;

        const uint8_t *record = &payload[at];

        if (found == capacity ||
            call->reply.count - at < ASTRA_VFS_DIRENT_HEADER)
            return ASTRA_VFS_ERR_PROTOCOL;
        entries[found].kind = astra_load_be16(record + 0u);
        entries[found].mode = astra_load_be16(record + 2u);
        entries[found].nlink = astra_load_be16(record + 4u);
        length = record[6];
        if (record[7] != 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
        entries[found].uid = astra_load_be32(record + 8u);
        entries[found].gid = astra_load_be32(record + 12u);
        entries[found].size = astra_load_be64(record + 16u);
        entries[found].mtime = (int64_t)astra_load_be64(record + 24u);
        entries[found].reserved = 0u;
        at += ASTRA_VFS_DIRENT_HEADER;
        if (length == 0u || length >= ASTRA_VFS_NAME_MAX ||
            length > call->reply.count - at)
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
    *next = call->reply.cursor;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_readdir_batch(AstraVfsClient *client, const char *path,
                        uint64_t cursor, AstraVfsDirEntry *entries,
                        uint32_t capacity, uint32_t *count, uint64_t *next)
{
    return readdir_batch(client, ASTRA_VFS_FILE_INVALID, path, cursor,
                         entries, capacity, count, next);
}

uint32_t
astra_vfs_readdir_file_batch(AstraVfsClient *client,
                              AstraVfsFile directory, const char *path,
                              uint64_t cursor, AstraVfsDirEntry *entries,
                              uint32_t capacity, uint32_t *count,
                              uint64_t *next)
{
    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return readdir_batch(client, directory, path, cursor, entries, capacity,
                         count, next);
}

uint32_t
astra_vfs_mkdir(AstraVfsClient *client, const char *path)
{
    return astra_vfs_mkdir_mode(client, path, ASTRA_VFS_MODE_DEFAULT);
}

uint32_t
astra_vfs_mkdir_mode(AstraVfsClient *client, const char *path,
                     uint16_t create_mode)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL ||
        (create_mode != ASTRA_VFS_MODE_DEFAULT &&
         (create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (create_mode != ASTRA_VFS_MODE_DEFAULT &&
        client->version < UINT16_C(14))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->mkdir(context, path, create_mode);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    if (!set_path(&call->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    call->request.offset = client->version >= UINT16_C(14) ? create_mode : 0u;
    return exchange(client, call, ASTRA_VFS_OP_MKDIR);
}

uint32_t
astra_vfs_unlink(AstraVfsClient *client, const char *path)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->unlink(context, path);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    if (!set_path(&call->request, path)) {
        return ASTRA_VFS_ERR_INVALID;
    }
    return exchange(client, call, ASTRA_VFS_OP_UNLINK);
}

uint32_t
astra_vfs_rename(AstraVfsClient *client, const char *from, const char *to)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || client->version < UINT16_C(11))
        return client == NULL ? ASTRA_VFS_ERR_INVALID :
                                ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        if (from == NULL || to == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->rename(context, from, to);
        backend_leave(client);
        return status;
    }
    call = call_state(client);
    if (client->version >= UINT16_C(16)) {
        AstraVfsRenameRequest *request = &call->rename_request;

        begin_request(client, &request->request,
                      (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE);
        if (!set_path(&request->request, from) ||
            !set_path_bytes(request->to, to))
            return ASTRA_VFS_ERR_INVALID;
        return exchange_request(client, ASTRA_VFS_OP_RENAME,
                                &request->request, call);
    }
    begin_request(client, &call->request, (uint16_t)ASTRA_VFS_REQUEST_SIZE);
    if (!set_path(&call->request, from))
        return ASTRA_VFS_ERR_INVALID;
    status = exchange(client, call, ASTRA_VFS_OP_RENAME_FROM);
    if (status != ASTRA_VFS_OK)
        return status;
    begin_request(client, &call->request, (uint16_t)ASTRA_VFS_REQUEST_SIZE);
    if (!set_path(&call->request, to))
        return ASTRA_VFS_ERR_INVALID;
    return exchange(client, call, ASTRA_VFS_OP_RENAME_TO);
}

uint32_t
astra_vfs_chmod(AstraVfsClient *client, const char *path, uint16_t mode)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(14))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->chmod(context, path, mode);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    call->request.offset = mode;
    return exchange(client, call, ASTRA_VFS_OP_CHMOD);
}

uint32_t
astra_vfs_readlink(AstraVfsClient *client, const char *path, void *buffer,
                   uint32_t capacity, uint32_t *length)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint8_t *bytes = buffer;
    uint32_t status;

    if (client == NULL || buffer == NULL || capacity == 0u || length == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(14))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (capacity > ASTRA_VFS_IO_MAX)
        capacity = ASTRA_VFS_IO_MAX;
    *length = 0u;
    if (client->direct_backend_ops != NULL) {
        if (path == NULL)
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->readlink(context, path, buffer, capacity, length);
        backend_leave(client);
        return status == ASTRA_VFS_OK && *length > capacity ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    call = begin(client);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    call->request.length = capacity;
    status = exchange(client, call, ASTRA_VFS_OP_READLINK);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.count > capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    for (uint32_t index = 0u; index < call->reply.count; ++index)
        bytes[index] = call->reply.payload[index];
    *length = call->reply.count;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_symlink(AstraVfsClient *client, const char *target,
                  const char *path)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call;
    uint32_t status;

    if (client == NULL || target == NULL || target[0] == '\0' || path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->version < UINT16_C(15))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->symlink(context, target, path);
        backend_leave(client);
        return status;
    }
    call = call_state(client);
    if (client->version >= UINT16_C(19)) {
        AstraVfsRenameRequest *request = &call->rename_request;

        begin_request(client, &request->request,
                      (uint16_t)ASTRA_VFS_RENAME_REQUEST_SIZE);
        if (!set_path(&request->request, target) ||
            !set_path_bytes(request->to, path))
            return ASTRA_VFS_ERR_INVALID;
        return exchange_request(client, ASTRA_VFS_OP_SYMLINK,
                                &request->request, call);
    }
    begin_request(client, &call->request, (uint16_t)ASTRA_VFS_REQUEST_SIZE);
    if (!set_path(&call->request, target))
        return ASTRA_VFS_ERR_INVALID;
    status = exchange(client, call, ASTRA_VFS_OP_SYMLINK_TARGET);
    if (status != ASTRA_VFS_OK)
        return status;
    begin_request(client, &call->request, (uint16_t)ASTRA_VFS_REQUEST_SIZE);
    if (!set_path(&call->request, path))
        return ASTRA_VFS_ERR_INVALID;
    return exchange(client, call, ASTRA_VFS_OP_SYMLINK_TO);
}
