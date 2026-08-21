#include <astra/library_loader.h>
#include <astra/runtime.h>
#include <astra/syscall.h>

#include <stddef.h>
#include <stdint.h>

#define ELF_HEADER_SIZE 52u
#define ELF_PHENT_SIZE 32u
#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_PF_W 2u
#define ELF_DT_NULL 0u
#define ELF_DT_RELA 7u
#define ELF_DT_RELASZ 8u
#define ELF_DT_RELAENT 9u
#define ELF_RELA_SIZE 12u
#define ELF_R_68K_RELATIVE 22u

typedef struct LibraryCacheEntry {
    AstraLoadedLibrary library;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint8_t used;
} LibraryCacheEntry;

static LibraryCacheEntry cache[ASTRA_LIBRARY_SLOT_COUNT];

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static int name_equal(const char *left, const char *right)
{
    uint32_t index;

    if (left == NULL || right == NULL)
        return 0;
    for (index = 0u; index < ASTRA_LIBRARY_NAME_MAX; ++index) {
        if (left[index] != right[index])
            return 0;
        if (left[index] == '\0')
            return 1;
    }
    return 0;
}

static int reference_matches(const AstraLibraryReference *reference,
                             const AstraLibrary *identity)
{
    return reference != NULL && identity != NULL &&
           reference->size == ASTRA_LIBRARY_REFERENCE_SIZE &&
           reference->flags == ASTRA_LIBRARY_REFERENCE_EXACT &&
           reference->major == identity->major &&
           reference->minor == identity->minor &&
           reference->patch == identity->patch &&
           reference->abi_major == identity->abi_major &&
           reference->abi_minor == identity->abi_minor &&
           reference->build_id == identity->build_id &&
           name_equal(reference->name, identity->name);
}

static int identity_valid(const uint8_t *record, const char *expected_name,
                          uint16_t abi_major, uint16_t minimum_abi_minor)
{
    const char *name = (const char *)(record + 32u);

    return read_be32(record) == ASTRA_LIBRARY_MAGIC &&
           read_be16(record + 4u) == ASTRA_LIBRARY_RECORD_VERSION &&
           read_be16(record + 6u) == ASTRA_LIBRARY_SIZE &&
           read_be16(record + 14u) == abi_major &&
           read_be16(record + 16u) >= minimum_abi_minor &&
           read_be16(record + 18u) == 0u &&
           read_be32(record + 20u) == ASTRA_LIBRARY_TARGET_M68030 &&
           read_be32(record + 28u) == ASTRA_LIBRARY_EXPORTS_OFFSET &&
           name_equal(name, expected_name);
}

static int range_inside(uint32_t at, uint32_t size, uint32_t start,
                        uint32_t span)
{
    return size <= span && at >= start && at - start <= span - size;
}

static int segment_contains(const uint8_t *mapping, uint32_t header_offset,
                            uint16_t header_count, uint32_t at, uint32_t size,
                            int writable)
{
    uint16_t index;

    for (index = 0u; index < header_count; ++index) {
        const uint8_t *header = mapping + header_offset +
                                ((uint32_t)index * ELF_PHENT_SIZE);
        uint32_t start;
        uint32_t span;

        if (read_be32(header) != ELF_PT_LOAD ||
            (writable && (read_be32(header + 24u) & ELF_PF_W) == 0u))
            continue;
        start = read_be32(header + 8u);
        span = read_be32(header + 20u);
        if (range_inside(at, size, start, span))
            return 1;
    }
    return 0;
}

