#include <astra/recent_items.h>
#include <astra/document_store.h>
#include <astra/endian.h>
#include <astra/runtime.h>
#include <astra/utf8.h>
#include <astra/vfs_path.h>

#include <stddef.h>
#include <string.h>

#define RECENT_HEADER_BYTES 44u
#define RECENT_NAME_BYTES 44u
#define RECENT_BYTES_MAX (RECENT_HEADER_BYTES + ASTRA_VFS_PATH_MAX - 1u)

static int valid_reference(const AstraVolumeReference *volume)
{
    return volume == NULL ||
        ((volume->epoch == 0u && volume->id == 0u &&
          volume->generation == 0u) ||
         (volume->epoch != 0u && volume->id != 0u &&
          volume->generation != 0u));
}

static int valid_path(const char *path)
{
    const char *end;
    uint32_t length;
    if (path == NULL || path[0] != '/') return 0;
    end = memchr(path, '\0', ASTRA_VFS_PATH_MAX);
    if (end == NULL) return 0;
    length = (uint32_t)(end - path);
    return !astra_path_requires_walk(path) &&
        astra_utf8_validate(path, length, 0u);
}

static void recent_name(char *name, uint64_t opened, uint64_t nonce,
    int temporary)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t at = 0u;
    if (temporary) name[at++] = '.';
    memcpy(name + at, "recent-", 7u);
    at += 7u;
    for (uint32_t digit = 0u; digit < 16u; ++digit)
        name[at++] = hex[(opened >> (60u - 4u * digit)) & 15u];
    name[at++] = '-';
    for (uint32_t digit = 0u; digit < 16u; ++digit)
        name[at++] = hex[(nonce >> (60u - 4u * digit)) & 15u];
    memcpy(name + at, temporary ? ".tmp" : ".ari", 5u);
}

static int valid_name(const char *name)
{
    const char *end = name != NULL ? memchr(name, '\0', ASTRA_VFS_NAME_MAX) :
        NULL;
    if (end == NULL || (uint32_t)(end - name) != RECENT_NAME_BYTES ||
        memcmp(name, "recent-", 7u) != 0 || name[23] != '-' ||
        memcmp(name + 40u, ".ari", 4u) != 0)
        return 0;
    for (uint32_t at = 7u; at < 40u; ++at) {
        if (at == 23u) continue;
        if (!((name[at] >= '0' && name[at] <= '9') ||
              (name[at] >= 'a' && name[at] <= 'f')))
            return 0;
    }
    return 1;
}

static uint64_t name_opened(const char *name)
{
    uint64_t value = 0u;
    for (uint32_t at = 7u; at < 23u; ++at)
        value = (value << 4u) |
            (uint64_t)(name[at] <= '9' ? name[at] - '0' :
                name[at] - 'a' + 10);
    return value;
}

/* Keep durable ordering even before realtime synchronization or after a
 * backward clock correction. Store contains at most 64 published events. */
static uint32_t next_opened(AstraFilesystem *filesystem,
    const char *store_directory, uint64_t *opened)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    uint64_t now = 0u, latest = 0u;
    uint32_t status;
    if (astra_clock_realtime(&now) != ASTRA_SYSCALL_OK) now = 0u;
    status = astra_filesystem_directory_open(filesystem, store_directory,
        &directory);
    if (status != ASTRA_VFS_OK) return status;
    for (;;) {
        AstraDirectoryEntry entries[1];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 1u,
            &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at)
            if (valid_name(entries[at].name)) {
                uint64_t value = name_opened(entries[at].name);
                if (value > latest) latest = value;
            }
    }
    astra_filesystem_directory_close(&directory);
    if (status != ASTRA_VFS_OK) return status;
    if (now <= latest) {
        if (latest == UINT64_MAX) return ASTRA_VFS_ERR_LIMIT;
        now = latest + 1u;
    }
    *opened = now;
    return ASTRA_VFS_OK;
}

