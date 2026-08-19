#include <astra/bytes.h>
#include <astra/bundle.h>
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
    /*
     * Borrowed images live in the client's transfer area rather than in an
     * area of their own: nothing was copied, so there is nothing to release,
     * and the bytes stay valid only until the next read on that client.
     */
    uint8_t borrowed;
} LibraryImage;

static AstraAssignTable assigns;
static struct {
    AstraVfsClient client;
    uint32_t handle;
} clients[PROCESS_VFS_CLIENT_MAX];
static uint32_t client_count;
static OpenLibraryRecord open_libraries[ASTRA_LIBRARY_SLOT_COUNT];

void astra_process_vfs_close(void)
{
    while (client_count != 0u) {
        --client_count;
        (void)astra_vfs_disconnect(&clients[client_count].client);
        clients[client_count].handle = 0u;
    }
}

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

static int newer(const uint16_t candidate[3], const uint16_t current[3])
{
    for (uint32_t part = 0u; part < 3u; ++part) {
        if (candidate[part] != current[part])
            return candidate[part] > current[part];
    }
    return 0;
}

static void release_library_image(LibraryImage *image)
{
    if (image->borrowed == 0u) {
        if (image->bytes != NULL)
            (void)astra_rt_area_unmap(image->bytes);
        if (image->area != 0u)
            (void)astra_close(image->area);
    }
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
        /*
         * Whole file, one round trip, read where it lands. Open, read and
         * close were three exchanges with a service for what is one intent,
         * and this path runs for every kit manifest and every library a
         * program opens.
         */
        {
            const uint8_t *whole = NULL;
            uint32_t got = 0u;
            uint64_t whole_size = 0u;

            status = astra_vfs_port_read_path(client, wire, &whole, &got,
                                              &whole_size);
            if (status == ASTRA_VFS_OK && got != 0u &&
                got == (uint32_t)whole_size &&
                got + 1u <= ASTRA_VFS_BULK_MAX) {
                image->bytes = (uint8_t *)(uintptr_t)whole;
                image->length = got;
                image->borrowed = 1u;
                image->bytes[image->length] = 0u;
                return ASTRA_VFS_OK;
            }
            if (status == ASTRA_VFS_ERR_NOT_FOUND)
                continue;
        }
        status = astra_vfs_open(client, wire, ASTRA_VFS_OPEN_READ, &file,
                                &size, &kind);
        if (status != ASTRA_VFS_OK) {
            continue;
        }
        if (kind == ASTRA_VFS_KIND_DIRECTORY || size == 0u ||
            size > ASTRA_LIBRARY_IMAGE_MAX) {
            (void)astra_vfs_close(client, file);
            return ASTRA_VFS_ERR_LIMIT;
        }
        /*
         * Read it where it lands. A file that fits the transfer area needs no
         * area of its own and no copy out of it -- and every caller here looks
         * at the bytes and releases them before the next read, which is the
         * lifetime a borrow gives. The area create, map and unmap this skips
         * cost a full ATC flush each on top of the copy.
         */
        if ((uint32_t)size + 1u <= ASTRA_VFS_BULK_MAX) {
            const uint8_t *borrowed = NULL;
            uint32_t moved = 0u;

            status = astra_vfs_port_read_borrow(client, file, 0u,
                                                (uint32_t)size, &borrowed,
                                                &moved);
            if (status == ASTRA_VFS_OK && moved == (uint32_t)size) {
                (void)astra_vfs_close(client, file);
                image->bytes = (uint8_t *)(uintptr_t)borrowed;
                image->length = moved;
                image->borrowed = 1u;
                image->bytes[image->length] = 0u;
                return ASTRA_VFS_OK;
            }
            /* Anything else falls through to the copying path below. */
        }
        status = astra_rt_area_create(
            (uint32_t)size + 1u,
            ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP,
            &image->area);
        if (status == ASTRA_SYSCALL_OK)
            status = astra_rt_area_map(
                image->area, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                (void **)&image->bytes, &mapped_size);
        if (status != ASTRA_SYSCALL_OK || mapped_size < size + 1u) {
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
        if (status == ASTRA_VFS_OK && image->length == (uint32_t)size) {
            image->bytes[image->length] = 0u;
            return ASTRA_VFS_OK;
        }
        release_library_image(image);
        return ASTRA_VFS_ERR_IO;
    }
}

