#include <astra/vfs_ram_backend.h>

#include <limits.h>
#include <string.h>

#define RAM_PAGE_BYTES 4096u

typedef struct RamEntry RamEntry;

struct AstraVfsRamNode {
    struct AstraVfsRamNode *parent;
    RamEntry *children;
    uint8_t **pages;
    char *target;
    uint64_t size;
    uint32_t slots;
    uint32_t opens;
    uint32_t links;
    uint16_t kind;
    uint16_t mode;
};

struct RamEntry {
    RamEntry *next;
    AstraVfsRamNode *node;
    char name[ASTRA_VFS_NAME_MAX];
};

static int locked(AstraVfsRamBackend *fs)
{
    return fs->lock == NULL || fs->lock(fs->lock_context);
}

static void unlocked(AstraVfsRamBackend *fs)
{
    if (fs->unlock != NULL)
        fs->unlock(fs->lock_context);
}

static void *charged(AstraVfsRamBackend *fs, size_t bytes)
{
    void *result;

    if (bytes == 0u || bytes > fs->max_bytes - fs->used_bytes)
        return NULL;
    result = fs->allocate(bytes);
    if (result != NULL) {
        fs->used_bytes += bytes;
        memset(result, 0, bytes);
    }
    return result;
}

static void released(AstraVfsRamBackend *fs, void *pointer, size_t bytes)
{
    if (pointer == NULL)
        return;
    fs->deallocate(pointer);
    fs->used_bytes -= bytes;
}

static RamEntry *child(AstraVfsRamNode *parent, const char *name, size_t length)
{
    for (RamEntry *entry = parent->children; entry != NULL;
         entry = entry->next)
        if (strlen(entry->name) == length &&
            memcmp(entry->name, name, length) == 0)
            return entry;
    return NULL;
}

static uint32_t walk(AstraVfsRamBackend *fs, AstraVfsRamNode *start,
                     const char *path, AstraVfsRamNode **node,
                     AstraVfsRamNode **parent, RamEntry **entry)
{
    AstraVfsRamNode *at;
    const char *part;

    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    at = path[0] == '/' ? fs->root : start;
    if (at == NULL)
        return ASTRA_VFS_ERR_BAD_HANDLE;
    part = path;
    while (*part == '/') ++part;
    if (*part == '\0') {
        if (node != NULL) *node = at;
        if (parent != NULL) *parent = NULL;
        if (entry != NULL) *entry = NULL;
        return ASTRA_VFS_OK;
    }
    for (;;) {
        const char *end = part;
        size_t length;
        RamEntry *found;

        while (*end != '\0' && *end != '/') ++end;
        length = (size_t)(end - part);
        if (length == 0u || length >= ASTRA_VFS_NAME_MAX)
            return ASTRA_VFS_ERR_INVALID;
        if (at->kind != ASTRA_VFS_KIND_DIRECTORY)
            return ASTRA_VFS_ERR_NOT_DIR;
        if (length == 1u && part[0] == '.') {
            found = NULL;
        } else if (length == 2u && part[0] == '.' && part[1] == '.') {
            at = at->parent == NULL ? at : at->parent;
            found = NULL;
        } else {
            found = child(at, part, length);
            if (found == NULL)
                return ASTRA_VFS_ERR_NOT_FOUND;
        }
        int trailing_slash = *end == '/';
        while (*end == '/') ++end;
        if (*end == '\0') {
            if (trailing_slash && found != NULL &&
                found->node->kind != ASTRA_VFS_KIND_DIRECTORY)
                return ASTRA_VFS_ERR_NOT_DIR;
            if (node != NULL) *node = found == NULL ? at : found->node;
            if (parent != NULL) *parent = found == NULL ? NULL : at;
            if (entry != NULL) *entry = found;
            return ASTRA_VFS_OK;
        }
        if (found != NULL) at = found->node;
        part = end;
    }
}

