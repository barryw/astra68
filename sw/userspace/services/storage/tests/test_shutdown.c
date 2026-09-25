#include "../shutdown.h"

#include <astra/status.h>
#include <astra/syscall.h>
#include <ext4.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static unsigned step;
static unsigned fail_at;

static int ext4_step(void)
{
    ++step;
    return step == fail_at ? EIO : EOK;
}

int ext4_journal_commit(const char *path)
{
    assert(path[0] == '/' && path[1] == 'v');
    return ext4_step();
}

int ext4_cache_write_back(const char *path, bool on)
{
    assert(path[0] == '/' && !on);
    return ext4_step();
}

int ext4_journal_stop(const char *path)
{
    assert(path[0] == '/');
    return ext4_step();
}

int ext4_umount(const char *path)
{
    assert(path[0] == '/');
    return ext4_step();
}

int ext4_device_unregister(const char *name)
{
    assert(name[0] == 'a');
    return ext4_step();
}

AstraBlockStatus astra_block_flush(AstraBlockDevice *device,
                                   uint64_t deadline)
{
    assert(device != NULL && deadline == ASTRA_DEADLINE_FOREVER);
    ++step;
    return step == fail_at ? ASTRA_BLOCK_IO_ERROR : ASTRA_BLOCK_OK;
}

int main(void)
{
    AstraBlockDevice device = {0};

    assert(storage_unmount_volume(NULL) == ASTRA_STATUS_IO);
    assert(step == 0u);
    assert(storage_flush_volume(&device) == ASTRA_STATUS_OK);
    assert(storage_unmount_volume(&device) == ASTRA_STATUS_OK);
    assert(step == 6u);
    for (fail_at = 1u; fail_at <= 6u; ++fail_at) {
        step = 0u;
        if (fail_at <= 2u)
            assert(storage_flush_volume(&device) == ASTRA_STATUS_IO);
        else {
            assert(storage_flush_volume(&device) == ASTRA_STATUS_OK);
            assert(storage_unmount_volume(&device) == ASTRA_STATUS_IO);
        }
        assert(step == fail_at);
    }
    puts("storage shutdown tests passed");
    return 0;
}