static uint32_t load_item(AstraFilesystem *filesystem, const char *directory,
    const char *name, char *path, AstraRecentItem *item)
{
    uint8_t *bytes = NULL;
    uint32_t length = 0u, path_length, status;
    if (!valid_name(name) || astra_document_path_join(directory, name,
            path, ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK)
        return ASTRA_VFS_ERR_INVALID;
    status = astra_document_read(filesystem, path, RECENT_HEADER_BYTES + 1u,
        RECENT_BYTES_MAX, &bytes, &length);
    if (status != ASTRA_VFS_OK) return status;
    path_length = astra_load_be32(bytes + 4u);
    item->kind = astra_load_be32(bytes + 8u);
    item->opened_ns = astra_load_be64(bytes + 12u);
    item->volume.epoch = astra_load_be64(bytes + 20u);
    item->volume.id = astra_load_be64(bytes + 28u);
    item->volume.generation = astra_load_be64(bytes + 36u);
    if (memcmp(bytes, "ARI1", 4u) != 0 ||
        path_length == 0u || path_length >= sizeof(item->path) ||
        RECENT_HEADER_BYTES + path_length != length ||
        (item->kind != ASTRA_VFS_KIND_FILE &&
         item->kind != ASTRA_VFS_KIND_DIRECTORY) ||
        item->opened_ns != name_opened(name) ||
        !valid_reference(&item->volume)) {
        status = ASTRA_VFS_ERR_INVALID;
    } else {
        memcpy(item->path, bytes + RECENT_HEADER_BYTES, path_length);
        item->path[path_length] = '\0';
        if (!valid_path(item->path)) status = ASTRA_VFS_ERR_INVALID;
        else {
            memcpy(item->filename, name, RECENT_NAME_BYTES + 1u);
        }
    }
    astra_runtime_deallocate(bytes);
    return status;
}

static int same_item(const AstraRecentItem *a, const AstraRecentItem *b)
{
    return strcmp(a->path, b->path) == 0 &&
        a->volume.epoch == b->volume.epoch &&
        a->volume.id == b->volume.id &&
        a->volume.generation == b->volume.generation;
}

static void insert_item(AstraRecentItemSnapshot *snapshot,
    const AstraRecentItem *item)
{
    uint32_t duplicate = UINT32_MAX;
    for (uint32_t at = 0u; at < snapshot->count; ++at)
        if (same_item(&snapshot->items[at], item)) {
            duplicate = at;
            break;
        }
    if (duplicate != UINT32_MAX) {
        if (strcmp(snapshot->items[duplicate].filename,
                item->filename) >= 0) return;
        for (uint32_t at = duplicate; at + 1u < snapshot->count; ++at)
            snapshot->items[at] = snapshot->items[at + 1u];
        --snapshot->count;
    }
    uint32_t position = 0u;
    while (position < snapshot->count &&
        strcmp(snapshot->items[position].filename, item->filename) > 0)
        ++position;
    if (position >= ASTRA_RECENT_ITEMS_LIMIT) return;
    if (snapshot->count < ASTRA_RECENT_ITEMS_LIMIT) ++snapshot->count;
    for (uint32_t at = snapshot->count - 1u; at > position; --at)
        snapshot->items[at] = snapshot->items[at - 1u];
    snapshot->items[position] = *item;
}

uint32_t astra_recent_item_snapshot_open(AstraFilesystem *filesystem,
    const char *store_directory, AstraRecentItemSnapshot *snapshot)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    AstraRecentItemSnapshot next = ASTRA_RECENT_ITEM_SNAPSHOT_INIT;
    AstraRecentItem *item;
    char *path;
    uint8_t *scratch;
    uint32_t status;
    if (filesystem == NULL || store_directory == NULL || snapshot == NULL ||
        store_directory[0] != '/' ||
        strlen(store_directory) >= ASTRA_VFS_PATH_MAX ||
        astra_path_requires_walk(store_directory))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_filesystem_directory_open(filesystem, store_directory,
        &directory);
    if (status == ASTRA_VFS_ERR_NOT_FOUND) {
        *snapshot = next;
        return ASTRA_VFS_OK;
    }
    if (status != ASTRA_VFS_OK) return status;
    next.items = astra_runtime_allocate((size_t)ASTRA_RECENT_ITEMS_LIMIT *
        sizeof(*next.items));
    scratch = astra_runtime_allocate(sizeof(*item) + ASTRA_VFS_PATH_MAX);
    if (next.items == NULL || scratch == NULL) {
        astra_filesystem_directory_close(&directory);
        astra_runtime_deallocate(scratch);
        astra_runtime_deallocate(next.items);
        return ASTRA_VFS_ERR_LIMIT;
    }
    item = (AstraRecentItem *)scratch;
    path = (char *)(scratch + sizeof(*item));
    for (;;) {
        AstraDirectoryEntry entries[1];
        uint32_t count = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 1u,
            &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            uint32_t loaded;
            if (!valid_name(entries[at].name)) continue;
            loaded = load_item(filesystem, store_directory,
                entries[at].name, path, item);
            if (loaded == ASTRA_VFS_ERR_INVALID ||
                loaded == ASTRA_VFS_ERR_NOT_FOUND) continue;
            if (loaded != ASTRA_VFS_OK) {
                status = loaded;
                break;
            }
            insert_item(&next, item);
        }
        if (status != ASTRA_VFS_OK) break;
    }
    astra_filesystem_directory_close(&directory);
    astra_runtime_deallocate(scratch);
    if (status != ASTRA_VFS_OK) {
        astra_recent_item_snapshot_close(&next);
        return status;
    }
    *snapshot = next;
    return ASTRA_VFS_OK;
}