static uint32_t parent_of(AstraVfsRamBackend *fs, AstraVfsRamNode *start,
                          const char *path, AstraVfsRamNode **parent,
                          char name[ASTRA_VFS_NAME_MAX])
{
    char prefix[ASTRA_VFS_PATH_MAX];
    size_t length;
    size_t base;
    uint32_t status;

    if (path == NULL)
        return ASTRA_VFS_ERR_INVALID;
    length = strlen(path);
    while (length > 1u && path[length - 1u] == '/') --length;
    if (length == 0u || length >= sizeof(prefix))
        return ASTRA_VFS_ERR_INVALID;
    base = length;
    while (base != 0u && path[base - 1u] != '/') --base;
    if (length - base == 0u || length - base >= ASTRA_VFS_NAME_MAX ||
        (length - base == 1u && path[base] == '.') ||
        (length - base == 2u && path[base] == '.' && path[base + 1u] == '.'))
        return ASTRA_VFS_ERR_INVALID;
    memcpy(name, path + base, length - base);
    name[length - base] = '\0';
    if (base == 0u)
        return walk(fs, start, ".", parent, NULL, NULL);
    if (base == 1u && path[0] == '/')
        return walk(fs, fs->root, "/", parent, NULL, NULL);
    memcpy(prefix, path, base - 1u);
    prefix[base - 1u] = '\0';
    status = walk(fs, start, prefix, parent, NULL, NULL);
    if (status == ASTRA_VFS_OK && (*parent)->kind != ASTRA_VFS_KIND_DIRECTORY)
        status = ASTRA_VFS_ERR_NOT_DIR;
    return status;
}

static void info_of(const AstraVfsRamNode *node, AstraVfsNodeInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->size = node->size;
    info->kind = node->kind;
    info->mode = node->mode;
    info->nlink = node->links > UINT16_MAX ? UINT16_MAX :
                  (uint16_t)node->links;
}

static void free_node(AstraVfsRamBackend *fs, AstraVfsRamNode *node)
{
    if (node->links != 0u || node->opens != 0u)
        return;
    for (uint32_t index = 0u; index < node->slots; ++index)
        released(fs, node->pages[index], RAM_PAGE_BYTES);
    released(fs, node->pages, (size_t)node->slots * sizeof(*node->pages));
    released(fs, node->target, node->target == NULL ? 0u :
             strlen(node->target) + 1u);
    released(fs, node, sizeof(*node));
    --fs->node_count;
}

