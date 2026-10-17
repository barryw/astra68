#include <astra/fonts.h>
#include <astra/filesystem_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_process.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("font-outline-probe", 0, 1, 0, "Astra68 contributors",
              "Astra native test");

#define SOURCE "/SYSTEM/fonts/outline-fixture.ttf"
#define NAME "outline-fixture.ttf"

enum {
    FAIL_FILES = ASTRA_STATUS_PROGRAM_FIRST,
    FAIL_SOURCE,
    FAIL_CATALOG,
    FAIL_IMPORT,
    FAIL_FACE,
    FAIL_COUNT,
    FAIL_SELECT,
    FAIL_REPEAT,
    FAIL_RESTART
};

static int has_face(const AstraFontsCatalog *catalog, const char *id)
{
    for (uint32_t at = 0u; at < catalog->face_count; ++at) {
        AstraInstalledFont face;
        if (astra_fonts_catalog_face(catalog, at, &face) != ASTRA_OK)
            return 0;
        if (face.id_length == strlen(id) &&
            memcmp(face.id, id, face.id_length) == 0 &&
            face.strike_count >= 4u)
            return 1;
    }
    return 0;
}

static int has_imported_style(const AstraFontsCatalog *catalog,
                              const char *style)
{
    for (uint32_t at = 0u; at < catalog->face_count; ++at) {
        AstraInstalledFont face;
        if (astra_fonts_catalog_face(catalog, at, &face) != ASTRA_OK)
            return 0;
        if ((face.flags & ASTRA_INSTALLED_FONT_SYSTEM) == 0u &&
            face.family_length == sizeof("STIX Two Text") - 1u &&
            memcmp(face.family, "STIX Two Text", face.family_length) == 0 &&
            face.style_length == strlen(style) &&
            memcmp(face.style, style, face.style_length) == 0)
            return 1;
    }
    return 0;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *read, *admin, *render, *bootstrap;
    AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
    AstraFontsCatalog before = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog after = ASTRA_FONTS_CATALOG_INIT;
    AstraFontsCatalog selected = ASTRA_FONTS_CATALOG_INIT;
    AstraArea face = ASTRA_AREA_INIT;
    AstraFile marker = ASTRA_FILE_INIT;
    void *source = NULL;
    uint32_t source_size = 0u, face_size = 0u;
    uint32_t expected_faces = 1u;
    uint32_t receive = 0u, send = 0u, index = 0u;
    uint32_t status = FAIL_FILES;
    int resumed = 0, expect_limit = 0, expect_later_failure = 0;
    int expect_variable = 0;
    AstraResult result;
    char id[ASTRA_FONT_ID_MAX] = {0};
    char again[ASTRA_FONT_ID_MAX] = {0};

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
    status = FAIL_CATALOG;
    result = astra_fonts_catalog_open(read->handle, &before);
    if (result != ASTRA_OK) {
        status = 60u + (uint32_t)-result;
        goto done;
    }
    resumed = astra_filesystem_open(&files.filesystem,
        "/SYSTEM/expect-outline-again", ASTRA_VFS_OPEN_READ,
        &marker) == ASTRA_VFS_OK;
    if (resumed && astra_filesystem_close(&marker) != ASTRA_VFS_OK)
        goto done;
    status = FAIL_RESTART;
    if (resumed != (strncmp(before.selected_id, "user:", 5u) == 0))
        goto done;
    expect_limit = astra_filesystem_open(&files.filesystem,
        "/SYSTEM/expect-outline-limit", ASTRA_VFS_OPEN_READ,
        &marker) == ASTRA_VFS_OK;
    if (expect_limit && astra_filesystem_close(&marker) != ASTRA_VFS_OK)
        goto done;
    expect_later_failure = astra_filesystem_open(&files.filesystem,
        "/SYSTEM/expect-outline-later-failure", ASTRA_VFS_OPEN_READ,
        &marker) == ASTRA_VFS_OK;
    if (expect_later_failure &&
        astra_filesystem_close(&marker) != ASTRA_VFS_OK)
        goto done;
    expect_variable = astra_filesystem_open(&files.filesystem,
        "/SYSTEM/expect-outline-variable", ASTRA_VFS_OPEN_READ,
        &marker) == ASTRA_VFS_OK;
    if (expect_variable && astra_filesystem_close(&marker) != ASTRA_VFS_OK)
        goto done;
    if (expect_variable) expected_faces = 4u;
    status = FAIL_SOURCE;
    if (astra_process_read_file_alloc(&files, SOURCE, &source,
                                      &source_size) != ASTRA_VFS_OK ||
        source_size == 0u || source_size > ASTRA_FONT_IMPORT_MAX)
        goto done;
    if (source_size >= 12u && memcmp(source, "ttcf", 4u) == 0) {
        const uint8_t *bytes = source;
        expected_faces = (uint32_t)bytes[8] << 24 |
                         (uint32_t)bytes[9] << 16 |
                         (uint32_t)bytes[10] << 8 | bytes[11];
        if (expected_faces == 0u || expected_faces > 64u) goto done;
    }
    status = FAIL_IMPORT;
    result = astra_fonts_import(admin->handle, source, source_size,
                                NAME, sizeof(NAME) - 1u, id);
    if (expect_limit) {
        if (result != ASTRA_ERROR_NO_RESOURCES ||
            astra_fonts_catalog_open(read->handle, &after) != ASTRA_OK ||
            after.face_count != before.face_count ||
            strcmp(after.selected_id, before.selected_id) != 0)
            goto done;
        status = ASTRA_STATUS_OK;
        goto done;
    }
    if (expect_later_failure) {
        if (result == ASTRA_OK ||
            astra_fonts_catalog_open(read->handle, &after) != ASTRA_OK ||
            after.face_count != before.face_count ||
            strcmp(after.selected_id, before.selected_id) != 0)
            goto done;
        status = ASTRA_STATUS_OK;
        goto done;
    }
    if (result != ASTRA_OK) {
        status = 50u + (uint32_t)-result;
        goto done;
    }
    status = FAIL_CATALOG;
    result = astra_fonts_catalog_open(read->handle, &after);
    if (result != ASTRA_OK) {
        status = 70u + (uint32_t)-result;
        goto done;
    }
    status = FAIL_COUNT;
    if (after.face_count != before.face_count +
                            (resumed ? 0u : expected_faces) ||
        !has_face(&after, id))
        goto done;
    if (expect_variable &&
        (!has_imported_style(&after, "Regular") ||
         !has_imported_style(&after, "Medium") ||
         !has_imported_style(&after, "SemiBold") ||
         !has_imported_style(&after, "Bold")))
        goto done;
    status = FAIL_FACE;
    if (astra_fonts_face_open(render->handle, id,
                              (uint32_t)strlen(id), &face, &face_size) !=
            ASTRA_OK || face_size < 4u ||
        memcmp(face.address, "AFNT", 4u) != 0)
        goto done;
    if (astra_area_close(&face) != ASTRA_OK) goto done;
    status = FAIL_SELECT;
    if (astra_fonts_select(admin->handle, id,
                           (uint32_t)strlen(id)) != ASTRA_OK ||
        astra_fonts_catalog_open(read->handle, &selected) != ASTRA_OK ||
        strcmp(selected.selected_id, id) != 0)
        goto done;
    status = FAIL_REPEAT;
    if (astra_fonts_import(admin->handle, source, source_size,
                           NAME, sizeof(NAME) - 1u, again) != ASTRA_OK ||
        strcmp(again, id) != 0)
        goto done;
    status = ASTRA_STATUS_OK;
done:
    if (face.handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_area_close(&face);
        (void)ignored;
    }
    if (selected.generation != 0u) {
        AstraResult ignored = astra_fonts_catalog_close(&selected);
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
    astra_process_filesystem_close(&files);
    if (status == ASTRA_STATUS_OK &&
        astra_rt_port_create(1u, 256u, &receive, &send) != ASTRA_SYSCALL_OK)
        status = ASTRA_STATUS_LIMIT;
    (void)astra_service_ready(bootstrap->handle, status, NULL, 0u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_STATUS_OK) {
        (void)astra_log_failure("font outline probe", status);
        return (int)status;
    }
    (void)astra_log(resumed ? "FONT_OUTLINE_AGAIN_PASS" :
                              "FONT_OUTLINE_FIRST_PASS");
    for (;;)
        if (astra_wait_multiple(&receive, 1u, ASTRA_DEADLINE_FOREVER,
                                &index, NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
}