void astra_recent_item_snapshot_close(AstraRecentItemSnapshot *snapshot)
{
    if (snapshot == NULL) return;
    astra_runtime_deallocate(snapshot->items);
    *snapshot = (AstraRecentItemSnapshot)ASTRA_RECENT_ITEM_SNAPSHOT_INIT;
}

/* Keep newest event names. Deleting only names below an observed threshold
 * avoids touching a concurrent writer's newly published document. */
static uint32_t prune_items(AstraFilesystem *filesystem,
    const char *store_directory)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    char *names = NULL;
    char *stale = NULL;
    char *path = NULL;
    uint32_t count = 0u, status;
    names = astra_runtime_allocate((size_t)ASTRA_RECENT_ITEMS_LIMIT *
        (RECENT_NAME_BYTES + 1u));
    stale = astra_runtime_allocate((size_t)ASTRA_RECENT_ITEMS_LIMIT *
        (RECENT_NAME_BYTES + 1u));
    path = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (names == NULL || stale == NULL || path == NULL) {
        astra_runtime_deallocate(names);
        astra_runtime_deallocate(stale);
        astra_runtime_deallocate(path);
        return ASTRA_VFS_ERR_LIMIT;
    }
    status = astra_filesystem_directory_open(filesystem, store_directory,
        &directory);
    if (status != ASTRA_VFS_OK) goto done;
    for (;;) {
        AstraDirectoryEntry entries[1];
        uint32_t read = 0u;
        status = astra_filesystem_directory_read(&directory, entries, 1u,
            &read);
        if (status != ASTRA_VFS_OK || read == 0u) break;
        for (uint32_t at = 0u; at < read; ++at) {
            if (!valid_name(entries[at].name)) continue;
            uint32_t position = 0u;
            while (position < count && strcmp(names +
                (size_t)position * (RECENT_NAME_BYTES + 1u),
                entries[at].name) > 0) ++position;
            if (position >= ASTRA_RECENT_ITEMS_LIMIT) continue;
            if (count < ASTRA_RECENT_ITEMS_LIMIT) ++count;
            for (uint32_t move = count - 1u; move > position; --move)
                memcpy(names + (size_t)move * (RECENT_NAME_BYTES + 1u),
                    names + (size_t)(move - 1u) * (RECENT_NAME_BYTES + 1u),
                    RECENT_NAME_BYTES + 1u);
            memcpy(names + (size_t)position * (RECENT_NAME_BYTES + 1u),
                entries[at].name, RECENT_NAME_BYTES + 1u);
        }
    }
    astra_filesystem_directory_close(&directory);
    if (status != ASTRA_VFS_OK || count < ASTRA_RECENT_ITEMS_LIMIT)
        goto done;
    const char *threshold = names +
        (size_t)(count - 1u) * (RECENT_NAME_BYTES + 1u);
    for (;;) {
        uint32_t stale_count = 0u;
        status = astra_filesystem_directory_open(filesystem, store_directory,
            &directory);
        if (status != ASTRA_VFS_OK) break;
        for (;;) {
            AstraDirectoryEntry entries[1];
            uint32_t read = 0u;
            status = astra_filesystem_directory_read(&directory, entries, 1u,
                &read);
            if (status != ASTRA_VFS_OK || read == 0u) break;
            for (uint32_t at = 0u; at < read; ++at) {
                if (!valid_name(entries[at].name) ||
                    strcmp(entries[at].name, threshold) >= 0) continue;
                memcpy(stale + (size_t)stale_count++ *
                    (RECENT_NAME_BYTES + 1u), entries[at].name,
                    RECENT_NAME_BYTES + 1u);
                if (stale_count == ASTRA_RECENT_ITEMS_LIMIT) break;
            }
            if (stale_count == ASTRA_RECENT_ITEMS_LIMIT) break;
        }
        astra_filesystem_directory_close(&directory);
        if (status != ASTRA_VFS_OK || stale_count == 0u) break;
        for (uint32_t at = 0u; at < stale_count; ++at) {
            status = astra_document_path_join(store_directory,
                stale + (size_t)at * (RECENT_NAME_BYTES + 1u),
                path, ASTRA_VFS_PATH_MAX);
            if (status != ASTRA_VFS_OK) break;
            status = astra_filesystem_unlink(filesystem, path, 0u);
            if (status == ASTRA_VFS_ERR_NOT_FOUND) status = ASTRA_VFS_OK;
            if (status != ASTRA_VFS_OK) break;
        }
        if (status != ASTRA_VFS_OK) break;
    }