static uint32_t create(AstraVfsRamBackend *fs, AstraVfsRamNode *parent,
                       const char *name, uint16_t kind, uint16_t mode,
                       const char *target, AstraVfsRamNode **out)
{
    AstraVfsRamNode *node;
    RamEntry *entry;
    size_t length = strlen(name);

    if (parent->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if (child(parent, name, length) != NULL)
        return ASTRA_VFS_ERR_EXISTS;
    node = charged(fs, sizeof(*node));
    if (node == NULL)
        return ASTRA_VFS_ERR_NO_SPACE;
    entry = charged(fs, sizeof(*entry));
    if (entry == NULL) {
        released(fs, node, sizeof(*node));
        return ASTRA_VFS_ERR_NO_SPACE;
    }
    if (target != NULL) {
        size_t bytes = strlen(target) + 1u;

        node->target = charged(fs, bytes);
        if (node->target == NULL) {
            released(fs, entry, sizeof(*entry));
            released(fs, node, sizeof(*node));
            return ASTRA_VFS_ERR_NO_SPACE;
        }
        memcpy(node->target, target, bytes);
        node->size = bytes - 1u;
    }
    node->kind = kind;
    node->mode = mode;
    node->links = 1u;
    node->parent = parent;
    entry->node = node;
    memcpy(entry->name, name, length + 1u);
    entry->next = parent->children;
    parent->children = entry;
    ++fs->node_count;
    if (out != NULL) *out = node;
    return ASTRA_VFS_OK;
}

int astra_vfs_ram_init(AstraVfsRamBackend *fs, uint64_t max_bytes,
                       AstraVfsRamAllocate allocate,
                       AstraVfsRamFree deallocate,
                       AstraVfsRamLock lock, AstraVfsRamUnlock unlock,
                       void *lock_context)
{
    if (fs == NULL || allocate == NULL || deallocate == NULL ||
        (lock == NULL) != (unlock == NULL) ||
        max_bytes < sizeof(AstraVfsRamNode))
        return 0;
    memset(fs, 0, sizeof(*fs));
    fs->max_bytes = max_bytes;
    fs->allocate = allocate;
    fs->deallocate = deallocate;
    fs->lock = lock;
    fs->unlock = unlock;
    fs->lock_context = lock_context;
    fs->root = charged(fs, sizeof(*fs->root));
    if (fs->root == NULL)
        return 0;
    fs->root->kind = ASTRA_VFS_KIND_DIRECTORY;
    fs->root->mode = 0777u;
    fs->root->links = 1u;
    fs->node_count = 1u;
    return 1;
}

static void destroy_children(AstraVfsRamBackend *fs, AstraVfsRamNode *parent)
{
    RamEntry *entry = parent->children;

    while (entry != NULL) {
        RamEntry *next = entry->next;
        AstraVfsRamNode *node = entry->node;

        if (node->kind == ASTRA_VFS_KIND_DIRECTORY)
            destroy_children(fs, node);
        --node->links;
        node->opens = 0u;
        free_node(fs, node);
        released(fs, entry, sizeof(*entry));
        entry = next;
    }
    parent->children = NULL;
}

void astra_vfs_ram_destroy(AstraVfsRamBackend *fs)
{
    if (fs == NULL || fs->root == NULL)
        return;
    destroy_children(fs, fs->root);
    fs->root->links = 0u;
    fs->root->opens = 0u;
    free_node(fs, fs->root);
    fs->root = NULL;
}

static uint32_t grow_slots(AstraVfsRamBackend *fs, AstraVfsRamNode *node,
                           uint32_t required)
{
    uint32_t capacity = node->slots == 0u ? 1u : node->slots;
    uint8_t **grown;

    if (required <= node->slots)
        return ASTRA_VFS_OK;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2u)
            return ASTRA_VFS_ERR_NO_SPACE;
        capacity *= 2u;
    }
    if ((uint64_t)capacity * sizeof(*grown) > SIZE_MAX)
        return ASTRA_VFS_ERR_NO_SPACE;
    grown = charged(fs, (size_t)capacity * sizeof(*grown));
    if (grown == NULL)
        return ASTRA_VFS_ERR_NO_SPACE;
    if (node->pages != NULL)
        memcpy(grown, node->pages, (size_t)node->slots * sizeof(*grown));
    released(fs, node->pages, (size_t)node->slots * sizeof(*grown));
    node->pages = grown;
    node->slots = capacity;
    return ASTRA_VFS_OK;
}

static uint32_t open_from(AstraVfsRamBackend *fs, AstraVfsRamNode *base,
                          const char *path, uint32_t flags,
                          uint16_t create_mode, uintptr_t *handle,
                          AstraVfsNodeInfo *info)
{
    AstraVfsRamNode *node = NULL;
    uint32_t status = walk(fs, base, path, &node, NULL, NULL);

    if (status == ASTRA_VFS_ERR_NOT_FOUND &&
        (flags & ASTRA_VFS_OPEN_CREATE) != 0u) {
        AstraVfsRamNode *parent;
        char name[ASTRA_VFS_NAME_MAX];

        if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u ||
            (path[0] != '\0' && path[strlen(path) - 1u] == '/'))
            return ASTRA_VFS_ERR_NOT_DIR;
        status = parent_of(fs, base, path, &parent, name);
        if (status == ASTRA_VFS_OK)
            status = create(fs, parent, name, ASTRA_VFS_KIND_FILE,
                            create_mode == ASTRA_VFS_MODE_DEFAULT ?
                                0666u : create_mode, NULL, &node);
    } else if (status == ASTRA_VFS_OK &&
               (flags & (ASTRA_VFS_OPEN_CREATE |
                         ASTRA_VFS_OPEN_EXCLUSIVE)) ==
                   (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) {
        return ASTRA_VFS_ERR_EXISTS;
    }
    if (status != ASTRA_VFS_OK)
        return status;
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u &&
        node->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) == 0u &&
        node->kind == ASTRA_VFS_KIND_DIRECTORY &&
        (flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_TRUNCATE)) != 0u)
        return ASTRA_VFS_ERR_IS_DIR;
    if (node->kind == ASTRA_VFS_KIND_SYMLINK)
        return ASTRA_VFS_ERR_LOOP;
    if (node->opens == UINT32_MAX)
        return ASTRA_VFS_ERR_LIMIT;
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) {
        if (node->kind != ASTRA_VFS_KIND_FILE)
            return ASTRA_VFS_ERR_IS_DIR;
        for (uint32_t index = 0u; index < node->slots; ++index) {
            released(fs, node->pages[index], RAM_PAGE_BYTES);
            node->pages[index] = NULL;
        }
        released(fs, node->pages,
                 (size_t)node->slots * sizeof(*node->pages));
        node->pages = NULL;
        node->slots = 0u;
        node->size = 0u;
    }
    ++node->opens;
    info_of(node, info);
    *handle = (uintptr_t)node;
    return ASTRA_VFS_OK;
}

