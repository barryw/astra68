#include <astra/story.h>

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

static uint32_t read_exact(AstraFile *file, uint64_t offset, void *buffer,
                           uint32_t length)
{
    uint32_t moved = 0u;
    uint32_t status = astra_filesystem_read_at(file, offset, buffer, length,
                                               &moved);
    return status != ASTRA_VFS_OK ? status :
           moved == length ? ASTRA_VFS_OK : ASTRA_VFS_ERR_LOST;
}

static uint32_t z_version(const uint8_t *bytes, uint64_t length)
{
    if (length < 64u || bytes[0] < 1u || bytes[0] > 8u) return 0u;
    uint16_t static_base = be16(bytes + 0x0eu);
    uint16_t dictionary = be16(bytes + 0x08u);
    uint16_t objects = be16(bytes + 0x0au);
    uint16_t globals = be16(bytes + 0x0cu);
    return static_base >= 64u && static_base <= length &&
           dictionary >= 64u && dictionary < length &&
           objects >= 64u && objects < length &&
           globals >= 64u && globals < length ? bytes[0] : 0u;
}

static uint32_t glulx_version(const uint8_t *bytes, uint64_t length)
{
    if (length < 256u || memcmp(bytes, "Glul", 4u) != 0) return 0u;
    uint32_t version = be32(bytes + 4u);
    uint32_t ram = be32(bytes + 8u);
    uint32_t end_file = be32(bytes + 12u);
    uint32_t end_mem = be32(bytes + 16u);
    uint32_t stack = be32(bytes + 20u);
    uint32_t start = be32(bytes + 24u);
    return version != 0u && ram >= 256u && (ram & 255u) == 0u &&
           ram <= end_file && end_file == length &&
           end_mem >= end_file && (end_mem & 255u) == 0u &&
           stack != 0u && (stack & 255u) == 0u &&
           start >= 36u && start < end_file ? version : 0u;
}

static uint32_t inspect_payload(AstraFile *file, uint64_t offset,
                                uint64_t length, AstraStoryInfo *info)
{
    if (length == 0u) return ASTRA_VFS_OK;
    uint8_t header[64] = {0};
    uint32_t bytes = length < sizeof(header) ? (uint32_t)length :
                     (uint32_t)sizeof(header);
    uint32_t status = read_exact(file, offset, header, bytes);
    if (status != ASTRA_VFS_OK) return status;
    uint32_t version = z_version(header, length);
    if (version != 0u) {
        info->vm = ASTRA_STORY_VM_ZMACHINE;
        info->z_version = version;
    } else if (bytes >= 36u &&
               (version = glulx_version(header, length)) != 0u) {
        info->vm = ASTRA_STORY_VM_GLULX;
        info->glulx_version = version;
    }
    if (info->vm != ASTRA_STORY_VM_NONE) {
        info->executable_offset = offset;
        info->executable_size = length;
    }
    return ASTRA_VFS_OK;
}

static uint32_t inspect_blorb(AstraFile *file, const uint8_t *head,
                              uint64_t length, AstraStoryInfo *info)
{
    if (length < 24u || memcmp(head, "FORM", 4u) != 0 ||
        memcmp(head + 8u, "IFRSRIdx", 8u) != 0 ||
        (uint64_t)be32(head + 4u) + 8u != length)
        return ASTRA_VFS_OK;
    uint32_t index_size = be32(head + 16u);
    uint32_t count = be32(head + 20u);
    if (count > 65536u ||
        (uint64_t)index_size != 4u + (uint64_t)count * 12u ||
        (uint64_t)index_size + 20u > length)
        return ASTRA_VFS_OK;
    info->container = ASTRA_STORY_CONTAINER_BLORB;
    uint64_t first_resource = 20u + index_size + (index_size & 1u);
    uint32_t found = 0u;
    for (uint32_t at = 0u; at < count;) {
        uint8_t entries[64u * 12u];
        uint32_t batch = count - at < 64u ? count - at : 64u;
        uint32_t status = read_exact(file, 24u + (uint64_t)at * 12u,
                                     entries, batch * 12u);
        if (status != ASTRA_VFS_OK) return status;
        for (uint32_t i = 0u; i < batch; ++i) {
            const uint8_t *entry = entries + i * 12u;
            if (memcmp(entry, "Exec", 4u) != 0 || be32(entry + 4u) != 0u)
                continue;
            if (found++) {
                info->vm = ASTRA_STORY_VM_NONE;
                info->z_version = 0u;
                info->glulx_version = 0u;
                info->executable_offset = 0u;
                info->executable_size = 0u;
                return ASTRA_VFS_OK;
            }
            uint64_t chunk = be32(entry + 8u);
            if (chunk < first_resource || chunk > length || length - chunk < 8u)
                return ASTRA_VFS_OK;
            uint8_t header[8];
            status = read_exact(file, chunk, header, sizeof(header));
            if (status != ASTRA_VFS_OK) return status;
            uint64_t payload = be32(header + 4u);
            if (payload > length - chunk - 8u ||
                (memcmp(header, "ZCOD", 4u) != 0 &&
                 memcmp(header, "GLUL", 4u) != 0))
                return ASTRA_VFS_OK;
            status = inspect_payload(file, chunk + 8u, payload, info);
            if (status != ASTRA_VFS_OK) return status;
            if ((memcmp(header, "ZCOD", 4u) == 0 &&
                 info->vm != ASTRA_STORY_VM_ZMACHINE) ||
                (memcmp(header, "GLUL", 4u) == 0 &&
                 info->vm != ASTRA_STORY_VM_GLULX)) {
                info->vm = ASTRA_STORY_VM_NONE;
                info->z_version = 0u;
                info->glulx_version = 0u;
                info->executable_offset = 0u;
                info->executable_size = 0u;
            }
        }
        at += batch;
    }
    return ASTRA_VFS_OK;
}

uint32_t astra_story_inspect(AstraFilesystem *filesystem, const char *path,
                             AstraStoryInfo *info)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo file_info = {0};
    uint8_t head[64] = {0};
    uint32_t status;
    if (filesystem == NULL || path == NULL || info == NULL ||
        info->size != sizeof(*info)) return ASTRA_VFS_ERR_INVALID;
    *info = (AstraStoryInfo)ASTRA_STORY_INFO_INIT;
    status = astra_filesystem_open(filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) return status;
    file_info.size = sizeof(file_info);
    status = astra_filesystem_file_info(&file, &file_info);
    if (status == ASTRA_VFS_OK && file_info.kind != ASTRA_VFS_KIND_FILE)
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK) {
        uint32_t bytes = file_info.byte_size < sizeof(head) ?
            (uint32_t)file_info.byte_size : (uint32_t)sizeof(head);
        if (bytes != 0u) status = read_exact(&file, 0u, head, bytes);
        if (status == ASTRA_VFS_OK && bytes >= 24u &&
            memcmp(head, "FORM", 4u) == 0)
            status = inspect_blorb(&file, head, file_info.byte_size, info);
        else if (status == ASTRA_VFS_OK)
            status = inspect_payload(&file, 0u, file_info.byte_size, info);
    }
    uint32_t close_status = astra_filesystem_close(&file);
    return status != ASTRA_VFS_OK ? status : close_status;
}
