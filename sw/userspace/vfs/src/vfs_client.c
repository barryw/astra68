/*
 * The client half of the storage protocol. Marshalling only: no filesystem
 * knowledge, no transport knowledge, no state beyond the session it agreed.
 */

#include <astra/vfs_client.h>

#include <astra/endian.h>
#include <astra/utf8.h>
#include <astra/vfs_backend.h>
#include <astra/vfs_path_payload.h>
#include <astra/vfs_reply.h>
#include <astra/vfs_call_storage.h>
#include <astra/runtime.h>

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
    status = astra_vfs_storage_status(client->direct_backend_enter(client));
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
     * The Kit does not reach for the runtime to read it. The owner selects
     * this client's current correlation when the work starts, in one place
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
    call->transfer = (AstraVfsTransfer){0};
    return call;
}

/*
 * Native argument validation and area marshalling share the path owner.
 * Complete paths are refused when oversized; they are never truncated.
 */
static int
path_text_valid(const char *path, uint32_t *length)
{
    return astra_vfs_path_measure(path, length) == ASTRA_VFS_OK;
}

static uint32_t set_paths(AstraVfsCallState *call, const char *first,
                          const char *second)
{
    uint32_t needed = ASTRA_VFS_PATH_MAX * 2u;
    uint32_t used = 0u;
    uint32_t status;

    if (first == NULL)
        return astra_vfs_path_pack(first, second, NULL, 0u,
            &call->request.body.paths, &used);

    if (call->path_capacity < needed) {
        uint8_t *grown = astra_runtime_reallocate(call->path_bytes, needed);
        if (grown == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        call->path_bytes = grown;
        call->path_capacity = needed;
    }
    status = astra_vfs_path_pack(first, second, call->path_bytes,
        call->path_capacity, &call->request.body.paths, &used);
    if (status == ASTRA_VFS_OK) {
        call->transfer.input = call->path_bytes;
        call->transfer.input_size = used;
    }
    return status;
}

static uint32_t result_storage(AstraVfsCallState *call, uint32_t capacity)
{
    if (call->result_capacity < capacity) {
        uint8_t *grown = astra_runtime_reallocate(call->result_bytes, capacity);
        if (grown == NULL)
            return ASTRA_VFS_ERR_LIMIT;
        call->result_bytes = grown;
        call->result_capacity = capacity;
    }
    call->transfer.output = call->result_bytes;
    call->transfer.output_capacity = capacity;
    return ASTRA_VFS_OK;
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
    status = astra_vfs_storage_status(
        client->transport(client->context, operation, request, &call->transfer,
                           reply));
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    /*
     * A reply whose size is not the one this build compiled against came from
     * a different version of the protocol. Trusting its fields would mean
     * reading whatever now lives at those offsets.
     */
    if (!astra_vfs_reply_valid(reply)) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    return reply->status;
}

static uint32_t set_path(AstraVfsCallState *call, const char *path)
{
    return set_paths(call, path, NULL);
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
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || transport == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    client->transport = transport;
    client->context = context;
    client->area_payload = NULL;
    client->call_acquire = NULL;
    memset(&client->call, 0, sizeof(client->call));
    client->description_acquire = NULL;
    client->description_release = NULL;
    client->ownership_epoch = 0u;
    client->session = ASTRA_VFS_SESSION_INVALID;
    client->version = ASTRA_VFS_VERSION;
    client->features = 0u;
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
    if (call->reply.session == ASTRA_VFS_SESSION_INVALID ||
        call->reply.version < ASTRA_VFS_VERSION_MIN ||
        call->reply.version > ASTRA_VFS_VERSION) {
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    client->session = call->reply.session;
    client->version = call->reply.version;
    client->features = call->reply.features;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_disconnect(AstraVfsClient *client)
{
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_open_mode(client, path, flags, ASTRA_VFS_MODE_DEFAULT,
                               file, size, kind);
}

static uint32_t
open_mode_at(AstraVfsClient *client, AstraVfsFile directory, const char *path,
             uint32_t flags, uint16_t create_mode, AstraVfsFile *file,
             uint64_t *size, uint16_t *kind)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsNodeInfo info = {0};
    uintptr_t node = 0u;
    uint32_t status;

    if (client == NULL || file == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (directory != ASTRA_VFS_FILE_INVALID &&
        (path == NULL || path[0] == '\0' ||
         path[0] == '/'))
        return ASTRA_VFS_ERR_INVALID;
    if (create_mode != ASTRA_VFS_MODE_DEFAULT &&
        ((create_mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u ||
         (flags & ASTRA_VFS_OPEN_CREATE) == 0u))
        return ASTRA_VFS_ERR_INVALID;
    *file = ASTRA_VFS_FILE_INVALID;
    if (client->direct_backend_ops != NULL) {
        const uint32_t allowed =
            ASTRA_VFS_OPEN_MASK;

        if (!path_text_valid(path, NULL) || (flags & ~allowed) != 0u ||
            (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0u ||
            ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0u &&
             (flags & ASTRA_VFS_OPEN_CREATE) == 0u))
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = directory == ASTRA_VFS_FILE_INVALID ?
            ops->open(context, path, flags, create_mode, &node, &info) :
            ops->open_at(context, directory, path, flags, create_mode, &node,
                         &info);
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
        if (client->description_acquire != NULL)
            client->description_acquire(client);
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.flags = flags;
    call->request.file = directory;
    call->request.offset = create_mode;
    status = exchange(client, call,
                      directory == ASTRA_VFS_FILE_INVALID ?
                          ASTRA_VFS_OP_OPEN : ASTRA_VFS_OP_OPEN_AT);
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
    if (client->description_acquire != NULL)
        client->description_acquire(client);
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_open_mode(AstraVfsClient *client, const char *path, uint32_t flags,
                    uint16_t create_mode, AstraVfsFile *file, uint64_t *size,
                    uint16_t *kind)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return open_mode_at(client, ASTRA_VFS_FILE_INVALID, path, flags,
                        create_mode, file, size, kind);
}

uint32_t
astra_vfs_open_at(AstraVfsClient *client, AstraVfsFile directory,
                  const char *path, uint32_t flags, AstraVfsFile *file,
                  uint64_t *size, uint16_t *kind)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_open_at_mode(client, directory, path, flags,
                                  ASTRA_VFS_MODE_DEFAULT, file, size, kind);
}

uint32_t
astra_vfs_open_at_mode(AstraVfsClient *client, AstraVfsFile directory,
                       const char *path, uint32_t flags,
                       uint16_t create_mode, AstraVfsFile *file,
                       uint64_t *size, uint16_t *kind)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return open_mode_at(client, directory, path, flags, create_mode, file,
                        size, kind);
}

uint32_t
astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status == ASTRA_VFS_OK) {
            status = ops->close(context, file);
            backend_leave(client);
        }
    } else {
        call = begin(client);
        call->request.file = file;
        status = exchange(client, call, ASTRA_VFS_OP_CLOSE);
    }
    /* Close consumes the caller's local description even after peer loss. */
    if (file != ASTRA_VFS_FILE_INVALID && client->description_release != NULL &&
        status != ASTRA_VFS_ERR_BAD_HANDLE && status != ASTRA_VFS_ERR_INVALID)
        client->description_release(client, client->ownership_epoch);
    return status;
}

