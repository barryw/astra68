#include <astra/dynamic_loader.h>
#include <astra/dynamic_process.h>
#include <astra/endian.h>
#include <astra/limits.h>
#include <astra/syscall.h>
#include <astra/tls.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ELF_HEADER_SIZE 52u
#define ELF_PROGRAM_HEADER_SIZE 32u
#define ELF_PT_LOAD 1u
#define ELF_PT_INTERP 3u
#define ELF_PT_GNU_RELRO 0x6474e552u
#define ELF_PF_W 2u
#define ELF_DT_NULL 0u
#define ELF_DT_NEEDED 1u
#define ELF_DT_HASH 4u
#define ELF_DT_BIND_NOW 24u
#define ELF_DT_FLAGS_1 0x6ffffffbu
#define ELF_DT_IGNORED_PROCESSOR 0x70000000u
#define ELF_R_68K_NONE 0u
#define ELF_R_68K_TLS_TPREL32 42u

typedef struct Fixture {
    uint8_t *mapping;
    uint32_t span;
    uint32_t header_address;
    AstraDynamicImage image;
} Fixture;

static uint8_t *read_file(const char *path, uint32_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= (long)ELF_HEADER_SIZE && size <= (long)UINT32_MAX);
    assert(fseek(file, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)size);
    assert(bytes != NULL);
    assert(fread(bytes, 1u, (size_t)size, file) == (size_t)size);
    assert(fclose(file) == 0);
    *length = (uint32_t)size;
    return bytes;
}

static Fixture load_fixture(const char *path, uint32_t load_bias)
{
    Fixture fixture = {0};
    uint32_t file_length;
    uint8_t *file = read_file(path, &file_length);
    uint32_t phoff = astra_load_be32(file + 28u);
    uint16_t phnum = astra_load_be16(file + 44u);

    assert(astra_load_be16(file + 42u) == ELF_PROGRAM_HEADER_SIZE);
    assert(phoff <= file_length &&
           (uint32_t)phnum <= (file_length - phoff) /
                                  ELF_PROGRAM_HEADER_SIZE);
    for (uint32_t index = 0u; index < phnum; ++index) {
        const uint8_t *header = file + phoff + index * ELF_PROGRAM_HEADER_SIZE;
        uint32_t end;

        if (astra_load_be32(header) != ELF_PT_LOAD)
            continue;
        assert(astra_load_be32(header + 8u) <= UINT32_MAX -
               astra_load_be32(header + 20u));
        end = astra_load_be32(header + 8u) + astra_load_be32(header + 20u);
        assert(end <= UINT32_MAX - (ASTRA_MEMORY_PAGE_SIZE - 1u));
        end = (end + ASTRA_MEMORY_PAGE_SIZE - 1u) &
              ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
        if (end > fixture.span)
            fixture.span = end;
        if (astra_load_be32(header + 4u) == 0u)
            fixture.header_address = astra_load_be32(header + 8u);
    }
    assert(fixture.span != 0u);
    fixture.mapping = calloc(1u, fixture.span);
    assert(fixture.mapping != NULL);
    for (uint32_t index = 0u; index < phnum; ++index) {
        const uint8_t *header = file + phoff + index * ELF_PROGRAM_HEADER_SIZE;
        uint32_t offset;
        uint32_t address;
        uint32_t size;

        if (astra_load_be32(header) != ELF_PT_LOAD)
            continue;
        offset = astra_load_be32(header + 4u);
        address = astra_load_be32(header + 8u);
        size = astra_load_be32(header + 16u);
        assert(offset <= file_length && size <= file_length - offset);
        assert(address <= fixture.span && size <= fixture.span - address);
        memcpy(fixture.mapping + address, file + offset, size);
    }
    free(file);
    assert(astra_dynamic_open((uintptr_t)fixture.mapping, fixture.span,
                              fixture.header_address, load_bias,
                              &fixture.image) == ASTRA_DYNAMIC_OK);
    return fixture;
}

static void unload_fixture(Fixture *fixture)
{
    free(fixture->mapping);
    *fixture = (Fixture){0};
}

