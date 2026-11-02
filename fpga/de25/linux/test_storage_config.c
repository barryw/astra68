#define _GNU_SOURCE
#include "astra_storage_config.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static void put(int directory, const char *name, const char *text)
{
    int file = openat(directory, name, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    assert(file >= 0 && write(file, text, strlen(text)) == (ssize_t)strlen(text));
    close(file);
}

static void expect(int directory, const char *name, const char *text)
{
    char bytes[128] = {0};
    int file = openat(directory, name, O_RDONLY | O_CLOEXEC);
    assert(file >= 0 && read(file, bytes, sizeof(bytes)) == (ssize_t)strlen(text));
    assert(memcmp(bytes, text, strlen(text)) == 0);
    close(file);
}

static void expect_empty(int directory)
{
    int scan = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(scan >= 0);
    DIR *entries = fdopendir(scan);
    assert(entries != NULL);
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL)
        assert(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);
    closedir(entries);
}

int main(void)
{
    char location[] = "/tmp/astra-config-migrate.XXXXXX";
    assert(mkdtemp(location) != NULL);
    int root = open(location, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root >= 0);
    assert(mkdirat(root, "system", 0700) == 0 && mkdirat(root, "private", 0700) == 0 &&
           mkdirat(root, "private_conflict", 0700) == 0);
    int system = openat(root, "system", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int private = openat(root, "private", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(system >= 0 && private >= 0 && mkdirat(system, "config", 0700) == 0);
    int legacy = openat(system, "config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(legacy >= 0);
    assert(mkdirat(legacy, "services", 0700) == 0);
    put(legacy, "original", "user setting");
    int original = openat(legacy, "original", O_RDONLY | O_CLOEXEC);
    assert(original >= 0);
    assert(fsetxattr(original, "user.astra", "metadata", 8u, 0) == 0);
    close(original);
    int services = openat(legacy, "services", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(services >= 0);
    put(services, "hostfs", "limit 64");
    assert(symlinkat("original", legacy, "shortcut") == 0);
    int conflict = openat(root, "private_conflict", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(conflict >= 0 && mkdirat(conflict, "config", 0700) == 0);
    int conflicting_config = openat(conflict, "config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(conflicting_config >= 0);
    put(conflicting_config, "original", "different user setting");
    assert(astra_storage_config_migrate(system, conflict) == EEXIST);
    expect(legacy, "original", "user setting");
    assert(unlinkat(conflicting_config, "original", 0) == 0);
    int copied_services = openat(conflicting_config, "services", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (copied_services >= 0) {
        assert(unlinkat(copied_services, "hostfs", 0) == 0);
        close(copied_services);
        assert(unlinkat(conflicting_config, "services", AT_REMOVEDIR) == 0);
    }
    if (unlinkat(conflicting_config, "shortcut", 0) != 0) assert(errno == ENOENT);
    close(conflicting_config);
    assert(unlinkat(conflict, "config", AT_REMOVEDIR) == 0);
    close(conflict);
    assert(unlinkat(root, "private_conflict", AT_REMOVEDIR) == 0);
    assert(astra_storage_config_migrate(system, private) == 0);
    expect_empty(legacy);
    int migrated = openat(private, "config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(migrated >= 0);
    expect(migrated, "original", "user setting");
    original = openat(migrated, "original", O_RDONLY | O_CLOEXEC);
    char attribute[16] = {0};
    assert(original >= 0 && fgetxattr(original, "user.astra", attribute, sizeof(attribute)) == 8);
    assert(memcmp(attribute, "metadata", 8u) == 0);
    close(original);
    int nested = openat(migrated, "services", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(nested >= 0);
    expect(nested, "hostfs", "limit 64");
    char target[32] = {0};
    assert(readlinkat(migrated, "shortcut", target, sizeof(target)) == 8);
    assert(strcmp(target, "original") == 0);
    put(legacy, "original", "shipped default");
    put(legacy, "new", "new default");
    assert(astra_storage_config_migrate(system, private) == 0);
    expect_empty(legacy);
    expect(migrated, "original", "user setting");
    expect(migrated, "new", "new default");
    assert(unlinkat(migrated, "shortcut", 0) == 0);
    assert(unlinkat(migrated, "original", 0) == 0);
    assert(unlinkat(migrated, "new", 0) == 0);
    assert(unlinkat(nested, "hostfs", 0) == 0);
    close(nested);
    assert(unlinkat(migrated, "services", AT_REMOVEDIR) == 0);
    close(migrated);
    assert(unlinkat(private, "config", AT_REMOVEDIR) == 0);
    assert(unlinkat(private, ".astra-config-migrated", 0) == 0);
    close(services);
    close(legacy);
    assert(unlinkat(system, "config", AT_REMOVEDIR) == 0);
    close(private);
    close(system);
    assert(unlinkat(root, "private", AT_REMOVEDIR) == 0);
    assert(unlinkat(root, "system", AT_REMOVEDIR) == 0);
    assert(mkdirat(root, "empty_system", 0700) == 0 &&
           mkdirat(root, "empty_private", 0700) == 0);
    system = openat(root, "empty_system", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    private = openat(root, "empty_private", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(system >= 0 && private >= 0);
    assert(astra_storage_config_migrate(system, private) == 0);
    migrated = openat(private, "config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(migrated >= 0);
    put(migrated, "choice", "saved setting");
    assert(mkdirat(system, "config", 0700) == 0);
    legacy = openat(system, "config", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(legacy >= 0);
    put(legacy, "choice", "new release default");
    assert(astra_storage_config_migrate(system, private) == 0);
    expect(migrated, "choice", "saved setting");
    expect_empty(legacy);
    assert(unlinkat(migrated, "choice", 0) == 0);
    close(migrated);
    assert(unlinkat(private, "config", AT_REMOVEDIR) == 0);
    assert(unlinkat(private, ".astra-config-migrated", 0) == 0);
    close(legacy);
    assert(unlinkat(system, "config", AT_REMOVEDIR) == 0);
    close(private);
    close(system);
    assert(unlinkat(root, "empty_private", AT_REMOVEDIR) == 0);
    assert(unlinkat(root, "empty_system", AT_REMOVEDIR) == 0);
    close(root);
    assert(rmdir(location) == 0);
    puts("config migration: private copy, recursive data, restart defaults, public removal PASS");
    return 0;
}
