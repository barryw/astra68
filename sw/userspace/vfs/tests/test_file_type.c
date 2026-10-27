#include <astra/file_type.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t *contents;
static uint32_t contents_length;
static uint16_t contents_kind = ASTRA_VFS_KIND_FILE;
static uint32_t fail_read;

uint32_t astra_filesystem_open(AstraFilesystem *filesystem, const char *path,
                               uint32_t flags, AstraFile *file)
{
    (void)filesystem;
    (void)path;
    (void)file;
    assert(flags == ASTRA_VFS_OPEN_READ);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_file_info(const AstraFile *file, AstraFileInfo *info)
{
    (void)file;
    assert(info->size == sizeof(*info));
    info->byte_size = contents_length;
    info->kind = contents_kind;
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_read_at(AstraFile *file, uint64_t offset,
                                  void *buffer, uint32_t length, uint32_t *moved)
{
    (void)file;
    assert(offset == 0u && length == 64u);
    if (fail_read) return ASTRA_VFS_ERR_LOST;
    *moved = contents_length < length ? contents_length : length;
    memcpy(buffer, contents, *moved);
    return ASTRA_VFS_OK;
}

uint32_t astra_filesystem_close(AstraFile *file)
{
    (void)file;
    return ASTRA_VFS_OK;
}

int main(void)
{
    AstraFilesystem filesystem = ASTRA_FILESYSTEM_INIT;
    AstraFileTypeInfo type = ASTRA_FILE_TYPE_INFO_INIT;
    uint8_t story[128] = {0};
    story[0] = 3u;
    story[0x08u] = 0u; story[0x09u] = 80u;
    story[0x0au] = 0u; story[0x0bu] = 88u;
    story[0x0cu] = 0u; story[0x0du] = 96u;
    story[0x0eu] = 0u; story[0x0fu] = 112u;
    contents = story;
    contents_length = sizeof(story);
    assert(astra_file_type_identify(&filesystem, "/dh0/story", &type) ==
           ASTRA_VFS_OK);
    assert(strcmp(type.type, ASTRA_FILE_TYPE_ZMACHINE) == 0);
    assert(type.source == ASTRA_FILE_TYPE_CONTENT && type.story_version == 3u);
    story[0] = 8u;
    assert(astra_file_type_identify(&filesystem, "/dh0/story", &type) ==
           ASTRA_VFS_OK && type.story_version == 8u);
    story[0] = 9u;
    assert(astra_file_type_identify(&filesystem, "/dh0/bad.z3", &type) ==
           ASTRA_VFS_OK && type.type[0] == '\0');
    story[0] = 3u;
    story[0x0fu] = 0u;
    assert(astra_file_type_identify(&filesystem, "/dh0/bad.z3", &type) ==
           ASTRA_VFS_OK && type.type[0] == '\0');
    contents_kind = ASTRA_VFS_KIND_DIRECTORY;
    assert(astra_file_type_identify(&filesystem, "/dh0/folder", &type) ==
           ASTRA_VFS_ERR_INVALID);
    contents_kind = ASTRA_VFS_KIND_FILE;
    fail_read = 1u;
    assert(astra_file_type_identify(&filesystem, "/dh0/story", &type) ==
           ASTRA_VFS_ERR_LOST);
    fail_read = 0u;
    contents = (const uint8_t *)"garbage";
    contents_length = 7u;
    assert(astra_file_type_identify(&filesystem, "/dh0/setup.apkg", &type) ==
           ASTRA_VFS_OK);
    assert(strcmp(type.type, ASTRA_FILE_TYPE_PACKAGE) == 0);
    assert(type.source == ASTRA_FILE_TYPE_EXTENSION_HINT);
    assert(astra_file_type_identify(&filesystem, "/dh0/unknown", &type) ==
           ASTRA_VFS_OK && type.source == ASTRA_FILE_TYPE_UNKNOWN);
    {
        uint8_t blorb[] = {'F','O','R','M',0,0,0,16,'I','F','R','S',
                           'R','I','d','x',0,0,0,4,0,0,0,0};
        contents = blorb;
        contents_length = sizeof(blorb);
        assert(astra_file_type_identify(&filesystem, "/dh0/game", &type) ==
               ASTRA_VFS_OK && strcmp(type.type, ASTRA_FILE_TYPE_BLORB) == 0);
        blorb[12] = 'X';
        assert(astra_file_type_identify(&filesystem, "/dh0/game", &type) ==
               ASTRA_VFS_OK && type.source == ASTRA_FILE_TYPE_UNKNOWN);
    }
    puts("file type tests passed");
    return 0;
}
