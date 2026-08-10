/*
 * The storage service core.
 *
 * Everything here is validation, handle bookkeeping and dispatch. The bytes
 * belong to a backend and the transport belongs to a caller; see
 * astra/vfs_backend.h and astra/vfs_service_core.h for why the three are apart.
 */

#include <astra/vfs_service_core.h>

#include <stddef.h>

/*
 * A handle is (generation << 16) | (slot + 1).
 *
 * Slot zero would collide with ASTRA_VFS_FILE_INVALID, so slots are stored
 * one-based on the wire. The generation advances every time a slot is reused,
 * which is what turns "this handle is stale" from undetectable into a counted
 * refusal.
 */
#define HANDLE_SLOT_MASK 0xffffu
#define HANDLE_MAKE(slot, generation) \
    ((((uint32_t)(generation)) << 16) | ((uint32_t)(slot) + 1u))
#define HANDLE_SLOT(handle) (((handle) & HANDLE_SLOT_MASK) - 1u)
#define HANDLE_GENERATION(handle) ((uint16_t)((handle) >> 16))

static void
clear_reply(AstraVfsReply *reply, uint16_t version)
{
    uint32_t index;
    unsigned char *bytes = (unsigned char *)reply;

    for (index = 0u; index < (uint32_t)sizeof(*reply); ++index) {
        bytes[index] = 0u;
    }
    reply->size = (uint16_t)ASTRA_VFS_REPLY_SIZE;
    reply->version = version;
    reply->status = ASTRA_VFS_ERR_PROTOCOL;
}

/*
 * The path is bytes off the wire, so it is not trusted to be terminated.
 * Anything unterminated is a malformed record rather than a long path,
 * because accepting it would mean reading past the record.
 *
 * Applied only to path-addressed operations. A WRITE carries payload in the
 * same bytes and must not be required to contain a NUL: a binary write of
 * exactly ASTRA_VFS_IO_MAX bytes legitimately has none.
 */
static int
path_valid(const AstraVfsRequest *request)
{
    uint32_t index;

    for (index = 0u; index < ASTRA_VFS_PATH_MAX; ++index) {
        if (request->body.path[index] == 0u) {
            return 1;
        }
    }
    return 0;
}

static int
operation_takes_path(uint32_t operation)
{
    return operation == ASTRA_VFS_OP_OPEN ||
           operation == ASTRA_VFS_OP_STAT ||
           operation == ASTRA_VFS_OP_READDIR ||
           operation == ASTRA_VFS_OP_READDIR_BATCH ||
           operation == ASTRA_VFS_OP_MKDIR ||
           operation == ASTRA_VFS_OP_UNLINK;
}

static void
handle_readdir_batch(AstraVfsService *service,
                     const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t cursor = request->offset;
    uint32_t entries = 0u;
    uint32_t used = 0u;

    if (request->length == 0u) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    while (entries < request->length &&
           used + 3u + ASTRA_VFS_NAME_MAX - 1u <= ASTRA_VFS_IO_MAX) {
        uint64_t next = 0u;
        uint32_t length = 0u;
        uint32_t status;

        info.size = 0u;
        info.kind = ASTRA_VFS_KIND_UNKNOWN;
        name[0] = '\0';
        status = service->backend.ops->readdir(
            service->backend.context, (const char *)request->body.path,
            cursor, name, (uint32_t)sizeof(name), &info, &next);
        if (status == ASTRA_VFS_ERR_NOT_FOUND) {
            if (entries == 0u) {
                reply->status = status;
                return;
            }
            cursor = 0u;
            break;
        }
        if (status != ASTRA_VFS_OK) {
            reply->status = status;
            return;
        }
        while (length < ASTRA_VFS_NAME_MAX && name[length] != '\0')
            ++length;
        if (length == 0u || length == ASTRA_VFS_NAME_MAX || next == cursor) {
            reply->status = ASTRA_VFS_ERR_PROTOCOL;
            return;
        }
        reply->payload[used++] = (uint8_t)(info.kind >> 8);
        reply->payload[used++] = (uint8_t)info.kind;
        reply->payload[used++] = (uint8_t)length;
        for (uint32_t index = 0u; index < length; ++index)
            reply->payload[used++] = (uint8_t)name[index];
        cursor = next;
        ++entries;
    }
    reply->status = ASTRA_VFS_OK;
    reply->count = used;
    reply->cursor = cursor;
}

