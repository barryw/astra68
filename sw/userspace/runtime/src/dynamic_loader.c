#include <astra/dynamic_loader.h>
#include <astra/endian.h>
#include <astra/limits.h>
#include <astra/tls.h>

#define ELF_HEADER_SIZE 52u
#define ELF_PROGRAM_HEADER_SIZE 32u
#define ELF_DYNAMIC_ENTRY_SIZE 8u
#define ELF_SYMBOL_SIZE 16u
#define ELF_RELA_SIZE 12u

#define ELF_ET_EXEC 2u
#define ELF_ET_DYN 3u
#define ELF_EM_68K 4u
#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_PT_INTERP 3u
#define ELF_PT_TLS 7u
#define ELF_PT_GNU_RELRO 0x6474e552u
#define ELF_PF_X 1u
#define ELF_PF_W 2u
#define ELF_PF_R 4u

#define ELF_DT_NULL 0u
#define ELF_DT_NEEDED 1u
#define ELF_DT_PLTRELSZ 2u
#define ELF_DT_PLTGOT 3u
#define ELF_DT_HASH 4u
#define ELF_DT_STRTAB 5u
#define ELF_DT_SYMTAB 6u
#define ELF_DT_RELA 7u
#define ELF_DT_RELASZ 8u
#define ELF_DT_RELAENT 9u
#define ELF_DT_STRSZ 10u
#define ELF_DT_SYMENT 11u
#define ELF_DT_INIT 12u
#define ELF_DT_FINI 13u
#define ELF_DT_SONAME 14u
#define ELF_DT_RPATH 15u
#define ELF_DT_SYMBOLIC 16u
#define ELF_DT_REL 17u
#define ELF_DT_RELSZ 18u
#define ELF_DT_RELENT 19u
#define ELF_DT_PLTREL 20u
#define ELF_DT_DEBUG 21u
#define ELF_DT_TEXTREL 22u
#define ELF_DT_JMPREL 23u
#define ELF_DT_BIND_NOW 24u
#define ELF_DT_INIT_ARRAY 25u
#define ELF_DT_FINI_ARRAY 26u
#define ELF_DT_INIT_ARRAYSZ 27u
#define ELF_DT_FINI_ARRAYSZ 28u
#define ELF_DT_RUNPATH 29u
#define ELF_DT_FLAGS 30u
#define ELF_DT_PREINIT_ARRAY 32u
#define ELF_DT_PREINIT_ARRAYSZ 33u
#define ELF_DT_GNU_HASH 0x6ffffef5u
#define ELF_DT_VERSYM 0x6ffffff0u
#define ELF_DT_VERDEF 0x6ffffffcu
#define ELF_DT_VERDEFNUM 0x6ffffffdu
#define ELF_DT_VERNEED 0x6ffffffeu
#define ELF_DT_VERNEEDNUM 0x6fffffffu
#define ELF_DT_FLAGS_1 0x6ffffffbu

#define ELF_DF_TEXTREL 0x4u
#define ELF_DF_BIND_NOW 0x8u
#define ELF_DF_1_NOW 0x1u

#define ELF_R_68K_NONE 0u
#define ELF_R_68K_32 1u
#define ELF_R_68K_PC32 4u
#define ELF_R_68K_COPY 19u
#define ELF_R_68K_GLOB_DAT 20u
#define ELF_R_68K_JMP_SLOT 21u
#define ELF_R_68K_RELATIVE 22u
#define ELF_R_68K_TLS_DTPMOD32 40u
#define ELF_R_68K_TLS_DTPREL32 41u
#define ELF_R_68K_TLS_TPREL32 42u

#define ELF_SHN_UNDEF 0u
#define ELF_SHN_ABS 0xfff1u
#define ELF_STB_GLOBAL 1u
#define ELF_STB_WEAK 2u
#define ELF_STT_TLS 6u
#define ELF_STV_INTERNAL 1u
#define ELF_STV_HIDDEN 2u

typedef struct DynamicFields {
    uint32_t soname;
    uint32_t rela_entry;
    uint32_t plt_rela_type;
    uint32_t flags;
    uint32_t flags1;
    uint32_t rel_address;
    uint32_t rel_size;
    uint32_t rel_entry;
    uint32_t init;
    uint32_t fini;
    uint32_t init_array;
    uint32_t init_array_size;
    uint32_t fini_array;
    uint32_t fini_array_size;
    uint32_t preinit_array;
    uint32_t preinit_array_size;
    uint8_t bind_now;
    uint8_t tag_seen[41];
} DynamicFields;

static int checked_add(uint32_t left, uint32_t right, uint32_t *sum)
{
    if (left > UINT32_MAX - right)
        return 0;
    *sum = left + right;
    return 1;
}

static int checked_multiply(uint32_t left, uint32_t right, uint32_t *product)
{
    if (left != 0u && right > UINT32_MAX / left)
        return 0;
    *product = left * right;
    return 1;
}

static uint8_t *mapped(const AstraDynamicImage *image, uint32_t address,
                       uint32_t size)
{
    if (image == NULL || size > image->mapping_span ||
        address > image->mapping_span - size ||
        image->mapping_origin > UINTPTR_MAX - address)
        return NULL;
    return (uint8_t *)(image->mapping_origin + address);
}

static uint8_t *mapped_offset(const AstraDynamicImage *image, uint32_t base,
                              uint32_t offset, uint32_t size)
{
    uint32_t address;

    return checked_add(base, offset, &address) ?
        mapped(image, address, size) : NULL;
}

static const uint8_t *program_header(const AstraDynamicImage *image,
                                     uint32_t index)
{
    uint32_t offset;

    if (index >= image->program_header_count ||
        !checked_multiply(index, ELF_PROGRAM_HEADER_SIZE, &offset) ||
        !checked_add(image->program_headers, offset, &offset))
        return NULL;
    return mapped(image, offset, ELF_PROGRAM_HEADER_SIZE);
}

static int load_contains(const AstraDynamicImage *image, uint32_t address,
                         uint32_t size, uint32_t required_flags,
                         int file_backed)
{
    uint32_t end;

    if (!checked_add(address, size, &end))
        return 0;
    for (uint32_t index = 0u; index < image->program_header_count; ++index) {
        const uint8_t *header = program_header(image, index);
        uint32_t start;
        uint32_t span;
        uint32_t load_end;

        if (header == NULL || astra_load_be32(header) != ELF_PT_LOAD ||
            (astra_load_be32(header + 24u) & required_flags) != required_flags)
            continue;
        start = astra_load_be32(header + 8u);
        span = astra_load_be32(header + (file_backed ? 16u : 20u));
        if (checked_add(start, span, &load_end) && address >= start &&
            end <= load_end)
            return 1;
    }
    return 0;
}