static uint8_t *fixture_program_header(Fixture *fixture, uint32_t type,
                                       uint32_t required_flags)
{
    for (uint32_t index = 0u;
         index < fixture->image.program_header_count; ++index) {
        uint8_t *header = fixture->mapping +
            fixture->image.program_headers + index * ELF_PROGRAM_HEADER_SIZE;

        if (astra_load_be32(header) == type &&
            (astra_load_be32(header + 24u) & required_flags) ==
                required_flags)
            return header;
    }
    return NULL;
}

static uint8_t *fixture_dynamic_entry(Fixture *fixture, uint32_t wanted)
{
    for (uint32_t offset = 0u; offset < fixture->image.dynamic_size;
         offset += 8u) {
        uint8_t *entry = fixture->mapping + fixture->image.dynamic_address +
                         offset;

        if (astra_load_be32(entry) == wanted)
            return entry;
    }
    return NULL;
}

static void test_dynamic_singleton_tags(const char *program_path)
{
    Fixture program = load_fixture(program_path, 0u);
    uint8_t *hash = fixture_dynamic_entry(&program, ELF_DT_HASH);
    uint8_t *terminator = fixture_dynamic_entry(&program, ELF_DT_NULL);
    AstraDynamicImage image;
    uint32_t hash_address;

    assert(hash != NULL && terminator != NULL);
    assert((uint32_t)(terminator - program.mapping) + 16u <= program.span);
    assert(astra_load_be32(terminator + 8u) == ELF_DT_NULL);
    assert(astra_dynamic_open((uintptr_t)program.mapping, program.span,
                              program.header_address, 0u, &image) ==
           ASTRA_DYNAMIC_OK);

    hash_address = astra_load_be32(hash + 4u);
    astra_store_be32(hash + 4u, 0u);
    astra_store_be32(terminator, ELF_DT_HASH);
    astra_store_be32(terminator + 4u, hash_address);
    assert(astra_dynamic_open((uintptr_t)program.mapping, program.span,
                              program.header_address, 0u, &image) ==
           ASTRA_DYNAMIC_BAD_TABLE);
    unload_fixture(&program);
}

static void test_relro_page_padding(const char *library_path)
{
    Fixture library = load_fixture(library_path, 0x24000000u);
    uint8_t *load = fixture_program_header(&library, ELF_PT_LOAD, ELF_PF_W);
    uint8_t *relro = fixture_program_header(&library, ELF_PT_GNU_RELRO, 0u);
    AstraDynamicImage image;
    uint32_t load_end;
    uint32_t mapped_end;
    uint32_t relro_address;
    uint32_t padded_size;
    uint32_t available_file_bytes;

    assert(load != NULL && relro != NULL);
    load_end = astra_load_be32(load + 8u) + astra_load_be32(load + 20u);
    mapped_end = (load_end + ASTRA_MEMORY_PAGE_SIZE - 1u) &
                 ~(ASTRA_MEMORY_PAGE_SIZE - 1u);
    relro_address = astra_load_be32(relro + 8u);
    assert(relro_address < mapped_end);
    padded_size = mapped_end - relro_address;
    assert(astra_load_be32(relro + 16u) <= padded_size);

    astra_store_be32(relro + 20u, padded_size);
    assert(astra_dynamic_open((uintptr_t)library.mapping, library.span,
                              library.header_address, 0x24000000u,
                              &image) == ASTRA_DYNAMIC_OK);
    assert(image.relro_size == padded_size);

    astra_store_be32(relro + 20u, padded_size + 1u);
    assert(astra_dynamic_open((uintptr_t)library.mapping, library.span,
                              library.header_address, 0x24000000u,
                              &image) == ASTRA_DYNAMIC_BAD_TABLE);

    astra_store_be32(relro + 20u, padded_size);
    available_file_bytes = astra_load_be32(load + 16u) -
        (astra_load_be32(relro + 4u) - astra_load_be32(load + 4u));
    assert(available_file_bytes < padded_size);
    astra_store_be32(relro + 16u, available_file_bytes + 1u);
    assert(astra_dynamic_open((uintptr_t)library.mapping, library.span,
                              library.header_address, 0x24000000u,
                              &image) == ASTRA_DYNAMIC_BAD_TABLE);
    unload_fixture(&library);
}