static int ends_with(const char *text, const char *ending)
{
    uint32_t length = (uint32_t)strlen(text);
    uint32_t suffix = (uint32_t)strlen(ending);

    return length >= suffix &&
           memcmp(text + length - suffix, ending, suffix) == 0;
}

static int provider_version(const AstraBundleManifest *manifest,
                            const char *name, uint16_t abi,
                            uint16_t version[3])
{
    int found = 0;

    if (manifest->kind != ASTRA_BUNDLE_KIT) return 0;
    for (uint32_t at = 0u; at < manifest->provide_count; ++at) {
        const AstraBundleLibrary *provider = &manifest->provides[at];
        uint16_t candidate[3] = {provider->version.major,
                                 provider->version.minor,
                                 provider->version.patch};

        if (provider->abi != abi || !same(provider->name, name) ||
            (found && !newer(candidate, version))) continue;
        for (uint32_t part = 0u; part < 3u; ++part)
            version[part] = candidate[part];
        found = 1;
    }
    return found;
}

#if defined(ASTRA_VFS_PROCESS_TEST)
int astra_vfs_process_test_provider(const AstraBundleManifest *manifest,
                                    const char *name, uint16_t abi,
                                    uint16_t version[3])
{
    return provider_version(manifest, name, abi, version);
}
#endif

/*
 * What one sweep of LIBS: found.
 *
 * resolve_library used to sweep LIBS: and parse every kit manifest on each
 * call, keeping only the one provider it had been asked about. A process that
 * opens three libraries -- which the Terminal does, and every GUI program will
 * -- therefore paid for three sweeps, and a sweep is a readdir round trip per
 * entry plus an open/read/close per kit manifest. Every one of those is a
 * synchronous message to the storage service and back, and a round trip costs
 * milliseconds, so the repeated sweeps were most of a program's startup.
 *
 * A sweep sees every provider in every kit whether or not it was asked about
 * them, so recording them all makes the second and later resolutions free.
 */
#define PROCESS_KIT_MAX 8u
#define PROCESS_KIT_PROVIDER_MAX 32u

typedef struct KitProvider {
    char name[ASTRA_BUNDLE_LIBRARY_NAME_MAX];
    uint16_t version[3];
    uint16_t abi;
    uint8_t kit;
} KitProvider;

static char kit_names[PROCESS_KIT_MAX][ASTRA_VFS_NAME_MAX];
static KitProvider kit_providers[PROCESS_KIT_PROVIDER_MAX];
static uint32_t kit_count;
static uint32_t kit_provider_count;
/* Set only when the table holds everything the sweep saw. A partial table
 * would resolve a library to the wrong kit or miss it entirely, so an overflow
 * is not cached and the next call sweeps again -- slow, and still correct. */
static uint8_t kit_table_complete;

static int kit_record(const char *kit, const AstraBundleManifest *manifest)
{
    uint32_t slot = kit_count;

    if (manifest->kind != ASTRA_BUNDLE_KIT || manifest->provide_count == 0u)
        return 1;
    if (slot == PROCESS_KIT_MAX)
        return 0;
    kit_names[slot][0] = '\0';
    if (!append(kit_names[slot], sizeof(kit_names[slot]), kit))
        return 0;
    for (uint32_t at = 0u; at < manifest->provide_count; ++at) {
        const AstraBundleLibrary *provider = &manifest->provides[at];
        KitProvider *entry;

        if (kit_provider_count == PROCESS_KIT_PROVIDER_MAX)
            return 0;
        entry = &kit_providers[kit_provider_count];
        entry->name[0] = '\0';
        if (!append(entry->name, sizeof(entry->name), provider->name))
            return 0;
        entry->version[0] = provider->version.major;
        entry->version[1] = provider->version.minor;
        entry->version[2] = provider->version.patch;
        entry->abi = provider->abi;
        entry->kit = (uint8_t)slot;
        ++kit_provider_count;
    }
    ++kit_count;
    return 1;
}

