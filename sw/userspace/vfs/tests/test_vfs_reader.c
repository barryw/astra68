#include <assert.h>
#include <stdint.h>
#include <string.h>

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
static uint8_t read_bytes[4096];

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
    assert(strcmp(path, "COMMANDS:large") == 0);
    assert(rights == ASTRA_RIGHT_READ);
    assert(flags == ASTRA_VFS_OPEN_READ);
    assert(wire != NULL && capacity >= ASTRA_VFS_PATH_MAX);
    if (open_status != ASTRA_VFS_OK)
        return open_status;
    *file = 9u;
    *size = open_size;
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
    read_offset = (uint32_t)offset;
    read_length = length;
    if (read_status == ASTRA_VFS_OK) {
        *bytes = read_bytes;
        *moved = read_moved;
    }
    return read_status;
}

uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    assert(client == &mock_client && file == 9u);
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
    return 0;
}
