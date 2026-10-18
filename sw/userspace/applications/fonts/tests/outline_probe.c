#include <astra/fonts.h>
#ifdef ASTRA_FONT_PROBE_AMIGA
#include <astra/graphics.h>
#include <astra/window.h>
#endif
#include <astra/filesystem_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/vfs_process.h>

#include <stdint.h>
#include <string.h>

#ifdef ASTRA_FONT_PROBE_AMIGA
#define PROBE_NAME "font-amiga-probe"
#define SOURCE "/SYSTEM/fonts/amiga-fixture.amiga"
#define NAME "amiga-fixture.amiga"
#define PROBE_ENCODING ASTRA_FONT_ENCODING_ISO_8859_2
#define PROBE_STRIKES 1u
#else
#define PROBE_NAME "font-outline-probe"
#define SOURCE "/SYSTEM/fonts/outline-fixture.ttf"
#define NAME "outline-fixture.ttf"
#define PROBE_ENCODING ASTRA_FONT_ENCODING_AUTO
#define PROBE_STRIKES 4u
#endif

ASTRA_PROGRAM(PROBE_NAME, 0, 1, 0, "Astra68 contributors",
              "Astra native test");

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

static struct {
    AstraHandle admin;
    const void *source;
    uint32_t size;
    AstraResult result;
    char id[ASTRA_FONT_ID_MAX];
} concurrent_import;

static void import_thread(uint32_t unused)
{
    (void)unused;
    concurrent_import.result = astra_fonts_import(concurrent_import.admin,
        concurrent_import.source, concurrent_import.size, NAME, sizeof(NAME) - 1u,
        PROBE_ENCODING, concurrent_import.id);
}

static AstraResult import_while_reading(AstraHandle admin, AstraHandle read,
    const void *source, uint32_t size, const AstraFontsCatalog *before,
    char id[ASTRA_FONT_ID_MAX])
{
    uint32_t thread = 0u, observed_busy = 0u, verified = 0u;
    AstraThreadStart start = {import_thread, 0u};
    concurrent_import.admin = admin;
    concurrent_import.source = source;
    concurrent_import.size = size;
    if (astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_READ, &thread, NULL) != ASTRA_SYSCALL_OK)
        return ASTRA_ERROR_NO_RESOURCES;
    /* A delayed real converter holds the first import open in this fixture. */
    for (uint32_t attempt = 0u; attempt < 64u && !observed_busy; ++attempt) {
        char ignored[ASTRA_FONT_ID_MAX];
        AstraResult result = astra_fonts_import(admin, "AFNT", 4u,
            "invalid.afnt", sizeof("invalid.afnt") - 1u,
            ASTRA_FONT_ENCODING_AUTO, ignored);
        if (result == ASTRA_ERROR_BUSY) observed_busy = 1u;
        else if (result != ASTRA_ERROR_INVALID_ARGUMENT) {
            (void)astra_log_failure("font concurrent contender", (uint32_t)-result);
            break;
        }
        if (astra_wait_one(thread, 0u, NULL) == ASTRA_SYSCALL_OK) break;
    }
    if (observed_busy) {
        AstraFontsCatalog during = ASTRA_FONTS_CATALOG_INIT;
        if (astra_fonts_catalog_open(read, &during) == ASTRA_OK) {
            verified = 1u |
                (during.generation == before->generation ? 2u : 0u) |
                (during.face_count == before->face_count ? 4u : 0u) |
                (strcmp(during.selected_id, before->selected_id) == 0 ? 8u : 0u) |
                (astra_wait_one(thread, 0u, NULL) == ASTRA_SYSCALL_TIMED_OUT ? 16u : 0u);
            AstraResult closed = astra_fonts_catalog_close(&during);
            if (closed == ASTRA_OK) verified |= 32u;
        }
    }
    if (!observed_busy)
        (void)astra_log_debug("font concurrent busy absent", 27u);
    if (verified != 63u)
        (void)astra_log_failure("font concurrent snapshot", verified);
    uint32_t waited = astra_wait_one(thread, ASTRA_DEADLINE_FOREVER, NULL);
    (void)astra_close(thread);
    if (verified != 63u || waited != ASTRA_SYSCALL_OK) return ASTRA_ERROR_IO;
    memcpy(id, concurrent_import.id, sizeof(concurrent_import.id));
    (void)astra_log("ASTRA FONT CONCURRENT CATALOG PASS unchanged during conversion");
    return concurrent_import.result;
}

