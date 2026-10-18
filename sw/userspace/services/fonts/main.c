#include <astra/afnt.h>
#include <astra/config_library.h>
#include <astra/filesystem_library.h>
#include <astra/font_catalog.h>
#include <astra/font_service.h>
#include <astra/font_encoding.h>
#include <astra/font_store.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/theme.h>
#include <astra/vfs_process.h>
#include <astra/vfs_service.h>

#include "policy.h"
#include "host.h"

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
typedef struct FontSelection {
    char id[ASTRA_FONT_FACE_ID_MAX];
    uint32_t area;
    uint32_t bytes;
} FontSelection;
static FontSelection selection;
static uint32_t font_changed_signal;
static char inventory_text[FONT_INVENTORY_MAX];
static char inventory_tokens[FONT_FACE_MAX][ASTRA_FONT_OBJECT_TOKEN_SIZE];
static uint32_t inventory_count;
static uint32_t inventory_ready;
static uint32_t config_ready;
static uint32_t font_device;

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
        int id_length = snprintf(id, sizeof(id), "user:%.*s",
                                  (int)ASTRA_FONT_OBJECT_TOKEN_LENGTH,
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

static uint32_t sync_object_directory(void)
{
    AstraFile directory = ASTRA_FILE_INIT;
    uint32_t status = astra_filesystem_open(
        &files.filesystem, "/CONFIG_SYSTEM/installed",
        ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY, &directory);
    if (status != ASTRA_VFS_OK) return status;
    status = astra_filesystem_sync(&directory);
    uint32_t closed = astra_filesystem_close(&directory);
    return status == ASTRA_VFS_OK ? closed : status;
}

static uint32_t remove_object(const char *token)
{
    char path[ASTRA_VFS_PATH_MAX];
    if (snprintf(path, sizeof(path),
                 "/CONFIG_SYSTEM/installed/%s.afnt", token) >=
            (int)sizeof(path))
        return ASTRA_VFS_ERR_LIMIT;
    uint32_t status = astra_filesystem_unlink(&files.filesystem, path, 0u);
    if (status != ASTRA_VFS_OK) return status;
    return sync_object_directory();
}

static uint32_t store_object(const char *token, const void *bytes,
                             uint32_t length, uint32_t *created)
{
    char path[ASTRA_VFS_PATH_MAX], temporary[ASTRA_VFS_PATH_MAX];
    AstraFile file = ASTRA_FILE_INIT;
    void *existing = NULL;
    uint32_t existing_length = 0u, used = 0u;
    uint32_t status;
    uint32_t opened;

    *created = 0u;
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
    status = sync_object_directory();
    if (status != ASTRA_VFS_OK) {
        (void)remove_object(token);
        return status;
    }
    *created = 1u;
    return status;
}

typedef struct FontImportStage {
    char manifest[FONT_INVENTORY_MAX];
    char tokens[FONT_FACE_MAX][ASTRA_FONT_OBJECT_TOKEN_SIZE];
    uint8_t created[FONT_FACE_MAX];
    uint32_t count;
} FontImportStage;

static uint32_t rollback_import(const FontImportStage *stage)
{
    uint32_t status = ASTRA_VFS_OK;
    for (uint32_t at = 0u; at < stage->count; ++at) {
        if (!stage->created[at]) continue;
        uint32_t removed = remove_object(stage->tokens[at]);
        if (removed != ASTRA_VFS_OK) status = removed;
    }
    return status;
}

static uint32_t stage_afnt(FontImportStage *stage, const void *bytes,
                           uint32_t length,
                           char imported[ASTRA_FONT_FACE_ID_MAX])
{
    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];
    FontEntry *entry;
    uint32_t status;
    uint32_t created;
    size_t used;

    if (length > FONT_FILE_MAX) return ASTRA_STATUS_LIMIT;
    if (length < 4u || memcmp(bytes, "AFNT", 4u) != 0)
        return ASTRA_STATUS_INVALID;
    astra_font_object_token(bytes, length, token);
    if (snprintf(imported, ASTRA_FONT_FACE_ID_MAX, "user:%s", token) >=
            (int)ASTRA_FONT_FACE_ID_MAX)
        return ASTRA_STATUS_LIMIT;
    if (find_face(imported) != NULL) return ASTRA_STATUS_OK;
    for (uint32_t at = 0u; at < stage->count; ++at)
        if (strcmp(stage->tokens[at], token) == 0)
            return ASTRA_STATUS_OK;
    if (installed_count + stage->count >= FONT_FACE_MAX ||
        inventory_count + stage->count >= FONT_FACE_MAX)
        return ASTRA_STATUS_LIMIT;
    entry = &installed[installed_count + stage->count];
    strcpy(entry->id, imported);
    if (!astra_font_catalog_face_from_afnt(
            bytes, length, entry->id,
            (uint32_t)strlen(entry->id), 0u,
            entry->family, sizeof(entry->family), entry->style,
            sizeof(entry->style), entry->strikes, FONT_STRIKE_MAX,
            &entry->face))
        return ASTRA_STATUS_INVALID;
    used = strlen(stage->manifest);
    if (used + (used != 0u ? 1u : 0u) +
        ASTRA_FONT_OBJECT_TOKEN_LENGTH + 1u > sizeof(stage->manifest))
        return ASTRA_STATUS_LIMIT;
    status = store_object(token, bytes, length, &created);
    if (status != ASTRA_VFS_OK) return status;
    if (used != 0u) stage->manifest[used++] = ' ';
    memcpy(stage->manifest + used, token, ASTRA_FONT_OBJECT_TOKEN_SIZE);
    memcpy(stage->tokens[stage->count++], token,
           ASTRA_FONT_OBJECT_TOKEN_SIZE);
    stage->created[stage->count - 1u] = (uint8_t)created;
    return ASTRA_STATUS_OK;
}

