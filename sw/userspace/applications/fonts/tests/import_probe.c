#include <astra/fonts.h>
#include <astra/filesystem_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_process.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("font-import-probe", 0, 1, 0, "Astra68 contributors",
              "Astra native test");

#define SOURCE "/SYSTEM/fonts/astra-workbench.afnt"
#define SOURCE_NAME "astra-workbench.afnt"
#define SECOND_SOURCE "/SYSTEM/fonts/astra-mono.afnt"

enum {
    PROBE_FAIL_FILES = ASTRA_STATUS_PROGRAM_FIRST,
    PROBE_FAIL_SOURCE,
    PROBE_FAIL_SIZE,
    PROBE_FAIL_CATALOG_BEFORE,
    PROBE_FAIL_IMPORT,
    PROBE_FAIL_CATALOG_AFTER,
    PROBE_FAIL_MATCH,
    PROBE_FAIL_COUNT,
    PROBE_FAIL_SELECT,
    PROBE_FAIL_CATALOG_SELECTED,
    PROBE_FAIL_SELECTED,
    PROBE_FAIL_DUPLICATE,
    PROBE_FAIL_DUPLICATE_ID,
    PROBE_FAIL_RESTART,
    PROBE_FAIL_CORRUPT_CATALOG,
    PROBE_FAIL_CORRUPT_CHOICE,
    PROBE_FAIL_CORRUPT_WRITE,
    PROBE_FAIL_OPEN_FACE,
    PROBE_FAIL_FACE_BYTES,
    PROBE_FAIL_FACE_CLOSE,
    PROBE_FAIL_UNINSTALL_ACTIVE,
    PROBE_FAIL_UNINSTALL_SYSTEM,
    PROBE_FAIL_UNINSTALL_SELECT,
    PROBE_FAIL_UNINSTALL,
    PROBE_FAIL_UNINSTALL_CATALOG,
    PROBE_FAIL_UNINSTALL_RENDER,
    PROBE_FAIL_UNINSTALL_RESTART,
    PROBE_FAIL_SECOND_IMPORT,
    PROBE_FAIL_RETAINED_FACE
};

static uint32_t matches(const AstraFontsCatalog *catalog, const char *id)
{
    uint32_t count = 0u;
    for (uint32_t at = 0u; at < catalog->face_count; ++at) {
        AstraInstalledFont face;
        if (astra_fonts_catalog_face(catalog, at, &face) != ASTRA_OK)
            return UINT32_MAX;
        if (face.id_length == strlen(id) &&
            memcmp(face.id, id, face.id_length) == 0)
            ++count;
    }
    return count;
}

