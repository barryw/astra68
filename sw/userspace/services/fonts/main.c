#include <astra/afnt.h>
#include <astra/config_library.h>
#include <astra/filesystem_library.h>
#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/font_store.h>
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
#define FONT_FILE_MAX ASTRA_FONT_SOURCE_MAX
#define FONT_INVENTORY_MAX (FONT_FACE_MAX * ASTRA_FONT_OBJECT_TOKEN_SIZE)

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
static char inventory_text[FONT_INVENTORY_MAX];
static char inventory_tokens[FONT_FACE_MAX][ASTRA_FONT_OBJECT_TOKEN_SIZE];
static uint32_t inventory_count;
static uint32_t inventory_ready;
static uint32_t config_ready;

static FontEntry *find_face(const char *id)
{
    for (uint32_t at = 0u; at < installed_count; ++at)
        if (strcmp(installed[at].id, id) == 0) return &installed[at];
    return NULL;
}

static uint32_t read_afnt(const char *path, void **bytes, uint32_t *length)
{
    AstraFile file = ASTRA_FILE_INIT;
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint32_t status = astra_filesystem_open(
        &files.filesystem, path, ASTRA_VFS_OPEN_READ, &file);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_file_info(&file, &info);
    if (status == ASTRA_VFS_OK &&
        (info.kind != ASTRA_VFS_KIND_FILE || info.byte_size == 0u ||
         info.byte_size > FONT_FILE_MAX))
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK) {
        *bytes = astra_runtime_allocate((size_t)info.byte_size);
        if (*bytes == NULL) status = ASTRA_VFS_ERR_LIMIT;
    }
    *length = 0u;
    while (status == ASTRA_VFS_OK && *length < info.byte_size) {
        uint32_t moved = 0u;
        status = astra_filesystem_read(
            &file, (uint8_t *)*bytes + *length,
            (uint32_t)info.byte_size - *length, &moved);
        if (status == ASTRA_VFS_OK && moved == 0u) status = ASTRA_VFS_ERR_IO;
        *length += moved;
    }
    uint32_t closed = astra_filesystem_close(&file);
    if (status == ASTRA_VFS_OK) status = closed;
    if (status != ASTRA_VFS_OK) {
        astra_runtime_deallocate(*bytes);
        *bytes = NULL;
        *length = 0u;
    }
    return status;
}

static uint32_t load_face(const char *path, const char *id, uint32_t flags,
                          const char *token)
{
    FontEntry *entry;
    void *bytes = NULL;
    uint32_t length = 0u;
    uint32_t status;

    if (installed_count == FONT_FACE_MAX || find_face(id) != NULL)
        return ASTRA_VFS_ERR_LIMIT;
    status = read_afnt(path, &bytes, &length);
    if (status != ASTRA_VFS_OK) return status;
    if (token != NULL) {
        char actual[ASTRA_FONT_OBJECT_TOKEN_SIZE];
        astra_font_object_token(bytes, length, actual);
        if (strcmp(actual, token) != 0) status = ASTRA_VFS_ERR_INVALID;
    }
    entry = &installed[installed_count];
    if (status == ASTRA_VFS_OK && strlen(id) >= sizeof(entry->id))
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK) {
        strcpy(entry->id, id);
    }
    if (status == ASTRA_VFS_OK &&
        (!astra_font_catalog_face_from_afnt(
             bytes, length, entry->id, (uint32_t)strlen(entry->id), flags,
             entry->family, sizeof(entry->family), entry->style,
             sizeof(entry->style), entry->strikes, FONT_STRIKE_MAX,
             &entry->face)))
        status = ASTRA_VFS_ERR_INVALID;
    if (status == ASTRA_VFS_OK)
        ++installed_count;
    astra_runtime_deallocate(bytes);
    return status;
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
            char id[ASTRA_FONT_FACE_ID_MAX];

            if (name_end == NULL) continue;
            name_length = (size_t)(name_end - node->name);
            if (node->kind != ASTRA_VFS_KIND_FILE ||
                node->byte_size == 0u || node->byte_size > FONT_FILE_MAX ||
                name_length < 6u || strcmp(node->name + name_length - 5u,
                                            ".afnt") != 0)
                continue;
            int id_length = snprintf(id, sizeof(id), "%s%.*s",
                                     prefix, (int)(name_length - 5u),
                                     node->name);
            int path_length = snprintf(path, sizeof(path), "%s/%s", root,
                                       node->name);
            if (id_length <= 0 || (size_t)id_length >= sizeof(id) ||
                path_length <= 0 || (size_t)path_length >= sizeof(path))
                continue;
            if (installed_count == FONT_FACE_MAX) {
                status = ASTRA_VFS_ERR_LIMIT;
                break;
            }
            (void)load_face(path, id, flags, NULL);
        }
    }
    astra_filesystem_directory_close(&directory);
    return status;
}

