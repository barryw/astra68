/*
 * The supervisor's clients of protected VFS services.
 *
 * Storage and synthetic trees are ports returned by manifest-launched
 * services. This file connects those ports, binds the supervisor's namespace,
 * and maps each assign back to its client. It contains no filesystem backend.
 */

#include <vfs_host.h>

#include <loader.h>

#include <astra/event_emit.h>
#include <astra/runtime.h>
#include <astra/vfs_port_transport.h>

static AstraVfsClient vfs_client;
static AstraAssignTable vfs_assigns;
static uint32_t vfs_handle;
static int vfs_ready;

/*
 * The namespace begins once the storage service publishes its port. Resolution
 * remains in the Kit rather than in the shell.
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
    (void)astra_assign_bind(&vfs_assigns, "SERVICES", vfs_handle,
                            ASTRA_RIGHT_READ, "services");
    (void)astra_assign_bind(&vfs_assigns, "STARTUP", vfs_handle,
                            ASTRA_RIGHT_READ, "startup");
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
     * Where programs live, and a union: the person's own directory first, then
     * the shipped one. A name found in `local/commands` shadows the command
     * the system shipped, which is what override means -- and the shadowing is
     * visible, because a listing shows both and a launch records which member
     * answered.
     *
     * Both members are made if they are missing and omitted rather than fatal
     * if the volume refuses, the same shape WORK: has. A volume with no
     * commands directory has not had one installed yet.
     */
    status = astra_vfs_mkdir(&vfs_client, "/local");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        status = astra_vfs_mkdir(&vfs_client, "/local/commands");
    }
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "COMMANDS", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                "local/commands");
    } else {
        /*
         * Said once, and the union keeps working. A name that silently returns
         * less than it did yesterday is worse than a name that says why.
         */
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "COMMANDS: local member skipped, mkdir refused with "
                     "status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/commands");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        /*
         * Bind if the first member never made it, join if it did: the shipped
         * member answers on its own rather than the name vanishing with the
         * writable one.
         */
        if (astra_assign_lookup(&vfs_assigns, "COMMANDS") == NULL) {
            (void)astra_assign_bind(&vfs_assigns, "COMMANDS", vfs_handle,
                                    ASTRA_RIGHT_READ, "commands");
        } else {
            (void)astra_assign_join(&vfs_assigns, "COMMANDS", vfs_handle,
                                    ASTRA_RIGHT_READ, "commands");
        }
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "COMMANDS: shipped member skipped, mkdir refused with "
                     "status %u", status);
    }
    /*
     * Said once more, at the point it becomes true. Two "member skipped"
     * warnings tell a reader that each attempt failed; neither says the
     * conclusion that follows from both together -- that COMMANDS: now has
     * no members at all, and every launch this boot will fail with
     * "not a command" for a reason no single one of those two lines states.
     */
    if (astra_assign_lookup(&vfs_assigns, "COMMANDS") == NULL) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "COMMANDS: has no members at all");
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
supervisor_vfs_start(uint32_t port_handle)
{
    if (vfs_ready) {
        return 1;
    }
    if (port_handle == 0u) {
        return 0;
    }
    vfs_handle = port_handle;
    /*
     * The supervisor's own client is opened here rather than by the terminal,
     * so that a failure to agree a protocol version is a start-up failure with
     * somewhere to report it, instead of the first `ls` returning something
     * unhelpful.
     */
    if (astra_vfs_port_connect(&vfs_client, vfs_handle) != ASTRA_VFS_OK) {
        return 0;
    }
    /*
     * The port, and the handle every assign is bound with. It has to exist
     * before the bindings do: a name is bound to the authority a child will be
     * granted, so binding to anything else would mean handing a child a
     * different number than the one the shell resolves.
     */
    if (supervisor_vfs_register(&vfs_client, vfs_handle) == 0u) {
        return 0;
    }
    vfs_ready = 1;
    /* Binding uses the client, so the client has to be usable first. */
    bind_standard_assigns();
    return 1;
}

AstraVfsClient *
supervisor_vfs_client(void)
{
    return vfs_ready ? &vfs_client : NULL;
}

uint32_t
supervisor_vfs_port(void)
{
    return vfs_ready ? vfs_handle : 0u;
}

AstraAssignTable *
supervisor_assigns(void)
{
    return vfs_ready ? &vfs_assigns : NULL;
}

/*
 * The router, and what an assign's handle now is.
 *
 * It used to be a token this file invented -- a slot and a session packed into
 * a word -- which worked for routing inside one process and could not be
 * granted to anything, because it was not a kernel handle. **It is the
 * service's port send handle now.** That is what makes a child's namespace
 * possible: the same number the shell routes on is the one a launch hands over.
 *
 * The table maps that authority back to the already-connected remote client;
 * storage and synthetic trees each have their own session.
 */
#define VFS_CLIENT_MAX SUPERVISOR_MANIFEST_ENTRY_MAX

static struct {
    AstraVfsClient *client;
    uint32_t handle;      /* the service's port send handle */
} vfs_clients[VFS_CLIENT_MAX];

uint32_t
supervisor_vfs_register(AstraVfsClient *client, uint32_t port_handle)
{
    if (client == NULL || port_handle == 0u) {
        return 0u;
    }
    for (uint32_t index = 0u; index < VFS_CLIENT_MAX; ++index) {
        if (vfs_clients[index].client == NULL ||
            vfs_clients[index].client == client) {
            vfs_clients[index].client = client;
            vfs_clients[index].handle = port_handle;
            return port_handle;
        }
    }
    return 0u;
}

void
supervisor_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < VFS_CLIENT_MAX; ++index) {
        if (vfs_clients[index].client != NULL) {
            vfs_clients[index].client->activity = activity;
        }
    }
}

AstraVfsClient *
supervisor_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL) {
        return supervisor_vfs_client();
    }
    for (uint32_t index = 0u; index < VFS_CLIENT_MAX; ++index) {
        if (vfs_clients[index].client != NULL &&
            vfs_clients[index].handle == assign->handle) {
            return vfs_clients[index].client;
        }
    }
    /*
     * A binding whose service is gone. NULL rather than the storage client:
     * sending a path for one mount to another would answer with somebody
     * else's namespace, which is worse than not answering.
     */
    return NULL;
}