static uint32_t persist_inventory(const char *manifest)
{
    if (astra_config_set_string(&config, "installed", 0u,
                                manifest) != ASTRA_CONFIG_OK) {
        /* Do not let a later writer overwrite a possibly published inventory. */
        inventory_ready = 0u;
        return ASTRA_STATUS_IO;
    }
    return ASTRA_STATUS_OK;
}

static uint32_t commit_import(FontImportStage *stage)
{
    if (stage->count == 0u) return ASTRA_STATUS_OK;
    uint32_t status = persist_inventory(stage->manifest);
    if (status != ASTRA_STATUS_OK) return status;
    strcpy(inventory_text, stage->manifest);
    for (uint32_t at = 0u; at < stage->count; ++at)
        memcpy(inventory_tokens[inventory_count + at], stage->tokens[at],
               ASTRA_FONT_OBJECT_TOKEN_SIZE);
    inventory_count += stage->count;
    installed_count += stage->count;
    if (++generation == 0u) generation = 1u;
    return ASTRA_STATUS_OK;
}

static uint32_t uninstall_face(const char *id)
{
    char manifest[FONT_INVENTORY_MAX] = {0};
    uint32_t face_at = installed_count;
    uint32_t token_at = inventory_count;
    uint32_t used = 0u;
    uint32_t status;

    if (strncmp(id, "system:", 7u) == 0) return ASTRA_STATUS_ACCESS;
    if (strncmp(id, "user:", 5u) != 0 ||
        strlen(id) != 5u + ASTRA_FONT_OBJECT_TOKEN_LENGTH ||
        !astra_font_object_token_valid(id + 5u,
                                       ASTRA_FONT_OBJECT_TOKEN_LENGTH))
        return ASTRA_STATUS_INVALID;
    if (!inventory_ready || !config_ready) return ASTRA_STATUS_IO;
    if (strcmp(selection.id, id) == 0) return ASTRA_STATUS_BUSY;
    for (uint32_t at = 0u; at < installed_count; ++at)
        if (strcmp(installed[at].id, id) == 0) { face_at = at; break; }
    for (uint32_t at = 0u; at < inventory_count; ++at)
        if (strcmp(inventory_tokens[at], id + 5u) == 0) {
            token_at = at;
            break;
        }
    if (face_at == installed_count || token_at == inventory_count)
        return ASTRA_STATUS_NOT_FOUND;
    for (uint32_t at = 0u; at < inventory_count; ++at) {
        if (at == token_at) continue;
        if (used + (used != 0u) + ASTRA_FONT_OBJECT_TOKEN_LENGTH >=
            sizeof(manifest)) return ASTRA_STATUS_LIMIT;
        if (used != 0u) manifest[used++] = ' ';
        memcpy(manifest + used, inventory_tokens[at],
               ASTRA_FONT_OBJECT_TOKEN_LENGTH);
        used += ASTRA_FONT_OBJECT_TOKEN_LENGTH;
        manifest[used] = '\0';
    }
    status = persist_inventory(manifest);
    if (status != ASTRA_STATUS_OK) return status;
    memcpy(inventory_text, manifest, used + 1u);
    for (uint32_t at = token_at; at + 1u < inventory_count; ++at)
        memcpy(inventory_tokens[at], inventory_tokens[at + 1u],
               ASTRA_FONT_OBJECT_TOKEN_SIZE);
    --inventory_count;
    for (uint32_t at = face_at; at + 1u < installed_count; ++at) {
        installed[at] = installed[at + 1u];
        installed[at].face.id = installed[at].id;
        installed[at].face.family = installed[at].family;
        installed[at].face.style = installed[at].style;
        installed[at].face.strikes = installed[at].strikes;
    }
    --installed_count;
    if (++generation == 0u) generation = 1u;
    status = remove_object(id + 5u);
    if (status != ASTRA_VFS_OK)
        (void)astra_log_failure("fonts uninstall cleanup", status);
    return ASTRA_STATUS_OK;
}

