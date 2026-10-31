#include "../../../common/folder_ops.h"

#include <assert.h>
#include <string.h>

static char created[ASTRA_VFS_PATH_MAX];
static uint32_t attempts;
static char renamed_from[ASTRA_VFS_PATH_MAX];
static char renamed_to[ASTRA_VFS_PATH_MAX];
static char unlinked[ASTRA_VFS_PATH_MAX];
static uint32_t rename_status;
static uint32_t source_kind = ASTRA_VFS_KIND_DIRECTORY;
static uint32_t unlinked_flags;
static char created_file[ASTRA_VFS_PATH_MAX];
static char created_link[ASTRA_VFS_PATH_MAX];
static char link_target[ASTRA_VFS_PATH_MAX];

uint32_t astra_filesystem_lstat(AstraFilesystem *filesystem,
    const char *path, AstraFileInfo *info)
{
    assert(filesystem != NULL && path != NULL && info != NULL);
    info->kind = source_kind;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_open_mode(AstraFilesystem *filesystem,
    const char *path, uint32_t flags, uint16_t mode, AstraFile *file)
{
    assert(filesystem != NULL && file != NULL && mode == 0600u);
    assert(flags == (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
        ASTRA_VFS_OPEN_EXCLUSIVE));
    strcpy(created_file, path);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    assert(file != NULL);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_symlink(const char *target,
    AstraFilesystem *filesystem, const char *path)
{
    assert(filesystem != NULL && target != NULL && path != NULL);
    strcpy(created_link, path);
    strcpy(link_target, target);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_mkdir(AstraFilesystem *filesystem, const char *path)
{
    assert(filesystem != NULL);
    assert(strlen(path) < sizeof(created));
    strcpy(created, path);
    return ++attempts < 3u ? ASTRA_VFS_ERR_EXISTS : ASTRA_VFS_OK;
}

uint32_t astra_filesystem_rename(AstraFilesystem *filesystem,
    const char *from, const char *to)
{
    assert(filesystem != NULL);
    strcpy(renamed_from, from);
    strcpy(renamed_to, to);
    return rename_status;
}

uint32_t astra_filesystem_unlink(AstraFilesystem *filesystem,
    const char *path, uint32_t flags)
{
    assert(filesystem != NULL);
    strcpy(unlinked, path);
    unlinked_flags = flags;
    return ASTRA_VFS_OK;
}

int main(void)
{
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    char path[ASTRA_VFS_PATH_MAX];
    assert(astra_folder_name_valid("Folder") &&
        astra_folder_name_valid("Pictures 📷"));
    assert(!astra_folder_name_valid("") && !astra_folder_name_valid(".") &&
        !astra_folder_name_valid("..") && !astra_folder_name_valid("a/b") &&
        !astra_folder_name_valid("bad\nname"));
    assert(astra_folder_path("/home/Desktop", "Folder1", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(path, "/home/Desktop/Folder1") == 0);
    assert(astra_folder_path("/home/Desktop", "Folder1", path, 8u) == ASTRA_VFS_ERR_LIMIT);
    assert(astra_folder_create_numbered(&filesystem, "/home/Desktop", "Folder",
        path, sizeof(path)) == ASTRA_VFS_OK);
    assert(attempts == 3u && strcmp(path, "/home/Desktop/Folder3") == 0 &&
        strcmp(created, path) == 0);
    attempts = 2u;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/Folder3",
        "/home/Desktop", "Pictures", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(renamed_from, "/home/Desktop/Folder3") == 0 &&
        strcmp(renamed_to, "/home/Desktop/Pictures") == 0);
    rename_status = ASTRA_VFS_ERR_IO;
    attempts = 2u;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/Pictures",
        "/home/Desktop", "Photos", path, sizeof(path)) == ASTRA_VFS_ERR_IO);
    assert(strcmp(unlinked, "/home/Desktop/Photos") == 0 &&
        unlinked_flags == ASTRA_VFS_UNLINK_DIRECTORY);
    rename_status = ASTRA_VFS_OK;
    attempts = 2u;
    assert(astra_entry_move_to_trash(&filesystem, "/home/Desktop/Pictures",
        "/home/.Trash", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(path, "/home/.Trash/Pictures-1") == 0 &&
        strcmp(renamed_to, path) == 0);
    source_kind = ASTRA_VFS_KIND_FILE;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/note.txt",
        "/home/Desktop", "draft.txt", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(created_file, "/home/Desktop/draft.txt") == 0 &&
        strcmp(renamed_to, created_file) == 0);
    rename_status = ASTRA_VFS_ERR_IO;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/note.txt",
        "/home/Desktop", "final.txt", path, sizeof(path)) == ASTRA_VFS_ERR_IO);
    assert(strcmp(unlinked, "/home/Desktop/final.txt") == 0 &&
        unlinked_flags == 0u);
    rename_status = ASTRA_VFS_OK;
    attempts = 2u;
    assert(astra_entry_move_to_trash(&filesystem, "/home/Desktop/note.txt",
        "/home/.Trash", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(created_file, "/home/.Trash/note.txt-1") == 0 &&
        strcmp(renamed_to, created_file) == 0);
    source_kind = ASTRA_VFS_KIND_SYMLINK;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/old-link",
        "/home/Desktop", "new-link", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(created_link, "/home/Desktop/new-link") == 0 &&
        strcmp(link_target, created_link) == 0 &&
        strcmp(renamed_to, created_link) == 0);
    rename_status = ASTRA_VFS_ERR_IO;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/old-link",
        "/home/Desktop", "failed-link", path, sizeof(path)) == ASTRA_VFS_ERR_IO);
    assert(strcmp(unlinked, "/home/Desktop/failed-link") == 0 &&
        unlinked_flags == 0u);
    rename_status = ASTRA_VFS_OK;
    attempts = 2u;
    assert(astra_entry_move_to_trash(&filesystem, "/home/Desktop/new-link",
        "/home/.Trash", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(created_link, "/home/.Trash/new-link-1") == 0 &&
        strcmp(renamed_to, created_link) == 0);
    return 0;
}
