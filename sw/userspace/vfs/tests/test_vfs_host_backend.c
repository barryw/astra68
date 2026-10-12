#include <astra/vfs_host_backend.h>
#include <astra/endian.h>

#include <assert.h>
#include <string.h>

static int operand(const char *bytes, uint32_t length, const char *expected)
{
    return length == strlen(expected) && memcmp(bytes, expected, length) == 0;
}

typedef struct FakeHost {
    AstraHostCommand command;
    uint16_t expected_operation;
    uint32_t calls;
    uint32_t escape_kind;
    uint32_t device_minor;
    uint64_t birth_seconds;
    uint32_t birth_nsec;
    uint32_t failure;
    uint32_t begin_failure;
} FakeHost;

uint32_t astra_vfs_host_transport_begin(void *context, uint32_t scope,
                                        uint32_t data_capacity,
                                        AstraVfsHostRequest *request)
{
    FakeHost *host = context;

    /* The backend under test was initialised without a scope. */
    assert(scope == 0u);
    (void)data_capacity;
    if (host->begin_failure != 0u)
        return host->begin_failure;
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
    /* Every command names the session its backend was made for, and its
     * path operands travel beside it, not in the record. */
    assert(command->session == (command->operation == ASTRA_HOST_FS_CLAIM ? 0u : 7u));
    assert(command->path_offset == 0u && command->path_length == 0u &&
           command->path2_length == 0u);
    assert(request->path_length == 0u || request->path != NULL);
    assert(request->path2_length == 0u || request->path2 != NULL);
    assert(command->operation == host->expected_operation);
    ++host->calls;
    if (host->failure != 0u) {
        command->status = host->failure;
        return host->failure;
    }
    switch (command->operation) {
    case ASTRA_HOST_FS_CLAIM:
        command->result_value = 17u;
        break;
    case ASTRA_HOST_FS_OPEN:
        if (command->handle == 0x1234u) {
            assert(operand(request->path, request->path_length, "child") &&
                   command->flags == ASTRA_VFS_OPEN_READ);
            command->handle = 0x5678u;
            command->node.size_lo = 23u;
            command->node.kind = ASTRA_VFS_KIND_FILE;
            break;
        }
        assert(operand(request->path, request->path_length, "/file"));
        assert((command->flags & ASTRA_VFS_OPEN_READ) != 0u);
        assert(command->handle == 0u);
        command->handle = 0x1234u;
        command->node.size_lo = 17u;
        command->node.kind = ASTRA_VFS_KIND_FILE;
        command->node.mode = 0640u;
        command->node.nlink = 1u;
        break;
    case ASTRA_HOST_FS_READ:
        assert(command->handle == 0x1234u &&
               command->offset_lo ==
                   (command->flags == ASTRA_HOST_FS_IO_SEQUENTIAL ? 0u : 4u));
        assert(input == NULL && input_size == 0u && output_capacity == 8u);
        memcpy(output, "abc", 3u);
        command->result_length = 3u;
        command->value_lo = 7u;
        break;
    case ASTRA_HOST_FS_WRITE:
        assert(command->handle == 0x1234u && input_size == 3u);
        assert(memcmp(input, "xyz", 3u) == 0);
        assert(command->flags == ASTRA_HOST_FS_WRITE_APPEND ||
               command->flags == ASTRA_HOST_FS_IO_SEQUENTIAL);
        command->result_length = 3u;
        assert(command->value_hi == UINT32_MAX && command->value_lo == UINT32_MAX);
        command->value_hi = 0u;
        command->value_lo = 20u;
        break;
    case ASTRA_HOST_FS_SEEK:
        assert(command->handle == 0x1234u && command->flags == 2u &&
               command->offset_hi == UINT32_MAX &&
               command->offset_lo == UINT32_MAX - 1u);
        command->value_lo = 9u;
        break;
    case ASTRA_HOST_FS_SHARE:
        assert(command->handle == 0x1234u && command->flags == 0u);
        command->value_hi = 0x01234567u;
        command->value_lo = 0x89abcdefu;
        break;
    case ASTRA_HOST_FS_ADOPT:
        assert(command->handle == 0u &&
               command->offset_hi == 0x01234567u &&
               command->offset_lo == 0x89abcdefu &&
               (command->flags & ~ASTRA_HOST_FS_ADOPT_DISCARD) == 0u);
        command->handle = 0x4321u;
        break;
    case ASTRA_HOST_FS_FILE_FLAGS:
        assert(command->handle == 0x1234u &&
               command->flags == ASTRA_VFS_OPEN_APPEND && command->value_lo == 0u);
        command->value_lo = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
        break;
    case ASTRA_HOST_FS_TRUNCATE:
        assert(command->handle == 0x1234u && command->value_lo == 12u);
        break;
    case ASTRA_HOST_FS_STAT:
        if (command->flags & ASTRA_HOST_FS_STAT_BUDGET) {
            assert(operand(request->path, request->path_length, "/escape"));
            assert(command->flags == (ASTRA_HOST_FS_STAT_BUDGET |
                                      ASTRA_HOST_FS_STAT_ENTRY));
            assert(command->offset_lo == 3u && output_capacity >= 4u);
            memcpy(output, "/dh0", 4u);
            command->result_length = 4u;
            command->result_value = 7u;
            command->value_lo = host->escape_kind;
            command->value_hi = 2u;
            return ASTRA_VFS_ERR_ESCAPED;
        }
        if (command->handle == 0u) {
            assert(operand(request->path, request->path_length, "/file"));
            command->node.size_lo = 12u;
            command->node.kind = ASTRA_VFS_KIND_FILE;
        } else if (request->path_length != 0u) {
            assert(command->handle == 0x1234u &&
                   operand(request->path, request->path_length, "child"));
            command->node.valid = ASTRA_HOST_NODE_TYPE | ASTRA_HOST_NODE_MODE |
                                  ASTRA_HOST_NODE_NLINK | ASTRA_HOST_NODE_UID |
                                  ASTRA_HOST_NODE_GID | ASTRA_HOST_NODE_SIZE |
                                  ASTRA_HOST_NODE_MTIME | ASTRA_HOST_NODE_INODE |
                                  ASTRA_HOST_NODE_DEVICE | ASTRA_HOST_NODE_BTIME |
                                  ASTRA_HOST_NODE_ATIME | ASTRA_HOST_NODE_CTIME |
                                  ASTRA_HOST_NODE_BLOCKS;
            command->node.inode_lo = 4242u;
            command->node.device_hi = 8u;
            command->node.device_lo = host->device_minor ? host->device_minor : 1u;
            command->node.btime.seconds_hi = (int32_t)(host->birth_seconds >> 32);
            command->node.btime.seconds_lo = (uint32_t)host->birth_seconds;
            command->node.btime.nanoseconds = host->birth_nsec;
            command->node.size_lo = 23u;
            command->node.mtime.seconds_lo = 99u;
            command->node.mtime.nanoseconds = 42u;
            command->node.atime.seconds_hi = -1;
            command->node.atime.seconds_lo = UINT32_MAX - 1u;
            command->node.atime.nanoseconds = 123u;
            command->node.ctime.seconds_lo = 100u;
            command->node.ctime.nanoseconds = 456u;
            command->node.blocks_hi = 1u;
            command->node.blocks_lo = 7u;
            command->node.block_size = 4096u;
            command->node.uid = 17u;
            command->node.gid = 23u;
            command->node.kind = ASTRA_VFS_KIND_FILE;
            command->node.mode = 0640u;
            command->node.nlink = 2u;
        } else {
            assert(command->handle == 0x1234u);
            command->node.size_lo = 31u;
            command->node.kind = ASTRA_VFS_KIND_FILE;
            command->node.mode = 0600u;
            command->node.uid = 42u;
            command->node.nlink = 1u;
        }
        break;
    case ASTRA_HOST_FS_READDIR:
        /* An open directory's next entry: the handle, no path. */
        assert(command->handle == 0x1234u && request->path_length == 0u);
        assert(command->value_lo == 1u || command->value_lo == 2u);
        assert(command->value_lo == 1u ? command->offset_lo == 5u :
               command->offset_hi == UINT32_MAX &&
               command->offset_lo == UINT32_MAX);
        assert(output_capacity >= 37u * command->value_lo);
        for (uint32_t index = 0u; index < command->value_lo; ++index) {
            uint8_t *record = (uint8_t *)output + index * 37u;
            memset(record, 0, 37u);
            astra_store_be16(record, ASTRA_VFS_KIND_FILE);
            record[6] = 5u;
            memcpy(record + ASTRA_VFS_DIRENT_HEADER, "entry", 5u);
        }
        command->result_length = 37u * command->value_lo;
        command->value_lo = 6u;
        command->node.kind = ASTRA_VFS_KIND_FILE;
        break;
    case ASTRA_HOST_FS_MKDIR:
        assert(operand(request->path, request->path_length, "/dir") &&
               command->value_lo == 0750u);
        break;
    case ASTRA_HOST_FS_UNLINK:
        if (command->handle == 0u) {
            assert(operand(request->path, request->path_length, "/file") &&
                   command->flags == 0u);
        } else {
            assert(command->handle == 0x1234u &&
                   operand(request->path, request->path_length, "child") &&
                   command->flags == ASTRA_HOST_FS_UNLINK_DIRECTORY);
        }
        break;
    case ASTRA_HOST_FS_RENAME:
        assert(command->handle == 0u && command->value_lo == 0u);
        assert(operand(request->path, request->path_length, "/old") &&
               operand(request->path2, request->path2_length, "/new"));
        break;
    case ASTRA_HOST_FS_CHMOD:
        assert((command->flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) == 0u);
        if (command->handle == 0u) {
            assert(operand(request->path, request->path_length, "/new") &&
                   command->value_lo == 0600u);
        } else if (request->path_length == 0u) {
            assert(command->handle == 0x1234u && command->value_lo == 0644u);
        } else {
            assert(command->handle == 0x1234u &&
                   operand(request->path, request->path_length, "child") &&
                   command->value_lo == 0750u);
        }
        break;
    case ASTRA_HOST_FS_READLINK:
        assert(operand(request->path, request->path_length, "/link") && output_capacity == ASTRA_VFS_IO_MAX);
        memcpy(output, "/new", 4u);
        command->result_length = 4u;
        break;
    case ASTRA_HOST_FS_SYMLINK:
        assert(operand(request->path, request->path_length, "/new") &&
               operand(request->path2, request->path2_length, "/link"));
        break;
    case ASTRA_HOST_FS_LINK:
        assert(operand(request->path, request->path_length, "/new") &&
               operand(request->path2, request->path2_length, "/hard-link"));
        break;
    case ASTRA_HOST_FS_FILESYSTEM_INFO: {
        AstraHostFilesystemInfo *about = output;

        assert(command->handle == 0u && operand(request->path, request->path_length, "/"));
        assert(output_capacity == ASTRA_VFS_IO_MAX);
        memset(about, 0, sizeof(*about));
        about->size = sizeof(*about);
        about->block_size = 4096u;
        about->fragment_size = 4096u;
        about->blocks_lo = 100u;
        about->blocks_free_lo = 40u;
        about->blocks_available_lo = 30u;
        about->files_lo = 200u;
        about->files_free_lo = 150u;
        about->name_max = 255u;
        command->result_length = sizeof(*about);
        break;
    }
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
    FakeHost host = { .birth_seconds = 7u, .birth_nsec = 5u };
    const AstraVfsBackendOps *ops;
    AstraVfsNodeInfo info;
    uintptr_t node = 0u;
    uintptr_t child_node = 0u;
    uint8_t bytes[16] = {0};
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t moved;
    uint64_t position;
    uint64_t next;
    AstraVfsFilesystemInfo filesystem_info;

    assert(!astra_vfs_host_init(NULL,
                               (AstraVfsHostTransport *)(void *)&host, 9u,
                               7u));
    assert(!astra_vfs_host_init(&backend, NULL, 9u, 7u));
    /* A backend names a provider session; 0 names none. */
    assert(!astra_vfs_host_init(
        &backend, (AstraVfsHostTransport *)(void *)&host, 9u, 0u));
    assert(astra_vfs_host_init(
        &backend, (AstraVfsHostTransport *)(void *)&host, 9u, 7u));
    assert(backend.session == 7u);
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
    assert(ops->write(&backend, node, 17u, ASTRA_VFS_WRITE_APPEND, UINT64_MAX,
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
    {
        uint8_t batch[74];
        EXPECT(ASTRA_HOST_FS_READDIR);
        assert(ops->readdir_batch(&backend, node, "",
                                   ASTRA_VFS_CURSOR_CURRENT, 2u, batch,
                                   sizeof(batch), &moved, &next) ==
               ASTRA_VFS_OK);
        assert(moved == sizeof(batch) && next == 6u &&
               batch[6] == 5u && batch[43] == 5u);
    }
    EXPECT(ASTRA_HOST_FS_MKDIR);
    assert(ops->mkdir(&backend, "/dir", 0750u) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_UNLINK);
    assert(ops->unlink(&backend, "/file", 0u) == ASTRA_VFS_OK);
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
    EXPECT(ASTRA_HOST_FS_LINK);
    assert(ops->link(&backend, "/new", "/hard-link") == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_OPEN);
    assert(ops->open_at(&backend, node, "child", ASTRA_VFS_OPEN_READ,
                        ASTRA_VFS_MODE_DEFAULT, &child_node, &info) ==
           ASTRA_VFS_OK);
    assert(child_node == 0x5678u && info.size == 23u);
    EXPECT(ASTRA_HOST_FS_UNLINK);
    assert(ops->unlink_at(&backend, node, "child",
                          ASTRA_VFS_UNLINK_DIRECTORY) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_CHMOD);
    assert(ops->chmod_node(&backend, node, 0644u) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_CHMOD);
    assert(ops->chmod_at(&backend, node, "child", 0750u,
                         ASTRA_VFS_AT_SYMLINK_NOFOLLOW) == ASTRA_VFS_OK);
    EXPECT(ASTRA_HOST_FS_FILESYSTEM_INFO);
    assert(ops->filesystem_info(&backend, 0u, "/", &filesystem_info) ==
           ASTRA_VFS_OK);
    assert(filesystem_info.block_size == 4096u &&
           filesystem_info.blocks == 100u &&
           filesystem_info.blocks_available == 30u &&
           filesystem_info.name_max == 255u);
    EXPECT(ASTRA_HOST_FS_STAT);
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK);
    assert(info.size == 23u && info.mtime == 99 && info.uid == 17u &&
           info.gid == 23u && info.mode == 0640u && info.nlink == 2u);
    assert((info.meta.valid & (ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
                              ASTRA_VFS_META_GENERATION | ASTRA_VFS_META_MODE)) ==
           (ASTRA_VFS_META_VOLUME | ASTRA_VFS_META_INODE |
            ASTRA_VFS_META_GENERATION | ASTRA_VFS_META_MODE));
    assert(info.meta.volume == 0x00800001u && info.meta.inode == 4242u &&
           info.meta.generation == UINT64_C(7000000005));
    assert(info.meta.atime == -2 && info.meta.atime_nsec == 123u &&
           info.meta.mtime_nsec == 42u && info.meta.ctime == 100 &&
           info.meta.ctime_nsec == 456u && info.meta.block_size == 4096u &&
           info.meta.blocks == (UINT64_C(1) << 32) + 7u);
    host.device_minor = 2u;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           info.meta.volume == 0x00800002u);
    host.device_minor = 1u;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           info.meta.volume == 0x00800001u);
    host.birth_seconds = UINT64_MAX / UINT64_C(1000000000);
    host.birth_nsec = UINT64_MAX % UINT64_C(1000000000);
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           (info.meta.valid & ASTRA_VFS_META_GENERATION) &&
           info.meta.generation == UINT64_MAX);
    ++host.birth_nsec;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           !(info.meta.valid & ASTRA_VFS_META_GENERATION));
    host.birth_seconds = UINT64_MAX;
    host.birth_nsec = 0u;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           !(info.meta.valid & ASTRA_VFS_META_GENERATION));
    EXPECT(ASTRA_HOST_FS_STAT);
    assert(ops->stat_node(&backend, node, &info) == ASTRA_VFS_OK);
    assert(info.size == 31u && info.uid == 42u && info.mode == 0600u);
    assert(info.meta.valid == 0u && info.meta.inode == 0u);
    assert(ops->stat_node(&backend, 0u, &info) == ASTRA_VFS_ERR_INVALID);
    EXPECT(ASTRA_HOST_FS_CLOSE);
    assert(ops->close(&backend, node) == ASTRA_VFS_OK);
