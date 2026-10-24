#define _GNU_SOURCE
#include "astra_storage_names.h"

#include <astra/crc32.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define NAME_MAP_MAX 1024u
#define NAME_MAP_FILE "volume-names"
#define NAME_MAP_PREVIOUS "volume-names.prev"

typedef struct NameMapHeader {
    char magic[8];
    uint32_t count;
    uint32_t checksum;
} NameMapHeader;

static int read_all(int fd, void *buffer, size_t size)
{
    unsigned char *at = buffer;
    while (size != 0u) {
        ssize_t got = read(fd, at, size);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return got == 0 ? EIO : errno;
        at += got;
        size -= (size_t)got;
    }
    return 0;
}

static int write_all(int fd, const void *buffer, size_t size)
{
    const unsigned char *at = buffer;
    while (size != 0u) {
        ssize_t wrote = write(fd, at, size);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return wrote == 0 ? EIO : errno;
        at += wrote;
        size -= (size_t)wrote;
    }
    return 0;
}

static int read_map(AstraStorageNames *names, const char *filename)
{
    int fd = openat(names->directory, filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno;
    struct stat info;
    int error = fstat(fd, &info) != 0 ? errno : 0;
    if (error == 0 && (!S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
        (info.st_mode & 077u) != 0u || info.st_size < (off_t)sizeof(NameMapHeader) ||
        info.st_size > (off_t)(sizeof(NameMapHeader) + NAME_MAP_MAX * sizeof(AstraStorageNameRow))))
        error = EINVAL;
    NameMapHeader header;
    if (error == 0) error = read_all(fd, &header, sizeof(header));
    if (error == 0 && (memcmp(header.magic, "A68VN001", 8u) != 0 ||
        header.count > NAME_MAP_MAX ||
        info.st_size != (off_t)(sizeof(header) + header.count * sizeof(AstraStorageNameRow))))
        error = EINVAL;
    AstraStorageNameRow *rows = NULL;
    if (error == 0 && header.count != 0u) {
        rows = calloc(header.count, sizeof(*rows));
        if (rows == NULL) error = ENOMEM;
    }
    if (error == 0 && header.count != 0u)
        error = read_all(fd, rows, header.count * sizeof(*rows));
    if (error == 0 && astra_crc32(rows, header.count * sizeof(*rows)) != header.checksum)
        error = EINVAL;
    for (uint32_t at = 0u; error == 0 && at < header.count; ++at) {
        if (rows[at].key[0] == '\0' ||
            strnlen(rows[at].key, sizeof(rows[at].key)) == sizeof(rows[at].key) ||
            rows[at].slot == UINT32_MAX) {
            error = EINVAL;
            break;
        }
        for (uint32_t prior = 0u; prior < at; ++prior)
            if (rows[prior].slot == rows[at].slot ||
                strcmp(rows[prior].key, rows[at].key) == 0) {
                error = EINVAL;
                break;
            }
    }
    close(fd);
    if (error != 0) free(rows);
    else {
        names->rows = rows;
        names->count = header.count;
    }
    return error;
}

int astra_storage_names_open(AstraStorageNames *names, const char *state_directory)
{
    if (names == NULL || names->directory >= 0 || names->rows != NULL ||
        state_directory == NULL || state_directory[0] != '/') return EINVAL;
    names->directory = open(state_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (names->directory < 0) return names->error = errno;
    struct stat info;
    if (fstat(names->directory, &info) != 0 || info.st_uid != geteuid() ||
        (info.st_mode & 077u) != 0u)
        return names->error = EACCES;
    int primary = read_map(names, NAME_MAP_FILE);
    names->primary_valid = primary == 0;
    int error = primary;
    if (primary != 0) {
        if (primary != ENOENT)
            fprintf(stderr, "astra-storage USB name map invalid (%d); trying previous\n", primary);
        error = read_map(names, NAME_MAP_PREVIOUS);
        if (error == ENOENT) error = primary == ENOENT ? 0 : primary;
    }
    if (error != 0)
        fprintf(stderr, "astra-storage USB name map unavailable (%d); USB mounts fenced\n", error);
    names->error = error;
    return error;
}

void astra_storage_names_close(AstraStorageNames *names)
{
    if (names == NULL) return;
    free(names->rows);
    if (names->directory >= 0) close(names->directory);
    *names = (AstraStorageNames)ASTRA_STORAGE_NAMES_INIT;
}

static int save(AstraStorageNames *names)
{
    unsigned nonce;
    ssize_t random_bytes;
    do random_bytes = getrandom(&nonce, sizeof(nonce), 0);
    while (random_bytes < 0 && errno == EINTR);
    if (random_bytes != sizeof(nonce)) return random_bytes < 0 ? errno : EIO;
    char temporary[64];
    int length = snprintf(temporary, sizeof(temporary), ".volume-names.%08x", nonce);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return EOVERFLOW;
    int fd = openat(names->directory, temporary,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return errno;
    NameMapHeader header = {.magic = "A68VN001", .count = names->count,
        .checksum = astra_crc32(names->rows, names->count * sizeof(*names->rows))};
    int error = write_all(fd, &header, sizeof(header));
    if (error == 0)
        error = write_all(fd, names->rows, names->count * sizeof(*names->rows));
    if (error == 0 && fsync(fd) != 0) error = errno;
    if (close(fd) != 0 && error == 0) error = errno;
    if (error == 0 && names->primary_valid && renameat(names->directory, NAME_MAP_FILE,
        names->directory, NAME_MAP_PREVIOUS) != 0 && errno != ENOENT)
        error = errno;
    if (error == 0 && renameat(names->directory, temporary,
        names->directory, NAME_MAP_FILE) != 0)
        error = errno;
    if (error == 0 && fsync(names->directory) != 0) error = errno;
    if (error != 0) {
        (void)unlinkat(names->directory, temporary, 0);
        if (faccessat(names->directory, NAME_MAP_FILE, F_OK, 0) != 0)
            (void)renameat(names->directory, NAME_MAP_PREVIOUS,
                names->directory, NAME_MAP_FILE);
    } else names->primary_valid = 1;
    return error;
}

int astra_storage_names_select(AstraStorageNames *names, const char *key,
    const AstraStorageCatalog *catalog, uint32_t *slot)
{
    if (names == NULL || catalog == NULL || slot == NULL) return EINVAL;
    *slot = 0u;
    if (names->error != 0) return names->error;
    if (key != NULL && (key[0] == '\0' ||
        strnlen(key, ASTRA_STORAGE_NAME_KEY_BYTES) == ASTRA_STORAGE_NAME_KEY_BYTES))
        return EINVAL;
    uint32_t next = 0u;
    for (uint32_t at = 0u; at < names->count; ++at) {
        if (key != NULL && strcmp(names->rows[at].key, key) == 0) {
            if (astra_storage_usb_slot_reserved(catalog, names->rows[at].slot))
                return EBUSY;
            *slot = names->rows[at].slot;
            return 0;
        }
        if (names->rows[at].slot >= next)
            next = names->rows[at].slot + 1u;
    }
    if (next == UINT32_MAX) return EOVERFLOW;
    while (astra_storage_usb_slot_reserved(catalog, next)) {
        if (next == UINT32_MAX - 1u) return EOVERFLOW;
        ++next;
    }
    if (key != NULL) {
        if (names->directory < 0 || names->count == NAME_MAP_MAX) return ENOSPC;
        AstraStorageNameRow *rows = realloc(names->rows,
            (names->count + 1u) * sizeof(*names->rows));
        if (rows == NULL) return ENOMEM;
        names->rows = rows;
        names->rows[names->count] = (AstraStorageNameRow){.slot = next};
        strcpy(names->rows[names->count].key, key);
        ++names->count;
        int error = save(names);
        if (error != 0) {
            --names->count;
            names->error = error;
            return error;
        }
    }
    *slot = next;
    return 0;
}
