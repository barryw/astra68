#define _GNU_SOURCE

#include <astra/bundle.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int path_join(char out[PATH_MAX], const char *left, const char *right)
{
    int count = snprintf(out, PATH_MAX, "%s/%s", left, right);
    return count > 0 && count < PATH_MAX;
}

static int read_file(const char *path, uint8_t **bytes, uint32_t *length,
                     uint32_t limit)
{
    struct stat info;
    FILE *file;

    *bytes = NULL;
    *length = 0u;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uint64_t)info.st_size > limit) return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    *bytes = malloc((size_t)info.st_size + 1u);
    if (*bytes == NULL || fread(*bytes, 1u, (size_t)info.st_size, file) !=
                          (size_t)info.st_size) {
        free(*bytes);
        *bytes = NULL;
        fclose(file);
        return 0;
    }
    fclose(file);
    (*bytes)[info.st_size] = 0u;
    *length = (uint32_t)info.st_size;
    return 1;
}

static int suffix(const char *path, const char *wanted)
{
    size_t length = strlen(path);
    size_t ending = strlen(wanted);
    while (length != 0u && path[length - 1u] == '/') --length;
    return length >= ending && memcmp(path + length - ending, wanted, ending) == 0;
}

static int kit_payload(const char *path, const AstraBundleLibrary *library)
{
    char relative[PATH_MAX];
    char payload[PATH_MAX];
    struct stat info;
    int count = snprintf(relative, sizeof(relative),
                         "libraries/%s/abi-%u/%u.%u.%u/m68k-68030/%s",
                         library->name, library->abi, library->version.major,
                         library->version.minor, library->version.patch,
                         library->name);

    return count > 0 && count < (int)sizeof(relative) &&
           path_join(payload, path, relative) && stat(payload, &info) == 0 &&
           S_ISREG(info.st_mode);
}

static int validate(const char *path, AstraBundleManifest *manifest,
                    int quiet)
{
    char child[PATH_MAX];
    uint8_t *text;
    uint8_t *icon_bytes;
    uint32_t length;
    uint32_t line;
    AstraAicon icon;
    struct stat info;
    uint32_t status;

    if (!path_join(child, path, "manifest") ||
        !read_file(child, &text, &length, ASTRA_BUNDLE_MANIFEST_MAX)) {
        if (!quiet) fprintf(stderr, "%s: missing readable manifest\n", path);
        return 0;
    }
    status = astra_bundle_manifest_parse((char *)text, length, manifest, &line);
    free(text);
    if (status != ASTRA_BUNDLE_OK) {
        if (!quiet) fprintf(stderr, "%s: invalid manifest at line %u (status %u)\n",
                            path, line, status);
        return 0;
    }
    if ((manifest->kind == ASTRA_BUNDLE_APPLICATION && !suffix(path, ".app")) ||
        (manifest->kind == ASTRA_BUNDLE_KIT && !suffix(path, ".kit"))) {
        if (!quiet) fprintf(stderr, "%s: suffix does not match manifest kind\n", path);
        return 0;
    }
    if (manifest->kind == ASTRA_BUNDLE_KIT) {
        for (uint32_t at = 0u; at < manifest->provide_count; ++at)
            if (!kit_payload(path, &manifest->provides[at])) {
                if (!quiet)
                    fprintf(stderr, "%s: missing payload for %s ABI %u %u.%u.%u\n",
                            path, manifest->provides[at].name,
                            manifest->provides[at].abi,
                            manifest->provides[at].version.major,
                            manifest->provides[at].version.minor,
                            manifest->provides[at].version.patch);
                return 0;
            }
        return 1;
    }
    if (!path_join(child, path, manifest->executable) || stat(child, &info) != 0 ||
        !S_ISREG(info.st_mode)) {
        if (!quiet) fprintf(stderr, "%s: executable is missing\n", path);
        return 0;
    }
    if (!path_join(child, path, manifest->icon) ||
        !read_file(child, &icon_bytes, &length, UINT32_C(1048576))) {
        if (!quiet) fprintf(stderr, "%s: icon is missing\n", path);
        return 0;
    }
    status = astra_aicon_open(icon_bytes, length, &icon);
    free(icon_bytes);
    if (status != ASTRA_BUNDLE_OK) {
        if (!quiet) fprintf(stderr, "%s: invalid .aicon (status %u)\n", path, status);
        return 0;
    }
    return 1;
}