static AstraVfsSessionSlot *
find_session(AstraVfsService *service, uint32_t session)
{
    uint32_t index;

    if (session == ASTRA_VFS_SESSION_INVALID) {
        return NULL;
    }
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (service->sessions[index].id == session) {
            return &service->sessions[index];
        }
    }
    return NULL;
}

/*
 * Resolves a file handle for one session. The two failure modes are counted
 * apart on purpose: a stale generation is a client using a handle it already
 * closed, and a cross-session hit is a client reaching for someone else's.
 * The first is a bug, the second is an attack, and a single counter would
 * hide the difference.
 */
static AstraVfsOpenFile *
find_file(AstraVfsService *service, uint32_t session, AstraVfsFile handle,
          uint32_t *status)
{
    uint32_t slot;
    AstraVfsOpenFile *file;

    *status = ASTRA_VFS_ERR_BAD_HANDLE;
    if (handle == ASTRA_VFS_FILE_INVALID ||
        (handle & HANDLE_SLOT_MASK) == 0u) {
        return NULL;
    }
    slot = HANDLE_SLOT(handle);
    if (slot >= ASTRA_VFS_FILE_MAX) {
        return NULL;
    }
    file = &service->files[slot];
    if (file->session == 0u) {
        return NULL;
    }
    if (file->generation != HANDLE_GENERATION(handle)) {
        ++service->stats.stale_handles;
        return NULL;
    }
    if (file->session != session) {
        ++service->stats.cross_session_denied;
        *status = ASTRA_VFS_ERR_ACCESS;
        return NULL;
    }
    *status = ASTRA_VFS_OK;
    return file;
}

static void
release_file(AstraVfsService *service, AstraVfsOpenFile *file)
{
    (void)service->backend.ops->close(service->backend.context, file->node);
    file->session = 0u;
    file->node = 0u;
    file->flags = 0u;
    file->kind = ASTRA_VFS_KIND_UNKNOWN;
    ++file->generation;
    --service->open_files;
    ++service->stats.files_closed;
}

int
astra_vfs_service_init(AstraVfsService *service, const AstraVfsBackendOps *ops,
                       void *context)
{
    uint32_t index;

    if (service == NULL || ops == NULL || ops->open == NULL ||
        ops->close == NULL || ops->read == NULL || ops->write == NULL ||
        ops->stat == NULL || ops->readdir == NULL || ops->mkdir == NULL ||
        ops->unlink == NULL) {
        return 0;
    }
    {
        unsigned char *bytes = (unsigned char *)service;

        for (index = 0u; index < (uint32_t)sizeof(*service); ++index) {
            bytes[index] = 0u;
        }
    }
    service->backend.ops = ops;
    service->backend.context = context;
    service->next_session = 1u;
    /*
     * Generations start at 1 so a zeroed handle can never be mistaken for a
     * valid one from slot 0 generation 0.
     */
    for (index = 0u; index < ASTRA_VFS_FILE_MAX; ++index) {
        service->files[index].generation = 1u;
    }
    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        service->sessions[index].generation = 1u;
    }
    return 1;
}

static void
handle_hello(AstraVfsService *service, const AstraVfsRequest *request,
             AstraVfsReply *reply)
{
    uint32_t index;

    /*
     * The client sends the newest version it can speak. Agreeing on the lower
     * of the two is what lets an old client keep working against a new service
     * -- and refusing when the ranges do not overlap is what stops a silent
     * downgrade into a protocol neither side actually implements.
     */
    if (request->version < ASTRA_VFS_VERSION_MIN) {
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        return;
    }
    reply->version = request->version < ASTRA_VFS_VERSION ?
        request->version : ASTRA_VFS_VERSION;

    for (index = 0u; index < ASTRA_VFS_SESSION_MAX; ++index) {
        if (service->sessions[index].id == 0u) {
            service->sessions[index].id = service->next_session++;
            service->sessions[index].version = reply->version;
            if (service->next_session == 0u) {
                service->next_session = 1u;
            }
            reply->session = service->sessions[index].id;
            reply->status = ASTRA_VFS_OK;
            ++service->open_sessions;
            ++service->stats.sessions_opened;
            if (service->open_sessions > service->stats.peak_sessions) {
                service->stats.peak_sessions = service->open_sessions;
            }
            return;
        }
    }
    reply->status = ASTRA_VFS_ERR_LIMIT;
}

