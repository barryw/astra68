#include <assert.h>

#include <proc_tree.h>
#include <vfs_host.h>
#include <astra/proc.h>

static AstraProcSnapshot snapshot_storage[40];
static size_t requested_bytes;
static int fail_allocation;
static SupervisorVfsClientEntry vfs_client_storage[64];

static void *
test_reallocate(void *pointer, size_t size)
{
    (void)pointer;
    requested_bytes = size;
    return fail_allocation ? NULL : snapshot_storage;
}

static void *
test_vfs_reallocate(void *pointer, size_t size)
{
    (void)pointer;
    return fail_allocation || size > sizeof(vfs_client_storage) ? NULL :
                                                                  vfs_client_storage;
}

int
main(void)
{
    SupervisorProcSnapshotStore store = {0};
    SupervisorVfsClientTable clients = {0};

    assert(supervisor_proc_path_is_root(NULL));
    assert(supervisor_proc_path_is_root(""));
    assert(supervisor_proc_path_is_root("/"));
    assert(!supervisor_proc_path_is_root("//"));
    assert(!supervisor_proc_path_is_root("/1"));
    assert(supervisor_proc_path_is_snapshot("snapshot"));
    assert(supervisor_proc_path_is_snapshot("/snapshot"));
    assert(!supervisor_proc_path_is_snapshot(NULL));
    assert(!supervisor_proc_path_is_snapshot("snapshot/"));
    assert(!supervisor_proc_path_is_snapshot("//snapshot"));
    assert(supervisor_proc_path_is_libraries("libraries"));
    assert(supervisor_proc_path_is_libraries("/libraries"));
    assert(!supervisor_proc_path_is_libraries("libraries/"));
    assert(supervisor_proc_path_is_library_memory("libraries/memory"));
    assert(supervisor_proc_path_is_library_memory("/libraries/memory"));
    assert(!supervisor_proc_path_is_library_memory("libraries/disk"));
    assert(supervisor_proc_path_is_library_disk("libraries/disk"));
    assert(supervisor_proc_path_is_library_disk("/libraries/disk"));
    assert(!supervisor_proc_path_is_library_disk("libraries/memory"));
    assert(sizeof(AstraProcSnapshot) == 112u);
    assert(sizeof(AstraProcLibrarySnapshot) == 76u);
    assert(ASTRA_PROC_NAME_MAX == 32u);
    assert(supervisor_proc_snapshot_reserve(&store, 40u,
                                            test_reallocate));
    assert(store.records == snapshot_storage);
    assert(store.capacity == 40u);
    fail_allocation = 0;
    assert(supervisor_vfs_client_table_reserve(
        &clients, 40u, test_vfs_reallocate));
    assert(clients.entries == vfs_client_storage && clients.capacity == 64u);
    assert(requested_bytes == sizeof(snapshot_storage));
    fail_allocation = 1;
    assert(!supervisor_proc_snapshot_reserve(&store, 41u,
                                             test_reallocate));
    assert(store.records == snapshot_storage);
    assert(store.capacity == 40u);
    assert(!supervisor_vfs_client_table_reserve(
        &clients, 65u, test_vfs_reallocate));
    assert(clients.entries == vfs_client_storage && clients.capacity == 64u);
    return 0;
}