static uint32_t ram_open(void *context, const char *path, uint32_t flags,
                         uint16_t create_mode, uintptr_t *handle,
                         AstraVfsNodeInfo *info)
{
    AstraVfsRamBackend *fs = context;
    uint32_t status;

    if (fs == NULL || handle == NULL || info == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = open_from(fs, fs->root, path, flags, create_mode, handle, info);
    unlocked(fs);
    return status;
}

static uint32_t ram_open_at(void *context, uintptr_t directory,
                            const char *path, uint32_t flags,
                            uint16_t create_mode, uintptr_t *handle,
                            AstraVfsNodeInfo *info)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *base = (AstraVfsRamNode *)directory;
    uint32_t status;

    if (fs == NULL || base == NULL || base->kind != ASTRA_VFS_KIND_DIRECTORY ||
        handle == NULL || info == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = open_from(fs, base, path, flags, create_mode, handle, info);
    unlocked(fs);
    return status;
}

static uint32_t ram_close(void *context, uintptr_t handle)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;

    if (fs == NULL || node == NULL || !locked(fs))
        return ASTRA_VFS_ERR_BAD_HANDLE;
    if (node->opens == 0u) {
        unlocked(fs);
        return ASTRA_VFS_ERR_BAD_HANDLE;
    }
    --node->opens;
    free_node(fs, node);
    unlocked(fs);
    return ASTRA_VFS_OK;
}

static uint32_t ram_read(void *context, uintptr_t handle, uint64_t offset,
                         void *buffer, uint32_t length, uint32_t *moved)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;
    uint8_t *out = buffer;
    uint32_t done = 0u;

    if (fs == NULL || node == NULL || moved == NULL ||
        (length != 0u && buffer == NULL) || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (node->kind != ASTRA_VFS_KIND_FILE) {
        unlocked(fs);
        return ASTRA_VFS_ERR_IS_DIR;
    }
    if (offset < node->size) {
        uint64_t available = node->size - offset;

        if (available < length) length = (uint32_t)available;
    } else {
        length = 0u;
    }
    while (done < length) {
        uint64_t at = offset + done;
        uint64_t index = at / RAM_PAGE_BYTES;
        uint32_t within = (uint32_t)(at % RAM_PAGE_BYTES);
        uint32_t chunk = RAM_PAGE_BYTES - within;

        if (chunk > length - done) chunk = length - done;
        if (index < node->slots && node->pages[index] != NULL)
            memcpy(out + done, node->pages[index] + within, chunk);
        else
            memset(out + done, 0, chunk);
        done += chunk;
    }
    *moved = done;
    unlocked(fs);
    return ASTRA_VFS_OK;
}