void
astra_vfs_service_release_session(AstraVfsService *service, uint32_t session)
{
    uint32_t index;
    AstraVfsSessionSlot *slot;

    if (service == NULL || session == ASTRA_VFS_SESSION_INVALID) {
        return;
    }
    /*
     * Files first: a session whose slot is already free would leave its open
     * files owned by an id nothing can present, which is a leak that only
     * shows up as a service that eventually refuses every open.
     */
    for (index = 0u; index < ASTRA_VFS_FILE_MAX; ++index) {
        if (service->files[index].session == session) {
            release_file(service, &service->files[index]);
        }
    }
    slot = find_session(service, session);
    if (slot == NULL) {
        return;
    }
    slot->id = 0u;
    ++slot->generation;
    --service->open_sessions;
    ++service->stats.sessions_closed;
}

static void
handle_open(AstraVfsService *service, uint32_t session,
            const AstraVfsRequest *request, AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint32_t index;
    uint32_t status;

    if ((request->flags & ~(ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                            ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
                            ASTRA_VFS_OPEN_DIRECTORY)) != 0u) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    /* A slot is found before the backend is asked, so a full table costs no
     * open that would then have to be undone. */
    for (index = 0u; index < ASTRA_VFS_FILE_MAX; ++index) {
        if (service->files[index].session == 0u) {
            break;
        }
    }
    if (index == ASTRA_VFS_FILE_MAX) {
        reply->status = ASTRA_VFS_ERR_LIMIT;
        return;
    }

    info.size = 0u;
    info.kind = ASTRA_VFS_KIND_UNKNOWN;
    status = service->backend.ops->open(service->backend.context,
                                        (const char *)request->body.path,
                                        request->flags, &node, &info);
    if (status != ASTRA_VFS_OK) {
        reply->status = status;
        return;
    }
    service->files[index].node = node;
    service->files[index].session = session;
    service->files[index].flags = request->flags;
    service->files[index].kind = info.kind;
    reply->file = HANDLE_MAKE(index, service->files[index].generation);
    reply->node_size = info.size;
    reply->kind = info.kind;
    reply->status = ASTRA_VFS_OK;
    ++service->open_files;
    ++service->stats.files_opened;
    if (service->open_files > service->stats.peak_open_files) {
        service->stats.peak_open_files = service->open_files;
    }
}

static void
handle_read(AstraVfsService *service, AstraVfsOpenFile *file,
            const AstraVfsRequest *request, AstraVfsReply *reply)
{
    uint32_t moved = 0u;
    uint32_t length = request->length;

    if ((file->flags & ASTRA_VFS_OPEN_READ) == 0u) {
        reply->status = ASTRA_VFS_ERR_ACCESS;
        return;
    }
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY) {
        reply->status = ASTRA_VFS_ERR_IS_DIR;
        return;
    }
    /*
     * Clamped rather than refused. A client asking for more than one message
     * carries is not wrong, it just gets a short read and asks again, which is
     * how every caller has to be written anyway once bulk transfer moves to
     * rings and a read can be short for a different reason.
     */
    if (length > ASTRA_VFS_IO_MAX) {
        length = ASTRA_VFS_IO_MAX;
    }
    reply->status = service->backend.ops->read(
        service->backend.context, file->node, request->offset, reply->payload,
        length, &moved);
    if (reply->status == ASTRA_VFS_OK) {
        reply->count = moved > length ? length : moved;
    }
}

static void
handle_write(AstraVfsService *service, AstraVfsOpenFile *file,
             const AstraVfsRequest *request, AstraVfsReply *reply)
{
    uint32_t moved = 0u;

    if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0u) {
        reply->status = ASTRA_VFS_ERR_ACCESS;
        return;
    }
    if (file->kind == ASTRA_VFS_KIND_DIRECTORY) {
        reply->status = ASTRA_VFS_ERR_IS_DIR;
        return;
    }
    /*
     * A write is refused rather than clamped, unlike a read: the payload
     * travels in the request, so a length past the record is a malformed
     * record and not an ambitious caller.
     */
    if (request->length > ASTRA_VFS_IO_MAX) {
        reply->status = ASTRA_VFS_ERR_INVALID;
        return;
    }
    reply->status = service->backend.ops->write(
        service->backend.context, file->node, request->offset,
        request->body.payload, request->length, &moved);
    if (reply->status == ASTRA_VFS_OK) {
        reply->count = moved;
    }
}