static AstraDynamicStatus resolve_tls_runtime(
    void *context, const char *name, const char *version,
    AstraDynamicResolution *resolution)
{
    (void)context;
    if (strcmp(name, "__m68k_read_tp") != 0 || version != NULL)
        return ASTRA_DYNAMIC_UNRESOLVED_SYMBOL;
    *resolution = (AstraDynamicResolution){
        .address = 0x00102000u,
    };
    return ASTRA_DYNAMIC_OK;
}

static void test_real_dynamic_pair(const char *program_path,
                                   const char *library_path)
{
    Fixture library = load_fixture(library_path, 0x24000000u);
    Fixture program = load_fixture(program_path, 0u);
    AstraDynamicClosure closure;
    AstraDynamicResolution symbol;
    uint32_t symbol_address;
    const char *needed;
    const char *interpreter;
    const char *soname;
    uint32_t relocation_target;
    uint32_t callback;
    uint8_t *target;

    assert(library.image.needed_count == 0u);
    assert(program.image.needed_count == 1u);
    assert(astra_dynamic_interpreter(&program.image, &interpreter) ==
           ASTRA_DYNAMIC_OK);
    assert(strcmp(interpreter, "loader.library.1") == 0);
    assert(astra_dynamic_interpreter(&library.image, &interpreter) ==
           ASTRA_DYNAMIC_BAD_ELF);
    assert(astra_dynamic_soname(&library.image, &soname) == ASTRA_DYNAMIC_OK);
    assert(strcmp(soname, "dynamic.library.1") == 0);
    assert(astra_dynamic_soname(&program.image, &soname) ==
           ASTRA_DYNAMIC_INVALID_ARGUMENT);
    assert(astra_dynamic_relocate(&library.image, NULL, NULL) ==
           ASTRA_DYNAMIC_OK);
    assert(astra_dynamic_initializer_count(&library.image) == 1u);
    assert(astra_dynamic_initializer(&library.image, 0u, &callback) ==
           ASTRA_DYNAMIC_OK);
    assert(callback >= 0x24000000u);
    assert(astra_dynamic_finalizer_count(&library.image) == 1u);
    assert(astra_dynamic_finalizer(&library.image, 0u, &callback) ==
           ASTRA_DYNAMIC_OK);
    assert(callback >= 0x24000000u);
    assert(astra_dynamic_preinitializer_count(&program.image) == 1u);
    assert(astra_dynamic_preinitializer(&program.image, 0u, &callback) ==
           ASTRA_DYNAMIC_OK);
    assert(callback >= program.header_address);
    closure = (AstraDynamicClosure){
        .images = &library.image,
        .count = 1u,
    };
    assert(astra_dynamic_needed(&program.image, 0u, &needed) ==
           ASTRA_DYNAMIC_OK);
    assert(strcmp(needed, "dynamic.library.1") == 0);
    assert(astra_dynamic_lookup(&library.image,
                                "astra_dynamic_contract_add", "ASTRA_1.0",
                                &symbol) == ASTRA_DYNAMIC_OK);
    assert(symbol.address >= 0x24000000u);
    symbol_address = symbol.address;
    assert(astra_dynamic_lookup(&library.image,
                                "astra_dynamic_contract_add", "ASTRA_2.0",
                                &symbol) == ASTRA_DYNAMIC_UNRESOLVED_SYMBOL);

    assert(program.image.jump_rela_size == 12u);
    relocation_target = astra_load_be32(
        program.mapping + program.image.jump_rela_address);
    target = program.mapping + relocation_target;
    assert(astra_dynamic_relocate(&program.image,
                                  astra_dynamic_resolve_closure,
                                  &closure) == ASTRA_DYNAMIC_OK);
    assert(astra_load_be32(target) == symbol_address);

    unload_fixture(&program);
    program = load_fixture(program_path, 0u);
    assert(astra_dynamic_relocate(&program.image, NULL, NULL) ==
           ASTRA_DYNAMIC_UNRESOLVED_SYMBOL);
    unload_fixture(&program);
    unload_fixture(&library);
}

static void test_none_relocation_has_no_target(const char *program_path)
{
    Fixture program = load_fixture(program_path, 0u);
    uint8_t *rela = program.mapping + program.image.jump_rela_address;

    assert(program.image.jump_rela_size == 12u);
    astra_store_be32(rela, 0u);
    astra_store_be32(rela + 4u, ELF_R_68K_NONE);
    astra_store_be32(rela + 8u, 0u);
    assert(astra_dynamic_relocate(&program.image, NULL, NULL) ==
           ASTRA_DYNAMIC_OK);
    unload_fixture(&program);
}

