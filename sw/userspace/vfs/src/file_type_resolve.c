#include <astra/file_type.h>

#include <string.h>

uint32_t astra_file_type_resolve(AstraProcessFilesystem *filesystem,
    const AstraApplicationCatalog *catalog, const char *path,
    AstraFileTypeInfo *info)
{
    if (filesystem == NULL || catalog == NULL || path == NULL || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    uint32_t status = astra_file_type_identify(&filesystem->filesystem,
                                                path, info);
    if (status != ASTRA_VFS_OK || info->type[0] != '\0') return status;

    const char *dot = NULL;
    for (const char *at = path; *at != '\0'; ++at) {
        if (*at == '/') dot = NULL;
        else if (*at == '.') dot = at;
    }
    if (dot == NULL) return ASTRA_VFS_OK;
    size_t length = strlen(dot);
    if (length < 2u || length >= ASTRA_BUNDLE_EXTENSION_MAX)
        return ASTRA_VFS_OK;
    char extension[ASTRA_BUNDLE_EXTENSION_MAX];
    for (size_t at = 0u; at <= length; ++at) {
        char value = dot[at];
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        extension[at] = value;
    }
    status = astra_application_catalog_extension_type(filesystem, catalog,
        extension, info->type, sizeof(info->type));
    if (status == ASTRA_VFS_OK && info->type[0] != '\0')
        info->source = ASTRA_FILE_TYPE_EXTENSION_HINT;
    return status;
}
