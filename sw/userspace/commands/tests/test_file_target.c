#include "../file_target.h"

#include <assert.h>
#include <string.h>

static uint32_t stat_status;
static uint32_t stat_kind;

uint32_t astra_filesystem_stat(AstraFilesystem *filesystem, const char *path,
    AstraFileInfo *info)
{
    (void)filesystem;
    assert(strcmp(path, "/net0") == 0);
    info->kind = stat_kind;
    return stat_status;
}

int astra_string_concat(char *out, uint32_t capacity, const char *text)
{
    size_t used = strlen(out);
    size_t added = strlen(text);
    if (used + added >= capacity) return 0;
    memcpy(out + used, text, added + 1u);
    return 1;
}

int main(void)
{
    AstraFilesystem filesystem = {0};
    char joined[ASTRA_VFS_PATH_MAX];
    const char *target = NULL;

    stat_status = ASTRA_VFS_ERR_NOT_FOUND;
    assert(astra_command_file_target(&filesystem, "/ram0/save", "/net0",
        joined, &target) == ASTRA_VFS_OK);
    assert(strcmp(target, "/net0") == 0);

    stat_status = ASTRA_VFS_OK;
    stat_kind = ASTRA_VFS_KIND_FILE;
    assert(astra_command_file_target(&filesystem, "/ram0/save", "/net0",
        joined, &target) == ASTRA_VFS_ERR_EXISTS);

    stat_kind = ASTRA_VFS_KIND_DIRECTORY;
    assert(astra_command_file_target(&filesystem, "/ram0/save", "/net0",
        joined, &target) == ASTRA_VFS_OK);
    assert(strcmp(target, "/net0/save") == 0);

    stat_status = ASTRA_VFS_ERR_ACCESS;
    assert(astra_command_file_target(&filesystem, "/ram0/save", "/net0",
        joined, &target) == ASTRA_VFS_ERR_ACCESS);
    return 0;
}