typedef struct ProcessFixtureContext {
    const char *library_path;
    uint32_t opens;
    uint32_t commits;
    uint32_t rollbacks;
} ProcessFixtureContext;

static uint32_t process_fixture_open(void *opaque, const char *identity,
                                     AstraDynamicImage *image, void **token)
{
    ProcessFixtureContext *context = opaque;
    Fixture *fixture;

    if (strcmp(identity, "dynamic.library.1") != 0)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    fixture = calloc(1u, sizeof(*fixture));
    if (fixture == NULL)
        return ASTRA_SYSCALL_OUT_OF_MEMORY;
    *fixture = load_fixture(context->library_path, 0x24000000u);
    *image = fixture->image;
    *token = fixture;
    ++context->opens;
    return ASTRA_SYSCALL_OK;
}

static uint32_t process_fixture_finish(void *opaque, void *token, int commit)
{
    ProcessFixtureContext *context = opaque;
    Fixture *fixture = token;

    assert(fixture != NULL);
    if (commit)
        ++context->commits;
    else
        ++context->rollbacks;
    unload_fixture(fixture);
    free(fixture);
    return ASTRA_SYSCALL_OK;
}

static void test_process_dependency_graph(const char *program_path,
                                          const char *library_path)
{
    ProcessFixtureContext context = {.library_path = library_path};
    Fixture program = load_fixture(program_path, 0u);
    AstraDynamicProcess *process = NULL;
    AstraDynamicTlsLayout layout;
    uint32_t failure_image = UINT32_MAX;

    assert(astra_dynamic_process_prepare(
               &program.image, process_fixture_open,
               process_fixture_finish, &context, &process) ==
           ASTRA_SYSCALL_OK);
    assert(process != NULL);
    assert(context.opens == 1u);
    assert(astra_dynamic_process_image_count(process) == 2u);
    assert(astra_dynamic_process_lifecycle_count(process) == 2u);
    assert(astra_dynamic_process_lifecycle_image(process, 0u) == 1u);
    assert(astra_dynamic_process_lifecycle_image(process, 1u) == 0u);
    assert(astra_dynamic_process_lifecycle_image(process, 2u) == UINT32_MAX);
    assert(astra_dynamic_process_relocate(
               process, &layout, &failure_image) == ASTRA_DYNAMIC_OK);
    assert(failure_image == UINT32_MAX);
    assert(astra_dynamic_process_identity(process, 0u) == NULL);
    assert(strcmp(astra_dynamic_process_identity(process, 1u),
                  "dynamic.library.1") == 0);
    assert(layout.memory_size == 0u && layout.alignment == 1u);
    assert(astra_dynamic_process_commit(process) == ASTRA_SYSCALL_OK);
    assert(context.commits == 1u && context.rollbacks == 0u);
    astra_dynamic_process_discard(process);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    process = NULL;
    assert(astra_dynamic_process_prepare(
               &program.image, process_fixture_open,
               process_fixture_finish, &context, &process) ==
           ASTRA_SYSCALL_OK);
    astra_dynamic_process_discard(process);
    assert(context.opens == 2u && context.commits == 1u &&
           context.rollbacks == 1u);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    program.image.hash_address = UINT32_MAX;
    process = NULL;
    failure_image = UINT32_MAX;
    assert(astra_dynamic_process_prepare(
               &program.image, process_fixture_open,
               process_fixture_finish, &context, &process) ==
           ASTRA_SYSCALL_OK);
    assert(astra_dynamic_process_relocate(
               process, &layout, &failure_image) == ASTRA_DYNAMIC_BAD_TABLE);
    assert(failure_image == 0u);
    astra_dynamic_process_discard(process);
    assert(context.opens == 3u && context.commits == 1u &&
           context.rollbacks == 2u);
    unload_fixture(&program);
}