static void send_reply(uint32_t port, uint32_t transaction, uint32_t status,
                        uint32_t area, uint32_t area_size, const char *imported)
{
    AstraFontServiceReply reply = {0};
    astra_message_header_set(&reply.header, sizeof(reply),
        ASTRA_FONT_SERVICE_PROTOCOL, ASTRA_FONT_SERVICE_VERSION,
        ASTRA_FONT_SERVICE_REPLY, transaction);
    reply.status = status;
    reply.generation = generation;
    reply.area_size = status == ASTRA_STATUS_OK ? area_size : 0u;
    memcpy(reply.selected_id, selection.id, sizeof(selection.id));
    if (status == ASTRA_STATUS_OK && imported != NULL)
        memcpy(reply.imported_id, imported, sizeof(reply.imported_id));
    if (port == 0u || astra_port_send(port, &reply, sizeof(reply),
        area == 0u ? NULL : &area, area == 0u ? 0u : 1u) != ASTRA_SYSCALL_OK) {
        if (area != 0u) (void)astra_close(area);
    }
}

typedef struct FontImportJob {
    void *source;
    uint32_t source_size;
    char name[ASTRA_FONT_SOURCE_NAME_MAX];
    uint32_t name_size;
    uint32_t encoding;
    uint32_t ready;
    uint32_t resume;
    uint32_t cancel;
    uint32_t status;
    uint32_t faces;
    uint32_t afnt_size;
    void *afnt;
    uint32_t worker;
    uint32_t reply;
    uint32_t transaction;
    uint32_t result;
    uint32_t completed;
    char imported[ASTRA_FONT_FACE_ID_MAX];
    FontImportStage stage;
} FontImportJob;

/* One inventory writer; readers and system-font selection remain available. */
static FontImportJob importing;

