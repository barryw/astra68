#include "directory_snapshot.h"
#include "folder_ops.h"

#include <astra/runtime.h>
#include <astra/vfs_path.h>

#include <string.h>

typedef struct TreeFrame {
    struct TreeFrame *parent;
    AstraDirectorySnapshot snapshot;
    uint32_t next;
    char path[];
} TreeFrame;

static TreeFrame *frame_new(const char *path, TreeFrame *parent)
{
    size_t length = strlen(path);
    if (length >= ASTRA_VFS_PATH_MAX) return NULL;
    TreeFrame *frame = astra_runtime_allocate(sizeof(*frame) + length + 1u);
    if (frame == NULL) return NULL;
    frame->parent = parent;
    frame->snapshot = (AstraDirectorySnapshot)ASTRA_DIRECTORY_SNAPSHOT_INIT;
    frame->next = 0u;
    memcpy(frame->path, path, length + 1u);
    return frame;
}

static TreeFrame *frame_close(TreeFrame *frame)
{
    TreeFrame *parent = frame->parent;
    astra_directory_snapshot_dispose(&frame->snapshot);
    astra_runtime_deallocate(frame);
    return parent;
}

uint32_t astra_directory_tree_walk(AstraFilesystem *filesystem,
    const char *directory, AstraDirectoryTreeVisit visit, void *context,
    const uint32_t *cancelled)
{
    if (filesystem == NULL || directory == NULL || visit == NULL ||
        directory[0] != '/' || strlen(directory) >= ASTRA_VFS_PATH_MAX ||
        astra_path_requires_walk(directory))
        return ASTRA_VFS_ERR_INVALID;
    char *child_path = astra_runtime_allocate(ASTRA_VFS_PATH_MAX);
    if (child_path == NULL) return ASTRA_VFS_ERR_LIMIT;
    TreeFrame *frame = frame_new(directory, NULL);
    if (frame == NULL) {
        astra_runtime_deallocate(child_path);
        return ASTRA_VFS_ERR_LIMIT;
    }
    uint32_t status = astra_directory_snapshot_load(&frame->snapshot,
        filesystem, frame->path, cancelled);
    while (frame != NULL && status == ASTRA_VFS_OK) {
        if (cancelled != NULL &&
            __atomic_load_n(cancelled, __ATOMIC_ACQUIRE) != 0u) {
            status = ASTRA_DIRECTORY_SNAPSHOT_CANCELLED;
            break;
        }
        if (frame->next == frame->snapshot.count) {
            frame = frame_close(frame);
            continue;
        }
        const AstraDirectoryEntry *entry = &frame->snapshot.entries[frame->next++];
        status = astra_folder_path(frame->path, entry->name, child_path,
            ASTRA_VFS_PATH_MAX);
        if (status != ASTRA_VFS_OK) break;
        uint32_t descend = 1u;
        status = visit(context, child_path, entry, &descend);
        if (status != ASTRA_VFS_OK) break;
        if (descend != 0u && entry->kind == ASTRA_VFS_KIND_DIRECTORY) {
            TreeFrame *child = frame_new(child_path, frame);
            if (child == NULL) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            frame = child;
            status = astra_directory_snapshot_load(&frame->snapshot,
                filesystem, frame->path, cancelled);
        }
    }
    while (frame != NULL) frame = frame_close(frame);
    astra_runtime_deallocate(child_path);
    return status;
}