static void test_real_tls_library(const char *library_path)
{
    Fixture library = load_fixture(library_path, 0x25000000u);
    uint32_t local_target = UINT32_MAX;
    uint32_t local_addend = UINT32_MAX;
    uint32_t exported_target = UINT32_MAX;
    uint32_t exported_offset = UINT32_MAX;
    uint32_t jump_target;

    assert(library.image.tls_file_size == 8u);
    assert(library.image.tls_memory_size == 8u);
    library.image.tls_module = 2u;
    library.image.tls_tp_offset = -0x200;
    for (uint32_t offset = 0u; offset < library.image.rela_size;
         offset += 12u) {
        const uint8_t *rela = library.mapping +
            library.image.rela_address + offset;
        uint32_t info = astra_load_be32(rela + 4u);

        if ((info & 0xffu) != ELF_R_68K_TLS_TPREL32)
            continue;
        if ((info >> 8) == 0u) {
            assert(local_target == UINT32_MAX);
            local_target = astra_load_be32(rela);
            local_addend = astra_load_be32(rela + 8u);
        } else {
            assert(exported_target == UINT32_MAX);
            exported_target = astra_load_be32(rela);
            exported_offset = astra_load_be32(
                library.mapping + library.image.symbol_address +
                (info >> 8) * 16u + 4u);
        }
    }
    assert(local_target != UINT32_MAX);
    assert(exported_target != UINT32_MAX);
    assert(library.image.jump_rela_size == 12u);
    jump_target = astra_load_be32(
        library.mapping + library.image.jump_rela_address);
    assert(astra_dynamic_relocate(&library.image, resolve_tls_runtime, NULL) ==
           ASTRA_DYNAMIC_OK);
    assert(astra_load_be32(library.mapping + exported_target) ==
           (uint32_t)(library.image.tls_tp_offset +
                      (int32_t)exported_offset));
    assert(astra_load_be32(library.mapping + local_target) ==
           (uint32_t)(library.image.tls_tp_offset +
                      (int32_t)local_addend));
    assert(astra_load_be32(library.mapping + jump_target) == 0x00102000u);
    unload_fixture(&library);

    library = load_fixture(library_path, 0x25000000u);
    assert(astra_dynamic_relocate(&library.image, resolve_tls_runtime, NULL) ==
           ASTRA_DYNAMIC_TLS_UNAVAILABLE);
    unload_fixture(&library);
}

static void test_combined_tls_layout(void)
{
    uint8_t first_template[] = {0x11u, 0x22u, 0x33u};
    uint8_t second_template[] = {0xaau, 0xbbu};
    uint8_t combined[32];
    AstraDynamicImage images[3] = {0};
    AstraDynamicTlsLayout layout;

    images[1].mapping_origin = (uintptr_t)first_template;
    images[1].mapping_span = sizeof(first_template);
    images[1].tls_address = 0u;
    images[1].tls_file_size = sizeof(first_template);
    images[1].tls_memory_size = 5u;
    images[1].tls_alignment = 4u;
    images[2].mapping_origin = (uintptr_t)second_template;
    images[2].mapping_span = sizeof(second_template);
    images[2].tls_address = 0u;
    images[2].tls_file_size = sizeof(second_template);
    images[2].tls_memory_size = 8u;
    images[2].tls_alignment = 16u;

    assert(astra_dynamic_tls_layout(images, 3u, &layout) ==
           ASTRA_DYNAMIC_OK);
    assert(layout.memory_size == 24u);
    assert(layout.alignment == 16u);
    assert(layout.module_count == 2u);
    assert(images[0].tls_module == 0u && images[0].tls_tp_offset == 0);
    assert(images[1].tls_module == 1u);
    assert(images[1].tls_tp_offset ==
           -(int32_t)ASTRA_M68K_TLS_THREAD_POINTER_BIAS);
    assert(images[2].tls_module == 2u);
    assert(images[2].tls_tp_offset ==
           16 - (int32_t)ASTRA_M68K_TLS_THREAD_POINTER_BIAS);

    memset(combined, 0xcc, sizeof(combined));
    assert(astra_dynamic_tls_initialize(images, 3u, &layout, combined,
                                        sizeof(combined)) ==
           ASTRA_DYNAMIC_OK);
    assert(memcmp(combined, first_template, sizeof(first_template)) == 0);
    for (uint32_t index = sizeof(first_template); index < 16u; ++index)
        assert(combined[index] == 0u);
    assert(memcmp(combined + 16u, second_template,
                  sizeof(second_template)) == 0);
    for (uint32_t index = 18u; index < layout.memory_size; ++index)
        assert(combined[index] == 0u);
    assert(combined[layout.memory_size] == 0xccu);

    assert(astra_dynamic_tls_initialize(images, 3u, &layout, combined,
                                        layout.memory_size - 1u) ==
           ASTRA_DYNAMIC_INVALID_ARGUMENT);
    images[2].tls_module = 1u;
    assert(astra_dynamic_tls_initialize(images, 3u, &layout, combined,
                                        sizeof(combined)) ==
           ASTRA_DYNAMIC_TLS_UNAVAILABLE);
    images[2].tls_module = 2u;
    images[2].tls_tp_offset = 1;
    assert(astra_dynamic_tls_initialize(images, 3u, &layout, combined,
                                        sizeof(combined)) ==
           ASTRA_DYNAMIC_TLS_UNAVAILABLE);

    images[1].tls_alignment = 3u;
    assert(astra_dynamic_tls_layout(images, 3u, &layout) ==
           ASTRA_DYNAMIC_BAD_ELF);
    assert(astra_dynamic_tls_layout(NULL, 1u, &layout) ==
           ASTRA_DYNAMIC_INVALID_ARGUMENT);
}