static uint32_t ram_write(void *context, uintptr_t handle, uint64_t offset,
                          uint32_t flags, const void *buffer,
                          uint32_t length, uint32_t *moved,
                          uint64_t *position)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;
    const uint8_t *input = buffer;
    uint32_t done = 0u;
    uint32_t status = ASTRA_VFS_OK;

    if (fs == NULL || node == NULL || moved == NULL || position == NULL ||
        (length != 0u && buffer == NULL) || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (node->kind != ASTRA_VFS_KIND_FILE) {
        unlocked(fs);
        return ASTRA_VFS_ERR_IS_DIR;
    }
    if ((flags & ASTRA_VFS_OPEN_APPEND) != 0u)
        offset = node->size;
    if (offset > UINT64_MAX - length) {
        unlocked(fs);
        return ASTRA_VFS_ERR_INVALID;
    }
    while (done < length) {
        uint64_t at = offset + done;
        uint64_t index = at / RAM_PAGE_BYTES;
        uint32_t within = (uint32_t)(at % RAM_PAGE_BYTES);
        uint32_t chunk = RAM_PAGE_BYTES - within;
        uint8_t *fresh = NULL;

        if (index >= UINT32_MAX) {
            status = ASTRA_VFS_ERR_NO_SPACE;
            break;
        }
        if (index >= node->slots || node->pages[index] == NULL) {
            fresh = charged(fs, RAM_PAGE_BYTES);
            if (fresh == NULL) {
                status = ASTRA_VFS_ERR_NO_SPACE;
                break;
            }
        }
        status = grow_slots(fs, node, (uint32_t)index + 1u);
        if (status != ASTRA_VFS_OK) {
            released(fs, fresh, RAM_PAGE_BYTES);
            break;
        }
        if (fresh != NULL) node->pages[index] = fresh;
        if (chunk > length - done) chunk = length - done;
        memcpy(node->pages[index] + within, input + done, chunk);
        done += chunk;
    }
    if (done != 0u && offset + done > node->size)
        node->size = offset + done;
    *moved = done;
    *position = offset + done;
    unlocked(fs);
    return done != 0u ? ASTRA_VFS_OK : status;
}

static uint32_t ram_sync(void *context, uintptr_t handle)
{
    (void)context;
    return handle == 0u ? ASTRA_VFS_ERR_BAD_HANDLE : ASTRA_VFS_OK;
}

static uint32_t ram_truncate(void *context, uintptr_t handle, uint64_t size)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;

    if (fs == NULL || node == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (node->kind != ASTRA_VFS_KIND_FILE) {
        unlocked(fs);
        return ASTRA_VFS_ERR_IS_DIR;
    }
    if (size < node->size) {
        uint64_t kept = size / RAM_PAGE_BYTES +
                        (size % RAM_PAGE_BYTES != 0u);

        for (uint32_t index = 0u; index < node->slots; ++index)
            if (index >= kept) {
                released(fs, node->pages[index], RAM_PAGE_BYTES);
                node->pages[index] = NULL;
            }
        if (size != 0u && size % RAM_PAGE_BYTES != 0u &&
            kept - 1u < node->slots && node->pages[kept - 1u] != NULL)
            memset(node->pages[kept - 1u] + size % RAM_PAGE_BYTES, 0,
                   RAM_PAGE_BYTES - (size % RAM_PAGE_BYTES));
        if (size == 0u) {
            released(fs, node->pages,
                     (size_t)node->slots * sizeof(*node->pages));
            node->pages = NULL;
            node->slots = 0u;
        }
    }
    node->size = size;
    unlocked(fs);
    return ASTRA_VFS_OK;
}

static uint32_t stat_from(AstraVfsRamBackend *fs, AstraVfsRamNode *base,
                          const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsRamNode *node;
    uint32_t status = walk(fs, base, path, &node, NULL, NULL);

    if (status == ASTRA_VFS_OK) info_of(node, info);
    return status;
}