static uint32_t prepare_library(uint8_t *mapping, uint32_t logical_base,
                                uint32_t span, const char *expected_name,
                                uint16_t abi_major,
                                uint16_t minimum_abi_minor,
                                AstraLoadedLibrary *library)
{
    uint32_t header_offset;
    uint16_t header_count;
    uint32_t dynamic_at = 0u;
    uint32_t dynamic_size = 0u;
    uint32_t rela_at = 0u;
    uint32_t rela_size = 0u;
    uint32_t rela_entry = 0u;
    uint16_t index;

    if (mapping == NULL || library == NULL || span < ELF_HEADER_SIZE ||
        span <= ASTRA_LIBRARY_FILE_OFFSET + ASTRA_LIBRARY_SIZE ||
        mapping[0] != 0x7fu || mapping[1] != 'E' || mapping[2] != 'L' ||
        mapping[3] != 'F' || mapping[4] != 1u || mapping[5] != 2u ||
        read_be16(mapping + 16u) != 3u ||
        read_be16(mapping + 18u) != 4u || read_be32(mapping + 24u) != 0u ||
        !identity_valid(mapping + ASTRA_LIBRARY_FILE_OFFSET, expected_name,
                        abi_major, minimum_abi_minor))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    header_offset = read_be32(mapping + 28u);
    header_count = read_be16(mapping + 44u);
    if (read_be16(mapping + 42u) != ELF_PHENT_SIZE || header_count == 0u ||
        !range_inside(header_offset,
                      (uint32_t)header_count * ELF_PHENT_SIZE, 0u, span))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    for (index = 0u; index < header_count; ++index) {
        const uint8_t *header = mapping + header_offset +
                                ((uint32_t)index * ELF_PHENT_SIZE);

        if (read_be32(header) == ELF_PT_DYNAMIC) {
            if (dynamic_at != 0u)
                return ASTRA_SYSCALL_INVALID_ARGUMENT;
            dynamic_at = read_be32(header + 8u);
            dynamic_size = read_be32(header + 20u);
        }
    }
    if (dynamic_at == 0u || dynamic_size < 8u ||
        !segment_contains(mapping, header_offset, header_count, dynamic_at,
                          dynamic_size, 1))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    for (uint32_t at = 0u; at + 8u <= dynamic_size; at += 8u) {
        const uint8_t *entry = mapping + dynamic_at + at;
        uint32_t tag = read_be32(entry);
        uint32_t value = read_be32(entry + 4u);

        if (tag == ELF_DT_NULL)
            break;
        if (tag == ELF_DT_RELA)
            rela_at = value;
        else if (tag == ELF_DT_RELASZ)
            rela_size = value;
        else if (tag == ELF_DT_RELAENT)
            rela_entry = value;
    }
    if (rela_at == 0u || rela_size == 0u || rela_entry != ELF_RELA_SIZE ||
        (rela_size % ELF_RELA_SIZE) != 0u ||
        !segment_contains(mapping, header_offset, header_count, rela_at,
                          rela_size, 0))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;

    for (uint32_t at = 0u; at < rela_size; at += ELF_RELA_SIZE) {
        const uint8_t *relocation = mapping + rela_at + at;
        uint32_t target = read_be32(relocation);
        uint32_t info = read_be32(relocation + 4u);
        uint32_t addend = read_be32(relocation + 8u);

        if ((info & 0xffu) != ELF_R_68K_RELATIVE || (info >> 8) != 0u ||
            !segment_contains(mapping, header_offset, header_count, target,
                              4u, 1) ||
            !segment_contains(mapping, header_offset, header_count, addend,
                              1u, 0))
            return ASTRA_SYSCALL_INVALID_ARGUMENT;
        write_be32(mapping + target, logical_base + addend);
    }

    if (!segment_contains(mapping, header_offset, header_count,
                          ASTRA_LIBRARY_EXPORTS_OFFSET, 4u, 1))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    library->identity = (const AstraLibrary *)(const void *)(
        mapping + ASTRA_LIBRARY_FILE_OFFSET);
    library->exports = mapping + ASTRA_LIBRARY_EXPORTS_OFFSET;
    library->base = logical_base;
    library->span = span;
    return ASTRA_SYSCALL_OK;
}

#if defined(ASTRA_LIBRARY_LOADER_TEST)
uint32_t astra_library_test_prepare(
    void *mapping, uint32_t logical_base, uint32_t span,
    const char *expected_name, uint16_t abi_major,
    uint16_t minimum_abi_minor, AstraLoadedLibrary *library)
{
    return prepare_library(mapping, logical_base, span, expected_name,
                           abi_major, minimum_abi_minor, library);
}
#endif

