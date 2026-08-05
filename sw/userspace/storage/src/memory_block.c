#include <astra/memory_block.h>

#include <astra/bytes.h>

static AstraBlockStatus
maybe_fail(AstraMemoryBlock *memory)
{
    ++memory->operation_count;
    return memory->fail_operation != 0u &&
           memory->operation_count == memory->fail_operation ?
        ASTRA_BLOCK_IO_ERROR : ASTRA_BLOCK_OK;
}

static AstraBlockStatus
memory_query(void *context, AstraBlockGeometry *geometry)
{
    AstraMemoryBlock *memory = context;

    if (memory == NULL || geometry == NULL || memory->storage == NULL ||
        memory->sector_size == 0u ||
        memory->storage_size < memory->sector_size) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if (maybe_fail(memory) != ASTRA_BLOCK_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    geometry->sector_count = memory->storage_size / memory->sector_size;
    geometry->sector_size = memory->sector_size;
    geometry->max_transfer_sectors = memory->max_transfer_sectors;
    geometry->media_generation = memory->media_generation;
    geometry->flags = memory->flags;
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
memory_read(void *context, uint64_t lba, uint32_t sector_count, void *buffer,
            uint64_t deadline)
{
    AstraMemoryBlock *memory = context;
    size_t offset = (size_t)lba * memory->sector_size;
    size_t count = (size_t)sector_count * memory->sector_size;

    (void)deadline;
    if (maybe_fail(memory) != ASTRA_BLOCK_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    memcpy(buffer, memory->storage + offset, count);
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
memory_write(void *context, uint64_t lba, uint32_t sector_count,
             const void *buffer,
             uint64_t deadline)
{
    AstraMemoryBlock *memory = context;
    size_t offset = (size_t)lba * memory->sector_size;
    size_t count = (size_t)sector_count * memory->sector_size;

    (void)deadline;
    if (maybe_fail(memory) != ASTRA_BLOCK_OK) {
        return ASTRA_BLOCK_IO_ERROR;
    }
    memcpy(memory->storage + offset, buffer, count);
    return ASTRA_BLOCK_OK;
}

static AstraBlockStatus
memory_flush(void *context, uint64_t deadline)
{
    AstraMemoryBlock *memory = context;

    (void)deadline;
    return maybe_fail(memory);
}

const AstraBlockBackend astra_memory_block_backend = {
    memory_query,
    memory_read,
    memory_write,
    memory_flush
};

void
astra_memory_block_init(AstraMemoryBlock *memory, void *storage,
                        size_t storage_size, uint32_t sector_size,
                        uint32_t max_transfer_sectors, uint32_t flags)
{
    if (memory == NULL) {
        return;
    }
    memory->storage = storage;
    memory->storage_size = storage_size;
    memory->sector_size = sector_size;
    memory->max_transfer_sectors = max_transfer_sectors;
    memory->media_generation = 1u;
    memory->flags = flags;
    memory->operation_count = 0u;
    memory->fail_operation = 0u;
}

void
astra_memory_block_set_present(AstraMemoryBlock *memory, int present)
{
    if (memory == NULL) {
        return;
    }
    if (present != 0) {
        memory->flags |= ASTRA_BLOCK_FLAG_PRESENT;
    } else {
        memory->flags &= ~ASTRA_BLOCK_FLAG_PRESENT;
    }
    ++memory->media_generation;
    if (memory->media_generation == 0u) {
        memory->media_generation = 1u;
    }
}

void
astra_memory_block_fail_at(AstraMemoryBlock *memory,
                           uint64_t operation_number)
{
    if (memory != NULL) {
        memory->fail_operation = operation_number;
    }
}