static void test_rejections(const char *program_path)
{
    Fixture program = load_fixture(program_path, 0u);
    uint8_t *rela = program.mapping + program.image.jump_rela_address;

    astra_store_be32(rela, program.header_address);
    assert(astra_dynamic_relocate(&program.image, resolve_tls_runtime, NULL) ==
           ASTRA_DYNAMIC_BAD_RELOCATION);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    program.mapping[program.image.interpreter_address +
                    program.image.interpreter_size - 1u] = 'x';
    assert(astra_dynamic_open((uintptr_t)program.mapping, program.span,
                              program.header_address, 0u, &program.image) ==
           ASTRA_DYNAMIC_BAD_STRING);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    program.mapping[program.image.interpreter_address + 1u] = '\0';
    assert(astra_dynamic_open((uintptr_t)program.mapping, program.span,
                              program.header_address, 0u, &program.image) ==
           ASTRA_DYNAMIC_BAD_STRING);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    for (uint32_t offset = 0u; offset < program.image.dynamic_size;
         offset += 8u) {
        uint8_t *entry = program.mapping + program.image.dynamic_address +
                         offset;
        uint32_t tag = astra_load_be32(entry);

        if (tag == ELF_DT_BIND_NOW)
            astra_store_be32(entry, ELF_DT_IGNORED_PROCESSOR);
        else if (tag == ELF_DT_FLAGS_1)
            astra_store_be32(entry + 4u, 0u);
    }
    assert(astra_dynamic_open((uintptr_t)program.mapping, program.span,
                              program.header_address, 0u, &program.image) ==
           ASTRA_DYNAMIC_UNSUPPORTED);
    unload_fixture(&program);

    program = load_fixture(program_path, 0u);
    for (uint32_t offset = 0u; offset < program.image.dynamic_size;
         offset += 8u) {
        uint8_t *entry = program.mapping + program.image.dynamic_address +
                         offset;

        if (astra_load_be32(entry) == ELF_DT_NEEDED) {
            astra_store_be32(entry + 4u, program.image.string_size);
            break;
        }
    }
    {
        const char *needed;

        assert(astra_dynamic_needed(&program.image, 0u, &needed) ==
               ASTRA_DYNAMIC_BAD_STRING);
    }
    unload_fixture(&program);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    test_real_dynamic_pair(argv[1], argv[2]);
    test_dynamic_singleton_tags(argv[1]);
    test_none_relocation_has_no_target(argv[1]);
    test_relro_page_padding(argv[2]);
    test_process_dependency_graph(argv[1], argv[2]);
    test_real_tls_library(argv[3]);
    test_combined_tls_layout();
    test_rejections(argv[1]);
    assert(strcmp(astra_dynamic_status_text(ASTRA_DYNAMIC_BAD_RELOCATION),
                  "invalid relocation") == 0);
    puts("astra eager dynamic loader: PASS");
    return 0;
}