static void convert_outline(uint32_t argument)
{
    FontImportJob *job = (FontImportJob *)(uintptr_t)argument;
    FontsHost host = {0};
    uint32_t faces = 0u;

    job->status = fonts_host_begin(&host, font_device, job->source,
        job->source_size, job->name, job->name_size);
    if (job->status != ASTRA_STATUS_OK) {
        (void)astra_rt_signal(job->ready, 1u, NULL);
        return;
    }
    for (uint32_t face = 0u; face == 0u || face < faces; ++face) {
        job->status = fonts_host_convert(&host, face, job->encoding, &job->afnt,
                                          &job->afnt_size, &job->faces);
        if (job->status == ASTRA_STATUS_OK &&
            faces != 0u && faces != job->faces)
            job->status = ASTRA_STATUS_PROTOCOL;
        if (job->status == ASTRA_STATUS_OK) faces = job->faces;
        if (astra_rt_signal(job->ready, 1u, NULL) != ASTRA_SYSCALL_OK ||
            job->status != ASTRA_STATUS_OK)
            break;
        if (astra_wait_one(job->resume, ASTRA_DEADLINE_FOREVER, NULL) !=
                ASTRA_SYSCALL_OK || job->cancel)
            break;
    }
    fonts_host_close(&host);
}

/* Called only after the worker has ended, or for an AFNT with no worker. */
static void finish_import(void)
{
    uint32_t status = importing.result;
    if (status == ASTRA_STATUS_OK) {
        /* A failed commit reply does not prove its rename did not land. */
        status = commit_import(&importing.stage);
        if (status != ASTRA_STATUS_OK)
            (void)astra_log_failure("fonts publish", status);
    } else {
        (void)astra_log_failure("fonts import", status);
        if (rollback_import(&importing.stage) != ASTRA_VFS_OK) {
            (void)astra_log_failure("fonts rollback", ASTRA_STATUS_IO);
            status = ASTRA_STATUS_IO;
        }
    }
    astra_runtime_deallocate(importing.afnt);
    astra_runtime_deallocate(importing.source);
    if (importing.worker != 0u) (void)astra_close(importing.worker);
    if (importing.resume != 0u) (void)astra_close(importing.resume);
    if (importing.ready != 0u) (void)astra_close(importing.ready);
    send_reply(importing.reply, importing.transaction, status, 0u, 0u,
                 importing.imported);
    (void)astra_close(importing.reply);
    importing = (FontImportJob){0};
}

static void import_face_ready(void)
{
    if (importing.result == ASTRA_STATUS_OK)
        importing.result = importing.status;
    if (importing.result == ASTRA_STATUS_OK) {
        importing.result = stage_afnt(&importing.stage, importing.afnt,
                                       importing.afnt_size, importing.imported);
        if (importing.result == ASTRA_STATUS_OK) ++importing.completed;
    }
    astra_runtime_deallocate(importing.afnt);
    importing.afnt = NULL;
    importing.cancel = importing.result != ASTRA_STATUS_OK;
    if (astra_rt_signal(importing.resume, 1u, NULL) != ASTRA_SYSCALL_OK) {
        /* A private live semaphore must be signalable; never strand its worker. */
        (void)astra_log_failure("fonts converter resume", ASTRA_STATUS_IO);
        astra_process_exit(ASTRA_STATUS_IO);
    }
}

static void import_worker_ended(void)
{
    if (importing.result == ASTRA_STATUS_OK &&
        (importing.status != ASTRA_STATUS_OK || importing.faces == 0u ||
         importing.completed != importing.faces))
        importing.result = importing.status != ASTRA_STATUS_OK ?
                           importing.status : ASTRA_STATUS_IO;
    finish_import();
}

