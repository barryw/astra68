/*
 * Freestanding link proof for the filesystem stack.
 *
 * Not a behavioural test: it exists so the linker has to resolve every symbol
 * the vendored library, the port and the bounded allocator reach for, under
 * the same -nostdlib contract a real service links with. The claim being
 * checked is that lwext4 on Astra needs no C library, and only a link can
 * settle that. A libc dependency that slips in through a vendored source or a
 * freestanding shim fails here, at build time, instead of surfacing as an
 * undefined symbol during hardware bring-up.
 *
 * Each entry point below is referenced through a volatile pointer so the
 * compiler cannot decide the calls are unreachable and drop the references
 * that are the entire point of the file.
 */

#include <astra/alloc.h>
#include <astra/block_device.h>
#include <astra/ext4_alloc.h>
#include <astra/ext4_port.h>
#include <astra/lease_block.h>
#include <astra/runtime.h>

#include <ext4.h>

static AstraExt4Port port;
static AstraBlockDevice device;
static AstraLeaseBlock lease;
static AstraAllocator allocator;
static AstraAllocScalar arena[8192];
static unsigned char sector[512];

/* The filesystem surface a VFS service will actually call. */
static void *volatile const referenced[] = {
    (void *)(uintptr_t)ext4_device_register,
    (void *)(uintptr_t)ext4_device_unregister,
    (void *)(uintptr_t)ext4_mount,
    (void *)(uintptr_t)ext4_umount,
    (void *)(uintptr_t)ext4_recover,
    (void *)(uintptr_t)ext4_journal_start,
    (void *)(uintptr_t)ext4_journal_stop,
    (void *)(uintptr_t)ext4_cache_write_back,
    (void *)(uintptr_t)ext4_fopen,
    (void *)(uintptr_t)ext4_fclose,
    (void *)(uintptr_t)ext4_fread,
    (void *)(uintptr_t)ext4_fwrite,
    (void *)(uintptr_t)ext4_fseek,
    (void *)(uintptr_t)ext4_fsize,
    (void *)(uintptr_t)ext4_fremove,
    (void *)(uintptr_t)ext4_frename,
    (void *)(uintptr_t)ext4_dir_mk,
    (void *)(uintptr_t)ext4_dir_rm,
    (void *)(uintptr_t)ext4_dir_open,
    (void *)(uintptr_t)ext4_dir_close,
    (void *)(uintptr_t)ext4_dir_entry_next,
    (void *)(uintptr_t)ext4_raw_inode_fill,
    (void *)(uintptr_t)ext4_mount_point_stats,
};

int
astra_main(const AstraStartupInfo *startup)
{
    (void)startup;
    (void)referenced;

    if (astra_alloc_init(&allocator, astra_ext4_alloc_classes,
                         ASTRA_EXT4_ALLOC_CLASS_COUNT, arena,
                         sizeof(arena)) != ASTRA_ALLOC_OK) {
        return 1;
    }
    astra_ext4_alloc_bind(&allocator);

    if (astra_lease_block_attach(&lease, 0u, 0u) != ASTRA_BLOCK_OK) {
        return 2;
    }
    astra_block_device_init(&device, astra_lease_block_backend(), &lease, NULL,
                            NULL);
    if (astra_ext4_port_init(&port, &device, sector, sizeof(sector), 0u) !=
        ASTRA_EXT4_OK) {
        return 3;
    }
    return ext4_device_register(astra_ext4_port_blockdev(&port), "astra");
}
