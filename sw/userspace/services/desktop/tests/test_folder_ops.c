#include "../../../common/folder_ops.h"

#include <assert.h>
#include <string.h>

static char created[ASTRA_VFS_PATH_MAX];
static uint32_t mkdir_attempts;
static uint32_t mkdir_collisions;
static char renamed_from[ASTRA_VFS_PATH_MAX];
static char renamed_to[ASTRA_VFS_PATH_MAX];
static uint32_t rename_attempts;
static uint32_t rename_collisions;
static uint32_t rename_status;

uint32_t astra_filesystem_mkdir(AstraFilesystem *filesystem, const char *path)
{
    assert(filesystem != NULL && path != NULL);
    assert(strlen(path) < sizeof(created));
    strcpy(created, path);
    ++mkdir_attempts;
    return mkdir_attempts <= mkdir_collisions ?
        ASTRA_VFS_ERR_EXISTS : ASTRA_VFS_OK;
}

uint32_t astra_filesystem_rename_noreplace(AstraFilesystem *filesystem,
    const char *from, const char *to)
{
    assert(filesystem != NULL && from != NULL && to != NULL);
    strcpy(renamed_from, from);
    strcpy(renamed_to, to);
    ++rename_attempts;
    return rename_attempts <= rename_collisions ?
        ASTRA_VFS_ERR_EXISTS : rename_status;
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
    assert(astra_folder_path("/home/Desktop", "Folder1", path,
        sizeof(path)) == ASTRA_VFS_OK);
    assert(strcmp(path, "/home/Desktop/Folder1") == 0);
    assert(astra_folder_path("/home/Desktop", "Folder1", path,
        8u) == ASTRA_VFS_ERR_LIMIT);

    mkdir_collisions = 2u;
    assert(astra_folder_create_numbered(&filesystem, "/home/Desktop",
        "Folder", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(mkdir_attempts == 3u &&
        strcmp(path, "/home/Desktop/Folder3") == 0 &&
        strcmp(created, path) == 0);

    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/Folder3",
        "/home/Desktop", "Pictures", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(rename_attempts == 1u &&
        strcmp(renamed_from, "/home/Desktop/Folder3") == 0 &&
        strcmp(renamed_to, "/home/Desktop/Pictures") == 0);
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/Pictures",
        "/home/Desktop", "Pictures", path, sizeof(path)) == ASTRA_VFS_OK);
    assert(rename_attempts == 1u);
    rename_collisions = 2u;
    assert(astra_entry_rename_safe(&filesystem, "/home/Desktop/Pictures",
        "/home/Desktop", "Photos", path, sizeof(path)) ==
        ASTRA_VFS_ERR_EXISTS);
    assert(rename_attempts == 2u);

    return 0;
}
