#include <astra/endian.h>
#include <astra/ascii.h>
#include <astra/vfs_provider_index.h>

#include <string.h>

#define PROVIDER_INDEX_MAGIC 0x41505256u /* "APRV" */
#define PROVIDER_INDEX_VERSION 2u
#define PROVIDER_INDEX_HEADER 24u

static int assign_valid(const char *assign, uint32_t *length)
{
    uint32_t at = 0u;

    if (assign == NULL || length == NULL)
        return 0;
    while (assign[at] != '\0') {
        char value = assign[at];

        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' ||
              value == '-'))
            return 0;
        ++at;
    }
    if (at == 0u)
        return 0;
    *length = at;
    return 1;
}

static int relative_path_valid(const uint8_t *path, uint32_t length)
{
    uint32_t segment = 0u;

    if (path == NULL || length == 0u || path[0] == '/' ||
        path[length - 1u] == '/')
        return 0;
    for (uint32_t at = 0u; at < length; ++at) {
        uint8_t value = path[at];

        if (value == '/') {
            if (segment == 0u ||
                (segment == 1u && path[at - 1u] == '.') ||
                (segment == 2u && path[at - 1u] == '.' &&
                 path[at - 2u] == '.'))
                return 0;
            segment = 0u;
            continue;
        }
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-'))
            return 0;
        ++segment;
    }
    return !((segment == 1u && path[length - 1u] == '.') ||
             (segment == 2u && path[length - 1u] == '.' &&
              path[length - 2u] == '.'));
}

static int reference_name_build(char target[ASTRA_LIBRARY_NAME_MAX],
                                const char *name, uint32_t name_length,
                                uint16_t abi)
{
    char digits[5];
    uint32_t digit_count = 0u;
    uint32_t value = abi;

    do {
        digits[digit_count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    if (name_length + 1u + digit_count >= ASTRA_LIBRARY_NAME_MAX)
        return 0;
    memcpy(target, name, name_length);
    target[name_length++] = '.';
    while (digit_count != 0u)
        target[name_length++] = digits[--digit_count];
    target[name_length] = '\0';
    return 1;
}

int astra_vfs_provider_identity_parse(const char *identity, char *name,
                                      uint32_t capacity, uint16_t *abi)
{
    uint32_t length = 0u;
    uint32_t separator = UINT32_MAX;
    uint32_t value = 0u;

    if (name != NULL && capacity != 0u)
        name[0] = '\0';
    if (abi != NULL)
        *abi = 0u;
    if (identity == NULL || name == NULL || capacity == 0u || abi == NULL)
        return 0;
    while (identity[length] != '\0') {
        char byte = identity[length];

        if (length + 1u >= ASTRA_LIBRARY_NAME_MAX + 6u ||
            !((byte >= 'a' && byte <= 'z') ||
              (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '.' ||
              byte == '_' || byte == '-'))
            return 0;
        if (byte == '.')
            separator = length;
        ++length;
    }
    if (separator == UINT32_MAX || separator == 0u ||
        separator + 1u == length || separator >= capacity ||
        separator >= ASTRA_LIBRARY_NAME_MAX)
        return 0;
    for (uint32_t at = separator + 1u; at < length; ++at) {
        uint32_t digit;

        if (identity[at] < '0' || identity[at] > '9')
            return 0;
        digit = (uint32_t)(identity[at] - '0');
        if (value > (UINT16_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    if (value == 0u ||
        (length - separator > 2u && identity[separator + 1u] == '0'))
        return 0;
    (void)memcpy(name, identity, separator);
    name[separator] = '\0';
    *abi = (uint16_t)value;
    return 1;
}

int astra_vfs_provider_index_parse(const uint8_t *bytes, uint32_t length,
                                   const char *assign, const char *name,
                                   uint16_t abi, char *path, uint32_t capacity,
                                   AstraLibraryReference *reference)
{
    uint32_t assign_length;
    uint32_t name_length = 0u;
    uint32_t path_length;

    if (path != NULL && capacity != 0u)
        path[0] = '\0';
    if (reference != NULL)
        *reference = (AstraLibraryReference){0};
    if (bytes == NULL || name == NULL || path == NULL || capacity == 0u ||
        reference == NULL ||
        length < PROVIDER_INDEX_HEADER + 1u ||
        astra_load_be32(bytes) != PROVIDER_INDEX_MAGIC ||
        !assign_valid(assign, &assign_length) ||
        astra_load_be16(bytes + 4u) != PROVIDER_INDEX_VERSION ||
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
    if (assign_length + 2u >= capacity ||
        path_length >= capacity - assign_length - 2u ||
        !relative_path_valid(bytes + PROVIDER_INDEX_HEADER, path_length))
        return 0;
    path[0] = '/';
    for (uint32_t at = 0u; at < assign_length; ++at)
        path[1u + at] = astra_ascii_lower(assign[at]);
    path[assign_length + 1u] = '/';
    for (uint32_t at = 0u; at < path_length; ++at) {
        uint8_t value = bytes[PROVIDER_INDEX_HEADER + at];

        path[assign_length + 2u + at] = (char)value;
    }
    path[assign_length + 2u + path_length] = '\0';
    reference->size = ASTRA_LIBRARY_REFERENCE_SIZE;
    reference->major = astra_load_be16(bytes + 8u);
    reference->minor = astra_load_be16(bytes + 10u);
    reference->patch = astra_load_be16(bytes + 12u);
    reference->abi_major = abi;
    reference->abi_minor = astra_load_be16(bytes + 16u);
    reference->build_id = astra_load_be32(bytes + 20u);
    return reference_name_build(reference->name, name, name_length, abi);
}
