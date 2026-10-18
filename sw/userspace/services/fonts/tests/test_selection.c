#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../main.c"

static uint8_t *fixture;
static uint32_t fixture_size, cursor, opens, allocations;
static uint32_t unavailable, no_area, fail_config, fail_signal, signals, writes;
static char saved[ASTRA_FONT_FACE_ID_MAX];
static struct { void *data; uint32_t size, refs; } areas[4];

void *astra_runtime_allocate(size_t size)
{
    void *memory = malloc(size);
    if (memory != NULL) ++allocations;
    return memory;
}
void astra_runtime_deallocate(void *memory)
{
    if (memory != NULL) { assert(allocations != 0u); --allocations; }
    free(memory);
}
uint32_t astra_filesystem_open(AstraFilesystem *fs, const char *path,
                              uint32_t flags, AstraFile *file)
{
    (void)fs; (void)path; (void)file;
    assert(flags == ASTRA_VFS_OPEN_READ);
    ++opens;
    cursor = 0u;
    return unavailable ? ASTRA_VFS_ERR_IO : ASTRA_VFS_OK;
}
uint32_t astra_filesystem_file_info(const AstraFile *file, AstraFileInfo *info)
{
    (void)file;
    info->kind = ASTRA_VFS_KIND_FILE;
    info->byte_size = fixture_size;
    return ASTRA_VFS_OK;
}
uint32_t astra_filesystem_read(AstraFile *file, void *buffer,
                              uint32_t size, uint32_t *moved)
{
    (void)file;
    assert(size <= fixture_size - cursor);
    memcpy(buffer, fixture + cursor, size);
    cursor += size;
    *moved = size;
    return ASTRA_VFS_OK;
}
uint32_t astra_filesystem_close(AstraFile *file)
{
    (void)file;
    return ASTRA_VFS_OK;
}
uint32_t astra_config_set_string(AstraConfig *store, const char *key,
                                uint32_t index, const char *value)
{
    assert(store == &config && strcmp(key, "system_font") == 0 && index == 0u);
    ++writes;
    if (fail_config) return ASTRA_CONFIG_IO;
    strcpy(saved, value);
    return ASTRA_CONFIG_OK;
}
uint32_t astra_rt_area_create(uint32_t size, uint32_t rights, uint32_t *handle)
{
    assert(rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
                     ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    if (no_area) return ASTRA_SYSCALL_OUT_OF_MEMORY;
    for (uint32_t at = 0u; at < 4u; ++at) if (areas[at].refs == 0u) {
        areas[at].data = malloc(size);
        assert(areas[at].data != NULL);
        areas[at].size = size;
        areas[at].refs = 1u;
        *handle = at + 1u;
        return ASTRA_SYSCALL_OK;
    }
    return ASTRA_SYSCALL_OUT_OF_MEMORY;
}
uint32_t astra_rt_area_map(uint32_t handle, uint32_t flags,
                          void **mapping, uint32_t *size)
{
    assert(handle > 0u && handle <= 4u && areas[handle - 1u].refs != 0u &&
           flags == (ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE));
    *mapping = areas[handle - 1u].data;
    *size = areas[handle - 1u].size;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_unmap(void *mapping)
{
    assert(mapping != NULL);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_handle_duplicate(uint32_t source, uint32_t rights, uint32_t *out)
{
    assert(source > 0u && source <= 4u && areas[source - 1u].refs != 0u &&
           rights == (ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER));
    ++areas[source - 1u].refs;
    *out = source;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle)
{
    assert(handle > 0u && handle <= 4u && areas[handle - 1u].refs != 0u);
    if (--areas[handle - 1u].refs == 0u) free(areas[handle - 1u].data);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    assert(handle == 99u && count == 1u && woken == NULL);
    ++signals;
    return fail_signal ? ASTRA_SYSCALL_IO_ERROR : ASTRA_SYSCALL_OK;
}
uint32_t astra_log_failure(const char *operation, uint32_t status)
{
    (void)operation; (void)status;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    FILE *file = fopen("../../graphics/fonts/astra-workbench.afnt", "rb");
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && length <= FONT_FILE_MAX && fseek(file, 0, SEEK_SET) == 0);
    fixture_size = (uint32_t)length;
    fixture = malloc(fixture_size);
    assert(fixture != NULL && fread(fixture, 1u, fixture_size, file) == fixture_size);
    assert(fclose(file) == 0);
    assert(load_face("fixture", "system:astra-workbench", ASTRA_FONT_CATALOG_SYSTEM, NULL) == ASTRA_VFS_OK);
    assert(load_face("fixture", "system:other", ASTRA_FONT_CATALOG_SYSTEM, NULL) == ASTRA_VFS_OK);
    AstraAfntView view;
    uint32_t metadata = 0u, metadata_size = 0u;
    assert(astra_afnt_validate(fixture, fixture_size, &view));
    assert(load_face_area("system:astra-workbench", 1u, &metadata, &metadata_size) == ASTRA_STATUS_OK);
    assert(metadata_size == view.name.length && metadata_size < fixture_size &&
        areas[metadata - 1u].size == metadata_size &&
        memcmp(areas[metadata - 1u].data, view.name.bytes, metadata_size) == 0);
    assert(astra_close(metadata) == ASTRA_SYSCALL_OK);
    assert(load_face_area("system:missing", 1u, &metadata, &metadata_size) == ASTRA_STATUS_NOT_FOUND);
    FontSelection initial = {0};
    assert(prepare_selection("system:astra-workbench", &initial) == ASTRA_STATUS_OK);
    adopt_selection(&initial);
    uint32_t initial_generation = generation;
    config_ready = 1u;
    font_changed_signal = 99u;
    assert(select_face(selection.id) == ASTRA_STATUS_OK && writes == 1u && signals == 1u);
    assert(strcmp(saved, selection.id) == 0);
    uint32_t held = 0u, bytes = 0u, previous_opens = opens;
    unavailable = 1u;
    assert(face_area(selection.id, &held, &bytes) == ASTRA_STATUS_OK &&
           bytes == fixture_size && opens == previous_opens);
    assert(select_face("system:other") == ASTRA_STATUS_IO && writes == 1u && signals == 1u);
    unavailable = 0u;
    no_area = 1u;
    assert(select_face("system:other") == ASTRA_STATUS_LIMIT && writes == 1u);
    no_area = 0u;
    fixture[0] ^= 1u;
    assert(select_face("system:other") == ASTRA_STATUS_INVALID && writes == 1u);
    fixture[0] ^= 1u;
    fail_config = 1u;
    assert(select_face("system:other") == ASTRA_STATUS_IO && generation == initial_generation &&
           strcmp(selection.id, "system:astra-workbench") == 0 && signals == 1u);
    fail_config = 0u;
    assert(select_face("system:other") == ASTRA_STATUS_OK &&
           generation == initial_generation + 1u && strcmp(saved, selection.id) == 0);
    assert(areas[held - 1u].refs == 1u &&
           memcmp(areas[held - 1u].data, fixture, fixture_size) == 0);
    assert(astra_close(held) == ASTRA_SYSCALL_OK);
    fail_signal = 1u;
    assert(select_face(selection.id) == ASTRA_STATUS_IO);
    fail_signal = 0u;
    unavailable = 1u;
    assert(select_face(selection.id) == ASTRA_STATUS_OK && generation == initial_generation + 1u);
    assert(astra_close(selection.area) == ASTRA_SYSCALL_OK);
    for (uint32_t at = 0u; at < 4u; ++at) assert(areas[at].refs == 0u);
    assert(allocations == 0u);
    free(fixture);
    puts("font selection transaction tests passed");
    return 0;
}
