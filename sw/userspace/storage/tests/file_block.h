#ifndef ASTRA_TEST_FILE_BLOCK_H
#define ASTRA_TEST_FILE_BLOCK_H

#include <stdint.h>

#include <astra/block_device.h>

/*
 * A host-only AstraBlockBackend over an ordinary file.
 *
 * Test scaffolding, deliberately not in src/: it calls pread/pwrite and has no
 * place in a service. It exists because the deterministic memory backend holds
 * the whole volume in RAM, which caps the volume size at what a 32-bit process
 * can address — around 2 GiB under qemu-m68k, and less in practice. Measuring
 * how lwext4's memory demand scales with volume size needs volumes far larger
 * than that, so the bytes have to stay on disk.
 *
 * Offsets are uint64_t throughout and the translation unit must be compiled
 * with _FILE_OFFSET_BITS=64. On LP32 a size_t byte offset overflows at 4 GiB,
 * which is exactly the range this backend exists to reach.
 */
typedef struct AstraFileBlock {
    int descriptor;
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t max_transfer_sectors;
    uint32_t media_generation;
    uint32_t flags;
} AstraFileBlock;

extern const AstraBlockBackend astra_file_block_backend;

/* Returns 0 on success. The file must already exist and be sector-aligned. */
int astra_file_block_open(AstraFileBlock *file, const char *path,
                          uint32_t sector_size,
                          uint32_t max_transfer_sectors);
void astra_file_block_close(AstraFileBlock *file);

#endif
