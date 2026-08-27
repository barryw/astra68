#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <astra/bundle.h>
#include <astra/library.h>
#include <astra/vfs_process.h>

int astra_vfs_process_test_provider(const AstraBundleManifest *manifest,
                                    const char *name, uint16_t abi,
                                    uint16_t version[3]);
int astra_vfs_process_test_index(const uint8_t *bytes, uint32_t length,
                                 const char *name, uint16_t abi, char *path,
                                 uint32_t capacity,
                                 AstraLibraryReference *reference);

static uint32_t closes;
static uint32_t connects;
static uint32_t disconnects;
static uint32_t abandons;
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
    (void)table;
    (void)path;
    (void)rights;
    (void)member;
    (void)wire;
    (void)capacity;
    (void)assign;
    return ASTRA_VFS_ERR_NOT_FOUND;
}

uint32_t astra_vfs_port_read_path(AstraVfsClient *client, const char *path,
                                  const uint8_t **bytes, uint32_t *moved,
                                  uint64_t *node_size)
{
    (void)client;
    (void)path;
    (void)bytes;
    (void)moved;
    (void)node_size;
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

uint32_t astra_vfs_port_write_bulk(AstraVfsClient *client, AstraVfsFile file,
                                   uint64_t offset, const void *buffer,
                                   uint32_t length, uint32_t *moved)
{
    (void)client; (void)file; (void)offset; (void)buffer; (void)length;
    (void)moved;
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

uint32_t astra_vfs_port_connect_lazy(AstraVfsClient *client,
                                     uint32_t service)
{
    return astra_vfs_port_connect(client, service);
}

void astra_vfs_port_abandon(AstraVfsClient *client)
{
    ++abandons;
    client->session = ASTRA_VFS_SESSION_INVALID;
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
    static const uint8_t indexed[] = {
        'A', 'P', 'R', 'V', 0, 1, 0, 24,
        0, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 0,
        0x12, 0x34, 0x56, 0x78,
        'L', 'I', 'B', 'S', ':', 'F', 'i', 'l', 'e'
    };
    uint16_t version[3];
    char path[128];
    AstraLibraryReference reference;

    assert(astra_vfs_process_test_index(
        (const uint8_t *)"LIBS:Filesystem.kit/library", 27u,
        "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(strcmp(path, "LIBS:Filesystem.kit/library") == 0);
    assert(reference.size == 0u);
    assert(astra_vfs_process_test_index(
        indexed, sizeof(indexed), "filesystem.library", 1u, path,
        sizeof(path), &reference));
    assert(strcmp(path, "LIBS:File") == 0);
    assert(reference.size == ASTRA_LIBRARY_REFERENCE_SIZE);
    assert(reference.major == 1u && reference.minor == 2u &&
           reference.patch == 3u && reference.abi_major == 1u &&
           reference.abi_minor == 4u &&
           reference.build_id == 0x12345678u);
    assert(!astra_vfs_process_test_index(
        (const uint8_t *)"SYS:Filesystem.kit/library", 26u,
        "filesystem.library", 1u, path, sizeof(path), &reference));
    assert(!astra_vfs_process_test_index(
        (const uint8_t *)"LIBS:Filesystem kit/library", 27u,
        "filesystem.library", 1u, path, sizeof(path), &reference));

    {
        AstraStartupInfo startup = { .capabilities_address = 1u };

        /*
         * Seeding connects nothing. A connect is two cross-process round trips
         * and a program that never names a mount must never pay for it -- so
         * this counter staying at zero here is the whole of that claim.
         */
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
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
            assert(restored._private_write_at == astra_vfs_port_write_bulk);
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
        assert(astra_process_vfs_init(&startup) == ASTRA_VFS_OK);
        assert(astra_process_vfs_client() != NULL && connects == 1u);
        assert(astra_process_vfs_after_fork_child(&startup) == ASTRA_VFS_OK);
        assert(abandons == 1u && disconnects == 0u);
        assert(astra_process_vfs_client() != NULL && connects == 2u);
        astra_process_vfs_close();
        assert(disconnects == 1u);

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