#ifdef ASTRA_FONT_PROBE_AMIGA
static AstraWindow probe_window = ASTRA_WINDOW_INIT;
static AstraDisplay probe_display = ASTRA_DISPLAY_INIT;
static AstraSurface probe_surface = ASTRA_SURFACE_INIT;

static int probe_named_draw(const AstraStartupInfo *startup, const char *id)
{
    const AstraStartupCapability *gui = astra_startup_capability(
        startup, ASTRA_CAPABILITY_GUI);
    AstraArea placeholder = ASTRA_AREA_INIT;
    AstraWindowCreateInfo info = ASTRA_WINDOW_CREATE_INFO_INIT;
    AstraDrawList list = ASTRA_DRAW_LIST_INIT;
    AstraFence fence = ASTRA_FENCE_INIT;
    AstraResult completion = ASTRA_OK;
    AstraResult result;
    AstraFontTextMetrics metrics = ASTRA_FONT_TEXT_METRICS_INIT;

    if (gui == NULL) return 0;
    result = astra_fonts_text_measure(gui->handle, id, (uint32_t)strlen(id),
        11u, "Amiga font", 10u, UINT32_MAX, &metrics);
    if (result != ASTRA_OK || metrics.width == 0u || metrics.height < 11u ||
        metrics.fitted_bytes != 10u) return 0;
    result = astra_fonts_text_measure(gui->handle, id, (uint32_t)strlen(id),
        11u, "Amiga font", 10u, metrics.width - 1u, &metrics);
    if (result != ASTRA_OK || metrics.fitted_bytes >= 10u) return 0;
    result = astra_fonts_text_measure(gui->handle, id, (uint32_t)strlen(id),
        UINT16_MAX, "Amiga font", 10u, UINT32_MAX, &metrics);
    if (result != ASTRA_ERROR_UNSUPPORTED || metrics.width != 0u ||
        metrics.height != 0u || metrics.fitted_bytes != 0u)
        return 0;
    (void)astra_log("ASTRA FONT MEASURE PASS");
    if (astra_area_create(4096u, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE |
            ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, &placeholder) != ASTRA_OK)
        return 0;
    info.x = 360u;
    info.y = 120u;
    info.width = 320u;
    info.height = 100u;
    info.content_format = ASTRA_WINDOW_CONTENT_SURFACE;
    info.type = ASTRA_WINDOW_STANDARD;
    info.title = "Amiga font draw probe";
    info.title_length = sizeof("Amiga font draw probe") - 1u;
    result = astra_window_create(gui->handle, placeholder.handle,
                                 &info, &probe_window);
    if (astra_area_close(&placeholder) != ASTRA_OK) return 0;
    if (result == ASTRA_OK)
        result = astra_window_display(&probe_window, &probe_display);
    if (result == ASTRA_OK)
        result = astra_window_surface(&probe_window, &probe_surface);
    if (result == ASTRA_OK)
        result = astra_draw_list_create(&probe_surface,
            &(AstraRectI32){0, 0, 320, 100}, &list);
    if (result == ASTRA_OK)
        result = astra_draw_font_text(&list, (AstraPointI32){20, 20},
            id, (uint32_t)strlen(id), "Amiga font", 10u, 11u, 0u,
            (AstraColorRGBA8){255u, 255u, 255u, 255u});
    if (result == ASTRA_OK)
        result = astra_draw_submit(&list, &fence);
    if (result == ASTRA_OK)
        result = astra_fence_wait(&fence, 5000u, &completion);
    if (result == ASTRA_OK) result = completion;
    if (result == ASTRA_OK)
        result = astra_window_present(&probe_window);
    if (fence._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_fence_close(&fence);
        (void)ignored;
    }
    if (list._private_handle != ASTRA_INVALID_HANDLE) {
        AstraResult ignored = astra_draw_list_close(&list);
        (void)ignored;
    }
    return result == ASTRA_OK;
}
#endif