static uint32_t marker(AstraProcessFilesystem *files, const char *path,
                       uint32_t *present)
{
    AstraFile file = ASTRA_FILE_INIT;
    uint32_t status = astra_filesystem_open(
        &files->filesystem, path, ASTRA_VFS_OPEN_READ, &file);

    *present = status == ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK) return astra_filesystem_close(&file);
    return status == ASTRA_VFS_ERR_NOT_FOUND ? ASTRA_VFS_OK : status;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *read, *admin, *render, *bootstrap;
    AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFontsCatalog before = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog after = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog selected = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog removed = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog remaining = ASTRA_FONTS_CATALOG_INIT;
    AstraArea face_area = ASTRA_AREA_INIT;
    void *source = NULL;
    void *second_source = NULL;
    uint32_t source_size = 0u;
    uint32_t second_size = 0u;
    uint32_t face_size = 0u;
    uint32_t receive = 0u, send = 0u, index = 0u;
    uint32_t status = PROBE_FAIL_FILES;
    char id[ASTRA_FONT_ID_MAX] = {0};
    char duplicate[ASTRA_FONT_ID_MAX] = {0};
    char second_id[ASTRA_FONT_ID_MAX] = {0};
    uint32_t resumed = 0u;
    uint32_t expect_again = 0u;
    uint32_t expect_corrupt = 0u;
    uint32_t expect_uninstalled = 0u;
    AstraResult result;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    read = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS);
    admin = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS_ADMIN);
    render = astra_startup_capability(startup, ASTRA_CAPABILITY_FONTS_RENDER);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (read == NULL || admin == NULL || render == NULL || bootstrap == NULL)
        return ASTRA_STATUS_BAD_HANDLE;
    if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        goto done;
    status = PROBE_FAIL_SOURCE;
    if (astra_process_read_file_alloc(&files, SOURCE, &source,
                                      &source_size) != ASTRA_VFS_OK)
        goto done;
    status = PROBE_FAIL_SIZE;
    if (source_size == 0u || source_size > ASTRA_FONT_IMPORT_MAX) goto done;
    status = PROBE_FAIL_OPEN_FACE;
    if (astra_fonts_face_open(render->handle, "system:astra-workbench", 22u,
                              &face_area, &face_size) != ASTRA_OK)
        goto done;
    status = PROBE_FAIL_FACE_BYTES;
    if (face_size != source_size ||
        memcmp(face_area.address, source, source_size) != 0)
        goto done;
    status = PROBE_FAIL_FACE_CLOSE;
    if (astra_area_close(&face_area) != ASTRA_OK) goto done;
    status = PROBE_FAIL_CATALOG_BEFORE;
    if (astra_fonts_catalog_open(read->handle, &before) != ASTRA_OK)
        goto done;
    if (marker(&files, "/SYSTEM/expect-font-import-again",
               &expect_again) != ASTRA_VFS_OK ||
        marker(&files, "/SYSTEM/expect-font-inventory-corrupt",
               &expect_corrupt) != ASTRA_VFS_OK ||
        marker(&files, "/SYSTEM/expect-font-uninstalled",
               &expect_uninstalled) != ASTRA_VFS_OK)
        goto done;
    if (expect_corrupt) {
        status = PROBE_FAIL_CORRUPT_CATALOG;
        for (uint32_t at = 0u; at < before.face_count; ++at) {
            AstraInstalledFont face;
            if (astra_fonts_catalog_face(&before, at, &face) != ASTRA_OK ||
                (face.id_length >= 5u &&
                 memcmp(face.id, "user:", 5u) == 0))
                goto done;
        }
        status = PROBE_FAIL_CORRUPT_CHOICE;
        if (strcmp(before.selected_id, "system:astra-workbench") != 0)
            goto done;
        status = PROBE_FAIL_CORRUPT_WRITE;
        if (astra_fonts_import(admin->handle, source, source_size,
                               SOURCE_NAME, sizeof(SOURCE_NAME) - 1u,
                               id) != ASTRA_ERROR_IO)
            goto done;
        status = ASTRA_STATUS_OK;
        goto done;
    }
    if (expect_uninstalled) {
        status = PROBE_FAIL_UNINSTALL_RESTART;
        if (strcmp(before.selected_id, "system:astra-workbench") != 0)
            goto done;
        for (uint32_t at = 0u; at < before.face_count; ++at) {
            AstraInstalledFont face;
            if (astra_fonts_catalog_face(&before, at, &face) != ASTRA_OK ||
                (face.id_length >= 5u &&
                 memcmp(face.id, "user:", 5u) == 0)) goto done;
        }
        status = ASTRA_STATUS_OK;
        goto done;
    }
    resumed = strncmp(before.selected_id, "user:", 5u) == 0;
    status = PROBE_FAIL_RESTART;
    if (resumed != expect_again) goto done;
    status = PROBE_FAIL_IMPORT;
    result = astra_fonts_import(admin->handle, source, source_size,
                                SOURCE_NAME, sizeof(SOURCE_NAME) - 1u, id);
    if (result != ASTRA_OK) {
        status = 50u + (uint32_t)-result;
        goto done;
    }
    status = PROBE_FAIL_CATALOG_AFTER;
    if (astra_fonts_catalog_open(read->handle, &after) != ASTRA_OK) goto done;
    status = PROBE_FAIL_MATCH;
    if (matches(&after, id) != 1u) goto done;
    status = PROBE_FAIL_OPEN_FACE;
    if (astra_fonts_face_open(render->handle, id, (uint32_t)strlen(id),
                              &face_area, &face_size) != ASTRA_OK)
        goto done;
    status = PROBE_FAIL_FACE_BYTES;
    if (face_size != source_size ||
        memcmp(face_area.address, source, source_size) != 0)
        goto done;
    status = PROBE_FAIL_FACE_CLOSE;
    if (astra_area_close(&face_area) != ASTRA_OK) goto done;
    status = PROBE_FAIL_COUNT;
    if (after.face_count != before.face_count + (resumed ? 0u : 1u))
        goto done;
    status = PROBE_FAIL_SELECT;
    if (astra_fonts_select(admin->handle, id,
                           (uint32_t)strlen(id)) != ASTRA_OK) goto done;
    status = PROBE_FAIL_CATALOG_SELECTED;
    if (astra_fonts_catalog_open(read->handle, &selected) != ASTRA_OK)
        goto done;
    status = PROBE_FAIL_SELECTED;
    if (strcmp(selected.selected_id, id) != 0 ||
        selected.generation != after.generation + (resumed ? 0u : 1u))
        goto done;
    status = PROBE_FAIL_DUPLICATE;
    if (astra_fonts_import(admin->handle, source, source_size,
                           SOURCE_NAME, sizeof(SOURCE_NAME) - 1u,
                           duplicate) != ASTRA_OK) goto done;
    status = PROBE_FAIL_DUPLICATE_ID;
    if (strcmp(duplicate, id) != 0) goto done;
    if (resumed) {
        status = PROBE_FAIL_SECOND_IMPORT;
        if (astra_process_read_file_alloc(&files, SECOND_SOURCE,
                                          &second_source, &second_size) !=
                ASTRA_VFS_OK || second_size == 0u ||
            astra_fonts_import(admin->handle, second_source, second_size,
                               "astra-mono.afnt", 15u,
                               second_id) != ASTRA_OK)
            goto done;
        status = PROBE_FAIL_UNINSTALL_ACTIVE;
        if (astra_fonts_uninstall(admin->handle, id,
                                  (uint32_t)strlen(id)) != ASTRA_ERROR_BUSY)
            goto done;
        status = PROBE_FAIL_UNINSTALL_SYSTEM;
        if (astra_fonts_uninstall(admin->handle,
                                  "system:astra-workbench", 22u) !=
            ASTRA_ERROR_PERMISSION) goto done;
        status = PROBE_FAIL_UNINSTALL_SELECT;
        if (astra_fonts_select(admin->handle,
                               "system:astra-workbench", 22u) != ASTRA_OK)
            goto done;
        status = PROBE_FAIL_UNINSTALL;
        if (astra_fonts_uninstall(admin->handle, id,
                                  (uint32_t)strlen(id)) != ASTRA_OK)
            goto done;
        status = PROBE_FAIL_RETAINED_FACE;
        if (astra_fonts_catalog_open(read->handle, &remaining) != ASTRA_OK ||
            matches(&remaining, id) != 0u ||
            matches(&remaining, second_id) != 1u ||
            remaining.face_count != before.face_count ||
            remaining.generation != selected.generation + 3u)
            goto done;
        if (astra_fonts_uninstall(admin->handle, second_id,
                                  (uint32_t)strlen(second_id)) != ASTRA_OK)
            goto done;
        status = PROBE_FAIL_UNINSTALL_CATALOG;
        if (astra_fonts_catalog_open(read->handle, &removed) != ASTRA_OK ||
            matches(&removed, id) != 0u ||
            removed.face_count != before.face_count - 1u ||
            strcmp(removed.selected_id, "system:astra-workbench") != 0 ||
            removed.generation != selected.generation + 4u)
            goto done;
        status = PROBE_FAIL_UNINSTALL_RENDER;
        if (astra_fonts_face_open(render->handle, id,
                                  (uint32_t)strlen(id), &face_area,
                                  &face_size) != ASTRA_ERROR_NOT_PRESENT)
            goto done;
    }
    status = ASTRA_STATUS_OK;
done:
    if (face_area.handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(&face_area);
        (void)ignored;
    }
    if (selected.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&selected);
        (void)ignored;
    }
    if (removed.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&removed);
        (void)ignored;
    }
    if (remaining.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&remaining);
        (void)ignored;
    }
    if (after.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&after);
        (void)ignored;
    }
    if (before.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&before);
        (void)ignored;
    }
    astra_runtime_deallocate(source);
    astra_runtime_deallocate(second_source);
    astra_process_filesystem_close(&files);
    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(1u, 256u, &receive, &send) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_log_failure("font import probe", status);
        return (int)status;
    }
    (void)astra_log(expect_corrupt ? "FONT_IMPORT_CORRUPT_PASS" :
                    expect_uninstalled ? "FONT_UNINSTALL_RESTART_PASS" :
                    resumed ? "FONT_IMPORT_AGAIN_PASS" :
                              "FONT_IMPORT_FIRST_PASS");
    for (;;)
        if (astra_wait_multiple(&receive, 1u, ASTRA_DEADLINE_FOREVER,
                                &index, NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
}
