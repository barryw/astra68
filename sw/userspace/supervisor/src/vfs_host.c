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
#include <astra/vfs_host_direct.h>
#include <astra/vfs_port_transport.h>

static AstraVfsClient vfs_client;
static AstraAssignTable vfs_assigns;
static uint32_t vfs_handle;
static int vfs_ready;

/*
 * The namespace begins once the storage service publishes its port. Resolution
 * remains in the Kit rather than in the shell.
 *
 * The boot SD partition is /dh0. /system names the selected boot volume; its
 * rights come from the grant, not from the ext4 mount.
 */
static void
bind_standard_assigns(void)
{
    uint32_t status;

    astra_assign_table_init(&vfs_assigns);
    (void)astra_assign_bind(&vfs_assigns, "DH0", vfs_handle,
                            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "");
    (void)astra_assign_bind(&vfs_assigns, "SYSTEM", vfs_handle,
                            ASTRA_RIGHT_READ, "");
    (void)astra_assign_bind(&vfs_assigns, "SERVICES", vfs_handle,
                            ASTRA_RIGHT_READ, "services");
    (void)astra_assign_bind(&vfs_assigns, "STARTUP", vfs_handle,
                            ASTRA_RIGHT_READ, "startup");
    status = astra_vfs_mkdir(&vfs_client, "/config");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "CONFIG", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                "config");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/config/ unbound, mkdir refused with status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/libs");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "LIBS", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "libs");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/libs/ unbound, mkdir refused with status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/apps");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "APPS", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "apps");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/apps/ unbound, mkdir refused with status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/trash");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "TRASH", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                "trash");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/trash/ unbound, mkdir refused with status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/tmp");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "TMP", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "tmp");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/tmp/ unbound, mkdir refused with status %u", status);
    }
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
                     "/work/ unbound, mkdir refused with status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/home");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "HOME", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "home");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/home/ unbound, mkdir refused with status %u", status);
    }
    /* PATH orders local overrides before the shipped commands directory. */
    status = astra_vfs_mkdir(&vfs_client, "/local");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        status = astra_vfs_mkdir(&vfs_client, "/local/commands");
    }
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "LOCAL", vfs_handle,
                                ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE,
                                "local");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/local/commands skipped, mkdir refused with "
                     "status %u", status);
    }
    status = astra_vfs_mkdir(&vfs_client, "/commands");
    if (status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS) {
        (void)astra_assign_bind(&vfs_assigns, "COMMANDS", vfs_handle,
                                ASTRA_RIGHT_READ, "commands");
    } else {
        ASTRA_EVENT1(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/commands/ shipped member skipped, mkdir refused with "
                     "status %u", status);
    }
    if (astra_assign_lookup(&vfs_assigns, "COMMANDS") == NULL) {
        ASTRA_EVENT0(ASTRA_EVENT_SUBSYSTEM_SUPERVISOR,
                     ASTRA_EVENT_LEVEL_WARNING,
                     "/commands/ has no members at all");
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
    if (astra_vfs_host_port_connect(&vfs_client, vfs_handle) !=
        ASTRA_VFS_OK) {
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
static SupervisorVfsClientTable vfs_clients;

uint32_t
supervisor_vfs_register(AstraVfsClient *client, uint32_t port_handle)
{
    if (client == NULL || port_handle == 0u) {
        return 0u;
    }
    for (uint32_t index = 0u; index < vfs_clients.count; ++index) {
        if (vfs_clients.entries[index].client == client) {
            vfs_clients.entries[index].handle = port_handle;
            return port_handle;
        }
    }
    if (!supervisor_vfs_client_table_reserve(
            &vfs_clients, vfs_clients.count + 1u,
            astra_runtime_reallocate))
        return 0u;
    vfs_clients.entries[vfs_clients.count].client = client;
    vfs_clients.entries[vfs_clients.count++].handle = port_handle;
    return port_handle;
}

void
supervisor_vfs_set_activity(uint32_t activity)
{
    for (uint32_t index = 0u; index < vfs_clients.count; ++index) {
        if (vfs_clients.entries[index].client != NULL) {
            vfs_clients.entries[index].client->activity = activity;
        }
    }
}

AstraVfsClient *
supervisor_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL) {
        return supervisor_vfs_client();
    }
    for (uint32_t index = 0u; index < vfs_clients.count; ++index) {
        if (vfs_clients.entries[index].client != NULL &&
            vfs_clients.entries[index].handle == assign->handle) {
            return vfs_clients.entries[index].client;
        }
    }
    /*
     * A binding whose service is gone. NULL rather than the storage client:
     * sending a path for one mount to another would answer with somebody
     * else's namespace, which is worse than not answering.
     */
    return NULL;
}

AstraVfsClient *
supervisor_vfs_assign_client(const AstraAssign *assign, void *context)
{
    (void)context;
    return supervisor_vfs_client_for(assign);
}