static void
handle_readdir(AstraVfsService *service, const AstraVfsRequest *request,
               AstraVfsReply *reply)
{
    AstraVfsNodeInfo info;
    char name[ASTRA_VFS_NAME_MAX];
    uint64_t next = 0u;
    uint32_t index;

    info.size = 0u;
    info.kind = ASTRA_VFS_KIND_UNKNOWN;
    name[0] = '\0';
    reply->status = service->backend.ops->readdir(
        service->backend.context, (const char *)request->body.path,
        request->offset, name, (uint32_t)sizeof(name), &info, &next);
    if (reply->status != ASTRA_VFS_OK) {
        return;
    }
    /*
     * Handed back unread. The cursor is the backend's own and the core has no
     * business knowing what it counts -- which is what keeps a scan stateless
     * here and costs nothing when a client walks away mid-listing.
     */
    reply->cursor = next;
    for (index = 0u; index < ASTRA_VFS_NAME_MAX && name[index] != '\0';
         ++index) {
        reply->payload[index] = (uint8_t)name[index];
    }
    reply->payload[index] = 0u;
    reply->count = index;
    reply->kind = info.kind;
    reply->node_size = info.size;
}

void
astra_vfs_service_dispatch(AstraVfsService *service, uint32_t operation,
                           const AstraVfsRequest *request,
                           AstraVfsReply *reply)
{
    AstraVfsSessionSlot *slot;
    AstraVfsOpenFile *file;
    uint32_t status;

    if (service == NULL || reply == NULL) {
        return;
    }
    clear_reply(reply, ASTRA_VFS_VERSION);
    if (request == NULL) {
        ++service->stats.protocol_rejects;
        return;
    }
    ++service->stats.requests;

    /*
     * Shape before meaning. A record whose own size is wrong came from a
     * different build of this protocol, and answering it as though the fields
     * meant what this build thinks they mean is how a version skew turns into
     * data loss instead of an error.
     *
     * The activity is not checked. It is the caller's account of what it was
     * doing, any value is as valid as any other, and a service that refused a
     * request over one would be refusing work because of a log field.
     */
    if (request->size != (uint16_t)ASTRA_VFS_REQUEST_SIZE ||
        operation == 0u || operation > ASTRA_VFS_OP_MAX ||
        (operation_takes_path(operation) && !path_valid(request))) {
        ++service->stats.protocol_rejects;
        goto done;
    }

    if (operation == ASTRA_VFS_OP_HELLO) {
        handle_hello(service, request, reply);
        goto done;
    }

    slot = find_session(service, request->session);
    if (slot == NULL) {
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_BAD_HANDLE;
        goto done;
    }
    reply->version = slot->version;
    reply->session = slot->id;

    switch (operation) {
    case ASTRA_VFS_OP_BYE:
        astra_vfs_service_release_session(service, slot->id);
        reply->session = ASTRA_VFS_SESSION_INVALID;
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_OPEN:
        handle_open(service, slot->id, request, reply);
        break;
    case ASTRA_VFS_OP_CLOSE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        release_file(service, file);
        reply->status = ASTRA_VFS_OK;
        break;
    case ASTRA_VFS_OP_READ:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        handle_read(service, file, request, reply);
        break;
    case ASTRA_VFS_OP_WRITE:
        file = find_file(service, slot->id, request->file, &status);
        if (file == NULL) {
            reply->status = status;
            break;
        }
        handle_write(service, file, request, reply);
        break;
    case ASTRA_VFS_OP_STAT: {
        AstraVfsNodeInfo info;

        info.size = 0u;
        info.kind = ASTRA_VFS_KIND_UNKNOWN;
        reply->status = service->backend.ops->stat(
            service->backend.context, (const char *)request->body.path, &info);
        if (reply->status == ASTRA_VFS_OK) {
            reply->node_size = info.size;
            reply->kind = info.kind;
        }
        break;
    }
    case ASTRA_VFS_OP_READDIR:
        handle_readdir(service, request, reply);
        break;
    case ASTRA_VFS_OP_READDIR_BATCH:
        if (slot->version < UINT16_C(4)) {
            reply->status = ASTRA_VFS_ERR_UNSUPPORTED;
            break;
        }
        handle_readdir_batch(service, request, reply);
        break;
    case ASTRA_VFS_OP_MKDIR:
        reply->status = service->backend.ops->mkdir(
            service->backend.context, (const char *)request->body.path);
        break;
    case ASTRA_VFS_OP_UNLINK:
        reply->status = service->backend.ops->unlink(
            service->backend.context, (const char *)request->body.path);
        break;
    default:
        ++service->stats.protocol_rejects;
        reply->status = ASTRA_VFS_ERR_PROTOCOL;
        break;
    }

done:
    if (reply->status != ASTRA_VFS_OK) {
        ++service->stats.replies_failed;
    }
}

const AstraVfsServiceStats *
astra_vfs_service_stats(const AstraVfsService *service)
{
    return service != NULL ? &service->stats : NULL;
}