static int relro_covered_by_load(const AstraDynamicImage *image,
                                 const uint8_t *relro)
{
    uint32_t file_offset = astra_load_be32(relro + 4u);
    uint32_t address = astra_load_be32(relro + 8u);
    uint32_t file_size = astra_load_be32(relro + 16u);
    uint32_t memory_size = astra_load_be32(relro + 20u);
    uint32_t file_end;
    uint32_t memory_end;

    if (astra_load_be32(relro + 24u) != ELF_PF_R || memory_size == 0u ||
        file_size > memory_size ||
        !checked_add(file_offset, file_size, &file_end) ||
        !checked_add(address, memory_size, &memory_end) ||
        mapped(image, address, memory_size) == NULL)
        return 0;
    for (uint32_t index = 0u; index < image->program_header_count; ++index) {
        const uint8_t *load = program_header(image, index);
        uint32_t load_file_offset;
        uint32_t load_file_end;
        uint32_t load_address;
        uint32_t load_memory_size;
        uint32_t load_memory_end;
        uint32_t load_mapping_end;

        if (load == NULL || astra_load_be32(load) != ELF_PT_LOAD ||
            (astra_load_be32(load + 24u) & ELF_PF_W) == 0u)
            continue;
        load_file_offset = astra_load_be32(load + 4u);
        load_address = astra_load_be32(load + 8u);
        load_memory_size = astra_load_be32(load + 20u);
        if (!checked_add(load_file_offset, astra_load_be32(load + 16u),
                         &load_file_end) ||
            !checked_add(load_address, load_memory_size, &load_memory_end) ||
            load_memory_end > UINT32_MAX - (ASTRA_MEMORY_PAGE_SIZE - 1u))
            continue;
        load_mapping_end =
            (load_memory_end + ASTRA_MEMORY_PAGE_SIZE - 1u) &
            ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
        if (address < load_address || memory_end > load_mapping_end)
            continue;
        if (file_size == 0u)
            return 1;
        if (file_offset >= load_file_offset && file_end <= load_file_end &&
            file_offset - load_file_offset == address - load_address)
            return 1;
    }
    return 0;
}

static int string_at(const AstraDynamicImage *image, uint32_t offset,
                     const char **result)
{
    const uint8_t *text;

    if (offset >= image->string_size)
        return 0;
    text = mapped_offset(image, image->string_address, offset,
                         image->string_size - offset);
    if (text == NULL)
        return 0;
    for (uint32_t index = 0u; index < image->string_size - offset; ++index) {
        if (text[index] == 0u) {
            *result = (const char *)(const void *)text;
            return 1;
        }
    }
    return 0;
}

static int text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return left == right;
    while (*left == *right) {
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
    return 0;
}

static int record_dynamic_tag(DynamicFields *fields, uint32_t tag)
{
    uint32_t index;

    if (tag == ELF_DT_NEEDED)
        return 1;
    if (tag <= ELF_DT_PREINIT_ARRAYSZ) {
        index = tag;
    } else {
        switch (tag) {
        case ELF_DT_GNU_HASH: index = 34u; break;
        case ELF_DT_VERSYM: index = 35u; break;
        case ELF_DT_FLAGS_1: index = 36u; break;
        case ELF_DT_VERDEF: index = 37u; break;
        case ELF_DT_VERDEFNUM: index = 38u; break;
        case ELF_DT_VERNEED: index = 39u; break;
        case ELF_DT_VERNEEDNUM: index = 40u; break;
        default: return 1;
        }
    }
    if (fields->tag_seen[index] != 0u)
        return 0;
    fields->tag_seen[index] = 1u;
    return 1;
}

