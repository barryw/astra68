#include <volume.h>

#include <astra/runtime.h>
#include <astra/vfs_service.h>

#include <ext4.h>
#include <ext4_errno.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define SOURCE_BYTES (300u * 1024u + 7u)

static uint8_t source[SOURCE_BYTES];
static uint64_t source_size;
static int fail_allocation;
static int short_read;
static uint32_t close_count;
static uint32_t free_count;

void *
astra_runtime_allocate(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}

void
astra_runtime_deallocate(void *pointer)
{
    if (pointer != NULL)
        ++free_count;
    free(pointer);
}

int
ext4_fopen(ext4_file *file, const char *path, const char *flags)
{
    assert(strcmp(path, "/vol/startup/system") == 0);
    assert(strcmp(flags, "rb") == 0);
    file->fpos = 0u;
    return EOK;
}

uint64_t
ext4_fsize(ext4_file *file)
{
    (void)file;
    return source_size;
}

int
ext4_fread(ext4_file *file, void *buffer, size_t size, size_t *moved)
{
    size_t available;

    if (short_read) {
        *moved = 0u;
        return EOK;
    }
    available = (size_t)(source_size - file->fpos);
    if (size > available)
        size = available;
    if (size > 4093u)
        size = 4093u;
    memcpy(buffer, source + file->fpos, size);
    file->fpos += size;
    *moved = size;
    return EOK;
}

int
ext4_fclose(ext4_file *file)
{
    (void)file;
    ++close_count;
    return EOK;
}

static void
reads_large_file_whole(void)
{
    uint8_t *bytes = NULL;
    uint32_t length = 0u;

    source_size = sizeof(source);
    for (uint32_t at = 0u; at < sizeof(source); ++at)
        source[at] = (uint8_t)(at * 17u);
    assert(supervisor_volume_read_alloc("/vol/startup/system",
                                        (void **)&bytes, &length) ==
           ASTRA_VFS_OK);
    assert(length == sizeof(source));
    assert(memcmp(bytes, source, sizeof(source)) == 0);
    assert(bytes[length] == '\0');
    astra_runtime_deallocate(bytes);
}

static void
refuses_failed_or_incomplete_read(void)
{
    void *bytes = (void *)1;
    uint32_t length = 99u;
    uint32_t freed = free_count;

    source_size = sizeof(source);
    fail_allocation = 1;
    assert(supervisor_volume_read_alloc("/vol/startup/system", &bytes,
                                        &length) == ASTRA_VFS_ERR_LIMIT);
    assert(bytes == NULL && length == 0u);
    fail_allocation = 0;

    bytes = (void *)1;
    length = 99u;
    short_read = 1;
    assert(supervisor_volume_read_alloc("/vol/startup/system", &bytes,
                                        &length) == ASTRA_VFS_ERR_IO);
    assert(bytes == NULL && length == 0u);
    assert(free_count == freed + 1u);
    short_read = 0;
}

int
main(void)
{
    supervisor_volume_test_set_mounted(1);
    reads_large_file_whole();
    refuses_failed_or_incomplete_read();
    assert(close_count == 3u);
    return 0;
}
