#include <astra/file_type.h>
#include <astra/string.h>

#include <string.h>

static uint16_t be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | bytes[3];
}

static int blorb_header(const uint8_t *bytes, uint32_t count, uint64_t size)
{
    if (count < 24u || size < 24u ||
        memcmp(bytes, "FORM", 4u) != 0 ||
        memcmp(bytes + 8u, "IFRSRIdx", 8u) != 0 ||
        (uint64_t)be32(bytes + 4u) + 8u != size)
        return 0;
    uint32_t index_size = be32(bytes + 16u);
    uint32_t entries = be32(bytes + 20u);
    return index_size >= 4u &&
           (uint64_t)index_size == 4u + (uint64_t)entries * 12u &&
           (uint64_t)index_size + 20u <= size;
}

static uint32_t zmachine_version(const uint8_t *bytes, uint32_t count,
                                 uint64_t size)
{
    if (count < 64u || bytes[0] < 1u || bytes[0] > 8u || size < 64u)
        return 0u;
    uint16_t static_base = be16(bytes + 0x0eu);
    uint16_t dictionary = be16(bytes + 0x08u);
    uint16_t objects = be16(bytes + 0x0au);
    uint16_t globals = be16(bytes + 0x0cu);
    if (static_base < 64u || static_base > size ||
        dictionary < 64u || dictionary >= size ||
        objects < 64u || objects >= size ||
        globals < 64u || globals >= size)
        return 0u;
    return bytes[0];
}

static int glulx_header(const uint8_t *bytes, uint32_t count, uint64_t size)
{
    if (count < 36u || size < 256u || memcmp(bytes, "Glul", 4u) != 0)
        return 0;
    uint32_t ram = be32(bytes + 8u);
    uint32_t end_file = be32(bytes + 12u);
    uint32_t end_mem = be32(bytes + 16u);
    uint32_t stack = be32(bytes + 20u);
    uint32_t start = be32(bytes + 24u);
    return be32(bytes + 4u) != 0u && ram >= 256u &&
           (ram & 255u) == 0u && ram <= end_file && end_file == size &&
           end_mem >= end_file && (end_mem & 255u) == 0u &&
           stack != 0u && (stack & 255u) == 0u &&
           start >= 36u && start < end_file;
}

uint32_t astra_file_type_identify(AstraFilesystem *filesystem,
                                  const char *path, AstraFileTypeInfo *info)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo file_info = {0};
    uint8_t header[64];
    uint32_t moved = 0u;
    uint32_t result;

    if (filesystem == NULL || path == NULL || info == NULL)
        return ASTRA_VFS_ERR_INVALID;
    *info = (AstraFileTypeInfo)ASTRA_FILE_TYPE_INFO_INIT;
    result = astra_filesystem_open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (result != ASTRA_VFS_OK) return result;
    file_info.size = sizeof(file_info);
    result = astra_filesystem_file_info(&file, &file_info);
    if (result == ASTRA_VFS_OK && file_info.kind != ASTRA_VFS_KIND_FILE)
        result = ASTRA_VFS_ERR_INVALID;
    if (result == ASTRA_VFS_OK)
        result = astra_filesystem_read_at(&file, 0u, header,
                                         sizeof(header), &moved);
    uint32_t close_result = astra_filesystem_close(&file);
    if (result != ASTRA_VFS_OK) return result;
    if (close_result != ASTRA_VFS_OK) return close_result;

    uint32_t version = zmachine_version(header, moved, file_info.byte_size);
    if (version != 0u) {
        strcpy(info->type, ASTRA_FILE_TYPE_ZMACHINE);
        info->source = ASTRA_FILE_TYPE_CONTENT;
        info->story_version = version;
    } else if (blorb_header(header, moved, file_info.byte_size)) {
        strcpy(info->type, ASTRA_FILE_TYPE_BLORB);
        info->source = ASTRA_FILE_TYPE_CONTENT;
    } else if (glulx_header(header, moved, file_info.byte_size)) {
        strcpy(info->type, ASTRA_FILE_TYPE_GLULX);
        info->source = ASTRA_FILE_TYPE_CONTENT;
    } else if (astra_string_has_suffix(path, ".apkg")) {
        strcpy(info->type, ASTRA_FILE_TYPE_PACKAGE);
        info->source = ASTRA_FILE_TYPE_EXTENSION_HINT;
    }
    return ASTRA_VFS_OK;
}