static uint32_t ram_stat(void *context, const char *path,
                         AstraVfsNodeInfo *info)
{
    AstraVfsRamBackend *fs = context;
    uint32_t status;

    if (fs == NULL || info == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = stat_from(fs, fs->root, path, info);
    unlocked(fs);
    return status;
}

static uint32_t ram_stat_at(void *context, uintptr_t directory,
                            const char *path, AstraVfsNodeInfo *info)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *base = (AstraVfsRamNode *)directory;
    uint32_t status;

    if (fs == NULL || base == NULL || info == NULL ||
        base->kind != ASTRA_VFS_KIND_DIRECTORY || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = stat_from(fs, base, path, info);
    unlocked(fs);
    return status;
}

static uint32_t ram_stat_node(void *context, uintptr_t handle,
                              AstraVfsNodeInfo *info)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;

    if (fs == NULL || node == NULL || info == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    info_of(node, info);
    unlocked(fs);
    return ASTRA_VFS_OK;
}

static uint32_t ram_readdir(void *context, uintptr_t directory,
                            const char *path, uint64_t cookie, char *name,
                            uint32_t capacity, AstraVfsNodeInfo *info,
                            uint64_t *next)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)directory;
    RamEntry *entry;
    uint64_t position = cookie;
    uint32_t status = ASTRA_VFS_OK;

    if (fs == NULL || name == NULL || info == NULL || next == NULL ||
        !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (directory == 0u) {
        status = walk(fs, fs->root, path, &node, NULL, NULL);
        if (status != ASTRA_VFS_OK) goto done;
    }
    if (node->kind != ASTRA_VFS_KIND_DIRECTORY) {
        status = ASTRA_VFS_ERR_NOT_DIR;
        goto done;
    }
    entry = node->children;
    while (entry != NULL && cookie != 0u) {
        entry = entry->next;
        --cookie;
    }
    if (entry == NULL) {
        status = ASTRA_VFS_ERR_NOT_FOUND;
        goto done;
    }
    if (strlen(entry->name) + 1u > capacity) {
        status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        goto done;
    }
    strcpy(name, entry->name);
    info_of(entry->node, info);
    *next = entry->next == NULL ? 0u : position + 1u;
done:
    unlocked(fs);
    return status;
}

static uint32_t ram_mkdir(void *context, const char *path,
                          uint16_t create_mode)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *parent;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t status;

    if (fs == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = parent_of(fs, fs->root, path, &parent, name);
    if (status == ASTRA_VFS_OK)
        status = create(fs, parent, name, ASTRA_VFS_KIND_DIRECTORY,
                        create_mode == ASTRA_VFS_MODE_DEFAULT ?
                            0777u : create_mode, NULL, NULL);
    unlocked(fs);
    return status;
}

