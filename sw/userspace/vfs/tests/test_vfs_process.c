#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <astra/library.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>
#include <astra/vfs_provider_index.h>

static uint32_t closes;
static uint32_t connects;
static uint32_t disconnects;
static uint32_t abandons;
static uint32_t fail_connect;
static uint32_t seed_distinct;
static uint32_t direct_connect;
static uint32_t direct_resumes;
static uint32_t fail_direct_resume;
static uint32_t direct_forks;
static uint32_t port_fork_rebinds;
static uint32_t next_semaphore = 100u;
static uint32_t seeds;
static int fail_allocate;
static int whole_file_enabled;
static uint32_t whole_file_moved;
static uint64_t whole_file_node_size;
static const uint8_t *whole_file_bytes;
static const uint8_t default_file_content[] = "hello";
static const uint8_t *mock_file_content = default_file_content;
static uint32_t mock_file_length = 5u;
static AstraAssign mock_assigns[ASTRA_STARTUP_CAPABILITY_MAX + 16u];

void *astra_runtime_allocate(size_t size)
{
    return fail_allocate ? NULL : malloc(size);
}
void *astra_runtime_reallocate(void *pointer, size_t size)
{
    return fail_allocate ? NULL : realloc(pointer, size);
}
void astra_runtime_deallocate(void *pointer) { free(pointer); }

uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation;
    return status;
}

int astra_startup_validate(const AstraStartupInfo *startup)
{
    return startup != NULL;
}

uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
                                   uint32_t rights, uint32_t *handle)
{
    (void)initial;
    (void)maximum;
    (void)rights;
    if (handle == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    *handle = next_semaphore++;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_assign_seed(AstraAssignTable *table,
                           const AstraStartupCapability *capabilities,
                           uint32_t count)
{
    (void)capabilities;
    (void)count;
    ++seeds;
    memset(table, 0, sizeof(*table));
    memset(mock_assigns, 0, sizeof(mock_assigns));
    table->entries = mock_assigns;
    table->capacity = (uint32_t)(sizeof(mock_assigns) / sizeof(mock_assigns[0]));
    if (seed_distinct != 0u) {
        table->count = ASTRA_STARTUP_CAPABILITY_MAX;
        for (uint32_t index = 0u; index < table->count; ++index)
            table->entries[index].handle = index + 1u;
        return ASTRA_VFS_OK;
    }
    table->count = 3u;
    (void)strcpy(table->entries[0].name, "CWD");
    table->entries[0].handle = 11u;
    table->entries[0].rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
    (void)strcpy(table->entries[1].name, "WORK");
    table->entries[1].handle = 11u;
    table->entries[1].rights = ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE;
    (void)strcpy(table->entries[2].name, "LIBS");
    table->entries[2].handle = 22u;
    table->entries[2].rights = ASTRA_RIGHT_READ;
    return ASTRA_VFS_OK;
}

const AstraAssign *astra_assign_lookup(const AstraAssignTable *table,
                                       const char *name)
{
    for (uint32_t index = 0u; index < table->count; ++index)
        if (strcmp(table->entries[index].name, name) == 0)
            return &table->entries[index];
    return NULL;
}

/*
 * astra_process_read_file takes the one-round-trip path when the service can
 * offer it. This suite exercises the resolver, not the transport, so both
 * refuse and the caller falls back to the library path it already covers.
 */
uint32_t astra_assign_resolve(const AstraAssignTable *table,
                              const char *path,
                              uint32_t rights, uint32_t member, char *wire,
                              uint32_t capacity, const AstraAssign **assign)
{
    (void)path;
    (void)rights;
    (void)member;
    if (whole_file_enabled != 0 && table != NULL && table->count != 0u &&
        wire != NULL && capacity >= 2u && assign != NULL) {
        wire[0] = '/';
        wire[1] = '\0';
        *assign = &table->entries[0];
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                                  const uint8_t **bytes, uint32_t *moved,
                                  uint64_t *node_size)
{
    (void)client;
    (void)path;
    if (whole_file_enabled != 0 && bytes != NULL && moved != NULL &&
        node_size != NULL) {
        *bytes = whole_file_bytes;
        *moved = whole_file_moved;
        *node_size = whole_file_node_size;
        return ASTRA_VFS_OK;
    }
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_read_bulk(AstraVfsClient *client, AstraVfsFile file,
                                  uint64_t offset, void *buffer,
                                  uint32_t length, uint32_t *moved)
{
    (void)client; (void)file; (void)offset; (void)buffer; (void)length;
    (void)moved;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_write_bulk_position(
    AstraVfsClient *client, AstraVfsFile file, uint64_t offset,
    uint32_t flags, const void *buffer, uint32_t length, uint32_t *moved,
    uint64_t *position)
{
    (void)client; (void)file; (void)offset; (void)flags; (void)buffer;
    (void)length; (void)moved; (void)position;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_transport(void *context, uint32_t operation,
                                  const AstraVfsRequest *request,
                                  AstraVfsReply *reply)
{
    (void)context; (void)operation; (void)request; (void)reply;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

AstraVfsCallState *astra_vfs_port_call_acquire(AstraVfsClient *client)
{
    return &client->call;
}

const uint8_t *astra_vfs_port_call_area(const AstraVfsClient *client,
                                        uint32_t *capacity)
{
    if (capacity != NULL)
        *capacity = client->port_lanes[0].area_size;
    return client->port_lanes[0].area_address;
}

uint32_t astra_vfs_port_exec_lane_export(AstraVfsClient *client,
                                         AstraVfsPortExecLane *state)
{
    if (client == NULL || state == NULL)
        return ASTRA_VFS_ERR_INVALID;
    memset(state, 0, sizeof(*state));
    state->owner_thread = 1u;
    state->session = client->session;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_port_exec_lane_import(AstraVfsClient *client,
                                         const AstraVfsPortExecLane *state)
{
    if (client == NULL || state == NULL || state->owner_thread == 0u)
        return ASTRA_VFS_ERR_INVALID;
    client->port_lanes[0].owner_thread = state->owner_thread;
    client->port_lanes[0].session = state->session;
    return ASTRA_VFS_OK;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t flags, void **address,
                           uint32_t *span)
{
    (void)handle; (void)flags; (void)address; (void)span;
    return ASTRA_SYSCALL_INVALID_HANDLE;
}

uint32_t astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    ++connects;
    if (connects == fail_connect)
        return ASTRA_VFS_ERR_IO;
    client->session = service;
    client->port_service = service;
    client->version = ASTRA_VFS_VERSION;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_port_connect_lazy(AstraVfsClient *client,
                                          uint32_t service)
{
    uint32_t status = astra_vfs_port_connect(client, service);

    if (status == ASTRA_VFS_OK && direct_connect != 0u) {
        client->port_direct_address = (void *)(uintptr_t)1u;
        client->port_direct_area = 0x41u;
        client->port_direct_device = 0x42u;
        client->port_direct_session = 0x43u;
    }
    return status;
}

uint32_t astra_vfs_host_direct_resume(AstraVfsClient *client, uint32_t area,
                                      uint32_t device, uint32_t session)
{
    assert(client != NULL && area == 0x41u && device == 0x42u &&
           session == 0x43u);
    ++direct_resumes;
    if (fail_direct_resume != 0u)
        return ASTRA_VFS_ERR_IO;
    client->port_direct_address = (void *)(uintptr_t)1u;
    client->port_direct_area = area;
    client->port_direct_device = device;
    client->port_direct_session = session;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_direct_after_fork(AstraVfsClient *client)
{
    assert(client != NULL && client->port_direct_address != NULL);
    ++direct_forks;
    return ASTRA_VFS_OK;
}

void astra_vfs_port_abandon(AstraVfsClient *client)
{
    ++abandons;
    client->session = ASTRA_VFS_SESSION_INVALID;
}

void astra_vfs_port_after_fork_child(AstraVfsClient *client)
{
    assert(client != NULL);
    ++port_fork_rebinds;
}

uint32_t astra_vfs_disconnect(AstraVfsClient *client)
{
    assert(client->session != ASTRA_VFS_SESSION_INVALID);
    ++disconnects;
    client->session = ASTRA_VFS_SESSION_INVALID;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    (void)filesystem;
    assert(strcmp(path, "/app/test") == 0 && flags == ASTRA_VFS_OPEN_READ);
    file->_private_file = 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_info(const AstraFile *file,
                                    AstraFileInfo *info)
{
    assert(file->_private_file == 1u);
    info->byte_size = mock_file_length;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read(AstraFile *file, void *bytes,
                               uint32_t capacity, uint32_t *moved)
{
    uint32_t offset = (uint32_t)file->_private_offset;
    uint32_t count = capacity < 2u ? capacity : 2u;

    if (count > mock_file_length - offset) count = mock_file_length - offset;
    memcpy(bytes, mock_file_content + offset, count);
    file->_private_offset += count;
    *moved = count;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    ++closes;
    file->_private_file = ASTRA_VFS_FILE_INVALID;
    return ASTRA_VFS_OK;
}

int main(void)
{
    static const uint8_t indexed[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'F', 'i', 'l', 'e'
    };
    static const uint8_t legacy_absolute[] = {
        'A', 'P', 'R', 'V', 0, 1, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'L', 'I', 'B', 'S', ':', 'F', 'i', 'l', 'e'
    };
    static const uint8_t absolute[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'L', 'I', 'B', 'S', ':', 'F', 'i', 'l', 'e'
    };
    static const uint8_t traversal[] = {
        'A', 'P', 'R', 'V', 0, 2, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        '.', '.', '/', 'F', 'i', 'l', 'e'
    };
    char path[128];
    AstraLibraryReference reference;
    uint16_t abi = 0u;

    assert(astra_vfs_provider_identity_parse(
        "loader.library.1", path, sizeof(path), &abi));
    assert(strcmp(path, "loader.library") == 0 && abi == 1u);
    assert(astra_vfs_provider_identity_parse(
        "runtime.library.65535", path, sizeof(path), &abi));
    assert(strcmp(path, "runtime.library") == 0 && abi == 65535u);
    assert(!astra_vfs_provider_identity_parse(
        "loader.library", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader.library.01", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader.library.65536", path, sizeof(path), &abi));
    assert(!astra_vfs_provider_identity_parse(
        "loader/library.1", path, sizeof(path), &abi));

    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/libs/Filesystem.kit/library", 27u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(path[0] == '\0');
    assert(reference.size == 0u);
    assert(astra_vfs_provider_index_parse(
        indexed, sizeof(indexed), "LIBS", "filesystem.library", 1u, path,
        sizeof(path), &reference));
    assert(strcmp(path, "/libs/File") == 0);
    assert(reference.size == ASTRA_LIBRARY_REFERENCE_SIZE);
    assert(strcmp(reference.name, "filesystem.library.1") == 0);
    assert(reference.major == 1u && reference.minor == 2u &&
           reference.patch == 3u && reference.abi_major == 1u &&
           reference.abi_minor == 4u &&
           reference.build_id == 0x12345678u);
    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/sys/Filesystem.kit/library", 26u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        (const uint8_t *)"/libs/Filesystem kit/library", 27u,
        "LIBS", "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        legacy_absolute, sizeof(legacy_absolute), "LIBS",
        "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        absolute, sizeof(absolute), "LIBS", "filesystem.library", 1u,
        path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        traversal, sizeof(traversal), "LIBS", "filesystem.library", 1u,
        path, sizeof(path), &reference));
    assert(!astra_vfs_provider_index_parse(
        indexed, sizeof(indexed), "BAD:ASSIGN", "filesystem.library", 1u,
        path, sizeof(path), &reference));

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };

        /*
         * Seeding connects nothing. A connect is two cross-process round trips
         * and a program that never names a mount must never pay for it -- so
         * this counter staying at zero here is the whole of that claim.
         */
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        {
            char path[64];
            uint32_t seeded = seeds;

            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "/cwd/file") == 0);
            assert(astra_process_vfs_set_current_directory("CWD", "proto") ==
                   ASTRA_VFS_OK);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "/cwd/proto/file") == 0);
            assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
            assert(seeds == seeded);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "/cwd/proto/file") == 0);
            assert(astra_process_vfs_after_fork_child(&startup) ==
                   ASTRA_VFS_OK);
            assert(seeds == seeded);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "/cwd/proto/file") == 0);
            assert(astra_process_vfs_set_current_directory("WORK", "other") ==
                   ASTRA_VFS_OK);
            assert(astra_process_path("file", path, sizeof(path)) ==
                       ASTRA_VFS_OK &&
                   strcmp(path, "/work/other/file") == 0);
            assert(astra_process_vfs_set_current_directory("MISSING", "") ==
                   ASTRA_VFS_ERR_NOT_FOUND);
        }
        assert(connects == 0u);
        /* First use is what connects, and only once however often it is asked. */
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 1u);
        {
            AstraFile original = ASTRA_FILE_INIT;
            AstraFile restored = ASTRA_FILE_INIT;
            AstraProcessFileState state;

            original._private_client = astra_process_vfs_client();
            original._private_read_at = astra_vfs_port_read_bulk;
            original._private_file = 0x1234u;
            original._private_flags = ASTRA_VFS_OPEN_READ |
                                      ASTRA_VFS_OPEN_WRITE;
            original._private_offset = UINT64_C(0x123456789);
            original._private_size = UINT64_C(0x223456789);
            original._private_kind = ASTRA_VFS_KIND_FILE;
            original._private_member = 2u;
            assert(astra_process_file_export(&original, &state) ==
                   ASTRA_VFS_OK);
            assert(astra_process_file_import(&state, &restored) ==
                   ASTRA_VFS_OK);
            assert(restored._private_client == original._private_client);
            assert(restored._private_read_at == astra_vfs_port_read_bulk);
            assert(restored._private_write_at ==
                   astra_vfs_port_write_bulk_position);
            assert(restored._private_file == original._private_file);
            assert(restored._private_flags == original._private_flags);
            assert(restored._private_offset == original._private_offset);
            assert(restored._private_size == original._private_size);
            assert(restored._private_kind == original._private_kind);
            assert(restored._private_member == original._private_member);
            state.service = 0xdeadbeefu;
            assert(astra_process_file_import(&state, &restored) ==
                   ASTRA_VFS_ERR_BAD_HANDLE);
        }
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 1u);
        astra_process_vfs_set_activity(0x12345678u);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[0])->activity ==
               0x12345678u);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[2])->activity ==
               0x12345678u);
        astra_process_vfs_close();
        assert(disconnects == 2u);

        /* A fork child drops its cloned reply channel without closing the
         * parent's service session, then reconnects lazily under its owner. */
        connects = 0u;
        disconnects = 0u;
        abandons = 0u;
        port_fork_rebinds = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() != NULL && connects == 1u);
        assert(astra_process_vfs_after_fork_child(&startup) == ASTRA_VFS_OK);
        assert(abandons == 1u && disconnects == 0u &&
               port_fork_rebinds == 1u);
        assert(astra_process_vfs_client() != NULL && connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 1u);

        /* A direct child replaces only its process-local channel. It does not
         * throw away the namespace and pay another service HELLO. */
        connects = 0u;
        disconnects = 0u;
        direct_connect = 1u;
        direct_forks = 0u;
        port_fork_rebinds = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() != NULL && connects == 1u);
        assert(astra_process_vfs_after_fork_child(&startup) == ASTRA_VFS_OK);
        assert(direct_forks == 1u && connects == 1u &&
               port_fork_rebinds == 1u);
        astra_process_vfs_close();
        assert(disconnects == 1u);
        direct_connect = 0u;

        /* Exec keeps the local service's session and open-file table. The
         * replacement image must resume that accelerator instead of silently
         * routing every operation back through the service port. */
        {
            void *state;
            uint32_t size;
            uint32_t used = 0u;
            AstraVfsClient *client;

            connects = 0u;
            disconnects = 0u;
            direct_connect = 1u;
            direct_resumes = 0u;
            assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
            client = astra_process_vfs_client();
            assert(client != NULL && client->port_direct_address != NULL);
            size = astra_process_vfs_state_size();
            state = malloc(size);
            assert(state != NULL);
            assert(astra_process_vfs_export(state, size, &used) ==
                       ASTRA_VFS_OK &&
                   used == size);
            direct_connect = 0u;
            fail_direct_resume = 1u;
            assert(astra_process_vfs_import(&startup, state, size) ==
                   ASTRA_VFS_ERR_IO);
            fail_direct_resume = 0u;
            assert(astra_process_vfs_import(&startup, state, size) ==
                   ASTRA_VFS_OK);
            free(state);
            client = astra_process_vfs_client();
            assert(client != NULL && direct_resumes == 2u &&
                   client->port_direct_address != NULL);
            astra_process_vfs_close();
        }

        /* A mount nobody named is a mount nobody disconnects either. */
        connects = 0u;
        disconnects = 0u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        astra_process_vfs_close();
        assert(connects == 0u && disconnects == 0u);

        /*
         * A connect that refuses answers NULL and is retried by the next
         * caller rather than remembered as fatal: seeding no longer has an
         * opinion about whether a mount is reachable, because seeding no
         * longer talks to one.
         */
        connects = 0u;
        disconnects = 0u;
        fail_connect = 1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() == NULL);
        assert(connects == 1u);
        fail_connect = 0u;
        assert(astra_process_vfs_client() != NULL);
        assert(connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 1u);

        /* Allocation failure refuses only the new cache entry. */
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        for (uint32_t index = 3u; index < 9u; ++index) {
            AstraAssignTable *table = astra_process_vfs_assigns();

            table->entries[index].handle = index + 100u;
            table->count = index + 1u;
            assert(astra_process_vfs_client_for(&table->entries[index]) !=
                   NULL);
        }
        {
            AstraAssignTable *table = astra_process_vfs_assigns();

            table->entries[9].handle = 109u;
            table->count = 10u;
            fail_allocate = 1;
            assert(astra_process_vfs_client_for(&table->entries[9]) == NULL);
            fail_allocate = 0;
            assert(astra_process_vfs_client_for(&table->entries[0]) != NULL);
            assert(astra_process_vfs_client_for(&table->entries[9]) != NULL);
        }
        astra_process_vfs_close();

        /* Every startup mount fits, and a later mount grows the cache. */
        seed_distinct = 1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[
                       ASTRA_STARTUP_CAPABILITY_MAX - 1u]) != NULL);
        astra_process_vfs_assigns()->entries[
            ASTRA_STARTUP_CAPABILITY_MAX].handle = 1000u;
        ++astra_process_vfs_assigns()->count;
        assert(astra_process_vfs_client_for(
                   &astra_process_vfs_assigns()->entries[
                       ASTRA_STARTUP_CAPABILITY_MAX]) != NULL);
        astra_process_vfs_close();
        seed_distinct = 0u;
    }

    {
        AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
        char bytes[5];
        uint32_t length = 0u;

        filesystem.filesystem._private_assigns = (const void *)1u;

        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == sizeof(bytes) && memcmp(bytes, "hello", 5u) == 0);
        assert(closes == 1u);
        assert(astra_process_read_file(&filesystem, "/app/test", bytes, 4u,
                                       &length) == ASTRA_VFS_ERR_LIMIT);
        assert(closes == 2u);
        {
            void *allocated = NULL;
            static uint8_t large[65u * 128u];

            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_OK);
            assert(length == 5u && memcmp(allocated, "hello\0", 6u) == 0);
            astra_runtime_deallocate(allocated);
            memset(large, 0xa5, sizeof(large));
            mock_file_content = large;
            mock_file_length = sizeof(large);
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_OK);
            assert(length == sizeof(large));
            assert(memcmp(allocated, large, sizeof(large)) == 0);
            assert(((uint8_t *)allocated)[length] == '\0');
            astra_runtime_deallocate(allocated);
            fail_allocate = 1;
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", &allocated, &length) ==
                   ASTRA_VFS_ERR_LIMIT);
            fail_allocate = 0;
            assert(allocated == NULL && length == 0u);
            mock_file_content = default_file_content;
            mock_file_length = 5u;
            assert(astra_process_read_file_alloc(
                       &filesystem, "/app/test", NULL, &length) ==
                   ASTRA_VFS_ERR_INVALID);
        }
    }

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };
        AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
        char bytes[5];
        uint32_t length = 0u;

        filesystem.filesystem._private_assigns = (const void *)1u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        whole_file_enabled = 1;
        whole_file_bytes = (const uint8_t *)"fast";
        whole_file_moved = 4u;
        whole_file_node_size = 4u;
        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == 4u && memcmp(bytes, "fast", 4u) == 0);

        whole_file_bytes = (const uint8_t *)"evil";
        whole_file_node_size = UINT64_C(0x100000004);
        assert(astra_process_read_file(&filesystem, "/app/test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == 5u && memcmp(bytes, "hello", 5u) == 0);
        whole_file_enabled = 0;
        astra_process_vfs_close();
    }

    puts("astra process library resolver: PASS");
    return 0;
}