/* Success takes both received capabilities and sends the final reply later. */
static uint32_t begin_import(uint32_t area, uint32_t reply,
                              const AstraFontServiceRequest *request)
{
    void *mapping = NULL;
    uint32_t mapped_size = 0u;
    uint32_t status;

    if (importing.reply != 0u) return ASTRA_STATUS_BUSY;
    if (!inventory_ready || !config_ready) {
        (void)astra_log_debug("fonts inventory unavailable", 27u);
        return ASTRA_STATUS_IO;
    }
    importing = (FontImportJob){0};
    strcpy(importing.stage.manifest, inventory_text);
    status = astra_rt_area_map(area, ASTRA_AREA_MAP_READ,
                               &mapping, &mapped_size);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_BAD_HANDLE;
    if (mapped_size < request->source_size) status = ASTRA_STATUS_PROTOCOL;
    else {
        /* A read-only capability does not revoke the sender's write access. */
        importing.source = astra_runtime_allocate(request->source_size);
        status = importing.source == NULL ? ASTRA_STATUS_LIMIT : ASTRA_STATUS_OK;
        if (status == ASTRA_STATUS_OK)
            memcpy(importing.source, mapping, request->source_size);
    }
    if (astra_rt_area_unmap(mapping) != ASTRA_SYSCALL_OK &&
        status == ASTRA_STATUS_OK) status = ASTRA_STATUS_IO;
    if (status != ASTRA_STATUS_OK) goto failed;
    importing.source_size = request->source_size;
    importing.encoding = request->source_encoding;
    memcpy(importing.name, request->source_name, sizeof(importing.name));
    importing.name_size = (uint32_t)strlen(importing.name);
    if (request->source_size >= 4u && memcmp(importing.source, "AFNT", 4u) == 0) {
        importing.reply = reply;
        importing.transaction = request->header.transaction_id;
        (void)astra_close(area);
        importing.result = request->source_encoding == ASTRA_FONT_ENCODING_AUTO ?
            stage_afnt(&importing.stage, importing.source, request->source_size,
                         importing.imported) : ASTRA_STATUS_INVALID;
        finish_import();
        return ASTRA_STATUS_OK;
    }
    if (font_device == 0u) { status = ASTRA_STATUS_UNSUPPORTED; goto failed; }
    uint32_t amiga = request->source_size >= 4u &&
        memcmp(importing.source, "\0\0\3\363", 4u) == 0;
    if ((amiga && request->source_encoding == ASTRA_FONT_ENCODING_AUTO) ||
        (!amiga && request->source_encoding != ASTRA_FONT_ENCODING_AUTO)) {
        status = ASTRA_STATUS_INVALID;
        goto failed;
    }
    status = ASTRA_STATUS_LIMIT;
    if (astra_rt_semaphore_create(0u, 1u,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &importing.ready) != ASTRA_SYSCALL_OK ||
        astra_rt_semaphore_create(0u, 1u,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL, &importing.resume) != ASTRA_SYSCALL_OK)
        goto failed;
    AstraThreadStart start = {convert_outline, (uint32_t)(uintptr_t)&importing};
    if (astra_rt_thread_create(&start, ASTRA_PROCESS_PRIORITY_NORMAL,
        ASTRA_RIGHT_WAIT | ASTRA_RIGHT_READ, &importing.worker, NULL) != ASTRA_SYSCALL_OK)
        goto failed;
    importing.reply = reply;
    importing.transaction = request->header.transaction_id;
    (void)astra_close(area);
    return ASTRA_STATUS_OK;
failed:
    (void)astra_log_failure("fonts import setup", status);
    astra_runtime_deallocate(importing.source);
    if (importing.resume != 0u) (void)astra_close(importing.resume);
    if (importing.ready != 0u) (void)astra_close(importing.ready);
    importing = (FontImportJob){0};
    return status;
}

