#include <astra/afnt.h>
#include <astra/config_library.h>
#include <astra/filesystem_library.h>
#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service.h>

#include "policy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

ASTRA_PROGRAM("fonts", 0, 1, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

/* shortcut: 64 faces; grow inventory when native import can exceed this. */
#define FONT_FACE_MAX 64u
#define FONT_STRIKE_MAX 32u
#define FONT_FILE_MAX (4u * 1024u * 1024u)

typedef struct FontEntry {
    char id[ASTRA_FONT_FACE_ID_MAX];
    char family[256];
    char style[128];
    AstraFontCatalogStrike strikes[FONT_STRIKE_MAX];
    AstraFontCatalogFace face;
} FontEntry;

static FontEntry installed[FONT_FACE_MAX];
static AstraProcessFilesystem files = ASTRA_PROCESS_FILESYSTEM_INIT;
static AstraConfig config = ASTRA_CONFIG_INIT;
static uint32_t installed_count;
static uint32_t generation = 1u;
static char selected[ASTRA_FONT_FACE_ID_MAX];

static FontEntry *find_face(const char *id)
{
    for (uint32_t at = 0u; at < installed_count; ++at)
        if (strcmp(installed[at].id, id) == 0) return &installed[at];
    return NULL;
}

static uint32_t scan(const char *root, const char *prefix, uint32_t flags)
{
    AstraDirectory directory = ASTRA_DIRECTORY_INIT;
    uint32_t status = astra_filesystem_directory_open(
        &files.filesystem, root, &directory);
    if (status != ASTRA_VFS_OK) return status;
    while (status == ASTRA_VFS_OK) {
        AstraDirectoryEntry entries[4];
        uint32_t count = 0u;

        status = astra_filesystem_directory_read(&directory, entries, 4u,
                                                 &count);
        if (status != ASTRA_VFS_OK || count == 0u) break;
        for (uint32_t at = 0u; at < count; ++at) {
            const AstraDirectoryEntry *node = &entries[at];
            const char *name_end = memchr(node->name, '\0',
                                          sizeof(node->name));
            size_t name_length;
            char path[ASTRA_VFS_PATH_MAX];
            FontEntry *entry;
            void *bytes = NULL;
            uint32_t length = 0u;

            if (name_end == NULL) continue;
            name_length = (size_t)(name_end - node->name);
            if (node->kind != ASTRA_VFS_KIND_FILE ||
                node->byte_size == 0u || node->byte_size > FONT_FILE_MAX ||
                name_length < 6u || strcmp(node->name + name_length - 5u,
                                            ".afnt") != 0)
                continue;
            if (installed_count == FONT_FACE_MAX) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            entry = &installed[installed_count];
            int id_length = snprintf(entry->id, sizeof(entry->id), "%s%.*s",
                                     prefix, (int)(name_length - 5u),
                                     node->name);
            int path_length = snprintf(path, sizeof(path), "%s/%s", root,
                                       node->name);
            if (id_length <= 0 || (size_t)id_length >= sizeof(entry->id) ||
                path_length <= 0 || (size_t)path_length >= sizeof(path))
                continue;
            if (astra_process_read_file_alloc(&files, path, &bytes, &length) !=
                ASTRA_VFS_OK) continue;
            if (length <= FONT_FILE_MAX &&
                astra_font_catalog_face_from_afnt(
                    bytes, length, entry->id, (uint32_t)id_length, flags,
                    entry->family, sizeof(entry->family), entry->style,
                    sizeof(entry->style), entry->strikes, FONT_STRIKE_MAX,
                    &entry->face)) ++installed_count;
            astra_runtime_deallocate(bytes);
        }
    }
    astra_filesystem_directory_close(&directory);
    return status;
}

static uint32_t catalog_area(uint32_t *area, uint32_t *size)
{
    AstraFontCatalogFace faces[FONT_FACE_MAX];
    void *mapping = NULL;
    uint32_t writable = 0u;
    uint32_t mapped_size = 0u;
    uint32_t status;

    for (uint32_t at = 0u; at < installed_count; ++at)
        faces[at] = installed[at].face;
    if (!astra_font_catalog_pack(faces, installed_count, generation,
                                 NULL, 0u, size) ||
        *size > ASTRA_AREA_SIZE_MAX)
        return ASTRA_STATUS_LIMIT;
    status = astra_rt_area_create(
        *size, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
               ASTRA_RIGHT_TRANSFER, &writable);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    status = astra_rt_area_map(writable, ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &mapping, &mapped_size);
    if (status != ASTRA_SYSCALL_OK || mapped_size < *size ||
        !astra_font_catalog_pack(faces, installed_count, generation,
                                 mapping, mapped_size, size)) {
        if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
        (void)astra_close(writable);
        return ASTRA_STATUS_IO;
    }
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK) {
        (void)astra_close(writable);
        return ASTRA_STATUS_IO;
    }
    status = astra_rt_handle_duplicate(
        writable, ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        area);
    (void)astra_close(writable);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_IO;
    return ASTRA_STATUS_OK;
}

