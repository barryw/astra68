// The application catalog against a fake /apps: ordering, skipping,
// duplicates from union members, icon paths, and allocation failure.

#include <astra/application_catalog.h>
#include <astra/runtime.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FakeNode {
    const char *name;
    uint16_t kind;
    const char *manifest; /* NULL: unreadable */
} FakeNode;

static const FakeNode *nodes;
static uint32_t node_count;
static uint32_t cursor;
static uint32_t fail_open;
static uint32_t fail_after_allocations = UINT32_MAX;
static uint32_t allocations;

void *astra_runtime_allocate(size_t size)
{
    return astra_runtime_reallocate(NULL, size);
}

void *astra_runtime_reallocate(void *pointer, size_t size)
{
    if (allocations++ >= fail_after_allocations)
        return NULL;
    return realloc(pointer, size);
}

void astra_runtime_deallocate(void *pointer)
{
    free(pointer);
}

uint32_t astra_filesystem_directory_open(AstraFilesystem *filesystem,
                                         const char *path,
                                         AstraDirectory *directory)
{
    (void)filesystem;
    (void)directory;
    assert(strcmp(path, "/apps") == 0);
    cursor = 0u;
    return fail_open ? ASTRA_VFS_ERR_NOT_FOUND : ASTRA_VFS_OK;
}

uint32_t astra_filesystem_directory_read(AstraDirectory *directory,
                                         AstraDirectoryEntry *entries,
                                         uint32_t capacity, uint32_t *count)
{
    (void)directory;
    *count = 0u;
    while (cursor < node_count && *count < capacity) {
        AstraDirectoryEntry *entry = &entries[(*count)++];

        memset(entry, 0, sizeof(*entry));
        strcpy(entry->name, nodes[cursor].name);
        entry->kind = nodes[cursor].kind;
        ++cursor;
    }
    return ASTRA_VFS_OK;
}

void astra_filesystem_directory_close(AstraDirectory *directory)
{
    (void)directory;
}

uint32_t astra_process_read_file_alloc(AstraProcessFilesystem *filesystem,
                                       const char *path, void **bytes,
                                       uint32_t *length)
{
    (void)filesystem;
    for (uint32_t at = 0u; at < node_count; ++at) {
        char expected[256];

        snprintf(expected, sizeof(expected), "/apps/%s/manifest",
                 nodes[at].name);
        if (strcmp(expected, path) == 0 && nodes[at].manifest != NULL) {
            size_t size = strlen(nodes[at].manifest);
            char *copy = astra_runtime_allocate(size + 1u);

            if (copy == NULL)
                return ASTRA_VFS_ERR_LIMIT;
            memcpy(copy, nodes[at].manifest, size + 1u);
            *bytes = copy;
            *length = (uint32_t)size;
            return ASTRA_VFS_OK;
        }
    }
    return ASTRA_VFS_ERR_NOT_FOUND;
}

#define APP(id, name, icon) \
    "astra-bundle 1\nkind application\nid " id "\nname \"" name "\"\n" \
    "version 1.2.3\nexecutable bin/m68k-68040/X\nicon " icon "\n"

static void catalog_orders_and_skips(void)
{
    static const FakeNode apps[] = {
        {"zeta.app", ASTRA_VFS_KIND_DIRECTORY,
         APP("org.z", "zeta", "resources/Z.aicon")},
        {"Terminal.app", ASTRA_VFS_KIND_DIRECTORY,
         APP("org.t", "Terminal", "resources/Terminal.aicon")},
        {"Broken.app", ASTRA_VFS_KIND_DIRECTORY, "astra-bundle 1\nkind\n"},
        {"Missing.app", ASTRA_VFS_KIND_DIRECTORY, NULL},
        {"Kit.app", ASTRA_VFS_KIND_DIRECTORY,
         "astra-bundle 1\nkind kit\nid org.k\nname \"K\"\n"
         "version 1.0.0\n"},
        {"Alpha.app", ASTRA_VFS_KIND_DIRECTORY, APP("org.a", "alpha", "i.aicon")},
        {"notes.txt", ASTRA_VFS_KIND_FILE, NULL},
        {"File.app", ASTRA_VFS_KIND_FILE, NULL},
        {".hidden.app", ASTRA_VFS_KIND_DIRECTORY,
         APP("org.h", "hidden", "i.aicon")},
        /* A later union member repeating a name adds nothing. */
        {"Terminal.app", ASTRA_VFS_KIND_DIRECTORY,
         APP("org.t2", "Terminal Two", "i.aicon")},
    };
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraApplicationCatalog catalog = ASTRA_APPLICATION_CATALOG_INIT;

    nodes = apps;
    node_count = sizeof(apps) / sizeof(apps[0]);
    assert(astra_application_catalog_load(&filesystem, "/apps", &catalog) ==
           ASTRA_VFS_OK);
    assert(catalog.count == 3u);
    assert(catalog.skipped == 3u); /* Broken, Missing, Kit */
    assert(strcmp(catalog.entries[0].name, "alpha") == 0);
    assert(strcmp(catalog.entries[1].name, "Terminal") == 0);
    assert(strcmp(catalog.entries[2].name, "zeta") == 0);
    assert(strcmp(catalog.entries[1].bundle, "/apps/Terminal.app") == 0);
    assert(strcmp(catalog.entries[1].id, "org.t") == 0);
    assert(strcmp(catalog.entries[1].icon,
                  "/apps/Terminal.app/resources/Terminal.aicon") == 0);
    assert(strcmp(catalog.entries[0].icon, "/apps/Alpha.app/i.aicon") == 0);
    assert(catalog.entries[2].version.major == 1u &&
           catalog.entries[2].version.minor == 2u &&
           catalog.entries[2].version.patch == 3u);

    /* Loading again replaces the previous snapshot. */
    assert(astra_application_catalog_load(&filesystem, "/apps", &catalog) ==
           ASTRA_VFS_OK && catalog.count == 3u);
    astra_application_catalog_destroy(&catalog);
    assert(catalog.entries == NULL && catalog.count == 0u);
}