static uint32_t load_inventory(void)
{
    uint32_t length = 0u, count = 0u, values = 0u;
    uint32_t result = astra_config_count(&config, "installed", &values);
    if (result != ASTRA_CONFIG_OK || values > 1u)
        return ASTRA_VFS_ERR_INVALID;
    if (values == 0u) {
        inventory_ready = 1u;
        return ASTRA_VFS_OK;
    }
    result = astra_config_get_string(&config, "installed", 0u,
                                     inventory_text, sizeof(inventory_text),
                                     &length);
    if (result != ASTRA_CONFIG_OK ||
        !fonts_inventory_parse(inventory_text, length, inventory_tokens,
                               FONT_FACE_MAX - installed_count, &count))
        return ASTRA_VFS_ERR_INVALID;
    for (uint32_t at = 0u; at < count; ++at) {
        char path[ASTRA_VFS_PATH_MAX];
        char id[ASTRA_FONT_FACE_ID_MAX];
        int path_length = snprintf(path, sizeof(path),
                                   "/CONFIG_SYSTEM/installed/%s.afnt",
                                   inventory_tokens[at]);
        int id_length = snprintf(id, sizeof(id), "user:%s",
                                 inventory_tokens[at]);
        if (path_length <= 0 || (size_t)path_length >= sizeof(path) ||
            id_length <= 0 || (size_t)id_length >= sizeof(id))
            return ASTRA_VFS_ERR_INVALID;
        uint32_t status = load_face(path, id, 0u, inventory_tokens[at]);
        if (status != ASTRA_VFS_OK) return status;
    }
    inventory_count = count;
    inventory_ready = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t store_object(const char *token, const void *bytes,
                             uint32_t length)
{
    char path[ASTRA_VFS_PATH_MAX], temporary[ASTRA_VFS_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    AstraFile directory = ASTRA_FILE_INIT;
    void *existing = NULL;
    uint32_t existing_length = 0u, used = 0u;
    uint32_t status;
    uint32_t opened;

    if (snprintf(path, sizeof(path),
                 "/CONFIG_SYSTEM/installed/%s.afnt", token) >=
            (int)sizeof(path) ||
        snprintf(temporary, sizeof(temporary),
                 "/CONFIG_SYSTEM/installed/%s.tmp", token) >=
            (int)sizeof(temporary))
        return ASTRA_VFS_ERR_LIMIT;
    status = read_afnt(path, &existing, &existing_length);
    if (status == ASTRA_VFS_OK) {
        char actual[ASTRA_FONT_OBJECT_TOKEN_SIZE];
        astra_font_object_token(existing, existing_length, actual);
        astra_runtime_deallocate(existing);
        return strcmp(actual, token) == 0 ? ASTRA_VFS_OK :
                                           ASTRA_VFS_ERR_INVALID;
    }
    if (status != ASTRA_VFS_ERR_NOT_FOUND) return status;
    status = astra_filesystem_unlink(&files.filesystem, temporary, 0u);
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_NOT_FOUND)
        return status;
    status = astra_filesystem_open_mode(
        &files.filesystem, temporary,
        ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
            ASTRA_VFS_OPEN_EXCLUSIVE,
        0600u, &file);
    opened = status == ASTRA_VFS_OK;
    while (status == ASTRA_VFS_OK && used < length) {
        uint32_t moved = 0u;
        status = astra_filesystem_write(
            &file, length, (const uint8_t *)bytes + used,
            length - used, &moved);
        if (status == ASTRA_VFS_OK && moved == 0u)
            status = ASTRA_VFS_ERR_IO;
        used += moved;
    }
    if (status == ASTRA_VFS_OK) status = astra_filesystem_sync(&file);
    if (opened) {
        uint32_t closed = astra_filesystem_close(&file);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    if (status == ASTRA_VFS_OK)
        status = astra_filesystem_rename(&files.filesystem, temporary, path);
    if (status != ASTRA_VFS_OK) {
        (void)astra_filesystem_unlink(&files.filesystem, temporary, 0u);
        return status;
    }
    status = astra_filesystem_open(
        &files.filesystem, "/CONFIG_SYSTEM/installed",
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY, &directory);
    opened = status == ASTRA_VFS_OK;
    if (status == ASTRA_VFS_OK) status = astra_filesystem_sync(&directory);
    if (opened) {
        uint32_t closed = astra_filesystem_close(&directory);
        if (status == ASTRA_VFS_OK) status = closed;
    }
    return status;
}

static uint32_t import_afnt(uint32_t area,
                            const AstraFontServiceRequest *request,
                            char imported[ASTRA_FONT_FACE_ID_MAX])
{
    void *mapping = NULL;
    uint32_t mapped_size = 0u;
    uint32_t status;
    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];
    char manifest[FONT_INVENTORY_MAX];
    FontEntry *entry;

    if (!inventory_ready || !config_ready)
        return ASTRA_STATUS_IO;
    status = astra_rt_area_map(area, ASTRA_AREA_MAP_READ,
                               &mapping, &mapped_size);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_BAD_HANDLE;
    if (mapped_size < request->source_size) {
        status = ASTRA_STATUS_PROTOCOL;
        goto done;
    }
    if (request->source_size < 4u ||
        memcmp(mapping, "AFNT", 4u) != 0) {
        status = ASTRA_STATUS_UNSUPPORTED;
        goto done;
    }
    astra_font_object_token(mapping, request->source_size, token);
    if (snprintf(imported, ASTRA_FONT_FACE_ID_MAX, "user:%s", token) >=
            (int)ASTRA_FONT_FACE_ID_MAX) {
        status = ASTRA_STATUS_LIMIT;
        goto done;
    }
    if (find_face(imported) != NULL) {
        status = ASTRA_STATUS_OK;
        goto done;
    }
    if (installed_count == FONT_FACE_MAX ||
        inventory_count >= FONT_FACE_MAX) {
        status = ASTRA_STATUS_LIMIT;
        goto done;
    }
    entry = &installed[installed_count];
    strcpy(entry->id, imported);
    if (!astra_font_catalog_face_from_afnt(
            mapping, request->source_size, entry->id,
            (uint32_t)strlen(entry->id), 0u,
            entry->family, sizeof(entry->family), entry->style,
            sizeof(entry->style), entry->strikes, FONT_STRIKE_MAX,
            &entry->face)) {
        status = ASTRA_STATUS_INVALID;
        goto done;
    }
    int size = snprintf(manifest, sizeof(manifest), "%s%s%s",
                        inventory_text, inventory_count == 0u ? "" : " ",
                        token);
    if (size <= 0 || (size_t)size >= sizeof(manifest)) {
        status = ASTRA_STATUS_LIMIT;
        goto done;
    }
    status = store_object(token, mapping, request->source_size);
    if (status != ASTRA_VFS_OK) goto done;
    if (astra_config_set_string(&config, "installed", 0u, manifest) !=
        ASTRA_CONFIG_OK) {
        status = ASTRA_STATUS_IO;
        goto done;
    }
    memcpy(inventory_text, manifest, (size_t)size + 1u);
    memcpy(inventory_tokens[inventory_count], token,
           ASTRA_FONT_OBJECT_TOKEN_SIZE);
    ++inventory_count;
    ++installed_count;
    if (++generation == 0u) generation = 1u;
    status = ASTRA_STATUS_OK;
done:
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK &&
        status == ASTRA_STATUS_OK)
        status = ASTRA_STATUS_IO;
    if (status != ASTRA_STATUS_OK) imported[0] = '\0';
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

static uint32_t face_area(const char *id, uint32_t *area, uint32_t *size)
{
    char path[ASTRA_VFS_PATH_MAX];
    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];
    AstraAfntView view;
    void *bytes = NULL, *mapping = NULL;
    uint32_t length = 0u, mapped_size = 0u, writable = 0u;
    uint32_t status;
    int path_length;

    if (find_face(id) == NULL) return ASTRA_STATUS_NOT_FOUND;
    if (strncmp(id, "system:", 7u) == 0)
        path_length = snprintf(path, sizeof(path), "/SYSTEM/fonts/%s.afnt",
                               id + 7u);
    else if (strncmp(id, "user:", 5u) == 0)
        path_length = snprintf(path, sizeof(path),
                               "/CONFIG_SYSTEM/installed/%s.afnt", id + 5u);
    else return ASTRA_STATUS_INVALID;
    if (path_length <= 0 || (size_t)path_length >= sizeof(path))
        return ASTRA_STATUS_LIMIT;
    status = read_afnt(path, &bytes, &length);
    if (status != ASTRA_VFS_OK) return ASTRA_STATUS_IO;
    status = astra_afnt_validate(bytes, length, &view) ? ASTRA_STATUS_OK :
                                                         ASTRA_STATUS_INVALID;
    if (status == ASTRA_STATUS_OK && strncmp(id, "user:", 5u) == 0) {
        astra_font_object_token(bytes, length, token);
        if (strcmp(token, id + 5u) != 0) status = ASTRA_STATUS_INVALID;
    }
    if (status != ASTRA_STATUS_OK) goto done;
    status = astra_rt_area_create(
        length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                    ASTRA_RIGHT_TRANSFER, &writable);
    if (status != ASTRA_SYSCALL_OK) {
        status = ASTRA_STATUS_LIMIT;
        goto done;
    }
    status = astra_rt_area_map(writable,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &mapping, &mapped_size);
    if (status != ASTRA_SYSCALL_OK || mapped_size < length) {
        status = ASTRA_STATUS_IO;
        goto done;
    }
    memcpy(mapping, bytes, length);
    status = astra_rt_area_unmap(mapping);
    mapping = NULL;
    if (status != ASTRA_SYSCALL_OK) {
        status = ASTRA_STATUS_IO;
        goto done;
    }
    status = astra_rt_handle_duplicate(
        writable, ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        area);
    if (status == ASTRA_SYSCALL_OK) *size = length;
    else status = ASTRA_STATUS_IO;
done:
    if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
    if (writable != 0u) (void)astra_close(writable);
    astra_runtime_deallocate(bytes);
    return status;
}

