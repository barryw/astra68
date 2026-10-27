/* Exercise the supervisor's real acquire/commit/rollback path. */
#include "../src/loader.c"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static AstraAssignTable assigns;
static AstraAssign bound_assign;
static uint32_t mount_state, connect_result, bind_result, register_result;
static AstraResult root_result;
static int fixed_present, bound, opens, snapshots_closed, roots, connects;
static int registers, unbinds, unregisters, disconnects, closes;

void *astra_runtime_callocate(size_t count, size_t size)
{
    return calloc(count, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

AstraAssignTable *supervisor_assigns(void)
{
    return &assigns;
}

const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name)
{
    assert(table == &assigns);
    if (fixed_present && strcmp(name, "apps") == 0) return &bound_assign;
    return bound && strcmp(name, "usb2") == 0 ? &bound_assign : NULL;
}

uint32_t astra_assign_bind(AstraAssignTable *table, const char *name,
                           uint32_t handle, uint32_t rights, const char *root)
{
    assert(table == &assigns && strcmp(name, "usb2") == 0 &&
           handle == 201u && rights == ASTRA_RIGHT_READ &&
           strcmp(root, "") == 0 && !bound);
    if (bind_result == ASTRA_VFS_OK) bound = 1;
    return bind_result;
}

uint32_t astra_assign_unbind(AstraAssignTable *table, const char *name)
{
    assert(table == &assigns && strcmp(name, "usb2") == 0 && bound);
    bound = 0;
    ++unbinds;
    return ASTRA_VFS_OK;
}

AstraResult astra_volume_snapshot_open(AstraHandle service,
                                       AstraVolumeSnapshot *snapshot)
{
    assert(service == 101u && snapshot->records == NULL);
    ++opens;
    snapshot->records = calloc(1u, sizeof(*snapshot->records));
    assert(snapshot->records != NULL);
    snapshot->epoch = 7u;
    snapshot->count = 1u;
    snapshot->records[0].id = 11u;
    snapshot->records[0].generation = 3u;
    snapshot->records[0].kind = ASTRA_VOLUME_MOUNT;
    snapshot->records[0].state = mount_state;
    snapshot->records[0].origin = ASTRA_VOLUME_ORIGIN_DEVICE;
    strcpy(snapshot->records[0].name, "/usb2");
    return ASTRA_OK;
}

void astra_volume_snapshot_close(AstraVolumeSnapshot *snapshot)
{
    ++snapshots_closed;
    free(snapshot->records);
    *snapshot = (AstraVolumeSnapshot)ASTRA_VOLUME_SNAPSHOT_INIT;
}

AstraResult astra_volume_root(AstraHandle broker,
                              const AstraVolumeReference *reference,
                              uint32_t rights, AstraHandle *root)
{
    assert(broker == 102u && reference->epoch == 7u &&
           reference->id == 11u && reference->generation == 3u &&
           rights == ASTRA_RIGHT_READ && *root == 0u);
    ++roots;
    if (root_result == ASTRA_OK) *root = 201u;
    return root_result;
}

uint32_t astra_vfs_host_port_connect(AstraVfsClient *client, uint32_t root)
{
    assert(client != NULL && root == 201u);
    ++connects;
    return connect_result;
}

uint32_t supervisor_vfs_register(AstraVfsClient *client, uint32_t root)
{
    assert(client != NULL && root == 201u);
    ++registers;
    return register_result;
}

void supervisor_vfs_unregister(AstraVfsClient *client)
{
    assert(client != NULL);
    ++unregisters;
}

uint32_t astra_vfs_disconnect(AstraVfsClient *client)
{
    assert(client != NULL);
    ++disconnects;
    return ASTRA_VFS_OK;
}

uint32_t astra_close(uint32_t handle)
{
    assert(handle == 201u);
    ++closes;
    return ASTRA_SYSCALL_OK;
}

static void reset(void)
{
    service_count = 2u;
    strcpy(service_names[0], ASTRA_CAPABILITY_VOLUMES);
    strcpy(service_names[1], ASTRA_CAPABILITY_VOLUME_DELEGATE);
    service_handles[0] = 101u;
    service_handles[1] = 102u;
    mount_state = ASTRA_VOLUME_MOUNTED;
    connect_result = bind_result = ASTRA_VFS_OK;
    register_result = 201u;
    root_result = ASTRA_OK;
    fixed_present = bound = opens = snapshots_closed = roots = connects = 0;
    registers = unbinds = unregisters = disconnects = closes = 0;
}

int main(void)
{
    SupervisorLaunchVolume volume = {0};
    SupervisorProcessRecord process = {0};

    reset();
    fixed_present = 1;
    assert(launch_volume_open(&volume, "/apps/Terminal.app") == ASTRA_STATUS_OK);
    launch_volume_close(&volume);
    assert(opens == 0 && roots == 0 && closes == 0);

    reset();
    assert(launch_volume_open(&volume, "/usb2/ChocolateDoom.app") == ASTRA_STATUS_OK);
    assert(bound && opens == 1 && snapshots_closed == 1 && roots == 1 &&
           connects == 1 && registers == 1 && volume.lease.registered);
    launch_volume_commit(&volume, &process);
    launch_volume_close(&volume);
    assert(!bound && unbinds == 1 && process.volume.root == 201u &&
           closes == 0 && unregisters == 0);
    volume_lease_close(&process.volume);
    assert(closes == 1 && unregisters == 1 && disconnects == 1 &&
           process.volume.root == 0u);

    reset();
    bind_result = ASTRA_VFS_ERR_LIMIT;
    assert(launch_volume_open(&volume, "/usb2/Failed.app") == ASTRA_STATUS_LIMIT);
    launch_volume_close(&volume);
    assert(!bound && unbinds == 0 && unregisters == 1 &&
           disconnects == 1 && closes == 1);

    reset();
    connect_result = ASTRA_VFS_ERR_IO;
    assert(launch_volume_open(&volume, "/usb2/Failed.app") == ASTRA_STATUS_IO);
    launch_volume_close(&volume);
    assert(registers == 0 && unregisters == 0 && disconnects == 1 &&
           closes == 1);

    reset();
    register_result = 0u;
    assert(launch_volume_open(&volume, "/usb2/Failed.app") == ASTRA_STATUS_LIMIT);
    launch_volume_close(&volume);
    assert(registers == 1 && unregisters == 0 && disconnects == 1 &&
           closes == 1);

    reset();
    root_result = ASTRA_ERROR_INVALID_HANDLE;
    assert(launch_volume_open(&volume, "/usb2/Stale.app") != ASTRA_STATUS_OK);
    launch_volume_close(&volume);
    assert(roots == 1 && connects == 0 && closes == 0);

    reset();
    mount_state = ASTRA_VOLUME_QUIESCING;
    assert(launch_volume_open(&volume, "/usb2/Stale.app") == ASTRA_STATUS_NOT_FOUND);
    launch_volume_close(&volume);
    assert(roots == 0 && closes == 0 && snapshots_closed == 1);

    puts("supervisor launch volume tests passed");
    return 0;
}