static void catalog_failures(void)
{
    static const FakeNode many[] = {
        {"A.app", ASTRA_VFS_KIND_DIRECTORY, APP("a", "A", "i.aicon")},
        {"B.app", ASTRA_VFS_KIND_DIRECTORY, APP("b", "B", "i.aicon")},
        {"C.app", ASTRA_VFS_KIND_DIRECTORY, APP("c", "C", "i.aicon")},
        {"D.app", ASTRA_VFS_KIND_DIRECTORY, APP("d", "D", "i.aicon")},
        {"E.app", ASTRA_VFS_KIND_DIRECTORY, APP("e", "E", "i.aicon")},
        {"F.app", ASTRA_VFS_KIND_DIRECTORY, APP("f", "F", "i.aicon")},
        {"G.app", ASTRA_VFS_KIND_DIRECTORY, APP("g", "G", "i.aicon")},
        {"H.app", ASTRA_VFS_KIND_DIRECTORY, APP("h", "H", "i.aicon")},
        {"I.app", ASTRA_VFS_KIND_DIRECTORY, APP("i", "I", "i.aicon")},
        {"J.app", ASTRA_VFS_KIND_DIRECTORY, APP("j", "J", "i.aicon")},
    };
    AstraProcessFilesystem filesystem = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraApplicationCatalog catalog = ASTRA_APPLICATION_CATALOG_INIT;

    nodes = many;
    node_count = sizeof(many) / sizeof(many[0]);
    /* More entries than one directory batch and one growth step. */
    assert(astra_application_catalog_load(&filesystem, "/apps", &catalog) ==
           ASTRA_VFS_OK && catalog.count == 10u);
    for (uint32_t at = 1u; at < catalog.count; ++at)
        assert(strcmp(catalog.entries[at - 1u].name,
                      catalog.entries[at].name) < 0);
    /* Growth failing mid-scan fails the scan and leaves nothing. */
    for (uint32_t limit = 0u; limit < 40u; ++limit) {
        uint32_t status;

        allocations = 0u;
        fail_after_allocations = limit;
        status = astra_application_catalog_load(&filesystem, "/apps",
                                                &catalog);
        fail_after_allocations = UINT32_MAX;
        assert(status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_LIMIT);
        assert(status == ASTRA_VFS_OK ?
               catalog.count + catalog.skipped == 10u :
               catalog.entries == NULL && catalog.count == 0u);
    }
    astra_application_catalog_destroy(&catalog);
    fail_open = 1u;
    assert(astra_application_catalog_load(&filesystem, "/apps", &catalog) ==
           ASTRA_VFS_ERR_NOT_FOUND && catalog.count == 0u);
    fail_open = 0u;
    assert(astra_application_catalog_load(NULL, "/apps", &catalog) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_application_catalog_load(&filesystem, "", &catalog) ==
           ASTRA_VFS_ERR_INVALID);
    assert(astra_application_catalog_load(&filesystem, "/apps", NULL) ==
           ASTRA_VFS_ERR_INVALID);
    astra_application_catalog_destroy(NULL);
}

int main(void)
{
    catalog_orders_and_skips();
    catalog_failures();
    puts("application catalog tests passed");
    return 0;
}
