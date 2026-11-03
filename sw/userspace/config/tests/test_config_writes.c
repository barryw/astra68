#define _POSIX_C_SOURCE 200809L
#include "../src/config_library.c"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char directory[] = "/tmp/astra-config-writes-XXXXXX";
static pthread_mutex_t areas_lock = PTHREAD_MUTEX_INITIALIZER;
static void *areas[4096];
static uint32_t next_area = 1u;
static uint32_t contention;

static void host_path(const char *path, char out[256])
{
    int used;
    assert(strncmp(path, "/app/", 5u) == 0);
    used = snprintf(out, 256u, "%s/%s", directory, path + 5u);
    assert(used > 0 && used < 256);
}

uint32_t astra_rt_area_create(uint32_t bytes, uint32_t rights, uint32_t *handle)
{
    void *data;
    (void)rights;
    data = calloc(bytes == 0u ? 1u : bytes, 1u);
    if (data == NULL) return ASTRA_SYSCALL_RESOURCE_LIMIT;
    pthread_mutex_lock(&areas_lock);
    assert(next_area < 4096u);
    *handle = next_area++;
    areas[*handle] = data;
    pthread_mutex_unlock(&areas_lock);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_map(uint32_t handle, uint32_t permissions,
                           void **address, uint32_t *bytes)
{
    (void)permissions;
    assert(handle > 0u && handle < next_area && areas[handle] != NULL);
    *address = areas[handle];
    *bytes = UINT32_MAX;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_rt_area_unmap(void *address)
{
    (void)address;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    pthread_mutex_lock(&areas_lock);
    assert(handle > 0u && handle < next_area && areas[handle] != NULL);
    free(areas[handle]);
    areas[handle] = NULL;
    pthread_mutex_unlock(&areas_lock);
    return ASTRA_SYSCALL_OK;
}

static uint32_t open_file(const char *path, uint32_t flags,
                          uint16_t mode, AstraVfsFile *file,
                          uint64_t *size, uint16_t *kind)
{
    char native[256];
    struct stat info;
    int native_flags = (flags & ASTRA_VFS_OPEN_WRITE) != 0u ? O_RDWR : O_RDONLY;
    int fd;

    host_path(path, native);
    if ((flags & ASTRA_VFS_OPEN_CREATE) != 0u) native_flags |= O_CREAT;
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0u) native_flags |= O_TRUNC;
    if ((flags & ASTRA_VFS_OPEN_NOFOLLOW) != 0u) native_flags |= O_NOFOLLOW;
    fd = open(native, native_flags, mode);
    if (fd < 0) return errno == ENOENT ? ASTRA_VFS_ERR_NOT_FOUND : ASTRA_VFS_ERR_IO;
    assert(fstat(fd, &info) == 0);
    *file = (AstraVfsFile)(fd + 1);
    *size = (uint64_t)info.st_size;
    *kind = S_ISREG(info.st_mode) ? ASTRA_VFS_KIND_FILE : ASTRA_VFS_KIND_DIRECTORY;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_open(AstraVfsClient *client, const char *path,
                        uint32_t flags, AstraVfsFile *file,
                        uint64_t *size, uint16_t *kind)
{
    (void)client;
    return open_file(path, flags, 0600u, file, size, kind);
}

uint32_t astra_vfs_open_mode(AstraVfsClient *client, const char *path,
    uint32_t flags, uint16_t mode, AstraVfsFile *file,
    uint64_t *size, uint16_t *kind)
{
    (void)client;
    return open_file(path, flags, mode, file, size, kind);
}

uint32_t astra_vfs_try_lock(AstraVfsClient *client, AstraVfsFile file)
{
    (void)client;
    if (flock((int)file - 1, LOCK_EX | LOCK_NB) == 0) return ASTRA_VFS_OK;
    if (errno == EWOULDBLOCK) {
        __atomic_add_fetch(&contention, 1u, __ATOMIC_RELAXED);
        return ASTRA_VFS_ERR_BUSY;
    }
    return ASTRA_VFS_ERR_IO;
}

uint32_t astra_vfs_close(AstraVfsClient *client, AstraVfsFile file)
{
    (void)client;
    return close((int)file - 1) == 0 ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

uint32_t astra_vfs_read(AstraVfsClient *client, AstraVfsFile file,
    uint64_t offset, void *buffer, uint32_t length, uint32_t *moved)
{
    ssize_t result;
    (void)client;
    result = pread((int)file - 1, buffer, length, (off_t)offset);
    if (result < 0) return ASTRA_VFS_ERR_IO;
    *moved = (uint32_t)result;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_write(AstraVfsClient *client, AstraVfsFile file,
    uint64_t offset, uint64_t max_end, const void *buffer,
    uint32_t length, uint32_t *moved)
{
    ssize_t result;
    (void)client;
    (void)max_end;
    result = pwrite((int)file - 1, buffer, length, (off_t)offset);
    if (result < 0) return ASTRA_VFS_ERR_IO;
    *moved = (uint32_t)result;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_sync(AstraVfsClient *client, AstraVfsFile file)
{
    struct timespec delay = {0, 2000000};
    (void)client;
    (void)nanosleep(&delay, NULL);
    return fsync((int)file - 1) == 0 ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

uint32_t astra_vfs_unlink(AstraVfsClient *client, const char *path, uint32_t flags)
{
    char native[256];
    (void)client;
    (void)flags;
    host_path(path, native);
    return unlink(native) == 0 ? ASTRA_VFS_OK :
           errno == ENOENT ? ASTRA_VFS_ERR_NOT_FOUND : ASTRA_VFS_ERR_IO;
}

uint32_t astra_vfs_rename(AstraVfsClient *client, const char *from, const char *to)
{
    char left[256], right[256];
    (void)client;
    host_path(from, left);
    host_path(to, right);
    return rename(left, right) == 0 ? ASTRA_VFS_OK : ASTRA_VFS_ERR_IO;
}

uint32_t astra_rt_thread_sleep(uint64_t duration, uint32_t flags,
    uint32_t mask, uint32_t *previous)
{
    struct timespec delay = {0, (long)duration};
    (void)flags;
    (void)mask;
    (void)previous;
    (void)nanosleep(&delay, NULL);
    return ASTRA_SYSCALL_TIMED_OUT;
}

uint32_t astra_vfs_port_transport(void *client, uint32_t operation,
    const AstraVfsRequest *request, const AstraVfsTransfer *transfer,
    AstraVfsReply *reply)
{
    (void)client; (void)operation; (void)request; (void)transfer; (void)reply;
    return ASTRA_VFS_ERR_UNSUPPORTED;
}

void astra_vfs_port_wipe_area(AstraVfsClient *client, uint32_t bytes)
{
    (void)client; (void)bytes;
}

typedef struct Writer {
    ConfigState state;
    AstraConfig config;
    pthread_barrier_t *start;
    const char *key;
} Writer;

static void *write_values(void *argument)
{
    Writer *writer = argument;
    int ready = pthread_barrier_wait(writer->start);
    assert(ready == 0 || ready == PTHREAD_BARRIER_SERIAL_THREAD);
    assert(astra_config_set_string(&writer->config, writer->key, 0u, "saved") ==
           ASTRA_CONFIG_OK);
    for (uint32_t at = 0u; at < 20u; ++at)
        assert(astra_config_append_u64(&writer->config, "items", at) ==
               ASTRA_CONFIG_OK);
    return NULL;
}

int main(void)
{
    const AstraConfigField fields[] = {
        {"left", 0u}, {"right", 0u}, {"items", 0u}, {"preferred", 0u}
    };
    const AstraConfigSchema schema = {1u, fields, 4u};
    pthread_barrier_t start;
    pthread_t threads[2];
    Writer writers[2] = {0};
    uint32_t count = 0u;
    uint32_t length = 0u;
    char value[16];
    char file[256];

    assert(mkdtemp(directory) != NULL);
    assert(pthread_barrier_init(&start, NULL, 2u) == 0);
    for (uint32_t at = 0u; at < 2u; ++at) {
        Writer *writer = &writers[at];
        writer->state.schema = &schema;
        writer->state.schema_version = 1u;
        writer->state.flags = ASTRA_CONFIG_OPEN_READ | ASTRA_CONFIG_OPEN_WRITE;
        strcpy(writer->state.root, "/app");
        assert(path(writer->state.path, sizeof(writer->state.path), "/app", "settings.conf"));
        assert(path(writer->state.temporary, sizeof(writer->state.temporary), "/app", ".settings.tmp"));
        assert(path(writer->state.lock_path, sizeof(writer->state.lock_path), "/app", ".settings.lock"));
        assert(initial(&writer->state) == ASTRA_CONFIG_OK);
        writer->config._private_area = 1u;
        writer->config._private_state = &writer->state;
        writer->key = at == 0u ? "left" : "right";
        writer->start = &start;
        assert(pthread_create(&threads[at], NULL, write_values, writer) == 0);
    }
    for (uint32_t at = 0u; at < 2u; ++at)
        assert(pthread_join(threads[at], NULL) == 0);
    assert(pthread_barrier_destroy(&start) == 0);
    assert(contention > 0u);
    assert(astra_config_reload(&writers[0].config, NULL) == ASTRA_CONFIG_OK);
    assert(astra_config_count(&writers[0].config, "items", &count) == ASTRA_CONFIG_OK);
    assert(count == 40u);
    for (uint32_t at = 0u; at < 2u; ++at) {
        assert(astra_config_get_string(&writers[0].config,
            writers[at].key, 0u, value, sizeof(value), &length) == ASTRA_CONFIG_OK);
        assert(strcmp(value, "saved") == 0);
    }
    assert(astra_config_reload(&writers[1].config, NULL) == ASTRA_CONFIG_OK);
    assert(astra_config_append_string_if_unchanged(&writers[0].config,
        "preferred", "z5 first") == ASTRA_CONFIG_OK);
    assert(astra_config_append_string_if_unchanged(&writers[1].config,
        "preferred", "z5 second") == ASTRA_CONFIG_CONFLICT);
    assert(astra_config_count(&writers[1].config, "preferred", &count) == ASTRA_CONFIG_OK);
    assert(count == 1u);
    assert(astra_config_set_string_if_unchanged(&writers[1].config,
        "preferred", 0u, "z5 second") == ASTRA_CONFIG_OK);
    assert(astra_config_reload(&writers[0].config, NULL) == ASTRA_CONFIG_OK);
    assert(astra_config_count(&writers[0].config, "preferred", &count) == ASTRA_CONFIG_OK);
    assert(count == 1u);
    assert(astra_config_get_string(&writers[0].config, "preferred", 0u,
        value, sizeof(value), &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "z5 second") == 0);
    const AstraConfigChange batch[] = {
        {"left", "batch-left", 0u, ASTRA_CONFIG_CHANGE_SET},
        {"right", "batch-right", 0u, ASTRA_CONFIG_CHANGE_SET},
        {"items", "99", 0u, ASTRA_CONFIG_CHANGE_APPEND}
    };
    assert(astra_config_apply_if_unchanged(&writers[0].config,
        batch, 3u) == ASTRA_CONFIG_OK);
    assert(astra_config_apply_if_unchanged(&writers[1].config,
        batch, 3u) == ASTRA_CONFIG_CONFLICT);
    assert(astra_config_reload(&writers[1].config, NULL) == ASTRA_CONFIG_OK);
    assert(astra_config_get_string(&writers[1].config, "left", 0u,
        value, sizeof(value), &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "batch-left") == 0);
    assert(astra_config_get_string(&writers[1].config, "right", 0u,
        value, sizeof(value), &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "batch-right") == 0);
    assert(astra_config_count(&writers[1].config, "items", &count) == ASTRA_CONFIG_OK);
    assert(count == 41u);
    const AstraConfigChange rejected[] = {
        {"left", "partial", 0u, ASTRA_CONFIG_CHANGE_SET},
        {"right", NULL, 99u, ASTRA_CONFIG_CHANGE_REMOVE}
    };
    assert(astra_config_apply(&writers[1].config, rejected, 2u) != ASTRA_CONFIG_OK);
    assert(astra_config_reload(&writers[1].config, NULL) == ASTRA_CONFIG_OK);
    assert(astra_config_get_string(&writers[1].config, "left", 0u,
        value, sizeof(value), &length) == ASTRA_CONFIG_OK);
    assert(strcmp(value, "batch-left") == 0);
    for (uint32_t at = 0u; at < 2u; ++at)
        release_text(&writers[at].state);
    host_path("/app/settings.conf", file);
    assert(unlink(file) == 0);
    host_path("/app/.settings.lock", file);
    assert(unlink(file) == 0);
    assert(rmdir(directory) == 0);
    puts("Config Kit concurrent writers PASS");
    return 0;
}
