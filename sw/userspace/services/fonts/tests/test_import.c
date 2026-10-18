#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../main.c"

static uint8_t *fixture;
static uint32_t fixture_size, allocations, mapped, released_source, replies;
static uint32_t semaphores, gates, worker_alive, worker_closed, reply_closed;
static uint32_t sessions, notifications, notifications_closed;
static uint32_t fail_port;
static uint32_t peer_dead;
static uint32_t notification_status;
static uint32_t fail_allocate, fail_semaphore, fail_thread, fail_commit;
static uint32_t objects, removed, published, output_size, output_cursor;
static uint8_t *output;
static char object_path[ASTRA_VFS_PATH_MAX];
static AstraFontServiceReply last_reply;
static uint8_t input[] = {0, 1, 0, 0};

void *astra_runtime_allocate(size_t size)
{
    if (fail_allocate) return NULL;
    void *result = malloc(size);
    if (result != NULL) ++allocations;
    return result;
}
void astra_runtime_deallocate(void *memory)
{
    if (memory != NULL) { assert(allocations != 0u); --allocations; }
    free(memory);
}
uint32_t astra_rt_area_map(uint32_t handle, uint32_t flags, void **data, uint32_t *size)
{
    assert(handle == 10u && flags == ASTRA_AREA_MAP_READ && mapped == 0u);
    *data = input; *size = sizeof(input); mapped = 1u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_area_unmap(void *data)
{
    assert(data == input && mapped == 1u); mapped = 0u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_semaphore_create(uint32_t initial, uint32_t maximum,
    uint32_t rights, uint32_t *handle)
{
    assert(initial == 0u && maximum == 1u &&
           rights == (ASTRA_RIGHT_WAIT | ASTRA_RIGHT_SIGNAL));
    if (++semaphores == fail_semaphore) return ASTRA_SYSCALL_OUT_OF_MEMORY;
    *handle = 20u + semaphores;
    ++gates;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_thread_create(const AstraThreadStart *start, uint32_t priority,
    uint32_t rights, uint32_t *handle, uint32_t *id)
{
    assert(start->entry == convert_outline &&
        start->argument == (uint32_t)(uintptr_t)&importing &&
        priority == ASTRA_PROCESS_PRIORITY_NORMAL &&
        rights == (ASTRA_RIGHT_WAIT | ASTRA_RIGHT_READ) && id == NULL);
    if (fail_thread) return ASTRA_SYSCALL_OUT_OF_MEMORY;
    *handle = 30u; worker_alive = 1u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_signal(uint32_t handle, uint32_t count, uint32_t *woken)
{
    assert(count == 1u && woken == NULL);
    assert(handle == importing.resume);
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_rt_port_create(uint32_t count, uint32_t bytes,
    uint32_t *receive, uint32_t *send)
{
    assert(count == 4u && bytes == 4u * sizeof(AstraFontServiceRequest));
    if (fail_port) return ASTRA_SYSCALL_OUT_OF_MEMORY;
    *receive = 52u; *send = 50u; sessions += 2u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_close(uint32_t handle)
{
    if (handle == 10u) ++released_source;
    else if (handle == 30u) { assert(!worker_alive); ++worker_closed; }
    else if (handle == 40u) ++reply_closed;
    else if (handle == 53u) ++notifications_closed;
    else if (handle == 50u || handle == 52u) { assert(sessions); --sessions; }
    else { assert((handle == 21u || handle == 22u) && gates != 0u); --gates; }
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_send(uint32_t handle, const void *data, uint32_t bytes,
    const uint32_t *handles, uint32_t count)
{
    if (handle == 53u) {
        const AstraMessageHeader *hint = data;
        assert(bytes == sizeof(*hint) && hint->operation == ASTRA_FONT_SERVICE_IMPORT_CHANGED &&
            handles == NULL && count == 0u);
        ++notifications;
        return notification_status;
    }
    assert(handle == 40u && bytes == sizeof(last_reply) && mapped == 0u);
    assert(count == 1u || count == 0u);
    if (count == 1u) {
        assert(handles[0] == 50u && sessions == 2u);
        --sessions;
    } else assert(handles == NULL);
    memcpy(&last_reply, data, bytes); ++replies;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_port_receive(uint32_t port, void *data, uint32_t capacity,
    uint32_t *handles, uint32_t maximum, uint32_t *bytes, uint32_t *count)
{
    assert(port == 52u && capacity == ASTRA_MESSAGE_SIZE_MAX && maximum == ASTRA_MESSAGE_HANDLES_MAX);
    if (peer_dead) return ASTRA_SYSCALL_PEER_DEAD;
    AstraFontServiceRequest request = {0};
    astra_message_header_set(&request.header, sizeof(request), ASTRA_FONT_SERVICE_PROTOCOL,
        ASTRA_FONT_SERVICE_VERSION, ASTRA_FONT_SERVICE_IMPORT_CANCEL, 9u);
    memcpy(data, &request, sizeof(request)); *bytes = sizeof(request);
    handles[0] = 40u; *count = 1u;
    return ASTRA_SYSCALL_OK;
}
uint32_t astra_log_failure(const char *text, uint32_t status)
{ (void)text; (void)status; return ASTRA_SYSCALL_OK; }
uint32_t astra_log_debug(const void *text, uint32_t length)
{ (void)text; (void)length; return ASTRA_SYSCALL_OK; }
void astra_process_exit(uint32_t status)
{ (void)status; abort(); }

uint32_t astra_filesystem_open(AstraFilesystem *fs, const char *path,
    uint32_t flags, AstraFile *file)
{
    (void)fs; (void)path; (void)file;
    if (flags == (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_DIRECTORY))
        return ASTRA_VFS_OK;
    assert(flags == ASTRA_VFS_OPEN_READ);
    return ASTRA_VFS_ERR_NOT_FOUND;
}
uint32_t astra_filesystem_file_info(const AstraFile *file, AstraFileInfo *info)
{ (void)file; (void)info; abort(); }
uint32_t astra_filesystem_read(AstraFile *file, void *bytes, uint32_t size, uint32_t *moved)
{ (void)file; (void)bytes; (void)size; (void)moved; abort(); }
uint32_t astra_filesystem_open_mode(AstraFilesystem *fs, const char *path,
    uint32_t flags, uint16_t mode, AstraFile *file)
{
    (void)fs; (void)file;
    assert(mode == 0600u && flags == (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
           ASTRA_VFS_OPEN_EXCLUSIVE));
    assert(strstr(path, ".tmp") != NULL);
    output_cursor = 0u;
    return ASTRA_VFS_OK;
}
uint32_t astra_filesystem_write(AstraFile *file, uint64_t maximum,
    const void *bytes, uint32_t size, uint32_t *moved)
{
    (void)file;
    assert(maximum == fixture_size && size <= fixture_size - output_cursor);
    memcpy(output + output_cursor, bytes, size); output_cursor += size; *moved = size;
    return ASTRA_VFS_OK;
}
uint32_t astra_filesystem_sync(AstraFile *file)
{ (void)file; return ASTRA_VFS_OK; }
uint32_t astra_filesystem_close(AstraFile *file)
{ (void)file; return ASTRA_VFS_OK; }
uint32_t astra_filesystem_rename(AstraFilesystem *fs, const char *from, const char *to)
{
    (void)fs; (void)from;
    assert(output_cursor == fixture_size && memcmp(output, fixture, fixture_size) == 0);
    strcpy(object_path, to); objects = 1u; output_size = output_cursor;
    return ASTRA_VFS_OK;
}
uint32_t astra_filesystem_unlink(AstraFilesystem *fs, const char *path, uint32_t flags)
{
    (void)fs; assert(flags == 0u);
    if (strstr(path, ".tmp") != NULL) return ASTRA_VFS_ERR_NOT_FOUND;
    assert(objects && strcmp(path, object_path) == 0);
    objects = 0u; ++removed;
    return ASTRA_VFS_OK;
}
uint32_t astra_config_set_string(AstraConfig *store, const char *key,
    uint32_t index, const char *text)
{
    assert(store == &config && strcmp(key, "installed") == 0 && index == 0u &&
           text[0] != '\0' && !worker_alive);
    ++published;
    return fail_commit ? ASTRA_CONFIG_IO : ASTRA_CONFIG_OK;
}
/* Coordinator tests inject converter handoffs; no second converter implementation. */
uint32_t fonts_host_begin(FontsHost *host, uint32_t device, const void *source,
    uint32_t size, const char *name, uint32_t name_size)
{ (void)host; (void)device; (void)source; (void)size; (void)name; (void)name_size; abort(); }
uint32_t fonts_host_convert(FontsHost *host, uint32_t face, uint32_t encoding,
    void **bytes, uint32_t *size, uint32_t *faces)
{ (void)host; (void)face; (void)encoding; (void)bytes; (void)size; (void)faces; abort(); }
void fonts_host_close(FontsHost *host) { (void)host; abort(); }
uint32_t astra_wait_one(uint32_t handle, uint64_t deadline, uint32_t *observed)
{ (void)handle; (void)deadline; (void)observed; abort(); }

static AstraFontServiceRequest request(void)
{
    AstraFontServiceRequest value = {0};
    value.header.transaction_id = 7u;
    value.source_size = sizeof(input);
    strcpy(value.source_name, "source.ttf");
    return value;
}
static void reset(void)
{
    assert(importing.state == 0u && allocations == 0u && mapped == 0u && gates == 0u && sessions == 0u);
    installed_count = inventory_count = 0u;
    inventory_text[0] = '\0'; generation = 1u;
    inventory_ready = config_ready = font_device = 1u;
    released_source = replies = semaphores = worker_closed = reply_closed = 0u;
    fail_allocate = fail_semaphore = fail_thread = fail_commit = 0u;
    fail_port = notifications = notifications_closed = peer_dead = 0u;
    notification_status = ASTRA_SYSCALL_OK;
    objects = removed = published = output_size = output_cursor = 0u;
    input[0] = 0u;
}
static void face_ready(uint32_t status, uint32_t faces)
{
    importing.status = status; importing.faces = faces;
    if (status == ASTRA_STATUS_OK) {
        importing.afnt = astra_runtime_allocate(fixture_size);
        assert(importing.afnt != NULL);
        memcpy(importing.afnt, fixture, fixture_size);
        importing.afnt_size = fixture_size;
    }
    import_face_ready();
}
static void ended(void)
{
    worker_alive = 0u;
    import_worker_ended();
    assert(replies >= 1u && worker_closed == 1u &&
           allocations == 0u && importing.worker == 0u && gates == 0u && sessions == 1u);
    import_reply(40u, 7u, NULL, 0u);
    close_import();
    assert(sessions == 0u && notifications_closed == 1u);
}
int main(void)
{
    FILE *file = fopen("../../graphics/fonts/astra-workbench.afnt", "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file); assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    fixture_size = (uint32_t)size;
    fixture = malloc(fixture_size); output = malloc(fixture_size);
    assert(fixture && output && fread(fixture, 1u, fixture_size, file) == fixture_size);
    assert(fclose(file) == 0);
    AstraFontServiceRequest value = request();
    reset();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK &&
           importing.worker == 30u && released_source == 1u && replies == 1u);
    value.source_name[0] = 'x'; input[0] = 99u;
    assert(strcmp(importing.name, "source.ttf") == 0 &&
           ((uint8_t *)importing.source)[0] == 0u);
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_BUSY);
    face_ready(ASTRA_STATUS_OK, 1u);
    assert(installed_count == 0u && inventory_count == 0u && generation == 1u &&
           objects == 1u && published == 0u && replies == 1u);
    ended();
    assert(last_reply.import_state == ASTRA_FONT_IMPORT_SUCCEEDED && last_reply.header.transaction_id == 7u &&
           last_reply.imported_id[0] != '\0' && installed_count == 1u &&
           inventory_count == 1u && generation == 2u && published == 1u);

    reset(); value = request();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    face_ready(ASTRA_STATUS_OK, 2u);
    face_ready(ASTRA_STATUS_INVALID, 2u);
    assert(importing.cancel && replies == 1u && objects == 1u && !published);
    ended();
    assert(last_reply.import_state == ASTRA_FONT_IMPORT_FAILED && last_reply.import_status == ASTRA_STATUS_INVALID && !last_reply.imported_id[0] &&
           !objects && removed == 1u && !installed_count && !inventory_count && !published);

    reset();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    ended();
    assert(last_reply.import_status == ASTRA_STATUS_IO && !published);

    for (uint32_t staged = 0u; staged < 2u; ++staged) {
        reset();
        assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
        if (staged) face_ready(ASTRA_STATUS_OK, 2u);
        serve_import();
        assert(last_reply.import_state == ASTRA_FONT_IMPORT_CANCELLING &&
               worker_alive && !published && notifications != 0u && reply_closed == 1u);
        face_ready(ASTRA_STATUS_OK, 2u);
        assert(importing.cancel && importing.completed == staged);
        ended();
        assert(last_reply.import_state == ASTRA_FONT_IMPORT_CANCELLED &&
               !last_reply.import_status && !last_reply.imported_id[0] && !objects && !published);
    }

    reset();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    face_ready(ASTRA_STATUS_OK, 1u);
    serve_import();
    ended();
    assert(last_reply.import_state == ASTRA_FONT_IMPORT_CANCELLED && !published && !objects);

    reset();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    face_ready(ASTRA_STATUS_OK, 1u);
    worker_alive = 0u; import_worker_ended();
    serve_import();
    assert(last_reply.import_state == ASTRA_FONT_IMPORT_SUCCEEDED && objects && published == 1u);
    close_import();

    const uint32_t hint_errors[] = {ASTRA_SYSCALL_WOULD_BLOCK,
        ASTRA_SYSCALL_PEER_DEAD, ASTRA_SYSCALL_INVALID_HANDLE};
    for (uint32_t at = 0u; at < sizeof(hint_errors) / sizeof(hint_errors[0]); ++at) {
        reset(); notification_status = hint_errors[at];
        assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
        face_ready(ASTRA_STATUS_OK, 1u); ended();
        assert(last_reply.import_state == ASTRA_FONT_IMPORT_SUCCEEDED && published == 1u);
    }

    reset();
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    face_ready(ASTRA_STATUS_OK, 2u);
    peer_dead = 1u; serve_import();
    assert(importing.abandoned && !importing.receive && worker_alive && allocations);
    face_ready(ASTRA_STATUS_OK, 2u);
    worker_alive = 0u; import_worker_ended();
    assert(!importing.state && !objects && !published && !allocations && !sessions && !gates);

    reset(); fail_commit = 1u;
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_OK);
    face_ready(ASTRA_STATUS_OK, 1u); ended();
    assert(last_reply.import_status == ASTRA_STATUS_IO && !installed_count &&
           objects == 1u && !removed && !inventory_ready);
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_IO &&
           objects == 1u && published == 1u && !allocations);

    for (uint32_t failure = 0u; failure < 5u; ++failure) {
        reset();
        fail_allocate = failure == 0u;
        fail_semaphore = failure == 1u ? 1u : failure == 2u ? 2u : 0u;
        fail_thread = failure == 3u;
        fail_port = failure == 4u;
        assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_LIMIT &&
               !allocations && !released_source && !replies && !importing.state && !gates && !sessions);
    }
    reset(); value.source_size = sizeof(input) + 1u;
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_PROTOCOL &&
           !allocations && !mapped && !released_source);
    reset(); value = request(); font_device = 0u;
    assert(begin_import(10u, 53u, 40u, &value) == ASTRA_STATUS_UNSUPPORTED &&
           !allocations && !mapped && !released_source);
    free(output); free(fixture);
    puts("font import coordinator ownership, publication and rollback passed");
    return 0;
}