#undef EXPECT
    /* Backend calls, including identity and generation boundary checks. */
    assert(host.calls == 28u);
    {
        AstraVfsEscape escape;

        host.expected_operation = ASTRA_HOST_FS_STAT;
        host.escape_kind = ASTRA_VFS_ESCAPE_SYMLINK;
        assert(ops->lookup(&backend, 0u, "/escape", ASTRA_VFS_LOOKUP_ENTRY,
                           3u, &info, &escape) == ASTRA_VFS_ERR_ESCAPED);
        assert(escape.kind == ASTRA_VFS_ESCAPE_SYMLINK && escape.links == 2u &&
               escape.consumed == 7u && escape.length == 4u &&
               memcmp(escape.path, "/dh0", 4u) == 0);
        host.escape_kind |= UINT32_C(0x10000);
        assert(ops->lookup(&backend, 0u, "/escape", ASTRA_VFS_LOOKUP_ENTRY,
                           3u, &info, &escape) == ASTRA_VFS_ERR_PROTOCOL);
        assert(ops->lookup(&backend, 0u, "/escape",
                           ASTRA_VFS_LOOKUP_ENTRY | ASTRA_VFS_LOOKUP_FOLLOW,
                           3u, &info, &escape) == ASTRA_VFS_ERR_INVALID);
        assert(host.calls == 30u);
    }
    {
        uint64_t token;
        uint32_t flags;

        host.expected_operation = ASTRA_HOST_FS_READ;
        assert(ops->read_sequential(&backend, 0x1234u, bytes, 8u,
                                     &moved, &position) == ASTRA_VFS_OK &&
               moved == 3u && position == 7u);
        host.expected_operation = ASTRA_HOST_FS_WRITE;
        assert(ops->write(&backend, 0x1234u, 0u, ASTRA_VFS_IO_SEQUENTIAL, UINT64_MAX,
                           "xyz", 3u, &moved, &position) == ASTRA_VFS_OK &&
               position == 20u);
        host.expected_operation = ASTRA_HOST_FS_SEEK;
        assert(ops->seek(&backend, 0x1234u, -2, 2u, &position) == ASTRA_VFS_OK &&
               position == 9u);
        host.expected_operation = ASTRA_HOST_FS_SHARE;
        assert(ops->share(&backend, 0x1234u, &token) == ASTRA_VFS_OK &&
               token == UINT64_C(0x0123456789abcdef));
        host.expected_operation = ASTRA_HOST_FS_ADOPT;
        assert(ops->adopt(&backend, token, 0u, &node) == ASTRA_VFS_OK &&
               node == 0x4321u);
        assert(ops->adopt(&backend, token, ASTRA_VFS_ADOPT_DISCARD, NULL) ==
               ASTRA_VFS_OK);
        host.expected_operation = ASTRA_HOST_FS_FILE_FLAGS;
        assert(ops->file_flags(&backend, 0x1234u, ASTRA_VFS_OPEN_APPEND,
                                0u, &flags) == ASTRA_VFS_OK &&
               flags == (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE));
    }
    host.failure = ASTRA_VFS_ERR_LOST;
    host.expected_operation = ASTRA_HOST_FS_READ;
    assert(ops->read(&backend, 0x1234u, 4u, bytes, 8u, &moved) == ASTRA_VFS_ERR_LOST);
    assert(backend.lost == 0u);
    host.expected_operation = ASTRA_HOST_FS_STAT;
    assert(ops->stat(&backend, "/file", &info) == ASTRA_VFS_ERR_LOST);
    assert(backend.lost == 1u);
    assert(astra_vfs_host_init(&backend, (AstraVfsHostTransport *)&host, 9u, 7u));
    assert(backend.lost == 0u);
    host.begin_failure = ASTRA_VFS_ERR_PEER;
    assert(ops->stat(&backend, "/file", &info) == ASTRA_VFS_ERR_LOST);
    assert(backend.lost == 1u);
    host.begin_failure = host.failure = 0u;
    host.expected_operation = ASTRA_HOST_FS_CLAIM;
    uint32_t epoch = 0u;
    assert(astra_vfs_host_claim((AstraVfsHostTransport *)&host, 9u, 0, &epoch) == ASTRA_VFS_OK);
    assert(epoch == 17u);
    return 0;
}
