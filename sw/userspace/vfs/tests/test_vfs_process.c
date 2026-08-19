#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/bundle.h>
#include <astra/vfs_process.h>

int astra_vfs_process_test_provider(const AstraBundleManifest *manifest,
                                    const char *name, uint16_t abi,
                                    uint16_t version[3]);

static uint32_t closes;
static uint32_t connects;
static uint32_t disconnects;
static uint32_t fail_connect;

int astra_startup_validate(const AstraStartupInfo *startup)
{
    return startup != NULL;
}

uint32_t astra_assign_seed(AstraAssignTable *table,
                           const AstraStartupCapability *capabilities,
                           uint32_t count)
{
    (void)capabilities;
    (void)count;
    memset(table, 0, sizeof(*table));
    table->count = 3u;
    table->entries[0].handle = 11u;
    table->entries[1].handle = 11u;
    table->entries[2].handle = 22u;
    return ASTRA_VFS_OK;
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
    (void)table; (void)path; (void)rights; (void)member; (void)wire;
    (void)capacity; (void)assign;
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                                  const uint8_t **bytes, uint32_t *moved,
                                  uint64_t *node_size)
{
    (void)client; (void)path; (void)bytes; (void)moved; (void)node_size;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

uint32_t astra_vfs_port_connect(AstraVfsClient *client, uint32_t service)
{
    ++connects;
    if (connects == fail_connect)
        return ASTRA_VFS_ERR_IO;
    client->session = service;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_disconnect(AstraVfsClient *client)
{
    assert(client->session != ASTRA_VFS_SESSION_INVALID);
    ++disconnects;
    client->session = ASTRA_VFS_SESSION_INVALID;
    return ASTRA_VFS_OK;
}

static uint32_t file_open(AstraFilesystem *filesystem, const char *path,
                          uint32_t flags, AstraFile *file)
{
    (void)filesystem;
    assert(strcmp(path, "APP:test") == 0 && flags == ASTRA_VFS_OPEN_READ);
    file->_private_file = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t file_info(const AstraFile *file, AstraFileInfo *info)
{
    assert(file->_private_file == 1u);
    info->byte_size = 5u;
    return ASTRA_VFS_OK;
}

static uint32_t file_read(AstraFile *file, void *bytes, uint32_t capacity,
                          uint32_t *moved)
{
    static const char content[] = "hello";
    uint32_t offset = (uint32_t)file->_private_offset;
    uint32_t count = capacity < 2u ? capacity : 2u;

    if (count > 5u - offset) count = 5u - offset;
    memcpy(bytes, content + offset, count);
    file->_private_offset += count;
    *moved = count;
    return ASTRA_VFS_OK;
}

static uint32_t file_close(AstraFile *file)
{
    ++closes;
    file->_private_file = ASTRA_VFS_FILE_INVALID;
    return ASTRA_VFS_OK;
}

int main(void)
{
    uint16_t version[3];

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };

        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 2u);

        connects = 0u;
        disconnects = 0u;
        fail_connect = 2u;
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_ERR_IO);
        assert(connects == 2u && disconnects == 1u);
        astra_process_vfs_close();
        assert(disconnects == 1u);
    }

    {
        AstraFilesystemLibraryV1 library = {
            .open = file_open,
            .close = file_close,
            .read = file_read,
            .file_info = file_info,
        };
        AstraProcessFilesystem filesystem = {
            .library = &library,
        };
        char bytes[5];
        uint32_t length = 0u;

        assert(astra_process_read_file(&filesystem, "APP:test", bytes,
                                       sizeof(bytes), &length) ==
               ASTRA_VFS_OK);
        assert(length == sizeof(bytes) && memcmp(bytes, "hello", 5u) == 0);
        assert(closes == 1u);
        assert(astra_process_read_file(&filesystem, "APP:test", bytes, 4u,
                                       &length) == ASTRA_VFS_ERR_LIMIT);
        assert(closes == 2u);
    }

    {
        AstraBundleManifest manifest = {0};

        manifest.kind = ASTRA_BUNDLE_KIT;
        manifest.provide_count = 2u;
        strcpy(manifest.provides[0].name, "graphics.library");
        manifest.provides[0].abi = 1u;
        manifest.provides[0].version = (AstraBundleVersion){1u, 1u, 0u};
        strcpy(manifest.provides[1].name, "font.library");
        manifest.provides[1].abi = 1u;
        manifest.provides[1].version = (AstraBundleVersion){1u, 0u, 0u};
        assert(astra_vfs_process_test_provider(
            &manifest, "graphics.library", 1u, version));
        assert(version[0] == 1u && version[1] == 1u && version[2] == 0u);
        assert(!astra_vfs_process_test_provider(
            &manifest, "graphics.library", 2u, version));
        manifest.provides[0].version = (AstraBundleVersion){0u, 9u, 0u};
        assert(astra_vfs_process_test_provider(
            &manifest, "graphics.library", 1u, version));
    }
    puts("astra process library resolver: PASS");
    return 0;
}