static void serve(uint32_t port, uint32_t admin)
{
    AstraFontServiceRequest request = {0};
    AstraFontServiceReply reply = {0};
    uint32_t handles[2] = {0};
    uint32_t bytes = 0u, count = 0u, area = 0u, area_size = 0u;
    uint32_t status = astra_port_receive(port, &request, sizeof(request),
                                          handles, 2u, &bytes, &count);
    if (status != ASTRA_SYSCALL_OK) return;
    if (count != 1u) {
        for (uint32_t at = 0u; at < count; ++at)
            (void)astra_close(handles[at]);
        return;
    }
    status = fonts_request_valid(&request, bytes, admin) ? ASTRA_STATUS_OK :
                                                    ASTRA_STATUS_PROTOCOL;
    if (status == ASTRA_STATUS_OK &&
        request.header.operation == ASTRA_FONT_SERVICE_CATALOG) {
        if (request.face_id[0] != '\0') status = ASTRA_STATUS_PROTOCOL;
        else status = catalog_area(&area, &area_size);
    } else if (status == ASTRA_STATUS_OK) {
        FontEntry *face = find_face(request.face_id);
        if (face == NULL) status = ASTRA_STATUS_NOT_FOUND;
        else if (!fonts_face_selectable(&face->face)) status = ASTRA_STATUS_UNSUPPORTED;
        else if (config._private_state == NULL ||
                 astra_config_set_string(&config, "system_font", 0u,
                                         face->id) != ASTRA_CONFIG_OK)
            status = ASTRA_STATUS_IO;
        else memcpy(selected, face->id, sizeof(selected));
    }
    astra_message_header_set(&reply.header, sizeof(reply),
                             ASTRA_FONT_SERVICE_PROTOCOL,
                             ASTRA_FONT_SERVICE_VERSION,
                             ASTRA_FONT_SERVICE_REPLY,
                             request.header.transaction_id);
    reply.status = status;
    reply.generation = generation;
    reply.catalog_size = status == ASTRA_STATUS_OK ? area_size : 0u;
    memcpy(reply.selected_id, selected, sizeof(selected));
    if (astra_port_send(handles[0], &reply, sizeof(reply),
                        area == 0u ? NULL : &area, area == 0u ? 0u : 1u) !=
            ASTRA_SYSCALL_OK && area != 0u)
        (void)astra_close(area);
    (void)astra_close(handles[0]);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    AstraConfigError error = {0};
    uint32_t receive[2] = {0};
    uint32_t published[2] = {0};
    uint32_t status;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL) return ASTRA_STATUS_BAD_HANDLE;
    if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    status = scan("/SYSTEM/fonts", "system:", ASTRA_FONT_CATALOG_SYSTEM);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_mkdir(&files.filesystem,
                                    "/CONFIG_SYSTEM/installed");
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        return status;
    status = scan("/CONFIG_SYSTEM/installed", "user:", 0u);
    if (status != ASTRA_VFS_OK) return status;
    if (astra_config_open_system(startup, 1u,
                                 ASTRA_CONFIG_OPEN_READ |
                                 ASTRA_CONFIG_OPEN_WRITE, &config,
                                 &error) == ASTRA_CONFIG_OK) {
        char wanted[ASTRA_FONT_FACE_ID_MAX] = {0};
        uint32_t length = 0u;
        if (astra_config_get_string(&config, "system_font", 0u, wanted,
                                    sizeof(wanted), &length) == ASTRA_CONFIG_OK) {
            FontEntry *face = find_face(wanted);
            if (face != NULL && fonts_face_selectable(&face->face))
                memcpy(selected, wanted, sizeof(selected));
        }
    }
    if (selected[0] == '\0') {
        FontEntry *fallback = find_face("system:astra-workbench");
        if (fallback != NULL && fonts_face_selectable(&fallback->face))
            memcpy(selected, fallback->id, sizeof(selected));
    }
    for (uint32_t at = 0u; at < 2u; ++at)
        if (astra_rt_port_create(16u, 16u * sizeof(AstraFontServiceRequest),
                                 &receive[at], &published[at]) !=
            ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 published, 2u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint32_t index = 0u;
        status = astra_wait_multiple(receive, 2u, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status != ASTRA_SYSCALL_OK || index > 1u)
            return ASTRA_STATUS_PEER_DEAD;
        serve(receive[index], index);
    }
}
