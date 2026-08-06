/*
 * The storage service, hosted by whoever mounted the volume.
 *
 * This is the only file in the supervisor that knows a filesystem exists. It
 * binds lwext4 to the storage protocol's backend interface, stands the service
 * core on top, and hands out a connected client. The terminal above it names
 * no filesystem at all.
 *
 * The service is in this process because userspace cannot create a process --
 * no syscall does it. That is a deployment limit, not a design one: every
 * caller already goes through the protocol, so the day a loader exists, this
 * file is replaced by a launch and a port transport, and no caller changes.
 * See docs/DRIVER_AND_SERVICE_ARCHITECTURE.md 9.1.
 */

#include <vfs_host.h>

#include <astra/event_emit.h>
#include <astra/vfs_ext4_backend.h>
#include <astra/vfs_local_transport.h>
#include <astra/vfs_service_core.h>

static AstraVfsExt4Backend vfs_backend;
static AstraVfsService vfs_service;
static AstraVfsClient vfs_client;
static AstraAssignTable vfs_assigns;
static uint32_t vfs_handle;
static int vfs_ready;

/*
 * The namespace begins at the process that mounted the volume, which today is
 * this one. The layout spec has the startup manifest hand these bindings over
 * as capabilities at launch; userspace has no way to start a process yet, so
 * the mounter binds them for itself. When a loader exists this moves and no
 * client of the Kit changes, which is the reason resolution lives in the Kit
 * rather than in the shell.
 *
 * One partition, so SYS: is the whole volume and its read-only-ness is the
 * right it carries rather than the mount it names. See the wiring plan's
 * "where this knowingly falls short of the spec".
 */
static void
bind_standard_assigns(void)
{
    uint32_t status;

    astra_assign_table_init(&vfs_assigns);
    (void)astra_assign_bind(&vfs_assigns, "SYS", vfs_handle,
                            ASTRA_RIGHT_READ, "");
    /*
     * A volume with no work directory on it has not been used yet, so making
     * one is what installs it. A volume that refuses -- full, or read-only --
     * boots without WORK: rather than not at all: a binding that cannot be
     * made is omitted, never fatal.
     */
    status = astra_vfs_mkdir(&vfs_client, "/work");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "WORK", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "work");
    } else {
        /*
         * The first typed event on the machine, and it is one that used to say
         * nothing at all: a volume that would not take a work directory left
         * WORK: quietly unbound, and the terminal then refused every path a
         * person typed for a reason nothing on the machine had recorded.
         */
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "WORK: unbound, mkdir refused with status %u", status);
    }
    /*
     * What namespace this boot actually got. A rare fact recorded once, which
     * every later event is read against: a person asking why a path was
     * refused needs to know what the process was holding, and reconstructing
     * that from the refusals afterwards is guesswork.
     */
    ASTRA_EVENT2(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR, ASTRA_EVENT_LEVEL_NOTICE,
                 "namespace bound, %u assigns on session %u",
                 vfs_assigns.count, vfs_client.session);
}

int
supervisor_vfs_start(const char *mount_point)
{
    if (vfs_ready) {
        return 1;
    }
    if (!astra_vfs_ext4_init(&vfs_backend, mount_point)) {
        return 0;
    }
    if (!astra_vfs_service_init(&vfs_service, astra_vfs_ext4_ops(),
                                &vfs_backend)) {
        return 0;
    }
    /*
     * The supervisor's own client is opened here rather than by the terminal,
     * so that a failure to agree a protocol version is a start-up failure with
     * somewhere to report it, instead of the first `ls` returning something
     * unhelpful.
     */
    if (astra_vfs_connect(&vfs_client, astra_vfs_local_transport,
                          &vfs_service) != ASTRA_VFS_OK) {
        return 0;
    }
    vfs_ready = 1;
    vfs_handle = supervisor_vfs_register(&vfs_client);
    if (vfs_handle == 0u) {
        return 0;
    }
    /* Binding uses the client, so the client has to be usable first. */
    bind_standard_assigns();
    return 1;
}

AstraVfsClient *
supervisor_vfs_client(void)
{
    return vfs_ready ? &vfs_client : NULL;
}

AstraAssignTable *
supervisor_assigns(void)
{
    return vfs_ready ? &vfs_assigns : NULL;
}

/*
 * The router. Two entries because there are two services, and matched by
 * session because that is what an assign carries as its handle today.
 */
#define VFS_CLIENT_MAX 2u
static AstraVfsClient *vfs_clients[VFS_CLIENT_MAX];

/*
 * The handle an assign carries: this table's slot, one-based, in the high
 * halfword, and the session in the low one. The session alone would not do --
 * each service numbers its own sessions from one, so every mount would answer
 * to the same handle and the first registered would answer for all of them.
 */
#define CLIENT_HANDLE(index, session) \
    ((((index) + 1u) << 16) | ((session) & 0xFFFFu))

uint32_t
supervisor_vfs_register(AstraVfsClient *client)
{
    if (client == NULL) {
        return 0u;
    }
    for (uint32_t index = 0u; index < VFS_CLIENT_MAX; ++index) {
        if (vfs_clients[index] == NULL || vfs_clients[index] == client) {
            vfs_clients[index] = client;
            return CLIENT_HANDLE(index, client->session);
        }
    }
    return 0u;
}

void
supervisor_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < VFS_CLIENT_MAX; ++index) {
        if (vfs_clients[index] != NULL) {
            vfs_clients[index]->activity = activity;
        }
    }
}

AstraVfsClient *
supervisor_vfs_client_for(const AstraAssign *assign)
{
    uint32_t slot;

    if (assign == NULL) {
        return supervisor_vfs_client();
    }
    slot = assign->handle >> 16;
    if (slot != 0u && slot <= VFS_CLIENT_MAX &&
        vfs_clients[slot - 1u] != NULL) {
        return vfs_clients[slot - 1u];
    }
    /*
     * A binding whose service is gone. NULL rather than the storage client:
     * sending a path for one mount to another would answer with somebody
     * else's namespace, which is worse than not answering.
     */
    return NULL;
}