static int copy_file(const char *source, const char *destination, mode_t mode)
{
    uint8_t buffer[65536];
    int input = open(source, O_RDONLY);
    int output;

    if (input < 0) return 0;
    output = open(destination, O_WRONLY | O_CREAT | O_EXCL, mode & 0777);
    if (output < 0) {
        close(input);
        return 0;
    }
    for (;;) {
        ssize_t got = read(input, buffer, sizeof(buffer));
        ssize_t at = 0;
        if (got < 0) goto fail;
        if (got == 0) break;
        while (at < got) {
            ssize_t put = write(output, buffer + at, (size_t)(got - at));
            if (put <= 0) goto fail;
            at += put;
        }
    }
    if (fsync(output) != 0) goto fail;
    close(output);
    close(input);
    return 1;
fail:
    close(output);
    close(input);
    unlink(destination);
    return 0;
}

static int copy_tree(const char *source, const char *destination)
{
    struct stat info;
    DIR *directory;
    struct dirent *entry;

    if (lstat(source, &info) != 0 || S_ISLNK(info.st_mode)) return 0;
    if (S_ISREG(info.st_mode)) return copy_file(source, destination, info.st_mode);
    if (!S_ISDIR(info.st_mode) || mkdir(destination, info.st_mode & 0777) != 0)
        return 0;
    directory = opendir(source);
    if (directory == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        char from[PATH_MAX];
        char to[PATH_MAX];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!path_join(from, source, entry->d_name) ||
            !path_join(to, destination, entry->d_name) || !copy_tree(from, to)) {
            closedir(directory);
            return 0;
        }
    }
    return closedir(directory) == 0;
}

static int remove_tree(const char *path)
{
    struct stat info;
    DIR *directory;
    struct dirent *entry;

    if (lstat(path, &info) != 0 || S_ISLNK(info.st_mode)) return 0;
    if (S_ISREG(info.st_mode)) return unlink(path) == 0;
    if (!S_ISDIR(info.st_mode) || (directory = opendir(path)) == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        char child[PATH_MAX];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!path_join(child, path, entry->d_name) || !remove_tree(child)) {
            closedir(directory);
            return 0;
        }
    }
    if (closedir(directory) != 0) return 0;
    return rmdir(path) == 0;
}

static int copy_bundle(const char *source, const char *destination)
{
    AstraBundleManifest manifest;
    struct stat info;

    if (!validate(source, &manifest, 0) || lstat(destination, &info) == 0 ||
        errno != ENOENT) return 0;
    if (!copy_tree(source, destination) ||
        !validate(destination, &manifest, 0)) {
        (void)remove_tree(destination);
        return 0;
    }
    return 1;
}

static int move_bundle(const char *source, const char *destination)
{
    AstraBundleManifest manifest;

    if (!validate(source, &manifest, 0)) return 0;
    if (renameat2(AT_FDCWD, source, AT_FDCWD, destination,
                  RENAME_NOREPLACE) == 0) return 1;
    if (errno != EXDEV || !copy_bundle(source, destination)) return 0;
    if (!remove_tree(source)) {
        fprintf(stderr, "copied but could not remove source: %s\n", source);
        return 0;
    }
    return 1;
}

static const char *base_name(const char *path)
{
    const char *base = strrchr(path, '/');
    return base == NULL ? path : base + 1;
}

typedef struct ScannedBundle {
    char path[PATH_MAX];
    AstraBundleManifest manifest;
} ScannedBundle;

static int version_at_least(AstraBundleVersion have, AstraBundleVersion need)
{
    if (have.major != need.major) return have.major > need.major;
    if (have.minor != need.minor) return have.minor > need.minor;
    return have.patch >= need.patch;
}

static int same_file(const char *left, const char *right)
{
    char a[PATH_MAX];
    char b[PATH_MAX];
    return realpath(left, a) != NULL && realpath(right, b) != NULL &&
           strcmp(a, b) == 0;
}

static int scan_roots(char **roots, int root_count, ScannedBundle **out,
                      size_t *out_count)
{
    ScannedBundle *found = NULL;
    size_t count = 0u;

    for (int root = 0; root < root_count; ++root) {
        DIR *directory = opendir(roots[root]);
        struct dirent *entry;
        if (directory == NULL) {
            free(found);
            return 0;
        }
        while ((entry = readdir(directory)) != NULL) {
            ScannedBundle candidate;
            ScannedBundle *grown;
            if (entry->d_name[0] == '.' ||
                (!suffix(entry->d_name, ".app") &&
                 !suffix(entry->d_name, ".kit")) ||
                !path_join(candidate.path, roots[root], entry->d_name) ||
                !validate(candidate.path, &candidate.manifest, 1))
                continue;
            grown = realloc(found, (count + 1u) * sizeof(*found));
            if (grown == NULL) {
                closedir(directory);
                free(found);
                return 0;
            }
            found = grown;
            found[count++] = candidate;
        }
        closedir(directory);
    }
    *out = found;
    *out_count = count;
    return 1;
}