static int has_face(const AstraFontsCatalog *catalog, const char *id)
{
    for (uint32_t at = 0u; at < catalog->face_count; ++at) {
        AstraInstalledFont face;
        if (astra_fonts_catalog_face(catalog, at, &face) != ASTRA_OK)
            return 0;
        if (face.id_length == strlen(id) &&
            memcmp(face.id, id, face.id_length) == 0 &&
            face.strike_count >= PROBE_STRIKES &&
            ((face.flags & ASTRA_INSTALLED_FONT_SELECTABLE) != 0u) ==
                (PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO))
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
    uint32_t source_size = 0u;
#ifndef ASTRA_FONT_PROBE_AMIGA
    uint32_t face_size = 0u;
#endif
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
    if (read == NULL || admin == NULL || bootstrap == NULL ||
        (PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO && render == NULL))
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
    if (PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO &&
        resumed != (strncmp(before.selected_id, "user:", 5u) == 0))
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
    uint32_t concurrent = astra_filesystem_open(&files.filesystem,
        "/SYSTEM/expect-outline-concurrent", ASTRA_VFS_OPEN_READ,
        &marker) == ASTRA_VFS_OK;
    if (concurrent && astra_filesystem_close(&marker) != ASTRA_VFS_OK)
        goto done;
    result = concurrent ? import_while_reading(admin->handle, read->handle,
        source, source_size, &before, id) :
        astra_fonts_import(admin->handle, source, source_size,
            NAME, sizeof(NAME) - 1u, PROBE_ENCODING, id);
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
#ifdef ASTRA_FONT_PROBE_AMIGA
    status = FAIL_FACE;
    if (!probe_named_draw(startup, id)) goto done;
#else
    status = FAIL_FACE;
    if (astra_fonts_face_open(render->handle, id,
                              (uint32_t)strlen(id), &face, &face_size) !=
            ASTRA_OK || face_size < 4u ||
        memcmp(face.address, "AFNT", 4u) != 0)
        goto done;
    if (astra_area_close(&face) != ASTRA_OK) goto done;
#endif
    status = FAIL_SELECT;
    result = astra_fonts_select(admin->handle, id,
                                (uint32_t)strlen(id));
    if (result != (PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO ?
                  ASTRA_OK : ASTRA_ERROR_UNSUPPORTED) ||
        astra_fonts_catalog_open(read->handle, &selected) != ASTRA_OK ||
        strcmp(selected.selected_id,
               PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO ?
               id : before.selected_id) != 0)
        goto done;
    status = FAIL_REPEAT;
    if (astra_fonts_import(admin->handle, source, source_size,
                           NAME, sizeof(NAME) - 1u,
                           PROBE_ENCODING, again) != ASTRA_OK ||
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
    (void)astra_log(PROBE_ENCODING == ASTRA_FONT_ENCODING_AUTO ?
        (resumed ? "FONT_OUTLINE_AGAIN_PASS" : "FONT_OUTLINE_FIRST_PASS") :
        (resumed ? "FONT_AMIGA_AGAIN_PASS" : "FONT_AMIGA_FIRST_PASS"));
    for (;;)
        if (astra_wait_multiple(&receive, 1u, ASTRA_DEADLINE_FOREVER,
                                &index, NULL) != ASTRA_SYSCALL_OK)
            return ASTRA_STATUS_PEER_DEAD;
}
