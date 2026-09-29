/** @file application_catalog.c
 *  @brief Installed applications, one directory scan and one manifest read
 *  per bundle (see astra/application_catalog.h).
 */

#include <astra/application_catalog.h>
#include <astra/filesystem_library.h>
#include <astra/runtime.h>
#include <astra/string.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { CATALOG_BATCH = 8u };

static int bundle_name(const char *name)
{
    return name[0] != '.' && strlen(name) > 4u &&
           astra_string_has_suffix(name, ".app");
}

/* Display-name order, case-insensitive for ASCII; the bundle path breaks
   ties so the order never depends on directory order. */
static int before(const AstraApplicationEntry *left,
                  const AstraApplicationEntry *right)
{
    int order = astra_string_compare_ascii_nocase(left->name, right->name);

    return order != 0 ? order < 0 : strcmp(left->bundle, right->bundle) < 0;
}

/* @p out = @p first + @p second + @p third, whole or not at all. */
static int join(char *out, uint32_t capacity, const char *first,
                const char *second, const char *third)
{
    AstraString text;

    astra_string_init(&text, out, capacity);
    return astra_string_append(&text, first) &&
           astra_string_append(&text, second) &&
           astra_string_append(&text, third);
}

static int reserve(AstraApplicationCatalog *catalog)
{
    AstraApplicationEntry *grown;
    uint32_t capacity = catalog->capacity == 0u ? 8u : catalog->capacity * 2u;

    if (capacity <= catalog->capacity ||
        capacity > UINT32_MAX / (uint32_t)sizeof(*grown))
        return 0;
    grown = astra_runtime_reallocate(catalog->entries,
                                     (size_t)capacity * sizeof(*grown));
    if (grown == NULL)
        return 0;
    catalog->entries = grown;
    catalog->capacity = capacity;
    return 1;
}

/* 1 added (or already present), 0 skipped, -1 out of memory. */
static int add_bundle(AstraProcessFilesystem *filesystem,
                      const char *directory, const char *name,
                      AstraApplicationCatalog *catalog)
{
    AstraBundleManifest manifest = ASTRA_BUNDLE_MANIFEST_INIT;
    AstraApplicationEntry entry;
    char path[ASTRA_APPLICATION_PATH_MAX];
    char *text = NULL;
    uint32_t length = 0u;
    uint32_t line = 0u;
    int added = 0;

    (void)memset(&entry, 0, sizeof(entry));
    if (!join(entry.bundle, sizeof(entry.bundle), directory, "/", name))
        return 0;
    /* A union assign can list one name from several members; the first is
       the one a launch resolves, so later copies are not applications. */
    for (uint32_t at = 0u; at < catalog->count; ++at)
        if (strcmp(catalog->entries[at].bundle, entry.bundle) == 0)
            return 1;
    if (!join(path, sizeof(path), entry.bundle, "/manifest", "") ||
        astra_process_read_file_alloc(filesystem, path, (void **)&text,
                                      &length) != ASTRA_VFS_OK)
        return 0;
    if (astra_bundle_manifest_parse(text, length, &manifest, &line) ==
            ASTRA_BUNDLE_OK &&
        manifest.kind == ASTRA_BUNDLE_APPLICATION &&
        astra_string_copy(entry.id, sizeof(entry.id), manifest.id) &&
        astra_string_copy(entry.name, sizeof(entry.name),
                          manifest.name[0] != '\0' ? manifest.name : name) &&
        join(entry.icon, sizeof(entry.icon), entry.bundle, "/",
             manifest.icon)) {
        entry.version = manifest.version;
        added = 1;
    }
    astra_bundle_manifest_destroy(&manifest);
    astra_runtime_deallocate(text);
    if (!added)
        return 0;
    if (catalog->count == catalog->capacity && !reserve(catalog))
        return -1;
    {
        uint32_t at = catalog->count;

        while (at > 0u && before(&entry, &catalog->entries[at - 1u])) {
            catalog->entries[at] = catalog->entries[at - 1u];
            --at;
        }
        catalog->entries[at] = entry;
    }
    ++catalog->count;
    return 1;
}

void astra_application_catalog_destroy(AstraApplicationCatalog *catalog)
{
    if (catalog == NULL)
        return;
    astra_runtime_deallocate(catalog->entries);
    *catalog = (AstraApplicationCatalog)ASTRA_APPLICATION_CATALOG_INIT;
}

uint32_t astra_application_catalog_load(AstraProcessFilesystem *filesystem,
                                        const char *directory,
                                        AstraApplicationCatalog *catalog)
{
    AstraDirectory scan = ASTRA_DIRECTORY_INIT;
    AstraDirectoryEntry batch[CATALOG_BATCH];
    uint32_t status;

    if (catalog == NULL)
        return ASTRA_VFS_ERR_INVALID;
    astra_application_catalog_destroy(catalog);
    if (filesystem == NULL || directory == NULL || directory[0] == '\0')
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_directory_open(&filesystem->filesystem,
                                             directory, &scan);
    while (status == ASTRA_VFS_OK) {
        uint32_t count = 0u;

        status = astra_filesystem_directory_read(&scan, batch, CATALOG_BATCH,
                                                 &count);
        if (status != ASTRA_VFS_OK || count == 0u)
            break;
        for (uint32_t at = 0u; at < count; ++at) {
            int result;

            if (batch[at].kind != ASTRA_VFS_KIND_DIRECTORY ||
                !bundle_name(batch[at].name))
                continue;
            result = add_bundle(filesystem, directory, batch[at].name,
                                catalog);
            if (result < 0) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            if (result == 0)
                ++catalog->skipped;
        }
    }
    astra_filesystem_directory_close(&scan);
    if (status != ASTRA_VFS_OK)
        astra_application_catalog_destroy(catalog);
    return status;
}
