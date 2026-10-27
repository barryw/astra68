#include <astra/file_type.h>
#include <astra/story.h>
#include <astra/string.h>

#include <string.h>

uint32_t astra_file_type_identify(AstraFilesystem *filesystem,
                                  const char *path, AstraFileTypeInfo *info)
{
    if (filesystem == NULL || path == NULL || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *info = (AstraFileTypeInfo)ASTRA_FILE_TYPE_INFO_INIT;
    AstraStoryInfo story = ASTRA_STORY_INFO_INIT;
    uint32_t status = astra_story_inspect(filesystem, path, &story);
    if (status != ASTRA_VFS_OK) return status;
    if (story.container == ASTRA_STORY_CONTAINER_BLORB)
        strcpy(info->type, ASTRA_FILE_TYPE_BLORB);
    else if (story.vm == ASTRA_STORY_VM_ZMACHINE) {
        strcpy(info->type, ASTRA_FILE_TYPE_ZMACHINE);
        info->story_version = story.z_version;
    } else if (story.vm == ASTRA_STORY_VM_GLULX)
        strcpy(info->type, ASTRA_FILE_TYPE_GLULX);
    else if (astra_string_has_suffix(path, ".apkg")) {
        strcpy(info->type, ASTRA_FILE_TYPE_PACKAGE);
        info->source = ASTRA_FILE_TYPE_EXTENSION_HINT;
    }
    if (info->type[0] != '\0' && info->source == ASTRA_FILE_TYPE_UNKNOWN)
        info->source = ASTRA_FILE_TYPE_CONTENT;
    return ASTRA_VFS_OK;
}
