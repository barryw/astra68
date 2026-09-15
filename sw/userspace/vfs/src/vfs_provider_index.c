#include <astra/endian.h>
#include <astra/vfs_provider_index.h>

#include <string.h>

#define PROVIDER_INDEX_MAGIC 0x41505256u /* "APRV" */
#define PROVIDER_INDEX_HEADER 24u

int astra_vfs_provider_index_parse(const uint8_t *bytes, uint32_t length,
                                   const char *name, uint16_t abi, char *path,
                                   uint32_t capacity,
                                   AstraLibraryReference *reference)
{
    uint32_t name_length = 0u;
    uint32_t path_length;

    if (path != NULL && capacity != 0u)
        path[0] = '\0';
    if (reference != NULL)
        *reference = (AstraLibraryReference){0};
    if (bytes == NULL || name == NULL || path == NULL || capacity == 0u ||
        reference == NULL ||
        length < PROVIDER_INDEX_HEADER + 6u ||
        astra_load_be32(bytes) != PROVIDER_INDEX_MAGIC ||
        astra_load_be16(bytes + 4u) != 1u ||
        astra_load_be16(bytes + 6u) != PROVIDER_INDEX_HEADER ||
        astra_load_be16(bytes + 14u) != abi ||
        astra_load_be16(bytes + 18u) != 0u)
        return 0;
    while (name_length < ASTRA_LIBRARY_NAME_MAX &&
           name[name_length] != '\0')
        ++name_length;
    if (name_length == 0u || name_length == ASTRA_LIBRARY_NAME_MAX)
        return 0;
    path_length = length - PROVIDER_INDEX_HEADER;
    if (path_length >= capacity || path_length < 6u ||
        memcmp(bytes + PROVIDER_INDEX_HEADER, "LIBS:", 5u) != 0)
        return 0;
    for (uint32_t at = 0u; at < path_length; ++at) {
        uint8_t value = bytes[PROVIDER_INDEX_HEADER + at];

        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == ':' ||
              value == '/' || value == '.' || value == '_' ||
              value == '-'))
            return 0;
        path[at] = (char)value;
    }
    path[path_length] = '\0';
    reference->size = ASTRA_LIBRARY_REFERENCE_SIZE;
    reference->major = astra_load_be16(bytes + 8u);
    reference->minor = astra_load_be16(bytes + 10u);
    reference->patch = astra_load_be16(bytes + 12u);
    reference->abi_major = abi;
    reference->abi_minor = astra_load_be16(bytes + 16u);
    reference->build_id = astra_load_be32(bytes + 20u);
    memcpy(reference->name, name, name_length);
    return 1;
}