/* 0: legacy remove, 1: rmdir, 2: unlinkat without AT_REMOVEDIR. */
static uint32_t remove_from(AstraVfsRamBackend *fs, AstraVfsRamNode *base,
                            const char *path, int kind_rule)
{
    AstraVfsRamNode *parent;
    AstraVfsRamNode *node;
    RamEntry *entry;
    RamEntry **link;
    uint32_t status = walk(fs, base, path, &node, &parent, &entry);

    if (status != ASTRA_VFS_OK) return status;
    if (parent == NULL || entry == NULL)
        return ASTRA_VFS_ERR_ACCESS;
    if (kind_rule == 1 && node->kind != ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_NOT_DIR;
    if (kind_rule == 2 && node->kind == ASTRA_VFS_KIND_DIRECTORY)
        return ASTRA_VFS_ERR_IS_DIR;
    if (node->kind == ASTRA_VFS_KIND_DIRECTORY && node->children != NULL)
        return ASTRA_VFS_ERR_NOT_EMPTY;
    link = &parent->children;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next;
    released(fs, entry, sizeof(*entry));
    --node->links;
    free_node(fs, node);
    return ASTRA_VFS_OK;
}

static uint32_t ram_unlink(void *context, const char *path)
{
    AstraVfsRamBackend *fs = context;
    uint32_t status;

    if (fs == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = remove_from(fs, fs->root, path, 0);
    unlocked(fs);
    return status;
}

static uint32_t ram_unlink_at(void *context, uintptr_t directory,
                              const char *path, uint32_t flags)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *base = (AstraVfsRamNode *)directory;
    uint32_t status;

    if (fs == NULL || base == NULL || path == NULL || path[0] == '/' ||
        path[0] == '\0' ||
        (flags & ~ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = remove_from(fs, base, path,
                         (flags & ASTRA_VFS_AT_REMOVE_DIRECTORY) != 0u ?
                             1 : 2);
    unlocked(fs);
    return status;
}

static uint32_t ram_rename(void *context, const char *from, const char *to)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *source;
    AstraVfsRamNode *old_parent;
    AstraVfsRamNode *new_parent;
    AstraVfsRamNode *existing;
    RamEntry *old_entry;
    RamEntry *target_entry;
    RamEntry **link;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t status;

    if (fs == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = walk(fs, fs->root, from, &source, &old_parent, &old_entry);
    if (status != ASTRA_VFS_OK) goto done;
    if (old_parent == NULL || old_entry == NULL) {
        status = ASTRA_VFS_ERR_ACCESS;
        goto done;
    }
    status = parent_of(fs, fs->root, to, &new_parent, name);
    if (status != ASTRA_VFS_OK) goto done;
    if (to[strlen(to) - 1u] == '/' &&
        child(new_parent, name, strlen(name)) == NULL) {
        status = ASTRA_VFS_ERR_NOT_DIR;
        goto done;
    }
    for (AstraVfsRamNode *at = new_parent; at != NULL; at = at->parent)
        if (at == source) {
            status = ASTRA_VFS_ERR_INVALID;
            goto done;
        }
    target_entry = child(new_parent, name, strlen(name));
    if (to[strlen(to) - 1u] == '/' && target_entry != NULL &&
        target_entry->node->kind != ASTRA_VFS_KIND_DIRECTORY) {
        status = ASTRA_VFS_ERR_NOT_DIR;
        goto done;
    }
    if (target_entry == old_entry) {
        status = ASTRA_VFS_OK;
        goto done;
    }
    existing = target_entry == NULL ? NULL : target_entry->node;
    if (existing == source) {
        status = ASTRA_VFS_OK;
        goto done;
    }
    if (existing != NULL) {
        if (source->kind == ASTRA_VFS_KIND_DIRECTORY &&
            existing->kind != ASTRA_VFS_KIND_DIRECTORY) {
            status = ASTRA_VFS_ERR_NOT_DIR;
            goto done;
        }
        if (source->kind != ASTRA_VFS_KIND_DIRECTORY &&
            existing->kind == ASTRA_VFS_KIND_DIRECTORY) {
            status = ASTRA_VFS_ERR_IS_DIR;
            goto done;
        }
        if (existing->children != NULL) {
            status = ASTRA_VFS_ERR_NOT_EMPTY;
            goto done;
        }
        link = &new_parent->children;
        while (*link != target_entry) link = &(*link)->next;
        *link = target_entry->next;
        released(fs, target_entry, sizeof(*target_entry));
        --existing->links;
        free_node(fs, existing);
    }
    link = &old_parent->children;
    while (*link != old_entry) link = &(*link)->next;
    *link = old_entry->next;
    strcpy(old_entry->name, name);
    old_entry->next = new_parent->children;
    new_parent->children = old_entry;
    if (source->kind == ASTRA_VFS_KIND_DIRECTORY)
        source->parent = new_parent;
    status = ASTRA_VFS_OK;
done:
    unlocked(fs);
    return status;
}

static uint32_t ram_chmod(void *context, const char *path, uint16_t mode)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node;
    uint32_t status;

    if (fs == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = walk(fs, fs->root, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK) node->mode = mode;
    unlocked(fs);
    return status;
}

static uint32_t ram_chmod_node(void *context, uintptr_t handle,
                               uint16_t mode)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node = (AstraVfsRamNode *)handle;

    if (fs == NULL || node == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    node->mode = mode;
    unlocked(fs);
    return ASTRA_VFS_OK;
}

static uint32_t ram_chmod_at(void *context, uintptr_t directory,
                             const char *path, uint16_t mode,
                             uint32_t flags)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *base = (AstraVfsRamNode *)directory;
    AstraVfsRamNode *node;
    uint32_t status;

    if (fs == NULL || base == NULL ||
        (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW) != 0u || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = walk(fs, base, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK) node->mode = mode;
    unlocked(fs);
    return status;
}

static uint32_t ram_readlink(void *context, const char *path, void *buffer,
                             uint32_t capacity, uint32_t *length)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node;
    uint32_t status;

    if (fs == NULL || length == NULL ||
        (capacity != 0u && buffer == NULL) || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = walk(fs, fs->root, path, &node, NULL, NULL);
    if (status == ASTRA_VFS_OK && node->kind != ASTRA_VFS_KIND_SYMLINK)
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK) {
        *length = (uint32_t)node->size;
        if (node->size > capacity)
            status = ASTRA_VFS_ERR_BUFFER_TOO_SMALL;
        else
            memcpy(buffer, node->target, (size_t)node->size);
    }
    unlocked(fs);
    return status;
}

static uint32_t ram_symlink(void *context, const char *target,
                            const char *path)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *parent;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t status;

    if (fs == NULL || target == NULL || target[0] == '\0' ||
        strlen(target) >= ASTRA_VFS_PATH_MAX || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (path == NULL || path[0] == '\0' ||
        path[strlen(path) - 1u] == '/') {
        unlocked(fs);
        return ASTRA_VFS_ERR_NOT_DIR;
    }
    status = parent_of(fs, fs->root, path, &parent, name);
    if (status == ASTRA_VFS_OK)
        status = create(fs, parent, name, ASTRA_VFS_KIND_SYMLINK,
                        0777u, target, NULL);
    unlocked(fs);
    return status;
}

static uint32_t ram_link(void *context, const char *from, const char *to)
{
    AstraVfsRamBackend *fs = context;
    AstraVfsRamNode *node;
    AstraVfsRamNode *parent;
    RamEntry *entry;
    char name[ASTRA_VFS_NAME_MAX];
    uint32_t status;

    if (fs == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    status = walk(fs, fs->root, from, &node, NULL, NULL);
    if (status != ASTRA_VFS_OK) goto done;
    if (node->kind == ASTRA_VFS_KIND_DIRECTORY) {
        status = ASTRA_VFS_ERR_ACCESS;
        goto done;
    }
    status = parent_of(fs, fs->root, to, &parent, name);
    if (status != ASTRA_VFS_OK) goto done;
    if (to[strlen(to) - 1u] == '/') {
        status = ASTRA_VFS_ERR_NOT_DIR;
        goto done;
    }
    if (child(parent, name, strlen(name)) != NULL) {
        status = ASTRA_VFS_ERR_EXISTS;
        goto done;
    }
    if (node->links == UINT32_MAX) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    entry = charged(fs, sizeof(*entry));
    if (entry == NULL) {
        status = ASTRA_VFS_ERR_NO_SPACE;
        goto done;
    }
    strcpy(entry->name, name);
    entry->node = node;
    entry->next = parent->children;
    parent->children = entry;
    ++node->links;
    status = ASTRA_VFS_OK;
done:
    unlocked(fs);
    return status;
}

static uint32_t ram_filesystem_info(void *context, uintptr_t handle,
                                    const char *path,
                                    AstraVfsFilesystemInfo *info)
{
    AstraVfsRamBackend *fs = context;
    uint64_t free_bytes;
    uint32_t status = ASTRA_VFS_OK;

    if (fs == NULL || info == NULL || !locked(fs))
        return ASTRA_VFS_ERR_INVALID;
    if (handle == 0u) {
        AstraVfsRamNode *node;

        status = walk(fs, fs->root, path, &node, NULL, NULL);
    }
    if (status == ASTRA_VFS_OK) {
        memset(info, 0, sizeof(*info));
        free_bytes = fs->max_bytes - fs->used_bytes;
        info->size = sizeof(*info);
        info->block_size = RAM_PAGE_BYTES;
        info->fragment_size = RAM_PAGE_BYTES;
        info->blocks = fs->max_bytes / RAM_PAGE_BYTES;
        info->blocks_free = free_bytes / RAM_PAGE_BYTES;
        info->blocks_available = info->blocks_free;
        info->files = fs->node_count;
        info->name_max = ASTRA_VFS_NAME_MAX - 1u;
    }
    unlocked(fs);
    return status;
}

static const AstraVfsBackendOps ram_ops = {
    .open = ram_open,
    .close = ram_close,
    .read = ram_read,
    .write = ram_write,
    .sync = ram_sync,
    .truncate = ram_truncate,
    .stat = ram_stat,
    .readdir = ram_readdir,
    .mkdir = ram_mkdir,
    .unlink = ram_unlink,
    .rename = ram_rename,
    .chmod = ram_chmod,
    .readlink = ram_readlink,
    .symlink = ram_symlink,
    .link = ram_link,
    .open_at = ram_open_at,
    .unlink_at = ram_unlink_at,
    .chmod_node = ram_chmod_node,
    .chmod_at = ram_chmod_at,
    .filesystem_info = ram_filesystem_info,
    .stat_at = ram_stat_at,
    .stat_node = ram_stat_node,
};

const AstraVfsBackendOps *astra_vfs_ram_ops(void)
{
    return &ram_ops;
}
