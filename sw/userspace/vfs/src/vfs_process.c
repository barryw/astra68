#include <astra/bytes.h>
#include <astra/library_loader.h>
#include <astra/runtime.h>
#include <astra/shared_library.h>
#include <astra/syscall.h>
#include <astra/vfs_path.h>
#include <astra/vfs_port_transport.h>
#include <astra/vfs_process.h>

#define PROCESS_VFS_CLIENT_MAX 4u

typedef struct OpenLibraryRecord {
    AstraLibraryHandle handle;
    const AstraLoadedLibrary *loaded;
    uint32_t references;
    uint8_t used;
} OpenLibraryRecord;

typedef struct LibraryImage {
    uint8_t *bytes;
    uint32_t length;
    uint32_t area;
} LibraryImage;

static AstraAssignTable assigns;
static struct {
    AstraVfsClient client;
    uint32_t handle;
} clients[PROCESS_VFS_CLIENT_MAX];
static uint32_t client_count;
static OpenLibraryRecord open_libraries[ASTRA_LIBRARY_SLOT_COUNT];

static int same(const char *left, const char *right)
{
    while (*left == *right) {
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
    return 0;
}

static int library_name_valid(const char *name)
{
    uint32_t length = 0u;

    if (name == NULL)
        return 0;
    while (name[length] != '\0') {
        char value = name[length];

        if (length + 1u >= ASTRA_LIBRARY_NAME_MAX ||
            !((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-'))
            return 0;
        ++length;
    }
    return length != 0u;
}

static int append(char *path, uint32_t capacity, const char *text)
{
    uint32_t at = 0u;

    while (at < capacity && path[at] != '\0')
        ++at;
    while (*text != '\0') {
        if (at + 1u >= capacity)
            return 0;
        path[at++] = *text++;
    }
    path[at] = '\0';
    return 1;
}

static int append_number(char *path, uint32_t capacity, uint16_t value)
{
    char digits[5];
    uint32_t count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        char digit[2] = {digits[--count], '\0'};

        if (!append(path, capacity, digit))
            return 0;
    }
    return 1;
}

static int parse_version(const char *name, uint16_t minimum,
                         uint16_t version[3])
{
    uint32_t index = 0u;

    for (uint32_t part = 0u; part < 3u; ++part) {
        uint32_t value = 0u;
        uint32_t digits = 0u;

        while (name[index] >= '0' && name[index] <= '9') {
            value = value * 10u + (uint32_t)(name[index++] - '0');
            if (value > UINT16_MAX)
                return 0;
            ++digits;
        }
        if (digits == 0u || (part != 2u && name[index++] != '.') ||
            (part == 2u && name[index] != '\0'))
            return 0;
        version[part] = (uint16_t)value;
    }
    return version[0] >= minimum;
}

static int newer(const uint16_t candidate[3], const uint16_t current[3])
{
    for (uint32_t part = 0u; part < 3u; ++part) {
        if (candidate[part] != current[part])
            return candidate[part] > current[part];
    }
    return 0;
}

#if defined(ASTRA_VFS_PROCESS_TEST)
int astra_vfs_process_test_parse_version(const char *name, uint16_t minimum,
                                         uint16_t version[3])
{
    return parse_version(name, minimum, version);
}
#endif

static void release_library_image(LibraryImage *image)
{
    if (image->bytes != NULL)
        (void)astra_rt_area_unmap(image->bytes);
    if (image->area != 0u)
        (void)astra_close(image->area);
    *image = (LibraryImage){0};
}

static uint32_t read_file(const char *path, LibraryImage *image)
{
    *image = (LibraryImage){0};
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        AstraVfsFile file;
        uint64_t size = 0u;
        uint32_t mapped_size = 0u;
        uint16_t kind = 0u;
        uint32_t status;
        char wire[ASTRA_VFS_PATH_MAX];

        status = astra_assign_resolve(&assigns, path, ASTRA_RIGHT_READ,
                                      member, wire, sizeof(wire), &assign);
        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            return status;
        if (status != ASTRA_VFS_OK)
            continue;
        client = astra_process_vfs_client_for(assign);
        if (client == NULL)
            continue;
        status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ, &file,
                                &size, &kind);
        if (status != ASTRA_VFS_OK)
            continue;
        if (kind == ASTRA_VFS_KIND_DIRECTORY || size == 0u ||
            size > ASTRA_LIBRARY_IMAGE_MAX) {
            (void)astra_vfs_close(client, file);
            return ASTRA_VFS_ERR_LIMIT;
        }
        status = astra_rt_area_create(
            (uint32_t)size,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            &image->area);
        if (status == ASTRA_SYSCALL_OK)
            status = astra_rt_area_map(
                image->area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                (void **)&image->bytes, &mapped_size);
        if (status != ASTRA_SYSCALL_OK || mapped_size < size) {
            (void)astra_vfs_close(client, file);
            release_library_image(image);
            return ASTRA_VFS_ERR_NO_SPACE;
        }
        while (image->length < (uint32_t)size) {
            uint32_t moved = 0u;

            status = astra_vfs_port_read_bulk(
                client, file, image->length, image->bytes + image->length,
                (uint32_t)size - image->length, &moved);
            if (status != ASTRA_VFS_OK || moved == 0u)
                break;
            image->length += moved;
        }
        (void)astra_vfs_close(client, file);
        if (status == ASTRA_VFS_OK && image->length == (uint32_t)size)
            return ASTRA_VFS_OK;
        release_library_image(image);
        return ASTRA_VFS_ERR_IO;
    }
}

static int resolve_library(const char *name, uint16_t minimum,
                           char *path, uint32_t capacity)
{
    uint16_t selected[3] = {0u, 0u, 0u};
    int found = 0;

    path[0] = '\0';
    if (!append(path, capacity, "LIBS:") || !append(path, capacity, name) ||
        !append(path, capacity, "/abi-") ||
        !append_number(path, capacity, minimum))
        return 0;
    for (uint32_t member = 0u; ; ++member) {
        const AstraAssign *assign = NULL;
        AstraVfsClient *client;
        uint64_t cursor = 0u;
        char wire[ASTRA_VFS_PATH_MAX];
        uint32_t status = astra_assign_resolve(
            &assigns, path, ASTRA_RIGHT_READ, member, wire, sizeof(wire),
            &assign);

        if (status == ASTRA_VFS_ERR_NOT_FOUND)
            break;
        if (status != ASTRA_VFS_OK)
            continue;
        client = astra_process_vfs_client_for(assign);
        if (client == NULL)
            continue;
        for (;;) {
            char entry[ASTRA_VFS_NAME_MAX];
            uint16_t kind;
            uint16_t candidate[3];
            uint64_t next;

            status = astra_vfs_readdir(client, wire, cursor, entry,
                                       sizeof(entry), &kind, &next);
            if (status != ASTRA_VFS_OK)
                break;
            if (kind == ASTRA_VFS_KIND_DIRECTORY &&
                parse_version(entry, minimum, candidate) &&
                (!found || newer(candidate, selected))) {
                for (uint32_t part = 0u; part < 3u; ++part)
                    selected[part] = candidate[part];
                found = 1;
            }
            if (next == 0u)
                break;
            cursor = next;
        }
    }
    if (!found || !append(path, capacity, "/") ||
        !append_number(path, capacity, selected[0]) ||
        !append(path, capacity, ".") ||
        !append_number(path, capacity, selected[1]) ||
        !append(path, capacity, ".") ||
        !append_number(path, capacity, selected[2]) ||
        !append(path, capacity, "/m68k-68030/") ||
        !append(path, capacity, name))
        return 0;
    return 1;
}

uint32_t astra_process_vfs_init(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *capabilities;

    if (!astra_startup_validate(startup) ||
        startup->capabilities_address == 0u)
        return ASTRA_VFS_ERR_INVALID;
    capabilities = (const AstraStartupCapability *)(uintptr_t)
        startup->capabilities_address;
    if (astra_assign_seed(&assigns, capabilities,
                          startup->capability_count) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    client_count = 0u;
    (void)memset(clients, 0, sizeof(clients));
    (void)memset(open_libraries, 0, sizeof(open_libraries));
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot].handle == assigns.entries[index].handle)
                break;
        if (slot != client_count)
            continue;
        if (client_count == PROCESS_VFS_CLIENT_MAX)
            return ASTRA_VFS_ERR_LIMIT;
        clients[client_count].handle = assigns.entries[index].handle;
        if (astra_vfs_port_connect(&clients[client_count].client,
                                   clients[client_count].handle) !=
            ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_IO;
        ++client_count;
    }
    return client_count != 0u ? ASTRA_VFS_OK : ASTRA_VFS_ERR_NOT_FOUND;
}