uint32_t
astra_vfs_read_position(AstraVfsClient *client, AstraVfsFile file,
                        uint64_t offset, uint32_t flags, void *buffer,
                        uint32_t length, uint32_t *moved, uint64_t *position)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;
    uint32_t index;
    uint8_t *out = buffer;

    if (client == NULL || buffer == NULL || moved == NULL || position == NULL ||
        flags & ~ASTRA_VFS_IO_SEQUENTIAL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    *position = offset;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
        (client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS) == 0u)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        {
            AstraVfsBackend backend = {ops, context};

            status = astra_vfs_backend_read_position(&backend, file,
                offset, flags, buffer, length, moved, position);
        }
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.flags = flags;
    call->request.offset = offset;
    call->request.length = length;
    status = exchange(client, call, ASTRA_VFS_OP_READ);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    if (call->reply.count > length ||
        ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
         (call->reply.position < call->reply.count ||
          call->reply.position > INT64_MAX))) {
        return ASTRA_VFS_ERR_PROTOCOL; /* a service claiming more than it sent */
    }
    for (index = 0u; index < call->reply.count; ++index) {
        out[index] = call->reply.payload[index];
    }
    *moved = call->reply.count;
    *position = call->reply.position;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_read(AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
               void *buffer, uint32_t length, uint32_t *moved)
{
    uint64_t position;

    return astra_vfs_read_position(client, file, offset, 0u, buffer, length,
                                    moved, &position);
}

