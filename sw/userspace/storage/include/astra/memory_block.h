#ifndef ASTRA_MEMORY_BLOCK_H
#define ASTRA_MEMORY_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include <astra/block_device.h>

typedef struct AstraMemoryBlock {
    uint8_t *storage;
    size_t storage_size;
    uint32_t sector_size;
    uint32_t max_transfer_sectors;
    uint32_t media_generation;
    uint32_t flags;
    uint64_t operation_count;
    uint64_t fail_operation;
} AstraMemoryBlock;

extern const AstraBlockBackend astra_memory_block_backend;

void astra_memory_block_init(AstraMemoryBlock *memory, void *storage,
                             size_t storage_size, uint32_t sector_size,
                             uint32_t max_transfer_sectors, uint32_t flags);
void astra_memory_block_set_present(AstraMemoryBlock *memory, int present);
void astra_memory_block_fail_at(AstraMemoryBlock *memory,
                                uint64_t operation_number);

#endif