static int requirement_matches(const AstraBundleLibrary *requirement,
                               const AstraBundleLibrary *provider)
{
    return strcmp(requirement->name, provider->name) == 0 &&
           requirement->abi == provider->abi &&
           version_at_least(provider->version, requirement->version);
}

static int has_dependents(const char *target,
                          const AstraBundleManifest *target_manifest,
                          char **roots, int root_count)
{
    ScannedBundle *bundles = NULL;
    size_t count = 0u;
    int blocked = 0;

    if (target_manifest->kind != ASTRA_BUNDLE_KIT) return 0;
    if (!scan_roots(roots, root_count, &bundles, &count)) return -1;
    for (size_t at = 0u; at < count; ++at) {
        AstraBundleManifest *consumer = &bundles[at].manifest;
        if (same_file(target, bundles[at].path)) continue;
        for (uint32_t need = 0u; need < consumer->require_count; ++need) {
            int supplied_by_target = 0;
            int alternative = 0;

            for (uint32_t provide = 0u;
                 provide < target_manifest->provide_count; ++provide)
                if (requirement_matches(&consumer->requires[need],
                                        &target_manifest->provides[provide]))
                    supplied_by_target = 1;
            if (!supplied_by_target) continue;
            for (size_t candidate = 0u; candidate < count && !alternative;
                 ++candidate) {
                if (same_file(target, bundles[candidate].path)) continue;
                for (uint32_t provide = 0u;
                     provide < bundles[candidate].manifest.provide_count;
                     ++provide)
                    if (requirement_matches(
                            &consumer->requires[need],
                            &bundles[candidate].manifest.provides[provide])) {
                        alternative = 1;
                        break;
                    }
            }
            if (!alternative) {
                fprintf(stderr, "%s requires %s ABI %u >= %u.%u.%u\n",
                        bundles[at].path, consumer->requires[need].name,
                        consumer->requires[need].abi,
                        consumer->requires[need].version.major,
                        consumer->requires[need].version.minor,
                        consumer->requires[need].version.patch);
                blocked = 1;
            }
        }
    }
    free(bundles);
    return blocked;
}

static void usage(void)
{
    fprintf(stderr, "usage: astra-bundle check BUNDLE\n"
                    "       astra-bundle copy SOURCE DESTINATION\n"
                    "       astra-bundle move SOURCE DESTINATION\n"
                    "       astra-bundle trash SOURCE TRASH-DIRECTORY [ROOT ...]\n"
                    "       astra-bundle delete BUNDLE ROOT [ROOT ...]\n");
}

int main(int argc, char **argv)
{
    AstraBundleManifest manifest;

    if (argc == 3 && strcmp(argv[1], "check") == 0) {
        if (!validate(argv[2], &manifest, 0)) return 1;
        printf("%s %s %u.%u.%u\n", manifest.kind == ASTRA_BUNDLE_APPLICATION ?
               "application" : "kit", manifest.id, manifest.version.major,
               manifest.version.minor, manifest.version.patch);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "copy") == 0)
        return copy_bundle(argv[2], argv[3]) ? 0 : 1;
    if (argc == 4 && strcmp(argv[1], "move") == 0)
        return move_bundle(argv[2], argv[3]) ? 0 : 1;
    if (argc >= 4 && strcmp(argv[1], "trash") == 0) {
        char destination[PATH_MAX];
        if (!validate(argv[2], &manifest, 0)) return 1;
        if (argc > 4) {
            int dependents = has_dependents(argv[2], &manifest,
                                            argv + 4, argc - 4);
            if (dependents < 0) return 1;
            if (dependents > 0)
                fprintf(stderr, "warning: moved to Trash with dependents\n");
        }
        if (mkdir(argv[3], 0755) != 0 && errno != EEXIST) return 1;
        if (!path_join(destination, argv[3], base_name(argv[2]))) return 1;
        return move_bundle(argv[2], destination) ? 0 : 1;
    }
    if (argc >= 4 && strcmp(argv[1], "delete") == 0) {
        int dependents;
        if (!validate(argv[2], &manifest, 0)) return 1;
        dependents = has_dependents(argv[2], &manifest, argv + 3, argc - 3);
        if (dependents != 0) {
            if (dependents > 0)
                fprintf(stderr, "refusing permanent deletion: dependencies remain\n");
            return 1;
        }
        return remove_tree(argv[2]) ? 0 : 1;
    }
    usage();
    return 2;
}
