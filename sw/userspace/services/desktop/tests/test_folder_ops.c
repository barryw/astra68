#include "../../../common/folder_ops.h"

#include <assert.h>
#include <string.h>

static char created[ASTRA_VFS_PATH_MAX];
static uint32_t attempts;
static char renamed_from[ASTRA_VFS_PATH_MAX];
static char renamed_to[ASTRA_VFS_PATH_MAX];
static char unlinked[ASTRA_VFS_PATH_MAX];
static uint32_t rename_status;

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
    assert(filesystem != NULL && flags == ASTRA_VFS_UNLINK_DIRECTORY);
    strcpy(unlinked, path);
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
    assert(astra_folder_rename_safe(&filesystem, "/home/Desktop/Folder3",
        "/home/Desktop", "Pictures", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(renamed_from, "/home/Desktop/Folder3") == 0 &&
        strcmp(renamed_to, "/home/Desktop/Pictures") == 0);
    rename_status = ASTRA_VFS_ERR_IO;
    attempts = 2u;
    assert(astra_folder_rename_safe(&filesystem, "/home/Desktop/Pictures",
        "/home/Desktop", "Photos", path, sizeof(path)) == ASTRA_VFS_ERR_IO);
    assert(strcmp(unlinked, "/home/Desktop/Photos") == 0);
    rename_status = ASTRA_VFS_OK;
    attempts = 2u;
    assert(astra_folder_move_to_trash(&filesystem, "/home/Desktop/Pictures",
        "/home/.Trash", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(path, "/home/.Trash/Pictures-1") == 0 &&
        strcmp(renamed_to, path) == 0);
    return 0;
}
