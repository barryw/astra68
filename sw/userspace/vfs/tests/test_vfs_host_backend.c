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
    uint32_t catalog_corrupt;
    uint32_t expected_parent;
    uint32_t expected_requester;
    uint32_t payload_limit;
    uint32_t usage_cursor;
    AstraVolumeReference expected_volume;
} FakeHost;

uint32_t astra_vfs_host_transport_payload_limit(const AstraVfsHostTransport *transport)
{
    const FakeHost *host = (const FakeHost *)transport;
    if (host->payload_limit != 0u)
        return host->payload_limit;
    return 2u * 1024u * 1024u - ASTRA_HOST_CHANNEL_HEADER_SIZE - ASTRA_HOST_COMMAND_SIZE;
}

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
    if (command->service == ASTRA_HOST_SERVICE_VOLUMES) {
        if (command->operation == ASTRA_HOST_VOLUME_HOLDERS_CHANGED) {
            assert(command->generation == 9u && command->session == 0u && command->handle == 0u && command->flags == 0u);
            assert(input == NULL && input_size == 0u && output == NULL && output_capacity == 0u);
            ++host->calls;
            return host->failure;
        }
        if (command->operation == ASTRA_HOST_VOLUME_USAGE) {
            assert(command->generation == 9u && command->session == 0u && command->handle == 0u && command->flags == 0u);
            assert(input_size == sizeof(AstraVolumeUsageQuery) && output_capacity == sizeof(AstraVolumeUsagePage));
            const uint8_t *query = input;
            assert(astra_load_be32(query) == ASTRA_VOLUME_USAGE_VERSION && astra_load_be32(query + 4u) == sizeof(AstraVolumeUsageQuery));
            if (host->usage_cursor) {
                assert(astra_load_be64(query + 48u) == UINT64_C(0x100000005) &&
                    astra_load_be32(query + 56u) == ASTRA_VOLUME_USAGE_ROOT && astra_load_be32(query + 60u) == 41u);
            }
            if (host->failure != 0u) return host->failure;
            uint8_t *page = output;
            memset(page, 0, output_capacity);
            astra_store_be32(page, ASTRA_VOLUME_USAGE_VERSION);
            astra_store_be32(page + 4u, sizeof(AstraVolumeUsagePage));
            astra_store_be64(page + 8u, UINT64_C(0xabcdef0100000002));
            astra_store_be64(page + 32u, UINT64_C(0x100000004));
            astra_store_be64(page + 40u, 17u);
            astra_store_be32(page + 60u, 1u);
            uint8_t *row = page + offsetof(AstraVolumeUsagePage, records);
            astra_store_be64(row, UINT64_C(0xabcdef0100000002));
            astra_store_be64(row + 8u, UINT64_C(0x100000003));
            astra_store_be64(row + 16u, 9u);
            astra_store_be64(row + 24u, UINT64_C(0x100000005));
            astra_store_be64(row + 32u, UINT64_C(0x100000006));
            astra_store_be32(row + 40u, 42u);
            astra_store_be32(row + 44u, ASTRA_VOLUME_USAGE_FILE);
            if (host->usage_cursor) {
                astra_store_be32(row + 44u, ASTRA_VOLUME_USAGE_ROOT);
                astra_store_be64(page + 48u, UINT64_C(0x100000005));
                astra_store_be32(page + 56u, ASTRA_VOLUME_USAGE_ROOT);
                astra_store_be32(page + 64u, 42u);
            }
            astra_store_be32(row + 48u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE);
            astra_store_be32(row + 56u, 12u);
            memcpy(row + 64u, "/cd0/fixture", 13u);
            if (host->catalog_corrupt == 1u) astra_store_be32(row + 60u, 1u);
            if (host->catalog_corrupt == 2u) astra_store_be64(page + 32u, 0u);
            if (host->catalog_corrupt == 3u) astra_store_be32(row + 56u, ASTRA_VOLUME_USAGE_PATH_MAX);
            command->result_length = sizeof(AstraVolumeUsagePage);
            ++host->calls;
            return ASTRA_VFS_OK;
        }
        assert(command->operation == ASTRA_HOST_VOLUME_CATALOG && command->generation == 9u);
        assert(command->session == 0u && command->handle == 0u && command->flags == 0u);
        assert(input_size == sizeof(AstraVolumeCatalogQuery) && output_capacity == sizeof(AstraVolumeCatalogPage));
        assert(astra_load_be32(input) == ASTRA_VOLUME_CATALOG_VERSION &&
            astra_load_be32((const uint8_t *)input + 4u) == sizeof(AstraVolumeCatalogQuery));
        uint8_t *page = output;
        memset(page, 0, output_capacity);
        astra_store_be32(page, ASTRA_VOLUME_CATALOG_VERSION);
        astra_store_be32(page + 4u, sizeof(AstraVolumeCatalogPage));
        astra_store_be64(page + 8u, UINT64_C(0xabcdef0100000002));
        astra_store_be64(page + 16u, 17u);
        astra_store_be32(page + 32u, 1u);
        uint8_t *row = page + offsetof(AstraVolumeCatalogPage, records);
        astra_store_be64(row + offsetof(AstraVolumeRecord, id), UINT64_C(0x100000003));
        astra_store_be64(row + offsetof(AstraVolumeRecord, generation), 9u);
        astra_store_be32(row + offsetof(AstraVolumeRecord, kind), ASTRA_VOLUME_MOUNT);
        astra_store_be32(row + offsetof(AstraVolumeRecord, state), ASTRA_VOLUME_MOUNTED);
        astra_store_be32(row + offsetof(AstraVolumeRecord, class_id), ASTRA_VOLUME_OPTICAL);
        astra_store_be32(row + offsetof(AstraVolumeRecord, flags), ASTRA_VOLUME_READ_ONLY);
        strcpy((char *)row + offsetof(AstraVolumeRecord, name), "/cd0");
        strcpy((char *)row + offsetof(AstraVolumeRecord, label), "Astra disc");
        command->result_length = sizeof(AstraVolumeCatalogPage);
        if (host->catalog_corrupt == 1u)
            astra_store_be32(row + offsetof(AstraVolumeRecord, reserved), 1u);
        if (host->catalog_corrupt == 2u)
            command->result_length = sizeof(AstraVolumeCatalogPage) - 1u;
        if (host->catalog_corrupt == 3u)
            astra_store_be64(page + 24u, 123u);
        ++host->calls;
        return ASTRA_VFS_OK;
    }
    assert(command->service == ASTRA_HOST_SERVICE_FILESYSTEM);
    assert(command->generation == 9u);
    /* Every command names the session its backend was made for, and its
     * path operands travel beside it, not in the record. */
    assert(command->session == (command->operation == ASTRA_HOST_FS_CLAIM ? 0u :
        command->operation == ASTRA_HOST_FS_SESSION_OPEN ? host->expected_parent : 7u));
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
    case ASTRA_HOST_FS_SESSION_OPEN:
        assert(command->value_hi == 0u && command->value_lo == 0u);
        assert(operand(request->path, request->path_length, "/"));
        assert(output_capacity == 0u);
        if (host->expected_parent == 0u) {
            assert(input_size == sizeof(AstraHostSessionOpen));
            assert(astra_load_be64(input) == host->expected_volume.epoch);
            assert(astra_load_be64((const uint8_t *)input + 8u) == host->expected_volume.id);
            assert(astra_load_be64((const uint8_t *)input + 16u) == host->expected_volume.generation);
        } else {
            assert(input_size == sizeof(AstraHostSessionOpen));
            assert(astra_load_be64(input) == 0u && astra_load_be64((const uint8_t *)input + 8u) == 0u &&
                astra_load_be64((const uint8_t *)input + 16u) == 0u);
        }
        assert(astra_load_be32((const uint8_t *)input + 24u) == host->expected_requester &&
            astra_load_be32((const uint8_t *)input + 28u) == 0u);
        command->result_value = 7u;
        break;
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
        assert(input == NULL && input_size == 0u &&
            output_capacity == (host->payload_limit != 0u ? host->payload_limit : 8u));
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
    FakeHost catalog_host = {0};
    AstraVolumeUsageQuery usage_query = {.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(usage_query)};
    AstraVolumeUsagePage usage_page;
    assert(astra_vfs_host_usage((AstraVfsHostTransport *)&catalog_host, 9u, &usage_query, &usage_page) == ASTRA_VFS_OK);
    assert(usage_page.sequence == UINT64_C(0x100000004) && usage_page.count == 1u &&
        usage_page.records[0].handle == UINT64_C(0x100000005) && usage_page.records[0].inode == UINT64_C(0x100000006) &&
        usage_page.records[0].process == 42u && strcmp(usage_page.records[0].path, "/cd0/fixture") == 0);
    catalog_host.usage_cursor = 1u;
    usage_query.volume.epoch = UINT64_C(0xabcdef0100000002);
    usage_query.sequence = UINT64_C(0x100000004); usage_query.catalog_sequence = 17u;
    usage_query.after = UINT64_C(0x100000005); usage_query.after_kind = ASTRA_VOLUME_USAGE_ROOT; usage_query.after_process = 41u;
    assert(astra_vfs_host_usage((AstraVfsHostTransport *)&catalog_host, 9u, &usage_query, &usage_page) == ASTRA_VFS_OK &&
        usage_page.next == usage_query.after && usage_page.next_kind == ASTRA_VOLUME_USAGE_ROOT && usage_page.next_process == 42u);
    catalog_host.usage_cursor = 0u;
    usage_query = (AstraVolumeUsageQuery){.version = ASTRA_VOLUME_USAGE_VERSION, .size = sizeof(usage_query)};
    for (uint32_t corrupt = 1u; corrupt <= 3u; ++corrupt) {
        catalog_host.catalog_corrupt = corrupt;
        assert(astra_vfs_host_usage((AstraVfsHostTransport *)&catalog_host, 9u, &usage_query, &usage_page) == ASTRA_VFS_ERR_PROTOCOL);
    }
    catalog_host.catalog_corrupt = 0u; catalog_host.failure = ASTRA_VFS_ERR_BUSY;
    assert(astra_vfs_host_usage((AstraVfsHostTransport *)&catalog_host, 9u, &usage_query, &usage_page) == ASTRA_VFS_ERR_BUSY);
    catalog_host.failure = 0u;
    assert(astra_vfs_host_usage_holders_changed((AstraVfsHostTransport *)&catalog_host, 9u) == ASTRA_VFS_OK);
    catalog_host.failure = ASTRA_VFS_ERR_IO;
    assert(astra_vfs_host_usage_holders_changed((AstraVfsHostTransport *)&catalog_host, 9u) == ASTRA_VFS_ERR_IO);
    catalog_host.failure = 0u;
    AstraVolumeCatalogQuery query = {.version = ASTRA_VOLUME_CATALOG_VERSION, .size = sizeof(query)};
    AstraVolumeCatalogPage page;
    assert(astra_vfs_host_catalog((AstraVfsHostTransport *)&catalog_host, 9u, &query, &page) == ASTRA_VFS_OK);
    assert(page.epoch == UINT64_C(0xabcdef0100000002) && page.sequence == 17u && page.count == 1u);
    assert(page.records[0].id == UINT64_C(0x100000003) && page.records[0].generation == 9u);
    assert(page.records[0].flags == ASTRA_VOLUME_READ_ONLY && strcmp(page.records[0].name, "/cd0") == 0);
    for (uint32_t corrupt = 1u; corrupt <= 3u; ++corrupt) {
        catalog_host.catalog_corrupt = corrupt;
        assert(astra_vfs_host_catalog((AstraVfsHostTransport *)&catalog_host, 9u, &query, &page) == ASTRA_VFS_ERR_PROTOCOL);
    }
    query.epoch = 0u;
    query.sequence = 1u;
    assert(astra_vfs_host_catalog((AstraVfsHostTransport *)&catalog_host, 9u, &query, &page) == ASTRA_VFS_ERR_INVALID);
    catalog_host.catalog_corrupt = 0u;
    AstraVolumeReference volume;
    assert(astra_vfs_host_lookup_mount((AstraVfsHostTransport *)&catalog_host, 9u,
        "/cd0", ASTRA_VOLUME_READ_ONLY, &page, &volume) == ASTRA_VFS_OK);
    assert(volume.epoch == UINT64_C(0xabcdef0100000002) &&
        volume.id == UINT64_C(0x100000003) && volume.generation == 9u);
    assert(astra_vfs_host_lookup_mount((AstraVfsHostTransport *)&catalog_host, 9u,
        "/cd0", ASTRA_VOLUME_SYSTEM, &page, &volume) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(volume.id == 0u && volume.epoch == 0u);
    FakeHost control_host = {.expected_operation = ASTRA_HOST_FS_SESSION_OPEN,
        .expected_requester = 42u, .expected_volume = {UINT64_C(0xabcdef0100000002), UINT64_C(0x100000003), 9u}};
    uint32_t session_id = 0u;
    assert(astra_vfs_host_session_open((AstraVfsHostTransport *)&control_host, 9u, 0u,
        &control_host.expected_volume, "/", 0u, 42u, &session_id) == ASTRA_VFS_OK && session_id == 7u);
    control_host.expected_parent = 7u;
    assert(astra_vfs_host_session_open((AstraVfsHostTransport *)&control_host, 9u, 7u,
        NULL, "/", 0u, 42u, &session_id) == ASTRA_VFS_OK && session_id == 7u);
    uint32_t calls = control_host.calls;
    assert(astra_vfs_host_session_open((AstraVfsHostTransport *)&control_host, 9u, 7u,
        &control_host.expected_volume, "/", 0u, 42u, &session_id) == ASTRA_VFS_ERR_INVALID);
    assert(astra_vfs_host_session_open((AstraVfsHostTransport *)&control_host, 9u, 0u,
        NULL, "/", 0u, 42u, &session_id) == ASTRA_VFS_ERR_INVALID);
    assert(control_host.calls == calls);
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
    uint32_t sync_calls = host.calls;
    host.failure = ASTRA_VFS_ERR_IO;
    assert(ops->sync(&backend, node) == ASTRA_VFS_ERR_IO &&
           host.calls == sync_calls + 1u && host.command.handle == node);
    host.failure = 0u;
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
    assert(info.meta.volume == UINT64_C(0x800000001) && info.meta.inode == 4242u &&
           info.meta.generation == UINT64_C(7000000005));
    assert(info.meta.atime == -2 && info.meta.atime_nsec == 123u &&
           info.meta.mtime_nsec == 42u && info.meta.ctime == 100 &&
           info.meta.ctime_nsec == 456u && info.meta.block_size == 4096u &&
           info.meta.blocks == (UINT64_C(1) << 32) + 7u);
    host.device_minor = 2u;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           info.meta.volume == UINT64_C(0x800000002));
    host.device_minor = 1u;
    assert(ops->stat_at(&backend, node, "child", 0u, &info) == ASTRA_VFS_OK &&
           info.meta.volume == UINT64_C(0x800000001));
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
    assert(host.calls == 30u);
    {
        uint8_t continuation[ASTRA_VFS_PATH_MAX];
        AstraVfsEscape escape = {.path = continuation, .capacity = ASTRA_VFS_ESCAPE_PATH_MAX};

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
        assert(host.calls == 32u);
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
    host.payload_limit = 3u;
    host.expected_operation = ASTRA_HOST_FS_READ;
    assert(ops->read(&backend, 0x1234u, 4u, bytes, 32u, &moved) == ASTRA_VFS_OK);
    assert(moved == 3u && memcmp(bytes, "abc", 3u) == 0);
    host.expected_operation = ASTRA_HOST_FS_WRITE;
    assert(ops->write(&backend, 0x1234u, 17u, ASTRA_VFS_WRITE_APPEND, UINT64_MAX,
        "xyz123", 6u, &moved, &position) == ASTRA_VFS_OK);
    assert(moved == 3u && position == 20u);
    return 0;
}
