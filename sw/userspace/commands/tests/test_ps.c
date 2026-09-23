#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <astra/posix.h>
#include <astra/proc.h>
#include <astra/runtime.h>
#include <astra/vfs_process.h>

static AstraStartupInfo startup;
static AstraProcSnapshot record;
static uint32_t open_status;
static uint32_t read_status;
static uint32_t read_length;
static uint32_t closes;
static uint32_t releases;

const AstraStartupInfo *astra_posix_startup(void)
{
    return &startup;
}

int astra_startup_validate(const AstraStartupInfo *candidate)
{
    return candidate == &startup;
}

uint32_t astra_process_filesystem_open(AstraProcessFilesystem *filesystem,
                                       const AstraStartupInfo *candidate)
{
    assert(filesystem != NULL && candidate == &startup);
    return open_status;
}

void astra_process_filesystem_close(AstraProcessFilesystem *filesystem)
{
    assert(filesystem != NULL);
    ++closes;
}

uint32_t astra_process_read_file_alloc(AstraProcessFilesystem *filesystem,
                                       const char *path, void **bytes,
                                       uint32_t *length)
{
    assert(filesystem != NULL);
    assert(strcmp(path, "/proc/snapshot") == 0);
    *bytes = read_status == ASTRA_VFS_OK ? &record : NULL;
    *length = read_status == ASTRA_VFS_OK ? read_length : 0u;
    return read_status;
}

void astra_runtime_deallocate(void *pointer)
{
    assert(pointer == &record);
    ++releases;
}

#define main astra_ps_main
#include "../ps/ps.c"
#undef main

int main(void)
{
    char *arguments[] = {"ps", NULL};

    record.process.size = sizeof(record.process);
    record.process.id = 7u;
    record.process.default_priority = ASTRA_PROCESS_PRIORITY_NORMAL;
    memcpy(record.name, "test", sizeof("test"));
    read_length = sizeof(record);
    assert(astra_ps_main(1, arguments) == 0);
    assert(closes == 1u && releases == 1u);

    read_status = ASTRA_VFS_ERR_NOT_FOUND;
    assert(astra_ps_main(1, arguments) == ASTRA_VFS_ERR_NOT_FOUND);
    assert(closes == 2u && releases == 1u);
    read_status = ASTRA_VFS_OK;

    read_length = sizeof(record) - 1u;
    assert(astra_ps_main(1, arguments) == ASTRA_VFS_ERR_PROTOCOL);
    assert(closes == 3u && releases == 2u);

    read_length = sizeof(record);
    record.process.size = 0u;
    assert(astra_ps_main(1, arguments) == ASTRA_VFS_ERR_PROTOCOL);
    assert(closes == 4u && releases == 3u);

    open_status = ASTRA_VFS_ERR_ACCESS;
    assert(astra_ps_main(1, arguments) == ASTRA_VFS_ERR_ACCESS);
    assert(closes == 4u && releases == 3u);
    return 0;
}