AstraAssignTable *astra_process_vfs_assigns(void)
{
    return &assigns;
}

AstraVfsClient *astra_process_vfs_client(void)
{
    return client_count != 0u ? &clients[0].client : NULL;
}

AstraVfsClient *astra_process_vfs_client_for(const AstraAssign *assign)
{
    if (assign == NULL)
        return astra_process_vfs_client();
    for (uint32_t index = 0u; index < client_count; ++index)
        if (clients[index].handle == assign->handle)
            return &clients[index].client;
    return NULL;
}

AstraVfsClient *astra_process_vfs_assign_client(const AstraAssign *assign,
                                                void *context)
{
    (void)context;
    return astra_process_vfs_client_for(assign);
}

uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup)
{
    uint32_t status;

    if (filesystem == NULL || filesystem->handle != NULL ||
        filesystem->library != NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return status;
    filesystem->handle = OpenLibrary(ASTRA_FILESYSTEM_LIBRARY_NAME,
                                     ASTRA_FILESYSTEM_LIBRARY_VERSION);
    if (filesystem->handle == NULL)
        return ASTRA_VFS_ERR_NOT_FOUND;
    filesystem->library = filesystem->handle->exports;
    if (filesystem->library->abi_major !=
            ASTRA_FILESYSTEM_LIBRARY_ABI_MAJOR ||
        filesystem->library->structure_size < sizeof(*filesystem->library)) {
        astra_process_filesystem_close(filesystem);
        return ASTRA_VFS_ERR_PROTOCOL;
    }
    status = filesystem->library->attach(
        &filesystem->filesystem, astra_process_vfs_assigns(),
        astra_process_vfs_assign_client, astra_vfs_port_read_bulk, NULL);
    if (status != ASTRA_VFS_OK)
        astra_process_filesystem_close(filesystem);
    return status;
}

void astra_process_filesystem_close(AstraProcessFilesystem *filesystem)
{
    if (filesystem == NULL)
        return;
    if (filesystem->library != NULL)
        filesystem->library->detach(&filesystem->filesystem);
    CloseLibrary(filesystem->handle);
    *filesystem = (AstraProcessFilesystem)ASTRA_PROCESS_FILESYSTEM_INIT;
}

AstraLibraryHandle *OpenLibrary(const char *name, uint16_t version)
{
    const AstraLoadedLibrary *loaded;
    LibraryImage image;
    char path[ASTRA_VFS_PATH_MAX];
    uint32_t status;

    if (!library_name_valid(name) || version == 0u)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        if (open_libraries[index].used != 0u &&
            same(open_libraries[index].loaded->identity->name, name) &&
            open_libraries[index].handle.abi_major == version &&
            open_libraries[index].references != UINT32_MAX) {
            ++open_libraries[index].references;
            return &open_libraries[index].handle;
        }
    }
    if (!resolve_library(name, version, path, sizeof(path)) ||
        read_file(path, &image) != ASTRA_VFS_OK)
        return NULL;
    status = astra_library_load(image.bytes, image.length, name, version, 0u,
                                &loaded);
    release_library_image(&image);
    if (status != ASTRA_SYSCALL_OK)
        return NULL;
    for (uint32_t index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        if (open_libraries[index].used != 0u)
            continue;
        open_libraries[index].loaded = loaded;
        open_libraries[index].references = 1u;
        open_libraries[index].used = 1u;
        open_libraries[index].handle.exports = loaded->exports;
        open_libraries[index].handle.version = loaded->identity->major;
        open_libraries[index].handle.revision = loaded->identity->minor;
        open_libraries[index].handle.abi_major = loaded->identity->abi_major;
        open_libraries[index].handle.abi_minor = loaded->identity->abi_minor;
        open_libraries[index].handle._private_slot = index + 1u;
        return &open_libraries[index].handle;
    }
    return NULL;
}

void CloseLibrary(AstraLibraryHandle *library)
{
    uint32_t slot;

    if (library == NULL || library->_private_slot == 0u)
        return;
    slot = library->_private_slot - 1u;
    if (slot < ASTRA_LIBRARY_SLOT_COUNT &&
        library == &open_libraries[slot].handle &&
        open_libraries[slot].references != 0u)
        --open_libraries[slot].references;
}

void astra_library_cleanup(AstraLibraryHandle **library)
{
    if (library != NULL)
        CloseLibrary(*library);
}