static AstraDynamicStatus parse_dynamic(AstraDynamicImage *image,
                                        DynamicFields *fields)
{
    uint8_t terminated = 0u;

    for (uint32_t offset = 0u; offset < image->dynamic_size;
         offset += ELF_DYNAMIC_ENTRY_SIZE) {
        const uint8_t *entry = mapped_offset(image, image->dynamic_address,
                                             offset,
                                             ELF_DYNAMIC_ENTRY_SIZE);
        uint32_t tag;
        uint32_t value;

        if (entry == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
        tag = astra_load_be32(entry);
        value = astra_load_be32(entry + 4u);
        if (tag == ELF_DT_NULL) {
            terminated = 1u;
            break;
        }
        if (!record_dynamic_tag(fields, tag))
            return ASTRA_DYNAMIC_BAD_TABLE;
        switch (tag) {
        case ELF_DT_NEEDED:
            ++image->needed_count;
            break;
        case ELF_DT_HASH:
            image->hash_address = value;
            break;
        case ELF_DT_STRTAB:
            image->string_address = value;
            break;
        case ELF_DT_STRSZ:
            image->string_size = value;
            break;
        case ELF_DT_SYMTAB:
            image->symbol_address = value;
            break;
        case ELF_DT_SYMENT:
            if (value != ELF_SYMBOL_SIZE)
                return ASTRA_DYNAMIC_UNSUPPORTED;
            break;
        case ELF_DT_RELA:
            image->rela_address = value;
            break;
        case ELF_DT_RELASZ:
            image->rela_size = value;
            break;
        case ELF_DT_RELAENT:
            fields->rela_entry = value;
            break;
        case ELF_DT_JMPREL:
            image->jump_rela_address = value;
            break;
        case ELF_DT_PLTRELSZ:
            image->jump_rela_size = value;
            break;
        case ELF_DT_PLTREL:
            fields->plt_rela_type = value;
            break;
        case ELF_DT_SONAME:
            fields->soname = value;
            break;
        case ELF_DT_BIND_NOW:
            fields->bind_now = 1u;
            break;
        case ELF_DT_FLAGS:
            fields->flags = value;
            break;
        case ELF_DT_FLAGS_1:
            fields->flags1 = value;
            break;
        case ELF_DT_REL:
            fields->rel_address = value;
            break;
        case ELF_DT_RELSZ:
            fields->rel_size = value;
            break;
        case ELF_DT_RELENT:
            fields->rel_entry = value;
            break;
        case ELF_DT_RPATH:
        case ELF_DT_RUNPATH:
        case ELF_DT_TEXTREL:
            return ASTRA_DYNAMIC_UNSUPPORTED;
        case ELF_DT_SYMBOLIC:
            /* Astra resolves every symbol defined by an image to that image
             * before consulting its dependency closure.  This is exactly the
             * self-first binding requested by DT_SYMBOLIC and emitted by the
             * canonical .library link policy. */
            break;
        case ELF_DT_INIT:
            fields->init = value;
            image->init_address = value;
            break;
        case ELF_DT_FINI:
            fields->fini = value;
            image->fini_address = value;
            break;
        case ELF_DT_INIT_ARRAY:
            fields->init_array = value;
            image->init_array_address = value;
            break;
        case ELF_DT_INIT_ARRAYSZ:
            fields->init_array_size = value;
            image->init_array_size = value;
            break;
        case ELF_DT_FINI_ARRAY:
            fields->fini_array = value;
            image->fini_array_address = value;
            break;
        case ELF_DT_FINI_ARRAYSZ:
            fields->fini_array_size = value;
            image->fini_array_size = value;
            break;
        case ELF_DT_PREINIT_ARRAY:
            fields->preinit_array = value;
            image->preinit_array_address = value;
            break;
        case ELF_DT_PREINIT_ARRAYSZ:
            fields->preinit_array_size = value;
            image->preinit_array_size = value;
            break;
        case ELF_DT_VERSYM:
            image->version_symbols = value;
            break;
        case ELF_DT_VERDEF:
            image->version_definitions = value;
            break;
        case ELF_DT_VERDEFNUM:
            image->version_definition_count = value;
            break;
        case ELF_DT_VERNEED:
            image->version_needs = value;
            break;
        case ELF_DT_VERNEEDNUM:
            image->version_need_count = value;
            break;
        case ELF_DT_PLTGOT:
        case ELF_DT_DEBUG:
        case ELF_DT_GNU_HASH:
            break;
        default:
            /* OS/processor tags may carry optional metadata. Required runtime
             * behavior must still be represented by the validated tags above. */
            break;
        }
    }
    if (terminated == 0u)
        return ASTRA_DYNAMIC_BAD_TABLE;
    if (fields->tag_seen[ELF_DT_SYMENT] == 0u)
        return ASTRA_DYNAMIC_BAD_TABLE;
    if ((fields->flags & ELF_DF_TEXTREL) != 0u || fields->rel_address != 0u ||
        fields->rel_size != 0u || fields->rel_entry != 0u)
        return ASTRA_DYNAMIC_UNSUPPORTED;
    if (fields->bind_now == 0u &&
        (fields->flags & ELF_DF_BIND_NOW) == 0u &&
        (fields->flags1 & ELF_DF_1_NOW) == 0u)
        return ASTRA_DYNAMIC_UNSUPPORTED;
    return ASTRA_DYNAMIC_OK;
}

static int table_range(const AstraDynamicImage *image, uint32_t address,
                       uint32_t size)
{
    return size == 0u ||
           (mapped(image, address, size) != NULL &&
            load_contains(image, address, size, ELF_PF_R, 1));
}

AstraDynamicStatus astra_dynamic_open(uintptr_t mapping_origin,
                                      uint32_t mapping_span,
                                      uint32_t header_address,
                                      uint32_t load_bias,
                                      AstraDynamicImage *image)
{
    const uint8_t *header;
    DynamicFields fields = {0};
    uint32_t ph_size;
    uint32_t hash_words;
    const uint8_t *hash;
    uint8_t dynamic_seen = 0u;
    uint8_t interpreter_seen = 0u;
    uint8_t tls_seen = 0u;
    uint8_t relro_seen = 0u;

    if (image == NULL || mapping_span < ELF_HEADER_SIZE)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    *image = (AstraDynamicImage){0};
    image->mapping_origin = mapping_origin;
    image->mapping_span = mapping_span;
    image->header_address = header_address;
    image->load_bias = load_bias;
    header = mapped(image, header_address, ELF_HEADER_SIZE);
    if (header == NULL || header[0] != 0x7fu || header[1] != 'E' ||
        header[2] != 'L' || header[3] != 'F' || header[4] != 1u ||
        header[5] != 2u || header[6] != 1u ||
        (header[7] != 0u && header[7] != 3u) ||
        (astra_load_be16(header + 16u) != ELF_ET_EXEC &&
         astra_load_be16(header + 16u) != ELF_ET_DYN) ||
        astra_load_be16(header + 18u) != ELF_EM_68K ||
        astra_load_be32(header + 20u) != 1u ||
        astra_load_be16(header + 42u) != ELF_PROGRAM_HEADER_SIZE ||
        astra_load_be16(header + 44u) == 0u)
        return ASTRA_DYNAMIC_BAD_ELF;
    image->elf_type = astra_load_be16(header + 16u);
    image->program_header_count = astra_load_be16(header + 44u);
    if (!checked_add(header_address, astra_load_be32(header + 28u),
                     &image->program_headers) ||
        !checked_multiply(image->program_header_count,
                          ELF_PROGRAM_HEADER_SIZE, &ph_size) ||
        mapped(image, image->program_headers, ph_size) == NULL)
        return ASTRA_DYNAMIC_BAD_ELF;

    for (uint32_t index = 0u; index < image->program_header_count; ++index) {
        const uint8_t *ph = program_header(image, index);
        uint32_t type;

        if (ph == NULL)
            return ASTRA_DYNAMIC_BAD_ELF;
        type = astra_load_be32(ph);
        if (type == ELF_PT_DYNAMIC) {
            if (dynamic_seen != 0u)
                return ASTRA_DYNAMIC_BAD_TABLE;
            dynamic_seen = 1u;
            image->dynamic_address = astra_load_be32(ph + 8u);
            image->dynamic_size = astra_load_be32(ph + 20u);
            if (image->dynamic_size < ELF_DYNAMIC_ENTRY_SIZE ||
                (image->dynamic_size & (ELF_DYNAMIC_ENTRY_SIZE - 1u)) != 0u ||
                astra_load_be32(ph + 16u) != image->dynamic_size ||
                astra_load_be32(ph + 24u) != (ELF_PF_R | ELF_PF_W) ||
                !load_contains(image, image->dynamic_address,
                               image->dynamic_size, ELF_PF_R | ELF_PF_W, 1))
                return ASTRA_DYNAMIC_BAD_TABLE;
        } else if (type == ELF_PT_INTERP) {
            const uint8_t *identity;

            if (interpreter_seen != 0u || image->elf_type != ELF_ET_EXEC)
                return ASTRA_DYNAMIC_BAD_ELF;
            interpreter_seen = 1u;
            image->interpreter_address = astra_load_be32(ph + 8u);
            image->interpreter_size = astra_load_be32(ph + 16u);
            if (image->interpreter_size < 2u ||
                astra_load_be32(ph + 20u) != image->interpreter_size ||
                astra_load_be32(ph + 24u) != ELF_PF_R ||
                !load_contains(image, image->interpreter_address,
                               image->interpreter_size, ELF_PF_R, 1))
                return ASTRA_DYNAMIC_BAD_ELF;
            identity = mapped(image, image->interpreter_address,
                              image->interpreter_size);
            if (identity == NULL || identity[0] == 0u ||
                identity[image->interpreter_size - 1u] != 0u)
                return ASTRA_DYNAMIC_BAD_STRING;
            for (uint32_t byte = 0u;
                 byte + 1u < image->interpreter_size; ++byte)
                if (identity[byte] == 0u)
                    return ASTRA_DYNAMIC_BAD_STRING;
        } else if (type == ELF_PT_TLS) {
            if (tls_seen != 0u)
                return ASTRA_DYNAMIC_BAD_TABLE;
            tls_seen = 1u;
            image->tls_address = astra_load_be32(ph + 8u);
            image->tls_file_size = astra_load_be32(ph + 16u);
            image->tls_memory_size = astra_load_be32(ph + 20u);
            image->tls_alignment = astra_load_be32(ph + 28u);
            if (image->tls_file_size > image->tls_memory_size ||
                image->tls_alignment == 0u ||
                (image->tls_alignment & (image->tls_alignment - 1u)) != 0u ||
                !table_range(image, image->tls_address,
                             image->tls_file_size))
                return ASTRA_DYNAMIC_BAD_TABLE;
        } else if (type == ELF_PT_GNU_RELRO) {
            if (relro_seen != 0u)
                return ASTRA_DYNAMIC_BAD_TABLE;
            relro_seen = 1u;
            image->relro_address = astra_load_be32(ph + 8u);
            image->relro_size = astra_load_be32(ph + 20u);
            if (!relro_covered_by_load(image, ph))
                return ASTRA_DYNAMIC_BAD_TABLE;
        }
    }
    if (dynamic_seen == 0u ||
        (image->elf_type == ELF_ET_EXEC && interpreter_seen == 0u) ||
        (image->elf_type == ELF_ET_DYN && interpreter_seen != 0u))
        return ASTRA_DYNAMIC_BAD_TABLE;
    {
        AstraDynamicStatus status = parse_dynamic(image, &fields);

        if (status != ASTRA_DYNAMIC_OK)
            return status;
    }
    if (image->hash_address == 0u || image->string_address == 0u ||
        image->string_size == 0u || image->symbol_address == 0u ||
        !table_range(image, image->hash_address, 8u) ||
        !table_range(image, image->string_address, image->string_size))
        return ASTRA_DYNAMIC_BAD_TABLE;
    hash = mapped(image, image->hash_address, 8u);
    if (hash == NULL || astra_load_be32(hash) == 0u)
        return ASTRA_DYNAMIC_BAD_TABLE;
    image->symbol_count = astra_load_be32(hash + 4u);
    if (image->symbol_count == 0u ||
        !checked_add(astra_load_be32(hash), image->symbol_count,
                     &hash_words) ||
        !checked_add(hash_words, 2u, &hash_words) ||
        !checked_multiply(hash_words, 4u, &hash_words) ||
        !table_range(image, image->hash_address, hash_words) ||
        !checked_multiply(image->symbol_count, ELF_SYMBOL_SIZE, &ph_size) ||
        !table_range(image, image->symbol_address, ph_size))
        return ASTRA_DYNAMIC_BAD_TABLE;
    if ((image->rela_size != 0u &&
         (image->rela_address == 0u || fields.rela_entry != ELF_RELA_SIZE ||
          (image->rela_size % ELF_RELA_SIZE) != 0u ||
          !table_range(image, image->rela_address, image->rela_size))) ||
        (image->jump_rela_size != 0u &&
         (image->jump_rela_address == 0u ||
          fields.plt_rela_type != ELF_DT_RELA ||
          (image->jump_rela_size % ELF_RELA_SIZE) != 0u ||
          !table_range(image, image->jump_rela_address,
                       image->jump_rela_size))))
        return ASTRA_DYNAMIC_BAD_TABLE;
    if (image->elf_type == ELF_ET_DYN) {
        const char *soname;

        if (fields.tag_seen[ELF_DT_SONAME] == 0u ||
            !string_at(image, fields.soname, &soname) || *soname == '\0')
            return ASTRA_DYNAMIC_BAD_STRING;
    }
    if (!checked_multiply(image->symbol_count, 2u, &ph_size) ||
        (image->version_symbols != 0u &&
         !table_range(image, image->version_symbols, ph_size)) ||
        ((image->version_definitions == 0u) !=
         (image->version_definition_count == 0u)) ||
        ((image->version_needs == 0u) != (image->version_need_count == 0u)))
        return ASTRA_DYNAMIC_BAD_TABLE;
    if ((image->init_address != 0u &&
         !load_contains(image, image->init_address, 1u,
                        ELF_PF_R | ELF_PF_X, 1)) ||
        (image->fini_address != 0u &&
         !load_contains(image, image->fini_address, 1u,
                        ELF_PF_R | ELF_PF_X, 1)) ||
        (image->init_array_size & 3u) != 0u ||
        (image->fini_array_size & 3u) != 0u ||
        (image->preinit_array_size & 3u) != 0u ||
        ((image->init_array_address == 0u) !=
         (image->init_array_size == 0u)) ||
        ((image->fini_array_address == 0u) !=
         (image->fini_array_size == 0u)) ||
        ((image->preinit_array_address == 0u) !=
         (image->preinit_array_size == 0u)) ||
        !table_range(image, image->init_array_address,
                     image->init_array_size) ||
        !table_range(image, image->fini_array_address,
                     image->fini_array_size) ||
        !table_range(image, image->preinit_array_address,
                     image->preinit_array_size) ||
        (image->elf_type != ELF_ET_EXEC &&
         image->preinit_array_size != 0u))
        return ASTRA_DYNAMIC_BAD_TABLE;
    return ASTRA_DYNAMIC_OK;
}

AstraDynamicStatus astra_dynamic_interpreter(
    const AstraDynamicImage *image, const char **name)
{
    const uint8_t *identity;

    if (image == NULL || name == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    *name = NULL;
    if (image->elf_type != ELF_ET_EXEC || image->interpreter_address == 0u ||
        image->interpreter_size < 2u)
        return ASTRA_DYNAMIC_BAD_ELF;
    identity = mapped(image, image->interpreter_address,
                      image->interpreter_size);
    if (identity == NULL || identity[0] == 0u ||
        identity[image->interpreter_size - 1u] != 0u)
        return ASTRA_DYNAMIC_BAD_STRING;
    *name = (const char *)(const void *)identity;
    return ASTRA_DYNAMIC_OK;
}

int astra_dynamic_writable_range(const AstraDynamicImage *image,
                                 uint32_t address, uint32_t size)
{
    return image != NULL && size != 0u &&
           load_contains(image, address, size, ELF_PF_W, 0);
}

AstraDynamicStatus astra_dynamic_needed(const AstraDynamicImage *image,
                                        uint32_t index, const char **name)
{
    uint32_t found = 0u;

    if (image == NULL || name == NULL || index >= image->needed_count)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    for (uint32_t offset = 0u; offset < image->dynamic_size;
         offset += ELF_DYNAMIC_ENTRY_SIZE) {
        const uint8_t *entry = mapped_offset(image, image->dynamic_address,
                                             offset,
                                             ELF_DYNAMIC_ENTRY_SIZE);
        uint32_t tag;

        if (entry == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
        tag = astra_load_be32(entry);
        if (tag == ELF_DT_NULL)
            break;
        if (tag == ELF_DT_NEEDED && found++ == index)
            return string_at(image, astra_load_be32(entry + 4u), name) ?
                ASTRA_DYNAMIC_OK : ASTRA_DYNAMIC_BAD_STRING;
    }
    return ASTRA_DYNAMIC_BAD_TABLE;
}

AstraDynamicStatus astra_dynamic_soname(const AstraDynamicImage *image,
                                        const char **name)
{
    if (image == NULL || name == NULL || image->elf_type != ELF_ET_DYN)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    *name = NULL;
    for (uint32_t offset = 0u; offset < image->dynamic_size;
         offset += ELF_DYNAMIC_ENTRY_SIZE) {
        const uint8_t *entry = mapped_offset(image, image->dynamic_address,
                                             offset,
                                             ELF_DYNAMIC_ENTRY_SIZE);
        uint32_t tag;

        if (entry == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
        tag = astra_load_be32(entry);
        if (tag == ELF_DT_NULL)
            break;
        if (tag == ELF_DT_SONAME)
            return string_at(image, astra_load_be32(entry + 4u), name) ?
                ASTRA_DYNAMIC_OK : ASTRA_DYNAMIC_BAD_STRING;
    }
    return ASTRA_DYNAMIC_BAD_TABLE;
}

static uint32_t elf_hash(const char *name)
{
    uint32_t hash = 0u;

    while (*name != '\0') {
        uint32_t high;

        hash = (hash << 4) + (uint8_t)*name++;
        high = hash & 0xf0000000u;
        if (high != 0u)
            hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

static AstraDynamicStatus symbol_version(const AstraDynamicImage *image,
                                         uint32_t symbol_index,
                                         int definition,
                                         const char **version,
                                         int *hidden)
{
    const uint8_t *versym;
    uint16_t version_index;
    uint32_t table;
    uint32_t count;

    *version = NULL;
    *hidden = 0;
    if (image->version_symbols == 0u)
        return ASTRA_DYNAMIC_OK;
    {
        uint32_t offset;

        if (!checked_multiply(symbol_index, 2u, &offset))
            return ASTRA_DYNAMIC_BAD_TABLE;
        versym = mapped_offset(image, image->version_symbols, offset, 2u);
    }
    if (versym == NULL)
        return ASTRA_DYNAMIC_BAD_TABLE;
    version_index = astra_load_be16(versym);
    *hidden = (version_index & 0x8000u) != 0u;
    version_index &= 0x7fffu;
    if (version_index <= 1u)
        return ASTRA_DYNAMIC_OK;
    table = definition ? image->version_definitions : image->version_needs;
    count = definition ? image->version_definition_count :
                         image->version_need_count;
    for (uint32_t item = 0u; item < count; ++item) {
        const uint8_t *record;
        uint32_t next;

        record = mapped(image, table, definition ? 20u : 16u);
        if (record == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
        if (definition) {
            uint16_t record_index = astra_load_be16(record + 4u) & 0x7fffu;
            uint32_t auxiliary = astra_load_be32(record + 12u);
            const uint8_t *aux;

            if (record_index == version_index) {
                if (auxiliary == 0u ||
                    !checked_add(table, auxiliary, &auxiliary))
                    return ASTRA_DYNAMIC_BAD_TABLE;
                aux = mapped(image, auxiliary, 8u);
                if (aux == NULL ||
                    !string_at(image, astra_load_be32(aux), version))
                    return ASTRA_DYNAMIC_BAD_STRING;
                return ASTRA_DYNAMIC_OK;
            }
            next = astra_load_be32(record + 16u);
        } else {
            uint16_t auxiliary_count = astra_load_be16(record + 2u);
            uint32_t auxiliary_offset = astra_load_be32(record + 8u);
            uint32_t auxiliary;

            if (auxiliary_offset == 0u ||
                !checked_add(table, auxiliary_offset, &auxiliary))
                return ASTRA_DYNAMIC_BAD_TABLE;

            for (uint32_t aux_index = 0u; aux_index < auxiliary_count;
                 ++aux_index) {
                const uint8_t *aux;
                uint32_t aux_next;

                aux = mapped(image, auxiliary, 16u);
                if (aux == NULL)
                    return ASTRA_DYNAMIC_BAD_TABLE;
                if ((astra_load_be16(aux + 6u) & 0x7fffu) == version_index) {
                    if (!string_at(image, astra_load_be32(aux + 8u), version))
                        return ASTRA_DYNAMIC_BAD_STRING;
                    return ASTRA_DYNAMIC_OK;
                }
                aux_next = astra_load_be32(aux + 12u);
                if (aux_index + 1u < auxiliary_count && aux_next == 0u)
                    return ASTRA_DYNAMIC_BAD_TABLE;
                if (aux_index + 1u < auxiliary_count &&
                    !checked_add(auxiliary, aux_next, &auxiliary))
                    return ASTRA_DYNAMIC_BAD_TABLE;
            }
            next = astra_load_be32(record + 12u);
        }
        if (item + 1u < count && next == 0u)
            return ASTRA_DYNAMIC_BAD_TABLE;
        if (!checked_add(table, next, &table))
            return ASTRA_DYNAMIC_BAD_TABLE;
    }
    return ASTRA_DYNAMIC_VERSION_MISMATCH;
}

static AstraDynamicStatus symbol_at(const AstraDynamicImage *image,
                                    uint32_t index, const uint8_t **symbol,
                                    const char **name)
{
    uint32_t address;

    if (index >= image->symbol_count ||
        !checked_multiply(index, ELF_SYMBOL_SIZE, &address) ||
        !checked_add(image->symbol_address, address, &address))
        return ASTRA_DYNAMIC_BAD_SYMBOL;
    *symbol = mapped(image, address, ELF_SYMBOL_SIZE);
    if (*symbol == NULL ||
        !string_at(image, astra_load_be32(*symbol), name))
        return ASTRA_DYNAMIC_BAD_SYMBOL;
    return ASTRA_DYNAMIC_OK;
}

AstraDynamicStatus astra_dynamic_lookup(
    const AstraDynamicImage *image, const char *name, const char *version,
    AstraDynamicResolution *resolution)
{
    const uint8_t *hash;
    uint32_t bucket_count;
    uint32_t index;

    if (image == NULL || name == NULL || *name == '\0' || resolution == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    hash = mapped(image, image->hash_address, 8u);
    if (hash == NULL)
        return ASTRA_DYNAMIC_BAD_TABLE;
    bucket_count = astra_load_be32(hash);
    {
        uint32_t words;
        uint32_t bytes;

        if (!checked_add(bucket_count, image->symbol_count, &words) ||
            !checked_add(words, 2u, &words) ||
            !checked_multiply(words, 4u, &bytes))
            return ASTRA_DYNAMIC_BAD_TABLE;
        hash = mapped(image, image->hash_address, bytes);
        if (hash == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
    }
    {
        uint32_t bucket_offset;

        if (!checked_multiply(elf_hash(name) % bucket_count, 4u,
                              &bucket_offset) ||
            !checked_add(bucket_offset, 8u, &bucket_offset))
            return ASTRA_DYNAMIC_BAD_TABLE;
        index = astra_load_be32(hash + bucket_offset);
    }
    for (uint32_t visited = 0u; index != 0u && visited < image->symbol_count;
         ++visited) {
        const uint8_t *symbol;
        const char *candidate;
        const char *provided_version;
        uint32_t chain_address;
        uint8_t bind;
        uint8_t visibility;
        uint16_t section;
        int hidden;
        AstraDynamicStatus status = symbol_at(image, index, &symbol,
                                              &candidate);

        if (status != ASTRA_DYNAMIC_OK)
            return status;
        bind = symbol[12u] >> 4;
        visibility = symbol[13u] & 3u;
        section = astra_load_be16(symbol + 14u);
        status = symbol_version(image, index, 1, &provided_version, &hidden);
        if (status != ASTRA_DYNAMIC_OK &&
            status != ASTRA_DYNAMIC_VERSION_MISMATCH)
            return status;
        if (text_equal(name, candidate) && section != ELF_SHN_UNDEF &&
            (bind == ELF_STB_GLOBAL || bind == ELF_STB_WEAK) &&
            visibility != ELF_STV_INTERNAL && visibility != ELF_STV_HIDDEN &&
            status == ASTRA_DYNAMIC_OK &&
            ((version != NULL && text_equal(version, provided_version)) ||
             (version == NULL && hidden == 0))) {
            uint32_t value = astra_load_be32(symbol + 4u);

            *resolution = (AstraDynamicResolution){0};
            resolution->symbol_type = symbol[12u] & 0x0fu;
            if (resolution->symbol_type == ELF_STT_TLS) {
                if (image->tls_module == 0u)
                    return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
                resolution->tls_module = image->tls_module;
                resolution->tls_offset = value;
                resolution->tls_tp_offset = image->tls_tp_offset;
            } else {
                resolution->address = section == ELF_SHN_ABS ? value :
                    image->load_bias + value;
            }
            return ASTRA_DYNAMIC_OK;
        }
        {
            uint32_t chain_base;
            uint32_t bucket_bytes;

            if (!checked_multiply(bucket_count, 4u, &bucket_bytes) ||
                !checked_add(image->hash_address, 8u, &chain_base) ||
                !checked_add(chain_base, bucket_bytes, &chain_base) ||
                !checked_multiply(index, 4u, &chain_address) ||
                !checked_add(chain_base, chain_address, &chain_address))
                return ASTRA_DYNAMIC_BAD_TABLE;
        }
        hash = mapped(image, chain_address, 4u);
        if (hash == NULL)
            return ASTRA_DYNAMIC_BAD_TABLE;
        index = astra_load_be32(hash);
        if (index >= image->symbol_count)
            return ASTRA_DYNAMIC_BAD_TABLE;
    }
    return ASTRA_DYNAMIC_UNRESOLVED_SYMBOL;
}

AstraDynamicStatus astra_dynamic_resolve_closure(
    void *context, const char *name, const char *version,
    AstraDynamicResolution *resolution)
{
    const AstraDynamicClosure *closure = context;

    if (closure == NULL || (closure->count != 0u && closure->images == NULL))
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < closure->count; ++index) {
        AstraDynamicStatus status = astra_dynamic_lookup(
            &closure->images[index], name, version, resolution);

        if (status == ASTRA_DYNAMIC_OK)
            return status;
        if (status != ASTRA_DYNAMIC_UNRESOLVED_SYMBOL)
            return status;
    }
    return ASTRA_DYNAMIC_UNRESOLVED_SYMBOL;
}

static AstraDynamicStatus resolve_symbol(
    AstraDynamicImage *image, uint32_t symbol_index,
    AstraDynamicResolver resolver, void *context,
    AstraDynamicResolution *resolution, int *weak)
{
    const uint8_t *symbol;
    const char *name;
    const char *version;
    uint16_t section;
    int hidden;
    AstraDynamicStatus status = symbol_at(image, symbol_index, &symbol, &name);

    if (status != ASTRA_DYNAMIC_OK)
        return status;
    *weak = (symbol[12u] >> 4) == ELF_STB_WEAK;
    section = astra_load_be16(symbol + 14u);
    if (section != ELF_SHN_UNDEF) {
        *resolution = (AstraDynamicResolution){0};
        resolution->symbol_type = symbol[12u] & 0x0fu;
        if (resolution->symbol_type == ELF_STT_TLS) {
            if (image->tls_module == 0u)
                return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
            resolution->tls_module = image->tls_module;
            resolution->tls_offset = astra_load_be32(symbol + 4u);
            resolution->tls_tp_offset = image->tls_tp_offset;
        } else {
            uint32_t value = astra_load_be32(symbol + 4u);
            resolution->address = section == ELF_SHN_ABS ? value :
                                  image->load_bias + value;
        }
        return ASTRA_DYNAMIC_OK;
    }
    if (resolver == NULL)
        return *weak ? ASTRA_DYNAMIC_OK : ASTRA_DYNAMIC_UNRESOLVED_SYMBOL;
    status = symbol_version(image, symbol_index, 0, &version, &hidden);
    if (status != ASTRA_DYNAMIC_OK)
        return status;
    (void)hidden;
    status = resolver(context, name, version, resolution);
    if (status == ASTRA_DYNAMIC_UNRESOLVED_SYMBOL && *weak) {
        *resolution = (AstraDynamicResolution){0};
        return ASTRA_DYNAMIC_OK;
    }
    return status;
}

static AstraDynamicStatus relocate_table(
    AstraDynamicImage *image, uint32_t address, uint32_t size,
    AstraDynamicResolver resolver, void *context)
{
    for (uint32_t offset = 0u; offset < size; offset += ELF_RELA_SIZE) {
        uint32_t rela_address;
        const uint8_t *rela;
        uint32_t target_address;
        uint32_t info;
        uint32_t symbol_index;
        uint32_t type;
        int32_t addend;
        uint8_t *target;
        AstraDynamicResolution resolution = {0};
        AstraDynamicStatus status;
        int weak = 0;
        uint32_t value;

        if (!checked_add(address, offset, &rela_address))
            return ASTRA_DYNAMIC_BAD_RELOCATION;
        rela = mapped(image, rela_address, ELF_RELA_SIZE);
        if (rela == NULL)
            return ASTRA_DYNAMIC_BAD_RELOCATION;
        info = astra_load_be32(rela + 4u);
        symbol_index = info >> 8;
        type = info & 0xffu;
        if (type == ELF_R_68K_NONE)
            continue;
        target_address = astra_load_be32(rela);
        addend = (int32_t)astra_load_be32(rela + 8u);
        if (!load_contains(image, target_address, 4u, ELF_PF_W, 0))
            return ASTRA_DYNAMIC_BAD_RELOCATION;
        target = mapped(image, target_address, 4u);
        if (target == NULL)
            return ASTRA_DYNAMIC_BAD_RELOCATION;
        if (type == ELF_R_68K_RELATIVE) {
            if (symbol_index != 0u)
                return ASTRA_DYNAMIC_BAD_RELOCATION;
            astra_store_be32(target, image->load_bias + (uint32_t)addend);
            continue;
        }
        if (type == ELF_R_68K_COPY)
            return ASTRA_DYNAMIC_UNSUPPORTED;
        if ((type == ELF_R_68K_TLS_DTPMOD32 ||
             type == ELF_R_68K_TLS_DTPREL32 ||
             type == ELF_R_68K_TLS_TPREL32) && symbol_index == 0u) {
            if (image->tls_module == 0u)
                return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
            resolution.symbol_type = ELF_STT_TLS;
            resolution.tls_module = image->tls_module;
            resolution.tls_tp_offset = image->tls_tp_offset;
        } else {
            status = resolve_symbol(image, symbol_index, resolver, context,
                                    &resolution, &weak);
            if (status != ASTRA_DYNAMIC_OK)
                return status;
        }
        if (type == ELF_R_68K_TLS_DTPMOD32) {
            if (resolution.tls_module == 0u)
                return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
            value = resolution.tls_module;
        } else if (type == ELF_R_68K_TLS_DTPREL32) {
            if (resolution.symbol_type != ELF_STT_TLS)
                return ASTRA_DYNAMIC_BAD_RELOCATION;
            value = resolution.tls_offset + (uint32_t)addend;
        } else if (type == ELF_R_68K_TLS_TPREL32) {
            if (resolution.symbol_type != ELF_STT_TLS)
                return ASTRA_DYNAMIC_BAD_RELOCATION;
            value = (uint32_t)(resolution.tls_tp_offset +
                               (int32_t)resolution.tls_offset + addend);
        } else {
            if (resolution.symbol_type == ELF_STT_TLS)
                return ASTRA_DYNAMIC_BAD_RELOCATION;
            if (type == ELF_R_68K_32 || type == ELF_R_68K_GLOB_DAT ||
                type == ELF_R_68K_JMP_SLOT) {
                value = resolution.address + (uint32_t)addend;
            } else if (type == ELF_R_68K_PC32) {
                value = resolution.address + (uint32_t)addend -
                        (image->load_bias + target_address);
            } else {
                return ASTRA_DYNAMIC_UNSUPPORTED;
            }
        }
        astra_store_be32(target, value);
    }
    return ASTRA_DYNAMIC_OK;
}

AstraDynamicStatus astra_dynamic_relocate(
    AstraDynamicImage *image, AstraDynamicResolver resolver, void *context)
{
    AstraDynamicStatus status;

    if (image == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    status = relocate_table(image, image->rela_address, image->rela_size,
                            resolver, context);
    if (status != ASTRA_DYNAMIC_OK)
        return status;
    return relocate_table(image, image->jump_rela_address,
                          image->jump_rela_size, resolver, context);
}

AstraDynamicStatus astra_dynamic_tls_layout(
    AstraDynamicImage *images, uint32_t count,
    AstraDynamicTlsLayout *layout)
{
    uint32_t cursor = 0u;
    uint32_t maximum_alignment = 1u;
    uint32_t module_count = 0u;

    if (layout == NULL || (count != 0u && images == NULL))
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    *layout = (AstraDynamicTlsLayout){0u, 1u, 0u};
    for (uint32_t index = 0u; index < count; ++index) {
        AstraDynamicImage *image = &images[index];
        uint32_t aligned;
        uint32_t mask;

        image->tls_module = 0u;
        image->tls_tp_offset = 0;
        if (image->tls_memory_size == 0u)
            continue;
        if (image->tls_file_size > image->tls_memory_size ||
            image->tls_alignment == 0u ||
            (image->tls_alignment & (image->tls_alignment - 1u)) != 0u)
            return ASTRA_DYNAMIC_BAD_ELF;
        mask = image->tls_alignment - 1u;
        if (!checked_add(cursor, mask, &aligned))
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        aligned &= ~mask;
        if (!checked_add(aligned, image->tls_memory_size, &cursor) ||
            aligned > (uint32_t)INT32_MAX +
                          ASTRA_M68K_TLS_THREAD_POINTER_BIAS)
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        if (module_count == UINT32_MAX)
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        image->tls_module = ++module_count;
        image->tls_tp_offset =
            (int32_t)((int64_t)aligned -
                      ASTRA_M68K_TLS_THREAD_POINTER_BIAS);
        if (image->tls_alignment > maximum_alignment)
            maximum_alignment = image->tls_alignment;
    }
    layout->memory_size = cursor;
    layout->alignment = maximum_alignment;
    layout->module_count = module_count;
    return ASTRA_DYNAMIC_OK;
}

AstraDynamicStatus astra_dynamic_tls_initialize(
    const AstraDynamicImage *images, uint32_t count,
    const AstraDynamicTlsLayout *layout, void *destination,
    uint32_t capacity)
{
    uint8_t *bytes = destination;
    uint32_t expected_module = 0u;

    if (layout == NULL || (count != 0u && images == NULL) ||
        layout->alignment == 0u ||
        (layout->alignment & (layout->alignment - 1u)) != 0u ||
        layout->memory_size > capacity ||
        (layout->memory_size != 0u && bytes == NULL))
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    for (uint32_t offset = 0u; offset < layout->memory_size; ++offset)
        bytes[offset] = 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const AstraDynamicImage *image = &images[index];
        const uint8_t *source;
        int64_t signed_offset;
        uint32_t offset;

        if (image->tls_memory_size == 0u) {
            if (image->tls_module != 0u || image->tls_tp_offset != 0)
                return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
            continue;
        }
        if (image->tls_module != ++expected_module ||
            image->tls_file_size > image->tls_memory_size ||
            image->tls_alignment == 0u ||
            (image->tls_alignment & (image->tls_alignment - 1u)) != 0u)
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        signed_offset = (int64_t)image->tls_tp_offset +
                        ASTRA_M68K_TLS_THREAD_POINTER_BIAS;
        if (signed_offset < 0 || signed_offset > UINT32_MAX)
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        offset = (uint32_t)signed_offset;
        if ((offset & (image->tls_alignment - 1u)) != 0u ||
            image->tls_memory_size > layout->memory_size ||
            offset > layout->memory_size - image->tls_memory_size)
            return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
        source = mapped(image, image->tls_address, image->tls_file_size);
        if (source == NULL && image->tls_file_size != 0u)
            return ASTRA_DYNAMIC_BAD_ELF;
        for (uint32_t byte = 0u; byte < image->tls_file_size; ++byte)
            bytes[offset + byte] = source[byte];
    }
    if (expected_module != layout->module_count)
        return ASTRA_DYNAMIC_TLS_UNAVAILABLE;
    return ASTRA_DYNAMIC_OK;
}

static int callback_address_valid(const AstraDynamicImage *image,
                                  uint32_t address)
{
    uint32_t relative;

    if (address < image->load_bias)
        return 0;
    relative = address - image->load_bias;
    return load_contains(image, relative, 1u, ELF_PF_R | ELF_PF_X, 1);
}

static AstraDynamicStatus callback_at(const AstraDynamicImage *image,
                                      uint32_t array_address,
                                      uint32_t array_size, uint32_t index,
                                      int reverse, uint32_t *address)
{
    uint32_t count;
    uint32_t offset;
    const uint8_t *entry;

    if (image == NULL || address == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    count = array_size / 4u;
    if (index >= count)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    if (reverse)
        index = count - index - 1u;
    if (!checked_multiply(index, 4u, &offset))
        return ASTRA_DYNAMIC_BAD_TABLE;
    entry = mapped_offset(image, array_address, offset, 4u);
    if (entry == NULL)
        return ASTRA_DYNAMIC_BAD_TABLE;
    *address = astra_load_be32(entry);
    return callback_address_valid(image, *address) ? ASTRA_DYNAMIC_OK :
                                                     ASTRA_DYNAMIC_BAD_TABLE;
}

uint32_t astra_dynamic_preinitializer_count(const AstraDynamicImage *image)
{
    return image != NULL ? image->preinit_array_size / 4u : 0u;
}

AstraDynamicStatus astra_dynamic_preinitializer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address)
{
    return callback_at(image, image != NULL ? image->preinit_array_address : 0u,
                       image != NULL ? image->preinit_array_size : 0u,
                       index, 0, address);
}

uint32_t astra_dynamic_initializer_count(const AstraDynamicImage *image)
{
    return image != NULL ? image->init_array_size / 4u +
                           (image->init_address != 0u) : 0u;
}

AstraDynamicStatus astra_dynamic_initializer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address)
{
    if (image == NULL || address == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    if (image->init_address != 0u) {
        if (index == 0u) {
            if (!checked_add(image->load_bias, image->init_address, address) ||
                !callback_address_valid(image, *address))
                return ASTRA_DYNAMIC_BAD_TABLE;
            return ASTRA_DYNAMIC_OK;
        }
        --index;
    }
    return callback_at(image, image->init_array_address,
                       image->init_array_size, index, 0, address);
}

uint32_t astra_dynamic_finalizer_count(const AstraDynamicImage *image)
{
    return image != NULL ? image->fini_array_size / 4u +
                           (image->fini_address != 0u) : 0u;
}

AstraDynamicStatus astra_dynamic_finalizer(
    const AstraDynamicImage *image, uint32_t index, uint32_t *address)
{
    uint32_t array_count;

    if (image == NULL || address == NULL)
        return ASTRA_DYNAMIC_INVALID_ARGUMENT;
    array_count = image->fini_array_size / 4u;
    if (index < array_count)
        return callback_at(image, image->fini_array_address,
                           image->fini_array_size, index, 1, address);
    if (index == array_count && image->fini_address != 0u) {
        if (!checked_add(image->load_bias, image->fini_address, address) ||
            !callback_address_valid(image, *address))
            return ASTRA_DYNAMIC_BAD_TABLE;
        return ASTRA_DYNAMIC_OK;
    }
    return ASTRA_DYNAMIC_INVALID_ARGUMENT;
}

const char *astra_dynamic_status_text(AstraDynamicStatus status)
{
    switch (status) {
    case ASTRA_DYNAMIC_OK: return "ok";
    case ASTRA_DYNAMIC_INVALID_ARGUMENT: return "invalid argument";
    case ASTRA_DYNAMIC_BAD_ELF: return "invalid ELF image";
    case ASTRA_DYNAMIC_BAD_TABLE: return "invalid dynamic table";
    case ASTRA_DYNAMIC_UNSUPPORTED: return "unsupported dynamic feature";
    case ASTRA_DYNAMIC_BAD_STRING: return "invalid dynamic string";
    case ASTRA_DYNAMIC_BAD_SYMBOL: return "invalid dynamic symbol";
    case ASTRA_DYNAMIC_BAD_RELOCATION: return "invalid relocation";
    case ASTRA_DYNAMIC_UNRESOLVED_SYMBOL: return "unresolved symbol";
    case ASTRA_DYNAMIC_VERSION_MISMATCH: return "symbol version mismatch";
    case ASTRA_DYNAMIC_TLS_UNAVAILABLE: return "TLS layout unavailable";
    }
    return "unknown dynamic-loader error";
}