uint32_t astra_library_find(const char *name, uint16_t abi_major,
                            uint16_t minimum_abi_minor,
                            const AstraLoadedLibrary **library)
{
    uint32_t index;

    if (name == NULL || library == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    for (index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        if (cache[index].used != 0u &&
            cache[index].abi_major == abi_major &&
            cache[index].abi_minor >= minimum_abi_minor &&
            name_equal(cache[index].library.identity->name, name)) {
            *library = &cache[index].library;
            return ASTRA_SYSCALL_OK;
        }
    }
    return ASTRA_SYSCALL_WOULD_BLOCK;
}

static uint32_t unused_slot(void)
{
    for (uint32_t index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index)
        if (cache[index].used == 0u)
            return index;
    return ASTRA_LIBRARY_SLOT_COUNT;
}

static uint32_t accept_attached(uint32_t index, uint32_t base, uint32_t span,
                                const char *name, uint16_t abi_major,
                                uint16_t minimum_abi_minor,
                                const AstraLibraryReference *exact,
                                const AstraLoadedLibrary **library)
{
    uint32_t status = prepare_library((uint8_t *)(uintptr_t)base, base, span,
                                      name, abi_major, minimum_abi_minor,
                                      &cache[index].library);

    if (status != ASTRA_SYSCALL_OK ||
        (exact != NULL &&
         !reference_matches(exact, cache[index].library.identity)))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    cache[index].abi_major = abi_major;
    cache[index].abi_minor = cache[index].library.identity->abi_minor;
    cache[index].used = 1u;
    *library = &cache[index].library;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_library_attach(const AstraLibraryReference *reference,
                              const AstraLoadedLibrary **library)
{
    uint32_t base;
    uint32_t span;
    uint32_t status;
    uint32_t index;

    if (reference == NULL || library == NULL || reference->abi_major == 0u ||
        (reference->major == 0u && reference->minor == 0u &&
         reference->patch == 0u) || reference->size !=
            ASTRA_LIBRARY_REFERENCE_SIZE ||
        reference->flags != ASTRA_LIBRARY_REFERENCE_EXACT)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    for (index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        if (cache[index].used != 0u &&
            reference_matches(reference, cache[index].library.identity)) {
            *library = &cache[index].library;
            return ASTRA_SYSCALL_OK;
        }
    }
    index = unused_slot();
    if (index == ASTRA_LIBRARY_SLOT_COUNT)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    status = astra_rt_library_attach(reference, &base, &span);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    return accept_attached(index, base, span, reference->name,
                           reference->abi_major, reference->abi_minor,
                           reference, library);
}

uint32_t astra_library_attach_cached(const char *name, uint16_t abi_major,
                                     uint16_t minimum_abi_minor,
                                     const AstraLoadedLibrary **library)
{
    AstraLibraryReference request = {
        .size = ASTRA_LIBRARY_REFERENCE_SIZE,
        .abi_major = abi_major,
        .abi_minor = minimum_abi_minor,
        .flags = ASTRA_LIBRARY_REFERENCE_LATEST,
    };
    uint32_t base;
    uint32_t span;
    uint32_t status;
    uint32_t index;

    if (name == NULL || library == NULL || abi_major == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_library_find(name, abi_major, minimum_abi_minor, library);
    if (status == ASTRA_SYSCALL_OK)
        return status;
    for (index = 0u; index + 1u < ASTRA_LIBRARY_NAME_MAX &&
                     name[index] != '\0'; ++index)
        request.name[index] = name[index];
    if (index == 0u || name[index] != '\0')
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    index = unused_slot();
    if (index == ASTRA_LIBRARY_SLOT_COUNT)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    status = astra_rt_library_attach(&request, &base, &span);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    return accept_attached(index, base, span, name, abi_major,
                           minimum_abi_minor, NULL, library);
}

uint32_t astra_library_load(const void *image, uint32_t length,
                            const char *expected_name, uint16_t abi_major,
                            uint16_t minimum_abi_minor,
                            const AstraLoadedLibrary **library)
{
    const uint8_t *bytes = image;
    uint32_t index;
    uint32_t status;
    uint32_t span;
    uint32_t base;

    if (image == NULL || expected_name == NULL || library == NULL ||
        length <= ASTRA_LIBRARY_FILE_OFFSET + ASTRA_LIBRARY_SIZE ||
        length > ASTRA_LIBRARY_IMAGE_MAX ||
        !identity_valid(bytes + ASTRA_LIBRARY_FILE_OFFSET, expected_name,
                        abi_major, minimum_abi_minor))
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_library_find(expected_name, abi_major, minimum_abi_minor,
                                library);
    if (status == ASTRA_SYSCALL_OK)
        return status;
    for (index = 0u; index < ASTRA_LIBRARY_SLOT_COUNT; ++index) {
        if (cache[index].used == 0u)
            break;
    }
    if (index == ASTRA_LIBRARY_SLOT_COUNT)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;

    status = astra_rt_library_map(image, length, &base, &span);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = prepare_library((uint8_t *)(uintptr_t)base, base, span,
                             expected_name, abi_major, minimum_abi_minor,
                             &cache[index].library);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    cache[index].abi_major = abi_major;
    cache[index].abi_minor =
        cache[index].library.identity->abi_minor;
    cache[index].used = 1u;
    *library = &cache[index].library;
    return ASTRA_SYSCALL_OK;
}
