#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/address_space.h>
#include <astra/vfs_reader.h>

static AstraVfsClient mock_client;
static uint32_t close_count;
static uint32_t open_status;
static uint64_t open_size;
static uint16_t open_kind;
static uint32_t read_status;
static uint32_t read_moved;
static uint32_t read_offset;
static uint32_t read_length;
static uint32_t read_count;
static uint8_t read_bytes[4096];
static uint8_t provider_record[] = {
    'A', 'P', 'R', 'V', 0, 2, 0, 24,
    0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
    0x12, 0x34, 0x56, 0x78,
    'R', 'u', 'n', 't', 'i', 'm', 'e', '.', 'k', 'i', 't', '/',
    'r', 'u', 'n', 't', 'i', 'm', 'e', '.', 'l', 'i', 'b', 'r',
    'a', 'r', 'y'
};
static uint32_t library_mode;
static uint32_t open_count;

uint32_t astra_vfs_assign_open(
    const AstraAssignTable *table, const char *path, uint32_t rights,
    uint32_t flags, AstraVfsAssignClientFn client_for, void *context,
    char *wire, uint32_t capacity, AstraVfsFile *file, uint64_t *size,
    uint16_t *kind, AstraVfsClient **client, uint32_t *member)
{
    (void)table;
    (void)client_for;
    (void)context;
    (void)member;
    assert(rights == ASTRA_RIGHT_READ);
    assert(flags == ASTRA_VFS_OPEN_READ);
    assert(wire != NULL && capacity >= ASTRA_VFS_PATH_MAX);
    if (open_status != ASTRA_VFS_OK)
        return open_status;
    if (library_mode != 0u) {
        if (open_count == 0u) {
            assert(strcmp(path,
                          "LIBS:.providers/runtime.library.abi-1") == 0);
            *file = 9u;
            *size = sizeof(provider_record);
        } else {
            assert(open_count == 1u);
            assert(strcmp(path, "LIBS:Runtime.kit/runtime.library") == 0);
            *file = 10u;
            *size = 77u;
        }
        ++open_count;
    } else {
        assert(strcmp(path, "COMMANDS:large") == 0);
        *file = 9u;
        *size = open_size;
    }
    *kind = open_kind;
    *client = &mock_client;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_port_read_borrow(AstraVfsClient *client,
                                    AstraVfsFile file, uint64_t offset,
                                    uint32_t length, const uint8_t **bytes,
                                    uint32_t *moved)
{
    assert(client == &mock_client && file == 9u);
    ++read_count;
    read_offset = (uint32_t)offset;
    read_length = length;
    if (read_status == ASTRA_VFS_OK) {
        if (library_mode != 0u) {
            assert(length == sizeof(provider_record));
            *bytes = provider_record;
            *moved = sizeof(provider_record);
        } else {
            *bytes = read_bytes;
            *moved = read_moved;
        }
    }
    return read_status;
}

uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    assert(client == &mock_client && (file == 9u || file == 10u));
    ++close_count;
    return ASTRA_VFS_OK;
}

static AstraVfsClient *client_for(const AstraAssign *assign, void *context)
{
    (void)assign;
    (void)context;
    return &mock_client;
}