uint32_t
astra_vfs_seek(AstraVfsClient *client, AstraVfsFile file, int64_t delta,
               uint32_t origin, uint64_t *position)
{
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || position == NULL || file == ASTRA_VFS_FILE_INVALID ||
        origin > 2u)
        return ASTRA_VFS_ERR_INVALID;
    if (!(client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = file;
    call->request.offset = (uint64_t)delta;
    call->request.flags = origin;
    status = exchange(client, call, ASTRA_VFS_OP_SEEK);
    if (status == ASTRA_VFS_OK) {
        if (call->reply.position > INT64_MAX)
            return ASTRA_VFS_ERR_PROTOCOL;
        *position = call->reply.position;
    }
    return status;
}

uint32_t
astra_vfs_share(AstraVfsClient *client, AstraVfsFile file, uint64_t *token)
{
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || token == NULL || file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    *token = 0u;
    if (!(client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = file;
    status = exchange(client, call, ASTRA_VFS_OP_SHARE);
    if (status == ASTRA_VFS_OK) {
        if (call->reply.token == 0u)
            return ASTRA_VFS_ERR_PROTOCOL;
        *token = call->reply.token;
    }
    return status;
}

static uint32_t
adopt_share(AstraVfsClient *client, uint64_t token, uint32_t flags,
             AstraVfsFile *file)
{
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || token == 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (!(client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.offset = token;
    call->request.flags = flags;
    status = exchange(client, call, ASTRA_VFS_OP_ADOPT);
    if (status == ASTRA_VFS_OK && file != NULL) {
        if (call->reply.file == ASTRA_VFS_FILE_INVALID)
            return ASTRA_VFS_ERR_PROTOCOL;
        *file = call->reply.file;
        if (client->description_acquire != NULL)
            client->description_acquire(client);
    }
    return status;
}

uint32_t
astra_vfs_adopt(AstraVfsClient *client, uint64_t token, AstraVfsFile *file)
{
    if (file == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *file = ASTRA_VFS_FILE_INVALID;
    return adopt_share(client, token, 0u, file);
}

uint32_t
astra_vfs_unshare(AstraVfsClient *client, uint64_t token)
{
    return adopt_share(client, token, ASTRA_VFS_ADOPT_DISCARD, NULL);
}

uint32_t
astra_vfs_file_flags(AstraVfsClient *client, AstraVfsFile file,
                     uint32_t mask, uint32_t flags, uint32_t *result)
{
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;
    const uint32_t allowed = ASTRA_VFS_OPEN_MASK;

    if (client == NULL || result == NULL || file == ASTRA_VFS_FILE_INVALID ||
        mask & ~ASTRA_VFS_FILE_STATUS_MASK || flags & ~mask)
        return ASTRA_VFS_ERR_INVALID;
    if (!(client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS))
        return ASTRA_VFS_ERR_UNSUPPORTED;
    call = begin(client);
    call->request.file = file;
    call->request.flags = mask;
    call->request.offset = flags;
    status = exchange(client, call, ASTRA_VFS_OP_FILE_FLAGS);
    if (status == ASTRA_VFS_OK) {
        if (call->reply.open_flags & ~allowed ||
            !(call->reply.open_flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)))
            return ASTRA_VFS_ERR_PROTOCOL;
        *result = call->reply.open_flags;
    }
    return status;
}

uint32_t
astra_vfs_write_position(AstraVfsClient *client, AstraVfsFile file,
                         uint64_t offset, uint32_t flags, uint64_t max_end, const void *buffer,
                         uint32_t length, uint32_t *moved,
                         uint64_t *position)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    const uint8_t *in = buffer;
    uint32_t status;
    uint32_t index;

    if (client == NULL || buffer == NULL || moved == NULL || position == NULL ||
        (flags & ~(ASTRA_VFS_WRITE_APPEND | ASTRA_VFS_IO_SEQUENTIAL)) != 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *moved = 0u;
    if ((flags & ASTRA_VFS_IO_SEQUENTIAL) != 0u &&
        (client->features & ASTRA_VFS_FEATURE_DESCRIPTIONS) == 0u)
        return ASTRA_VFS_ERR_UNSUPPORTED;
    *position = offset;
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->write(context, file, offset, flags, max_end, buffer, length,
                            moved, position);
        backend_leave(client);
        return status == ASTRA_VFS_OK && *moved > length ?
            ASTRA_VFS_ERR_PROTOCOL : status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.offset = offset;
    call->request.length = length;
    call->request.flags = flags;
    call->request.write_limit = max_end;
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
    *position = call->reply.position;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_write(AstraVfsClient *client, AstraVfsFile file, uint64_t offset, uint64_t max_end,
                const void *buffer, uint32_t length, uint32_t *moved)
{
    uint64_t position;

    return astra_vfs_write_position(client, file, offset, 0u, max_end, buffer, length,
                                    moved, &position);
}

uint32_t
astra_vfs_sync(AstraVfsClient *client, AstraVfsFile file)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
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
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL)
        return ASTRA_VFS_ERR_INVALID;
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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL || meta == NULL)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        if (!path_text_valid(path, NULL))
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
        meta->meta = info.meta;
        meta->reserved = 0u;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
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
    meta->meta = call->reply.meta;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_stat_at_meta(AstraVfsClient *client, AstraVfsFile directory,
                       const char *path, uint32_t flags, AstraVfsDirEntry *meta)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL || meta == NULL ||
        directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    if (!path_text_valid(path, NULL) || path[0] == '\0' || path[0] == '/' ||
        (flags & ~ASTRA_VFS_STAT_FOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->stat_at(context, directory, path, flags, &info);
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
        meta->meta = info.meta;
        meta->reserved = 0u;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.file = directory;
    call->request.flags = flags;
    status = exchange(client, call, ASTRA_VFS_OP_STAT_AT);
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
    meta->meta = call->reply.meta;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_stat_file_meta(AstraVfsClient *client, AstraVfsFile file,
                         AstraVfsDirEntry *meta)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL || meta == NULL || file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->stat_node(context, file, &info);
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
        meta->meta = info.meta;
        meta->reserved = 0u;
        return ASTRA_VFS_OK;
    }
    call = begin(client);
    call->request.file = file;
    status = exchange(client, call, ASTRA_VFS_OP_STAT_FILE);
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
    meta->meta = call->reply.meta;
    meta->reserved = 0u;
    return ASTRA_VFS_OK;
}

uint32_t
astra_vfs_stat(AstraVfsClient *client, const char *path, uint64_t *size,
               uint16_t *kind)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsNodeInfo info = {0};
    uint32_t status;

    if (client == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        if (!path_text_valid(path, NULL))
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
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;
    uint32_t at;

    if (client == NULL || name == NULL || capacity == 0u || next == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    name[0] = '\0';
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.offset = cursor;
    status = result_storage(call, ASTRA_VFS_NAME_MAX);
    if (status != ASTRA_VFS_OK)
        return status;
    status = exchange(client, call, ASTRA_VFS_OP_READDIR);
    if (status != ASTRA_VFS_OK) {
        return status;
    }
    *next = call->reply.cursor;
    if (call->reply.count >= ASTRA_VFS_NAME_MAX)
        return ASTRA_VFS_ERR_PROTOCOL;
    if (call->reply.count >= capacity) {
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (at = 0u; at < call->reply.count; ++at) {
        name[at] = (char)call->result_bytes[at];
    }
    if (!astra_utf8_validate(name, call->reply.count, 0u))
        return ASTRA_VFS_ERR_PROTOCOL;
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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    const uint8_t *payload;
    uint32_t operation = ASTRA_VFS_OP_READDIR_AREA;
    uint32_t status;
    uint32_t at = 0u;
    uint32_t found = 0u;

    if (client == NULL || entries == NULL || capacity == 0u || count == NULL ||
        next == NULL) {
        return ASTRA_VFS_ERR_INVALID;
    }
    *count = 0u;
    *next = cursor;
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.offset = cursor;
    call->request.length = capacity;
    call->request.file = directory;
    if (capacity > ASTRA_VFS_BULK_MAX /
            (ASTRA_VFS_DIRENT_HEADER + ASTRA_VFS_NAME_MAX - 1u))
        return ASTRA_VFS_ERR_LIMIT;
    status = result_storage(call, capacity *
        (ASTRA_VFS_DIRENT_HEADER + ASTRA_VFS_NAME_MAX - 1u));
    if (status != ASTRA_VFS_OK)
        return status;
    status = exchange(client, call, operation);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.count > call->transfer.output_capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    payload = call->result_bytes;
    while (at < call->reply.count) {
        uint32_t length;

        const uint8_t *record = &payload[at];

        if (found == capacity ||
            call->reply.count - at < ASTRA_VFS_DIRENT_HEADER)
            return ASTRA_VFS_ERR_PROTOCOL;
        memset(&entries[found], 0, sizeof(entries[found]));
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
        if (!astra_utf8_validate(entries[found].name, length, 0u))
            return ASTRA_VFS_ERR_PROTOCOL;
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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

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
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return readdir_batch(client, directory, path, cursor, entries, capacity,
                         count, next);
}

static uint32_t path_operation(AstraVfsClient *client, uint32_t operation,
    AstraVfsFile base, const char *path, AstraVfsFile second_base,
    const char *second_path, uint64_t value, void *buffer,
    uint32_t capacity, uint32_t *length)
{
    if (second_path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(second_path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    AstraVfsRequest *request;
    const AstraVfsBackendOps *ops;
    void *context;
    uint32_t status;
    int two = operation == ASTRA_VFS_OP_RENAME || operation == ASTRA_VFS_OP_LINK ||
              operation == ASTRA_VFS_OP_SYMLINK;

    if (client == NULL ||
        (operation != ASTRA_VFS_OP_DIRECTORY_PATH && !path_text_valid(path, NULL)) ||
        (two && !path_text_valid(second_path, NULL)) ||
        ((operation == ASTRA_VFS_OP_READLINK || operation == ASTRA_VFS_OP_DIRECTORY_PATH) &&
         (buffer == NULL || capacity == 0u || length == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    if (length != NULL)
        *length = 0u;
    if (capacity > ASTRA_VFS_PATH_MAX - 1u)
        capacity = ASTRA_VFS_PATH_MAX - 1u;
    call = begin(client);
    request = &call->request;
    status = set_paths(call, operation == ASTRA_VFS_OP_DIRECTORY_PATH ? NULL : path,
        two ? second_path : NULL);
    if (status != ASTRA_VFS_OK)
        return status;
    status = result_storage(call, capacity);
    if (status != ASTRA_VFS_OK)
        return status;
    request->file = base;
    request->second_file = two && operation != ASTRA_VFS_OP_SYMLINK ? second_base : 0u;
    request->offset = value;
    request->length = capacity;
    if (client->direct_backend_ops != NULL) {
        AstraVfsBackend backend;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        backend = (AstraVfsBackend){ops, context};
        memset(&call->reply, 0, sizeof(call->reply));
        status = astra_vfs_backend_path_operation(&backend, operation, request,
            &call->transfer, base, second_base, &call->reply);
        backend_leave(client);
    } else
        status = exchange_request(client, operation, request, call);
    if (status == ASTRA_VFS_OK && length != NULL) {
        if (call->reply.count > capacity ||
            !astra_utf8_validate(call->result_bytes, call->reply.count, 0u))
            return ASTRA_VFS_ERR_PROTOCOL;
        memcpy(buffer, call->result_bytes, call->reply.count);
        *length = call->reply.count;
    }
    return status;
}

uint32_t
astra_vfs_mkdir(AstraVfsClient *client, const char *path)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_mkdir_mode(client, path, ASTRA_VFS_MODE_DEFAULT);
}

uint32_t
astra_vfs_mkdir_mode(AstraVfsClient *client, const char *path,
                     uint16_t create_mode)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_mkdir_at(client, 0u, path, create_mode);
}

uint32_t
astra_vfs_unlink(AstraVfsClient *client, const char *path, uint32_t flags)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || (flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u) {
        return ASTRA_VFS_ERR_INVALID;
    }
    if (client->direct_backend_ops != NULL) {
        if (!path_text_valid(path, NULL))
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->unlink(context, path, flags);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.flags = flags;
    return exchange(client, call, ASTRA_VFS_OP_UNLINK);
}

uint32_t
astra_vfs_unlink_at(AstraVfsClient *client, AstraVfsFile directory,
                    const char *path, uint32_t flags)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || directory == ASTRA_VFS_FILE_INVALID)
        return client == NULL ? ASTRA_VFS_ERR_INVALID :
                                ASTRA_VFS_ERR_BAD_HANDLE;
    if (!path_text_valid(path, NULL) || path[0] == '\0' || path[0] == '/' ||
        (flags & ~ASTRA_VFS_UNLINK_DIRECTORY) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->unlink_at(context, directory, path, flags);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.file = directory;
    call->request.flags = flags;
    return exchange(client, call, ASTRA_VFS_OP_UNLINK_AT);
}

uint32_t
astra_vfs_rename(AstraVfsClient *client, const char *from, const char *to)
{
    return astra_vfs_rename_at(client, 0u, from, 0u, to);
}

uint32_t
astra_vfs_chmod(AstraVfsClient *client, const char *path, uint16_t mode)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        if (!path_text_valid(path, NULL))
            return ASTRA_VFS_ERR_INVALID;
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->chmod(context, path, mode);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.offset = mode;
    return exchange(client, call, ASTRA_VFS_OP_CHMOD);
}

uint32_t
astra_vfs_chmod_file(AstraVfsClient *client, AstraVfsFile file,
                     uint16_t mode)
{
    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->chmod_node(context, file, mode);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    call->request.offset = mode;
    return exchange(client, call, ASTRA_VFS_OP_CHMOD_FILE);
}

uint32_t
astra_vfs_chmod_at(AstraVfsClient *client, AstraVfsFile directory,
                   const char *path, uint16_t mode, uint32_t flags)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || (mode & (uint16_t)~ASTRA_VFS_MODE_MASK) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (!path_text_valid(path, NULL) || path[0] == '\0' || path[0] == '/' ||
        (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u)
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->chmod_at(context, directory, path, mode, flags);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    call->request.file = directory;
    call->request.offset = mode;
    call->request.flags = flags;
    return exchange(client, call, ASTRA_VFS_OP_CHMOD_AT);
}

uint32_t
astra_vfs_readlink(AstraVfsClient *client, const char *path, void *buffer,
                   uint32_t capacity, uint32_t *length)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_readlink_at(client, 0u, path, buffer, capacity, length);
}

uint32_t
astra_vfs_symlink(AstraVfsClient *client, const char *target,
                  const char *path)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return astra_vfs_symlink_at(client, target, 0u, path);
}

uint32_t
astra_vfs_link(AstraVfsClient *client, const char *from, const char *to)
{
    return astra_vfs_link_at(client, 0u, from, 0u, to);
}

static uint32_t
filesystem_info(AstraVfsClient *client, AstraVfsFile file, const char *path,
                AstraVfsFilesystemInfo *info)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    const AstraVfsBackendOps *ops;
    const uint8_t *payload;
    void *context;
    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || info == NULL ||
        ((file == ASTRA_VFS_FILE_INVALID) == (path == NULL)))
        return ASTRA_VFS_ERR_INVALID;
    if (path != NULL && !path_text_valid(path, NULL))
        return ASTRA_VFS_ERR_INVALID;
    if (client->direct_backend_ops != NULL) {
        status = backend_enter(client, &ops, &context);
        if (status != ASTRA_VFS_OK)
            return status;
        status = ops->filesystem_info(context, file, path, info);
        backend_leave(client);
        return status;
    }
    call = begin(client);
    call->request.file = file;
    if (path != NULL) {
        uint32_t path_status = set_path(call, path);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }
    status = exchange(client, call, ASTRA_VFS_OP_FILESYSTEM_INFO);
    if (status != ASTRA_VFS_OK)
        return status;
    if (call->reply.count != ASTRA_VFS_FILESYSTEM_INFO_SIZE)
        return ASTRA_VFS_ERR_PROTOCOL;
    payload = call->reply.payload;
    memset(info, 0, sizeof(*info));
    info->size = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, size));
    info->flags = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, flags));
    info->block_size = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, block_size));
    info->fragment_size = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, fragment_size));
    info->blocks = astra_load_be64(
        payload + offsetof(AstraVfsFilesystemInfo, blocks));
    info->blocks_free = astra_load_be64(
        payload + offsetof(AstraVfsFilesystemInfo, blocks_free));
    info->blocks_available = astra_load_be64(
        payload + offsetof(AstraVfsFilesystemInfo, blocks_available));
    info->files = astra_load_be64(
        payload + offsetof(AstraVfsFilesystemInfo, files));
    info->files_free = astra_load_be64(
        payload + offsetof(AstraVfsFilesystemInfo, files_free));
    info->name_max = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, name_max));
    info->reserved = astra_load_be32(
        payload + offsetof(AstraVfsFilesystemInfo, reserved));
    return info->size == sizeof(*info) && info->reserved == 0u ?
        ASTRA_VFS_OK : ASTRA_VFS_ERR_PROTOCOL;
}