done:
    astra_runtime_deallocate(names);
    astra_runtime_deallocate(stale);
    astra_runtime_deallocate(path);
    return status;
}

uint32_t astra_recent_item_record(AstraFilesystem *filesystem,
    const char *store_directory, const char *path, uint32_t kind,
    const AstraVolumeReference *volume)
{
    char temporary_name[RECENT_NAME_BYTES + 6u];
    char final_name[RECENT_NAME_BYTES + 1u];
    char *paths;
    char *temporary_path;
    char *final_path;
    uint8_t *bytes;
    uint32_t status, length;
    uint64_t opened = 0u, nonce;
    if (filesystem == NULL || store_directory == NULL || !valid_path(path) ||
        !valid_reference(volume) ||
        (kind != ASTRA_VFS_KIND_FILE && kind != ASTRA_VFS_KIND_DIRECTORY) ||
        store_directory[0] != '/' ||
        strlen(store_directory) > ASTRA_VFS_PATH_MAX - sizeof(temporary_name) ||
        astra_path_requires_walk(store_directory))
        return ASTRA_VFS_ERR_INVALID;
    status = astra_document_store_prepare(filesystem, store_directory);
    if (status != ASTRA_VFS_OK) return status;
    status = next_opened(filesystem, store_directory, &opened);
    if (status != ASTRA_VFS_OK) return status;
    length = RECENT_HEADER_BYTES + (uint32_t)strlen(path);
    bytes = astra_runtime_allocate(length);
    if (bytes == NULL) return ASTRA_VFS_ERR_LIMIT;
    paths = astra_runtime_allocate(2u * ASTRA_VFS_PATH_MAX);
    if (paths == NULL) {
        astra_runtime_deallocate(bytes);
        return ASTRA_VFS_ERR_LIMIT;
    }
    temporary_path = paths;
    final_path = paths + ASTRA_VFS_PATH_MAX;
    memcpy(bytes, "ARI1", 4u);
    astra_store_be32(bytes + 4u, length - RECENT_HEADER_BYTES);
    astra_store_be32(bytes + 8u, kind);
    astra_store_be64(bytes + 12u, opened);
    astra_store_be64(bytes + 20u, volume != NULL ? volume->epoch : 0u);
    astra_store_be64(bytes + 28u, volume != NULL ? volume->id : 0u);
    astra_store_be64(bytes + 36u, volume != NULL ? volume->generation : 0u);
    memcpy(bytes + RECENT_HEADER_BYTES, path, length - RECENT_HEADER_BYTES);
    nonce = astra_clock_monotonic();
    status = ASTRA_VFS_ERR_EXISTS;
    for (uint32_t attempt = 0u; attempt < 32u; ++attempt) {
        recent_name(temporary_name, opened, nonce + attempt, 1);
        recent_name(final_name, opened, nonce + attempt, 0);
        if (astra_document_path_join(store_directory, temporary_name,
                temporary_path, ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK ||
            astra_document_path_join(store_directory, final_name,
                final_path, ASTRA_VFS_PATH_MAX) != ASTRA_VFS_OK) {
            status = ASTRA_VFS_ERR_LIMIT;
            break;
        }
        status = astra_document_stage(filesystem, temporary_path,
            bytes, length);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        if (status != ASTRA_VFS_OK) break;
        status = astra_filesystem_link(filesystem, temporary_path, final_path);
        (void)astra_filesystem_unlink(filesystem, temporary_path, 0u);
        if (status == ASTRA_VFS_ERR_EXISTS) continue;
        break;
    }
    astra_runtime_deallocate(paths);
    astra_runtime_deallocate(bytes);
    if (status != ASTRA_VFS_OK) return status;
    (void)prune_items(filesystem, store_directory);
    return ASTRA_VFS_OK;
}
