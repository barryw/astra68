#include <astra/vfs_host_backend.h>

#include <assert.h>
#include <string.h>

typedef struct FakeHost {
    AstraHostCommand command;
    uint16_t expected_operation;
    uint32_t calls;
} FakeHost;

uint32_t astra_vfs_host_transport_begin(void *context,
                                        uint32_t data_capacity,
                                        AstraVfsHostRequest *request)
{
    FakeHost *host = context;

    (void)data_capacity;
    memset(&host->command, 0, sizeof(host->command));
    memset(request, 0, sizeof(*request));
    request->command = &host->command;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_transport_submit(
    void *context, AstraVfsHostRequest *request, const void *input,
    uint32_t input_size, void *output, uint32_t output_capacity)
{
    FakeHost *host = context;
    AstraHostCommand *command = request->command;

    assert(command->size == ASTRA_HOST_COMMAND_SIZE);
    assert(command->version == ASTRA_HOST_COMMAND_VERSION);
    assert(command->service == ASTRA_HOST_SERVICE_FILESYSTEM);
    assert(command->generation == 9u);
    assert(command->operation == host->expected_operation);
    ++host->calls;
    switch (command->operation) {
    case ASTRA_HOST_FS_OPEN:
        assert(strcmp(command->path, "/file") == 0);
        assert((command->flags & ASTRA_VFS_OPEN_READ) != 0u);
        command->handle = 0x1234u;
        command->node_size_lo = 17u;
        command->kind = ASTRA_VFS_KIND_FILE;
        command->mode = 0640u;
        command->nlink = 1u;
        break;
    case ASTRA_HOST_FS_READ:
        assert(command->handle == 0x1234u && command->offset_lo == 4u);
        assert(input == NULL && input_size == 0u && output_capacity == 8u);
        memcpy(output, "abc", 3u);
        command->result_length = 3u;
        break;
    case ASTRA_HOST_FS_WRITE:
        assert(command->handle == 0x1234u && input_size == 3u);
        assert(memcmp(input, "xyz", 3u) == 0);
        assert((command->flags & ASTRA_HOST_FS_WRITE_APPEND) != 0u);
        command->result_length = 3u;
        command->value_lo = 20u;
        break;
    case ASTRA_HOST_FS_TRUNCATE:
        assert(command->handle == 0x1234u && command->value_lo == 12u);
        break;
    case ASTRA_HOST_FS_STAT:
        assert(strcmp(command->path, "/file") == 0);
        command->node_size_lo = 12u;
        command->kind = ASTRA_VFS_KIND_FILE;
        break;
    case ASTRA_HOST_FS_READDIR:
        assert(command->handle == 0x1234u &&
               strcmp(command->path, "/") == 0 && command->offset_lo == 5u);
        assert(output_capacity == ASTRA_VFS_NAME_MAX);
        memcpy(output, "entry", 5u);
        command->result_length = 5u;
        command->value_lo = 6u;
        command->kind = ASTRA_VFS_KIND_FILE;
        break;
    case ASTRA_HOST_FS_MKDIR:
        assert(strcmp(command->path, "/dir") == 0 &&
               command->value_lo == 0750u);
        break;
    case ASTRA_HOST_FS_UNLINK:
        assert(strcmp(command->path, "/file") == 0);
        break;
    case ASTRA_HOST_FS_RENAME:
        assert(strcmp(command->path, "/old") == 0 &&
               strcmp(command->path2, "/new") == 0);
        break;
    case ASTRA_HOST_FS_CHMOD:
        assert(strcmp(command->path, "/new") == 0 &&
               command->value_lo == 0600u);
        break;
    case ASTRA_HOST_FS_READLINK:
        assert(strcmp(command->path, "/link") == 0 && output_capacity == 16u);
        memcpy(output, "/new", 4u);
        command->result_length = 4u;
        break;
    case ASTRA_HOST_FS_SYMLINK:
        assert(strcmp(command->path, "/new") == 0 &&
               strcmp(command->path2, "/link") == 0);
        break;
    case ASTRA_HOST_FS_SYNC:
    case ASTRA_HOST_FS_CLOSE:
        assert(command->handle == 0x1234u);
        break;
    default:
        assert(0);
    }
    return ASTRA_VFS_OK;
}

int main(void)
{
    AstraVfsHostBackend backend;
    FakeHost host = {0};
    const AstraVfsBackendOps *ops;
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uint8_t bytes[16] = {0};
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t moved;
    uint64_t position;
    uint64_t next;

    assert(!astra_vfs_host_init(NULL,
                               (AstraVfsHostTransport *)(void *)&host, 9u));
    assert(!astra_vfs_host_init(&backend, NULL, 9u));
    assert(astra_vfs_host_init(
        &backend, (AstraVfsHostTransport *)(void *)&host, 9u));
    ops = astra_vfs_host_ops();

#define EXPECT(operation) host.expected_operation = (operation)
    EXPECT(ASTRA_HOST_FS_OPEN);
    assert(ops->open(&backend, "/file", ASTRA_VFS_OPEN_READ,
                     ASTRA_VFS_MODE_DEFAULT, &node, &info) == ASTRA_VFS_OK);
    assert(node == 0x1234u && info.size == 17u && info.mode == 0640u);
    EXPECT(ASTRA_HOST_FS_READ);
    assert(ops->read(&backend, node, 4u, bytes, 8u, &moved) == ASTRA_VFS_OK);
    assert(moved == 3u && memcmp(bytes, "abc", 3u) == 0);
    EXPECT(ASTRA_HOST_FS_WRITE);
    assert(ops->write(&backend, node, 17u, ASTRA_VFS_OPEN_APPEND,
                      "xyz", 3u, &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 3u && position == 20u);
    EXPECT(ASTRA_HOST_FS_SYNC);
    assert(ops->sync(&backend, node) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_TRUNCATE);
    assert(ops->truncate(&backend, node, 12u) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_STAT);
    assert(ops->stat(&backend, "/file", &info) == ASTRA_VFS_OK);
    assert(info.size == 12u);
    EXPECT(ASTRA_HOST_FS_READDIR);
    assert(ops->readdir(&backend, node, "/", 5u, name, sizeof(name), &info,
                        &next) == ASTRA_VFS_OK);
    assert(strcmp(name, "entry") == 0 && next == 6u);
    EXPECT(ASTRA_HOST_FS_MKDIR);
    assert(ops->mkdir(&backend, "/dir", 0750u) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_UNLINK);
    assert(ops->unlink(&backend, "/file") == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_RENAME);
    assert(ops->rename(&backend, "/old", "/new") == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_CHMOD);
    assert(ops->chmod(&backend, "/new", 0600u) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_READLINK);
    assert(ops->readlink(&backend, "/link", bytes, sizeof(bytes), &moved) ==
           ASTRA_VFS_OK);
    assert(moved == 4u && memcmp(bytes, "/new", 4u) == 0);
    EXPECT(ASTRA_HOST_FS_SYMLINK);
    assert(ops->symlink(&backend, "/new", "/link") == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_CLOSE);
    assert(ops->close(&backend, node) == ASTRA_VFS_OK);
#undef EXPECT
    assert(host.calls == 14u);
    return 0;
}