uint32_t
astra_vfs_filesystem_info_path(AstraVfsClient *client, const char *path,
                               AstraVfsFilesystemInfo *info)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return filesystem_info(client, ASTRA_VFS_FILE_INVALID, path, info);
}

uint32_t
astra_vfs_filesystem_info_file(AstraVfsClient *client, AstraVfsFile file,
                               AstraVfsFilesystemInfo *info)
{
    if (file == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return filesystem_info(client, file, NULL, info);
}

uint32_t astra_vfs_lookup(AstraVfsClient *client, AstraVfsFile directory, const char *path,
                           uint32_t flags, uint32_t links_left,
                           AstraVfsDirEntry *meta, AstraVfsEscape *escape)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    AstraVfsCallState *call ASTRA_AUTO_VFS_CALL_STORAGE = NULL;
    uint32_t status;

    if (client == NULL || meta == NULL || escape == NULL ||
        (escape->capacity != 0u && escape->path == NULL) ||
        links_left > ASTRA_VFS_LINK_MAX ||
        (flags & ~(ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_MISSING |
                   ASTRA_VFS_LOOKUP_ENTRY)) != 0u ||
        (flags & (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY)) ==
            (ASTRA_VFS_LOOKUP_FOLLOW | ASTRA_VFS_LOOKUP_ENTRY))
        return ASTRA_VFS_ERR_INVALID;
    uint8_t *continuation = escape->path;
    uint32_t continuation_capacity = escape->capacity;
    *escape = (AstraVfsEscape){.path = continuation, .capacity = continuation_capacity};
    call = begin(client);
    if (directory != ASTRA_VFS_FILE_INVALID && (path == NULL || path[0] == '\0' || path[0] == '/'))
        return ASTRA_VFS_ERR_INVALID;
    status = set_path(call, path);
    if (status != ASTRA_VFS_OK)
        return status;
    status = result_storage(call, ASTRA_VFS_ESCAPE_HEADER + ASTRA_VFS_PATH_MAX);
    if (status != ASTRA_VFS_OK)
        return status;
    call->request.file = directory;
    call->request.flags = flags;
    call->request.length = links_left;
    status = exchange(client, call, ASTRA_VFS_OP_LOOKUP);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_ESCAPED)
        return status;
    if (call->reply.count < ASTRA_VFS_ESCAPE_HEADER ||
        call->reply.count > call->transfer.output_capacity)
        return ASTRA_VFS_ERR_PROTOCOL;
    escape->kind = astra_load_be16(call->result_bytes);
    escape->length = astra_load_be16(call->result_bytes + 2u);
    escape->consumed = astra_load_be32(call->result_bytes + 4u);
    escape->links = astra_load_be32(call->result_bytes + 8u);
    if (escape->length > ASTRA_VFS_ESCAPE_PATH_MAX ||
        call->reply.count != ASTRA_VFS_ESCAPE_HEADER + escape->length)
        return ASTRA_VFS_ERR_PROTOCOL;
    call->result_bytes[call->reply.count] = '\0';
    if (status == ASTRA_VFS_ERR_ESCAPED &&
        astra_vfs_path_measure((const char *)call->result_bytes + ASTRA_VFS_ESCAPE_HEADER,
            NULL) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_PROTOCOL;
    if (escape->length > continuation_capacity)
        return ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
    if (escape->length != 0u)
        memcpy(continuation, call->result_bytes + ASTRA_VFS_ESCAPE_HEADER, escape->length);
    if (escape->links > links_left ||
        escape->length > ASTRA_VFS_ESCAPE_PATH_MAX ||
        call->reply.count != ASTRA_VFS_ESCAPE_HEADER + escape->length ||
        (status == ASTRA_VFS_OK ? escape->kind != 0u || escape->length != 0u ||
                                 escape->consumed != 0u :
         (escape->kind != ASTRA_VFS_ESCAPE_PARENT &&
          escape->kind != ASTRA_VFS_ESCAPE_SYMLINK) || escape->length == 0u ||
          escape->path[0] != '/' || escape->consumed == 0u ||
          escape->consumed > strlen(path) ||
          !astra_utf8_validate(escape->path, escape->length, 0u)))
        return ASTRA_VFS_ERR_PROTOCOL;
    if (status == ASTRA_VFS_OK) {
        if (call->reply.kind > ASTRA_VFS_KIND_SYMLINK ||
            (call->reply.kind == ASTRA_VFS_KIND_UNKNOWN &&
             (!(flags & ASTRA_VFS_LOOKUP_MISSING) || call->reply.meta.valid != 0u)) ||
            (call->reply.kind == ASTRA_VFS_KIND_SYMLINK &&
             (flags & ASTRA_VFS_LOOKUP_FOLLOW)))
            return ASTRA_VFS_ERR_PROTOCOL;
        memset(meta, 0, sizeof(*meta));
        meta->size = call->reply.node_size;
        meta->mtime = call->reply.mtime;
        meta->uid = call->reply.uid;
        meta->gid = call->reply.gid;
        meta->kind = call->reply.kind;
        meta->mode = call->reply.mode;
        meta->nlink = call->reply.nlink;
        meta->meta = call->reply.meta;
    }
    return status;
}