static int resolve_cached(const char *name, uint16_t minimum,
                          uint16_t selected[3], char *selected_kit,
                          uint32_t capacity)
{
    int found = 0;

    for (uint32_t at = 0u; at < kit_provider_count; ++at) {
        const KitProvider *provider = &kit_providers[at];
        uint16_t candidate[3] = {provider->version[0], provider->version[1],
                                 provider->version[2]};

        if (provider->abi != minimum || !same(provider->name, name) ||
            (found && !newer(candidate, selected))) continue;
        for (uint32_t part = 0u; part < 3u; ++part)
            selected[part] = candidate[part];
        selected_kit[0] = '\0';
        if (!append(selected_kit, capacity, kit_names[provider->kit]))
            return 0;
        found = 1;
    }
    return found;
}

static int resolve_library(const char *name, uint16_t minimum,
                           char *path, uint32_t capacity)
{
    uint16_t selected[3] = {0u, 0u, 0u};
    char selected_kit[ASTRA_VFS_NAME_MAX] = {0};
    int found = 0;
    int complete = 1;

    path[0] = '\0';
    if (kit_table_complete != 0u) {
        found = resolve_cached(name, minimum, selected, selected_kit,
                               sizeof(selected_kit));
        goto build;
    }
    kit_count = 0u;
    kit_provider_count = 0u;
    if (!append(path, capacity, "LIBS:"))
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
            /*
             * Batched, and static rather than automatic: a user thread gets one
             * 4 KiB stack, and this is 528 bytes. One entry per exchange made a
             * directory scan cost a round trip per name, and a round trip to a
             * service is milliseconds -- the sweep of LIBS: was most of it.
             * A peer too old to batch answers one entry at a time by itself.
             */
            static AstraVfsDirEntry batch[8];
            uint32_t count = 0u;
            uint64_t next = 0u;

            status = astra_vfs_readdir_batch(
                client, wire, cursor, batch,
                (uint32_t)(sizeof(batch) / sizeof(batch[0])), &count, &next);
            if (status != ASTRA_VFS_OK || count == 0u)
                break;
            for (uint32_t item = 0u; item < count; ++item) {
            const char *entry = batch[item].name;
            uint16_t candidate[3] = {0u, 0u, 0u};

            if (batch[item].kind == ASTRA_VFS_KIND_DIRECTORY &&
                ends_with(entry, ".kit")) {
                LibraryImage image;
                AstraBundleManifest manifest;
                char manifest_path[ASTRA_VFS_PATH_MAX] = "LIBS:";
                uint32_t line = 0u;

                if (append(manifest_path, sizeof(manifest_path), entry) &&
                    append(manifest_path, sizeof(manifest_path), "/manifest") &&
                    read_file(manifest_path, &image) == ASTRA_VFS_OK) {
                    if (astra_bundle_manifest_parse(
                            (char *)image.bytes, image.length, &manifest,
                            &line) == ASTRA_BUNDLE_OK) {
                        if (!kit_record(entry, &manifest))
                            complete = 0;
                        if (provider_version(&manifest, name, minimum,
                                             candidate) &&
                            (!found || newer(candidate, selected))) {
                            for (uint32_t part = 0u; part < 3u; ++part)
                                selected[part] = candidate[part];
                            selected_kit[0] = '\0';
                            if (!append(selected_kit, sizeof(selected_kit),
                                        entry)) {
                                release_library_image(&image);
                                return 0;
                            }
                            found = 1;
                        }
                    }
                    release_library_image(&image);
                }
            }
            }
            if (next == 0u)
                break;
            cursor = next;
        }
    }
    kit_table_complete = complete != 0 ? 1u : 0u;

