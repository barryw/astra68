#define _GNU_SOURCE
#include "astra_storage_names.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void damage(const char *directory, const char *name)
{
    char path[256];
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    assert(fd >= 0);
    assert(pwrite(fd, "X", 1u, 0) == 1);
    assert(close(fd) == 0);
}

int main(void)
{
    char directory[] = "/tmp/astra-storage-names.XXXXXX";
    assert(mkdtemp(directory) != NULL);
    AstraStorageCatalog catalog;
    assert(astra_storage_catalog_init(&catalog, 1u << 20) == 0);
    AstraStorageNames names = ASTRA_STORAGE_NAMES_INIT;
    assert(astra_storage_names_open(&names, directory) == 0);
    uint32_t slot;
    assert(astra_storage_names_select(&names, "s:drive:1", &catalog, &slot) == 0 && slot == 0u);
    assert(astra_storage_names_select(&names, "s:drive:2", &catalog, &slot) == 0 && slot == 1u);
    astra_storage_names_close(&names);

    assert(astra_storage_names_open(&names, directory) == 0);
    assert(astra_storage_names_select(&names, "s:drive:2", &catalog, &slot) == 0 && slot == 1u);
    assert(astra_storage_names_select(&names, "s:drive:1", &catalog, &slot) == 0 && slot == 0u);
    uint64_t device, medium, source, mount;
    assert(astra_storage_device(&catalog, ASTRA_VOLUME_USB, 0u, 0u, &device) == 0);
    assert(astra_storage_medium(&catalog, device, 1u, &medium) == 0);
    assert(astra_storage_source(&catalog, medium, 1u, 0u, 0u, 0u, 0u, &source) == 0);
    assert(astra_storage_probe_result(&catalog, source, 1u, ASTRA_VOLUME_EXT4,
        "", "", "", 0, 0) == 0);
    assert(astra_storage_mount_begin(&catalog, source, 1u, 0u, 0u, 0u, 0u, &mount) == 0);
    assert(strcmp(astra_storage_find(&catalog, mount)->name, "/usb0") == 0);
    assert(astra_storage_names_select(&names, "s:drive:1", &catalog, &slot) == EBUSY);
    assert(astra_storage_names_select(&names, "s:drive:3", &catalog, &slot) == 0 && slot == 2u);
    astra_storage_names_close(&names);

    damage(directory, "volume-names");
    assert(astra_storage_names_open(&names, directory) == 0);
    assert(astra_storage_names_select(&names, "s:drive:2", &catalog, &slot) == 0 && slot == 1u);
    astra_storage_names_close(&names);
    damage(directory, "volume-names.prev");
    assert(astra_storage_names_open(&names, directory) == EINVAL);
    assert(astra_storage_names_select(&names, "s:drive:4", &catalog, &slot) == EINVAL);
    astra_storage_names_close(&names);

    char path[256];
    assert(snprintf(path, sizeof(path), "%s/volume-names", directory) < (int)sizeof(path));
    assert(unlink(path) == 0);
    assert(snprintf(path, sizeof(path), "%s/volume-names.prev", directory) < (int)sizeof(path));
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    astra_storage_catalog_destroy(&catalog);
    puts("storage names PASS");
    return 0;
}