static uint32_t catalog_area(uint32_t *area, uint32_t *size)
{
    AstraFontCatalogFace faces[FONT_FACE_MAX];
    void *mapping = NULL;
    uint32_t writable = 0u;
    uint32_t mapped_size = 0u;
    uint32_t status;

    for (uint32_t at = 0u; at < installed_count; ++at) {
        faces[at] = installed[at].face;
        if (fonts_face_selectable(&installed[at].face))
            faces[at].flags |= ASTRA_FONT_CATALOG_SELECTABLE;
    }
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

static uint32_t load_face_area(const char *id, uint32_t information,
                               uint32_t *area, uint32_t *size)
{
    char path[ASTRA_VFS_PATH_MAX];
    char token[ASTRA_FONT_OBJECT_TOKEN_SIZE];
    AstraAfntView view;
    void *bytes = NULL, *mapping = NULL;
    uint32_t length = 0u, mapped_size = 0u, writable = 0u, payload_length;
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
    payload_length = information ? view.name.length : length;
    status = astra_rt_area_create(
        payload_length, ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE | ASTRA_RIGHT_MAP |
                    ASTRA_RIGHT_TRANSFER, &writable);
    if (status != ASTRA_SYSCALL_OK) {
        status = ASTRA_STATUS_LIMIT;
        goto done;
    }
    status = astra_rt_area_map(writable,
                               ASTRA_AREA_MAP_READ | ASTRA_AREA_MAP_WRITE,
                               &mapping, &mapped_size);
    if (status != ASTRA_SYSCALL_OK || mapped_size < payload_length) {
        status = ASTRA_STATUS_IO;
        goto done;
    }
    memcpy(mapping, information ? view.name.bytes : bytes, payload_length);
    status = astra_rt_area_unmap(mapping);
    mapping = NULL;
    if (status != ASTRA_SYSCALL_OK) {
        status = ASTRA_STATUS_IO;
        goto done;
    }
    status = astra_rt_handle_duplicate(
        writable, ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER,
        area);
    if (status == ASTRA_SYSCALL_OK)
        *size = payload_length;
    else status = ASTRA_STATUS_IO;
done:
    if (mapping != NULL) (void)astra_rt_area_unmap(mapping);
    if (writable != 0u) (void)astra_close(writable);
    astra_runtime_deallocate(bytes);
    return status;
}

static uint32_t face_area(const char *id, uint32_t *area, uint32_t *size)
{
    if (selection.area == 0u || strcmp(selection.id, id) != 0)
        return load_face_area(id, 0u, area, size);
    uint32_t status = astra_rt_handle_duplicate(selection.area,
        ASTRA_RIGHT_READ | ASTRA_RIGHT_MAP | ASTRA_RIGHT_TRANSFER, area);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_IO;
    *size = selection.bytes;
    return ASTRA_STATUS_OK;
}

static uint32_t prepare_selection(const char *id, FontSelection *next)
{
    FontEntry *face = find_face(id);
    if (face == NULL) return ASTRA_STATUS_NOT_FOUND;
    if (!fonts_face_selectable(&face->face)) return ASTRA_STATUS_UNSUPPORTED;
    uint32_t status = load_face_area(id, 0u, &next->area, &next->bytes);
    if (status == ASTRA_STATUS_OK) memcpy(next->id, face->id, sizeof(next->id));
    return status;
}

static void adopt_selection(FontSelection *next)
{
    uint32_t previous = selection.area;
    selection = *next;
    *next = (FontSelection){0};
    if (previous != 0u && astra_close(previous) != ASTRA_SYSCALL_OK)
        (void)astra_log_failure("fonts previous selection close", ASTRA_STATUS_IO);
    if (++generation == 0u) generation = 1u;
}

static uint32_t select_face(const char *id)
{
    FontSelection next = {0};
    uint32_t status;
    if (!config_ready || (!inventory_ready && strncmp(id, "user:", 5u) == 0))
        return ASTRA_STATUS_IO;
    if (strcmp(selection.id, id) != 0) {
        status = prepare_selection(id, &next);
        if (status != ASTRA_STATUS_OK) return status;
    }
    if (astra_config_set_string(&config, "system_font", 0u,
            next.area != 0u ? next.id : selection.id) != ASTRA_CONFIG_OK) {
        if (next.area != 0u) (void)astra_close(next.area);
        return ASTRA_STATUS_IO;
    }
    if (next.area != 0u) adopt_selection(&next);
    /* Re-selecting the current face also retries notification. */
    return astra_rt_signal(font_changed_signal, 1u, NULL) == ASTRA_SYSCALL_OK ?
        ASTRA_STATUS_OK : ASTRA_STATUS_IO;
}

static void serve(uint32_t port, uint32_t role)
{
    AstraFontServiceRequest request = {0};
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
    if (status != ASTRA_STATUS_OK)
        (void)astra_log_debug("fonts request invalid", 21u);
    if (status == ASTRA_STATUS_OK &&
        request.header.operation == ASTRA_FONT_SERVICE_CATALOG) {
        if (request.face_id[0] != '\0') status = ASTRA_STATUS_PROTOCOL;
        else status = catalog_area(&area, &area_size);
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_INFORMATION) {
        status = load_face_area(request.face_id, 1u, &area, &area_size);
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_IMPORT) {
        /* Port-call appends the reply capability after the source area. */
        status = begin_import(handles[0], handles[1], &request);
        if (status == ASTRA_STATUS_OK) return;
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_OPEN_FACE) {
        status = face_area(request.face_id, &area, &area_size);
    } else if (status == ASTRA_STATUS_OK &&
               request.header.operation == ASTRA_FONT_SERVICE_UNINSTALL) {
        status = importing.reply != 0u ? ASTRA_STATUS_BUSY :
                 uninstall_face(request.face_id);
    } else if (status == ASTRA_STATUS_OK) {
        status = select_face(request.face_id);
    }
    send_reply(count != 0u ? handles[count - 1u] : 0u,
                 request.header.transaction_id, status, area, area_size, NULL);
    for (uint32_t at = 0u; at < count; ++at)
        (void)astra_close(handles[at]);
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *bootstrap;
    const AstraStartupCapability *device;
    AstraConfigError error = {0};
    uint32_t receive[5] = {0};
    uint32_t published[4] = {0};
    uint32_t status;

    if (!astra_startup_validate(startup)) return ASTRA_STATUS_INVALID;
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    device = astra_startup_capability(startup, ASTRA_CAPABILITY_HOST_DEVICE);
    font_device = device == NULL ? 0u : device->handle;
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
            if (face != NULL && fonts_face_selectable(&face->face)) {
                FontSelection next = {0};
                if (prepare_selection(wanted, &next) == ASTRA_STATUS_OK)
                    adopt_selection(&next);
                else (void)astra_log("fonts system_font unreadable");
            }
            else (void)astra_log("fonts system_font unavailable");
        } else if (choice_status != ASTRA_CONFIG_NOT_FOUND)
            (void)astra_log_failure("fonts system_font", choice_status);
    } else
        (void)astra_log_failure("fonts system settings", config_status);
    if (selection.area == 0u) {
        FontSelection next = {0};
        status = prepare_selection("system:astra-workbench", &next);
        if (status != ASTRA_STATUS_OK) return status;
        adopt_selection(&next);
    }
    for (uint32_t at = 0u; at < 3u; ++at)
        if (astra_rt_port_create(16u, 16u * sizeof(AstraFontServiceRequest),
                                 &receive[at], &published[at]) !=
            ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    const uint32_t notification_rights =
        ASTRA_RIGHT_SIGNAL | ASTRA_RIGHT_WAIT | ASTRA_RIGHT_TRANSFER;
    status = astra_rt_event_create(0u, notification_rights, &font_changed_signal);
    if (status == ASTRA_SYSCALL_OK)
        status = astra_rt_handle_duplicate(font_changed_signal,
            notification_rights, &published[3]);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_LIMIT;
    status = astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK,
                                 published, 4u);
    (void)astra_close(bootstrap->handle);
    if (status != ASTRA_SYSCALL_OK) return ASTRA_STATUS_PEER_DEAD;
    uint32_t first_wait = 0u;
    for (;;) {
        uint32_t index = 0u, waits[5];
        uint32_t count = importing.worker != 0u ? 5u : 3u;
        receive[3] = importing.ready;
        receive[4] = importing.worker;
        for (uint32_t at = 0u; at < count; ++at)
            waits[at] = receive[(first_wait + at) % count];
        status = astra_wait_multiple(waits, count, ASTRA_DEADLINE_FOREVER,
                                     &index, NULL);
        if (status != ASTRA_SYSCALL_OK || index >= count)
            return ASTRA_STATUS_PEER_DEAD;
        index = (first_wait + index) % count;
        first_wait = (index + 1u) % count;
        if (index == 3u) import_face_ready();
        else if (index == 4u) import_worker_ended();
        else serve(receive[index], index);
    }
}