build:
    path[0] = '\0';
    if (!found || !append(path, capacity, "LIBS:") ||
        !append(path, capacity, selected_kit) ||
        !append(path, capacity, "/libraries/") ||
        !append(path, capacity, name) ||
        !append(path, capacity, "/abi-") ||
        !append_number(path, capacity, minimum) ||
        !append(path, capacity, "/") ||
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
    astra_process_vfs_close();
    (void)memset(clients, 0, sizeof(clients));
    (void)memset(open_libraries, 0, sizeof(open_libraries));
    /* The table describes the namespace this seeding just replaced. */
    kit_table_complete = 0u;
    kit_count = 0u;
    kit_provider_count = 0u;
    for (uint32_t index = 0u; index < assigns.count; ++index) {
        uint32_t slot;

        for (slot = 0u; slot < client_count; ++slot)
            if (clients[slot].handle == assigns.entries[index].handle)
                break;
        if (slot != client_count)
            continue;
        if (client_count == PROCESS_VFS_CLIENT_MAX) {
            astra_process_vfs_close();
            return ASTRA_VFS_ERR_LIMIT;
        }
        clients[client_count].handle = assigns.entries[index].handle;
        if (astra_vfs_port_connect(&clients[client_count].client,
                                   clients[client_count].handle) !=
            ASTRA_VFS_OK) {
            clients[client_count].handle = 0u;
            astra_process_vfs_close();
            return ASTRA_VFS_ERR_IO;
        }
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

/*
 * How many callers hold one open.
 *
 * The namespace, its clients and their transfer areas are per *process*, not
 * per caller: `astra_process_vfs_init` re-seeds file-scope state, so a second
 * caller opening one would tear the first one's clients out from under it in
 * the middle of a read. That never happened while a command was the only
 * thing asking, and it is exactly what the POSIX layer does the moment a
 * program calls `open()` in a command that also uses the Astra API directly.
 * So the seeding happens once and the teardown happens when the last holder
 * lets go.
 */
static uint32_t filesystem_opens;

uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *startup)
{
    uint32_t status;

    if (filesystem == NULL || filesystem->handle != NULL ||
        filesystem->library != NULL)
        return ASTRA_VFS_ERR_INVALID;
    status = filesystem_opens != 0u ? ASTRA_VFS_OK :
                                      astra_process_vfs_init(startup);
    if (status != ASTRA_VFS_OK)
        return status;
    /*
     * Counted from here, not after the last step: every failure below unwinds
     * through `close`, and it can only take the namespace down at the right
     * moment if it is already counted as up.
     */
    ++filesystem_opens;
    filesystem->handle = OpenLibrary(ASTRA_FILESYSTEM_LIBRARY_NAME,
                                     ASTRA_FILESYSTEM_LIBRARY_VERSION);
    if (filesystem->handle == NULL) {
        astra_process_filesystem_close(filesystem);
        return ASTRA_VFS_ERR_NOT_FOUND;
    }
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
    if (status != ASTRA_VFS_OK) {
        astra_process_filesystem_close(filesystem);
        return status;
    }
    return status;
}

/*
 * A word somebody typed, to a path this process can resolve.
 *
 * `ASSIGN:path` is already absolute and comes back unchanged. A bare name is
 * relative to CWD:, which is where the launcher says the prompt is standing,
 * and to WORK: when nothing granted a CWD: -- a launcher that says nothing
 * about where it is still gets the writable volume rather than a refusal.
 *
 * Every command that takes a path needs exactly this, and each one that grew
 * its own copy grew a slightly different one: `ls` did not qualify at all, so
 * `ls probe` answered INVALID for a directory `mkdir probe` had just made.
 */
uint32_t astra_process_path(const AstraProcessFilesystem *filesystem,
                            const char *typed, char *out, uint32_t capacity)
{
    const char *assign;

    if (filesystem == NULL || filesystem->library == NULL)
        return ASTRA_VFS_ERR_INVALID;
    assign = filesystem->library->assign_lookup(
                 astra_process_vfs_assigns(), "CWD") != NULL ? "CWD" : "WORK";
    return filesystem->library->qualify(assign, "", typed, out, capacity);
}

uint32_t astra_process_read_file(AstraProcessFilesystem *filesystem,
                                 const char *path, void *bytes,
                                 uint32_t capacity, uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status;

    if (filesystem == NULL || filesystem->library == NULL || path == NULL ||
        bytes == NULL || length == NULL) return ASTRA_VFS_ERR_INVALID;
    *length = 0u;
    /*
     * One round trip when the service can do it. This is the read every
     * program uses -- a manifest, an icon, a data file -- and open, size,
     * read, close was four exchanges for one intent.
     */
    {
        const AstraAssign *assign = NULL;
        char wire[ASTRA_VFS_PATH_MAX];

        if (astra_assign_resolve(&assigns, path, ASTRA_RIGHT_READ, 0u, wire,
                                 sizeof(wire), &assign) == ASTRA_VFS_OK) {
            AstraVfsClient *client = astra_process_vfs_client_for(assign);
            const uint8_t *whole = NULL;
            uint32_t moved = 0u;
            uint64_t node_size = 0u;

            if (client != NULL &&
                astra_vfs_port_read_path(client, wire, &whole, &moved,
                                         &node_size) == ASTRA_VFS_OK &&
                moved == (uint32_t)node_size && moved <= capacity) {
                memcpy(bytes, whole, moved);
                *length = moved;
                return ASTRA_VFS_OK;
            }
        }
    }
    status = filesystem->library->open(&filesystem->filesystem, path,
                                       ASTRA_VFS_OPEN_READ, &file);
    if (status == ASTRA_VFS_OK)
        status = filesystem->library->file_info(&file, &info);
    if (status == ASTRA_VFS_OK && info.byte_size > capacity)
        status = ASTRA_VFS_ERR_LIMIT;
    while (status == ASTRA_VFS_OK && *length < info.byte_size) {
        uint32_t moved = 0u;

        status = filesystem->library->read(
            &file, (uint8_t *)bytes + *length,
            (uint32_t)info.byte_size - *length, &moved);
        if (status != ASTRA_VFS_OK || moved == 0u)
            break;
        *length += moved;
    }
    if (file._private_file != ASTRA_VFS_FILE_INVALID)
        (void)filesystem->library->close(&file);
    return status == ASTRA_VFS_OK && *length == info.byte_size ?
        ASTRA_VFS_OK : (status == ASTRA_VFS_OK ? ASTRA_VFS_ERR_IO : status);
}

void astra_process_filesystem_close(AstraProcessFilesystem *filesystem)
{
    if (filesystem == NULL)
        return;
    if (filesystem->library != NULL)
        filesystem->library->detach(&filesystem->filesystem);
    CloseLibrary(filesystem->handle);
    /* The last holder takes the namespace down; an earlier one must not. */
    if (filesystem_opens != 0u && --filesystem_opens == 0u)
        astra_process_vfs_close();
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
    if (!resolve_library(name, version, path, sizeof(path))) {
        return NULL;
    }
    if (read_file(path, &image) != ASTRA_VFS_OK) {
        return NULL;
    }
    status = astra_library_load(image.bytes, image.length, name, version, 0u,
                                &loaded);
    release_library_image(&image);
    if (status != ASTRA_SYSCALL_OK) {
        return NULL;
    }
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