static void serve(uint32_t port, uint32_t role)
{
    AstraFontServiceRequest request = {0};
    AstraFontServiceReply reply = {0};
    uint8_t packet[ASTRA_MESSAGE_SIZE_MAX];
    uint32_t handles[ASTRA_MESSAGE_HANDLES_MAX] = {0};
    uint32_t bytes = 0u, count = 0u, area = 0u, area_size = 0u;
    uint32_t status = astra_port_receive(
        port, packet, sizeof(packet), handles,
        ASTRA_MESSAGE_HANDLES_MAX, &bytes, &count);
    if (status != ASTRA_SYSCALL_OK) return;
    if (bytes == sizeof(request)) memcpy(&request, packet, sizeof(request));
    status = fonts_request_valid(&request, bytes, role) ? ASTRA_STATUS_OK :
                                                    ASTRA_STATUS_PROTOCOL;
    if (count != (request.header.operation == ASTRA_FONT_SERVICE_IMPORT ?
                  2u : 1u)) status = ASTRA_STATUS_PROTOCOL;
    if (status == ASTRA_STATUS_OK &&
        request.header.operation == ASTRA_FONT_SERVICE_CATALOG) {
        if (request.face_id[0] != '\0') status = ASTRA_STATUS_PROTOCOL;
        else status = catalog_area(&area, &area_size);
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_IMPORT) {
        /* Port-call appends the reply capability after the source area. */
        status = import_afnt(handles[0], &request, reply.imported_id);
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_OPEN_FACE) {
        status = face_area(request.face_id, &area, &area_size);
    } else if (status == ASTRA_STATUS_OK) {
        FontEntry *face = find_face(request.face_id);
        if (face == NULL) status = ASTRA_STATUS_NOT_FOUND;
        else if (!fonts_face_selectable(&face->face)) status = ASTRA_STATUS_UNSUPPORTED;
        else if (!config_ready ||
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
    reply.area_size = status == ASTRA_STATUS_OK ? area_size : 0u;
    memcpy(reply.selected_id, selected, sizeof(selected));
    if (count != 0u &&
        astra_port_send(handles[count - 1u], &reply, sizeof(reply),
                        area == 0u ? NULL : &area, area == 0u ? 0u : 1u) !=
            ASTRA_SYSCALL_OK && area != 0u)
        (void)astra_close(area);
    else if (count == 0u && area != 0u)
        (void)astra_close(area);
    for (uint32_t at = 0u; at < count; ++at)
        (void)astra_close(handles[at]);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    AstraConfigError error = {0};
    uint32_t receive[3] = {0};
    uint32_t published[3] = {0};
    uint32_t status;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (bootstrap == NULL) return ASTRA_STATUS_BAD_HANDLE;
    if (astra_process_filesystem_open(&files, startup) != ASTRA_VFS_OK)
        return ASTRA_STATUS_NOT_FOUND;
    status = scan("/SYSTEM/fonts", "system:", ASTRA_FONT_CATALOG_SYSTEM);
    if (status != ASTRA_VFS_OK) return status;
    uint32_t system_count = installed_count;
    status = astra_filesystem_mkdir(&files.filesystem,
                                    "/CONFIG_SYSTEM/installed");
    if (status != ASTRA_VFS_OK && status != ASTRA_VFS_ERR_EXISTS)
        (void)astra_log_failure("fonts installed directory", status);
    uint32_t config_status =
        status == ASTRA_VFS_OK || status == ASTRA_VFS_ERR_EXISTS ?
        astra_config_open_system(startup, 1u,
                                 ASTRA_CONFIG_OPEN_READ |
                                 ASTRA_CONFIG_OPEN_WRITE, &config,
                                 &error) : ASTRA_CONFIG_IO;
    if (config_status == ASTRA_CONFIG_OK) {
        config_ready = 1u;
        char wanted[ASTRA_FONT_FACE_ID_MAX] = {0};
        uint32_t length = 0u;
        /* Damaged inventory stays untouched; built-ins still boot. */
        status = load_inventory();
        if (status != ASTRA_VFS_OK) {
            (void)astra_log_failure("fonts installed inventory", status);
            installed_count = system_count;
        }
        uint32_t choice_status = astra_config_get_string(
            &config, "system_font", 0u, wanted, sizeof(wanted), &length);
        if (choice_status == ASTRA_CONFIG_OK) {
            FontEntry *face = find_face(wanted);
            if (face != NULL && fonts_face_selectable(&face->face))
                memcpy(selected, wanted, sizeof(selected));
            else (void)astra_log("fonts system_font unavailable");
        } else if (choice_status != ASTRA_CONFIG_NOT_FOUND)
            (void)astra_log_failure("fonts system_font", choice_status);
    } else
        (void)astra_log_failure("fonts system settings", config_status);
    if (selected[0] == '\0') {
        FontEntry *fallback = find_face("system:astra-workbench");
        if (fallback != NULL && fonts_face_selectable(&fallback->face))
            memcpy(selected, fallback->id, sizeof(selected));
    }
    for (uint32_t at = 0u; at < 3u; ++at)
        if (astra_rt_port_create(16u, 16u * sizeof(AstraFontServiceRequest),
                                 &receive[at], &published[at]) !=
            ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 published, 3u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_PEER_DEAD;
    for (;;) {
        uint32_t index = 0u;
        status = astra_wait_multiple(receive, 3u, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status != ASTRA_SYSCALL_OK || index > 2u)
            return ASTRA_STATUS_PEER_DEAD;
        serve(receive[index], index);
    }
}