uint32_t astra_vfs_mkdir_at(AstraVfsClient *client, AstraVfsFile directory,
                            const char *path, uint16_t mode)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return path_operation(client, ASTRA_VFS_OP_MKDIR, directory, path,
                           0u, NULL, mode, NULL, 0u, NULL);
}

uint32_t astra_vfs_rename_at(AstraVfsClient *client, AstraVfsFile from_directory,
                             const char *from, AstraVfsFile to_directory,
                             const char *to)
{
    return path_operation(client, ASTRA_VFS_OP_RENAME, from_directory, from,
                           to_directory, to, 0u, NULL, 0u, NULL);
}

uint32_t astra_vfs_link_at(AstraVfsClient *client, AstraVfsFile from_directory,
                           const char *from, AstraVfsFile to_directory,
                           const char *to)
{
    return path_operation(client, ASTRA_VFS_OP_LINK, from_directory, from,
                           to_directory, to, 0u, NULL, 0u, NULL);
}

uint32_t astra_vfs_symlink_at(AstraVfsClient *client, const char *target,
                              AstraVfsFile directory, const char *path)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return path_operation(client, ASTRA_VFS_OP_SYMLINK, directory, target,
                           0u, path, 0u, NULL, 0u, NULL);
}

uint32_t astra_vfs_readlink_at(AstraVfsClient *client, AstraVfsFile directory,
                               const char *path, void *buffer,
                               uint32_t capacity, uint32_t *length)
{
    if (path != NULL) {
        uint32_t path_status = astra_vfs_path_measure(path, NULL);
        if (path_status != ASTRA_VFS_OK)
            return path_status;
    }

    return path_operation(client, ASTRA_VFS_OP_READLINK, directory, path,
                           0u, NULL, 0u, buffer, capacity, length);
}

uint32_t astra_vfs_directory_path(AstraVfsClient *client, AstraVfsFile directory,
                                  void *buffer, uint32_t capacity,
                                  uint32_t *length)
{
    if (directory == ASTRA_VFS_FILE_INVALID)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    return path_operation(client, ASTRA_VFS_OP_DIRECTORY_PATH, directory, NULL,
                           0u, NULL, 0u, buffer, capacity, length);
}