int main(void)
{
    AstraAssignTable table = {0};
    AstraVfsReadSource source = ASTRA_VFS_READ_SOURCE_INIT;
    AstraLibraryReference reference;
    const uint8_t *bytes = NULL;
    uint32_t moved = 0u;

    open_status = ASTRA_VFS_OK;
    open_size = (5u * 1024u * 1024u) + 17u;
    open_kind = ASTRA_VFS_KIND_FILE;
    assert(astra_vfs_read_source_open(
               &source, &table, "COMMANDS:large", client_for, NULL) ==
           ASTRA_VFS_OK);
    assert(source.length == (uint32_t)open_size);

    read_status = ASTRA_VFS_OK;
    read_moved = 17u;
    assert(astra_vfs_read_source_read_at(
               &source, 5u * 1024u * 1024u, 17u, &bytes, &moved) ==
           ASTRA_VFS_OK);
    assert(bytes == read_bytes && moved == 17u);
    assert(read_offset == 5u * 1024u * 1024u && read_length == 17u);

    read_moved = ASTRA_EXECUTABLE_HEADER_SIZE;
    assert(astra_vfs_read_source_read_at(
               &source, 0u, ASTRA_EXECUTABLE_HEADER_SIZE, &bytes, &moved) ==
           ASTRA_VFS_OK);
    assert(bytes == read_bytes && moved == ASTRA_EXECUTABLE_HEADER_SIZE);
    assert(read_offset == 0u &&
           read_length == ASTRA_EXECUTABLE_HEADER_SIZE);

    read_moved = source.length;
    assert(astra_vfs_read_source_read_at(
               &source, 0u, ASTRA_MEMORY_PAGE_SIZE, &bytes, &moved) ==
           ASTRA_VFS_OK);
    assert(bytes == read_bytes && moved == ASTRA_MEMORY_PAGE_SIZE);
    assert(read_offset == 0u && read_length == source.length);

    read_moved = source.length;
    assert(astra_vfs_read_source_read_at(
               &source, 0u, source.length, &bytes, &moved) == ASTRA_VFS_OK);
    assert(bytes == read_bytes && moved == source.length);
    assert(read_offset == 0u && read_length == source.length);

    read_moved = ASTRA_MEMORY_PAGE_SIZE - 1u;
    bytes = read_bytes;
    moved = UINT32_MAX;
    assert(astra_vfs_read_source_read_at(
               &source, 0u, ASTRA_MEMORY_PAGE_SIZE, &bytes, &moved) ==
           ASTRA_VFS_ERR_IO);
    assert(bytes == NULL && moved == 0u && read_count == 5u);

    assert(astra_vfs_read_source_read_at(
               &source, source.length - 1u, 2u, &bytes, &moved) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_read_source_close(&source) == ASTRA_VFS_OK);
    assert(close_count == 1u);
    assert(astra_vfs_read_source_close(&source) == ASTRA_VFS_OK);
    assert(close_count == 1u);

    open_kind = ASTRA_VFS_KIND_DIRECTORY;
    assert(astra_vfs_read_source_open(
               &source, &table, "COMMANDS:large", client_for, NULL) ==
           ASTRA_VFS_ERR_INVALID);
    assert(close_count == 2u);

    open_kind = ASTRA_VFS_KIND_FILE;
    open_size = UINT64_C(0x100000000);
    assert(astra_vfs_read_source_open(
               &source, &table, "COMMANDS:large", client_for, NULL) ==
           ASTRA_VFS_ERR_LIMIT);
    assert(close_count == 3u);

    open_status = ASTRA_VFS_ERR_NOT_FOUND;
    assert(astra_vfs_read_source_open(
               &source, &table, "COMMANDS:large", client_for, NULL) ==
           ASTRA_VFS_ERR_NOT_FOUND);
    assert(source.file == ASTRA_VFS_FILE_INVALID);

    library_mode = 1u;
    open_count = 0u;
    open_status = ASTRA_VFS_OK;
    open_kind = ASTRA_VFS_KIND_FILE;
    read_status = ASTRA_VFS_OK;
    {
        char path[ASTRA_VFS_PATH_MAX];

        assert(astra_vfs_library_resolve(
                   &table, "LIBS", "runtime.library.1", client_for, NULL,
                   path, sizeof(path), &reference) == ASTRA_VFS_OK);
        assert(open_count == 1u);
        assert(strcmp(path, "LIBS:Runtime.kit/runtime.library") == 0);
        assert(reference.size == ASTRA_LIBRARY_REFERENCE_SIZE);
    }
    open_count = 0u;
    assert(astra_vfs_library_source_open(
               &source, &table, "LIBS", "runtime.library.1", client_for,
               NULL, &reference) == ASTRA_VFS_OK);
    assert(open_count == 2u && source.file == 10u && source.length == 77u);
    assert(reference.size == ASTRA_LIBRARY_REFERENCE_SIZE);
    assert(strcmp(reference.name, "runtime.library.1") == 0);
    assert(reference.major == 1u && reference.minor == 2u &&
           reference.patch == 3u && reference.abi_major == 1u &&
           reference.abi_minor == 4u &&
           reference.build_id == UINT32_C(0x12345678));
    assert(astra_vfs_read_source_close(&source) == ASTRA_VFS_OK);

    open_count = 0u;
    provider_record[0] = 'X';
    {
        char path[ASTRA_VFS_PATH_MAX] = "stale";

        assert(astra_vfs_library_resolve(
                   &table, "LIBS", "runtime.library.1", client_for, NULL,
                   path, sizeof(path), &reference) ==
               ASTRA_VFS_ERR_PROTOCOL);
        assert(open_count == 1u && path[0] == '\0' && reference.size == 0u);
    }
    open_count = 0u;
    assert(astra_vfs_library_source_open(
               &source, &table, "LIBS", "runtime.library.1", client_for,
               NULL, &reference) == ASTRA_VFS_ERR_PROTOCOL);
    assert(open_count == 1u && source.file == ASTRA_VFS_FILE_INVALID &&
           reference.size == 0u);
    provider_record[0] = 'A';

    open_count = 0u;
    assert(astra_vfs_library_source_open(
               &source, &table, "LIBS", "runtime.library.01", client_for,
               NULL, &reference) == ASTRA_VFS_ERR_INVALID);
    assert(open_count == 0u && reference.size == 0u);
    return 0;
}
