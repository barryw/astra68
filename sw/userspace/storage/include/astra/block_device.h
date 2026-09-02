#ifndef ASTRA_BLOCK_DEVICE_H
#define ASTRA_BLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include <astra/metrics.h>

#define ASTRA_BLOCK_SECTOR_SIZE_MIN 512u
#define ASTRA_BLOCK_SECTOR_SIZE_MAX 4096u
#define ASTRA_BLOCK_TRANSFER_SECTORS_MAX 128u

#define ASTRA_BLOCK_FLAG_READ_ONLY (1u << 0)
#define ASTRA_BLOCK_FLAG_REMOVABLE (1u << 1)
#define ASTRA_BLOCK_FLAG_PRESENT   (1u << 2)

typedef enum AstraBlockStatus {
    ASTRA_BLOCK_OK = 0,
    ASTRA_BLOCK_INVALID_ARGUMENT = 1,
    ASTRA_BLOCK_NO_MEDIA = 2,
    ASTRA_BLOCK_READ_ONLY = 3,
    ASTRA_BLOCK_OUT_OF_RANGE = 4,
    ASTRA_BLOCK_TRANSFER_TOO_LARGE = 5,
    ASTRA_BLOCK_TIMED_OUT = 6,
    ASTRA_BLOCK_CANCELLED = 7,
    ASTRA_BLOCK_MEDIA_CHANGED = 8,
    ASTRA_BLOCK_IO_ERROR = 9,
    ASTRA_BLOCK_CORRUPT = 10
} AstraBlockStatus;

typedef enum AstraBlockOperation {
    ASTRA_BLOCK_OPERATION_QUERY = 0,
    ASTRA_BLOCK_OPERATION_READ = 1,
    ASTRA_BLOCK_OPERATION_WRITE = 2,
    ASTRA_BLOCK_OPERATION_FLUSH = 3,
    ASTRA_BLOCK_OPERATION_COUNT = 4
} AstraBlockOperation;

typedef struct AstraBlockGeometry {
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t max_transfer_sectors;
    uint32_t queue_depth;
    uint32_t media_generation;
    uint32_t flags;
} AstraBlockGeometry;

/*
 * Operation accounting uses the shared shape from <astra/metrics.h>; `units`
 * counts sectors here. The device publishes itself through
 * astra_block_sampler, which its owner registers under an instance name.
 */
typedef struct AstraBlockMetrics {
    AstraOpMetrics operation[ASTRA_BLOCK_OPERATION_COUNT];
} AstraBlockMetrics;

typedef AstraClock AstraBlockClock;

struct AstraBlockDevice;

typedef AstraBlockStatus (*AstraBlockTransfer)(
    void *context, uint64_t lba, uint32_t sector_count, void *buffer,
    uint64_t deadline);
typedef AstraBlockStatus (*AstraBlockWrite)(
    void *context, uint64_t lba, uint32_t sector_count, const void *buffer,
    uint64_t deadline);
typedef struct AstraBlockVector {
    const void *const *buffers;
    const uint32_t *sector_counts;
    uint32_t count;
} AstraBlockVector;
typedef AstraBlockStatus (*AstraBlockWritev)(
    void *context, uint64_t lba, const AstraBlockVector *vector,
    uint64_t deadline);
typedef AstraBlockStatus (*AstraBlockFlush)(void *context, uint64_t deadline);
typedef AstraBlockStatus (*AstraBlockQuery)(void *context,
                                            AstraBlockGeometry *geometry);

typedef struct AstraBlockBackend {
    AstraBlockQuery query;
    AstraBlockTransfer read;
    AstraBlockWrite write;
    AstraBlockWritev writev;
    AstraBlockFlush flush;
} AstraBlockBackend;

typedef struct AstraBlockDevice {
    const AstraBlockBackend *backend;
    void *backend_context;
    AstraBlockClock clock;
    void *clock_context;
    AstraBlockGeometry geometry;
    AstraBlockMetrics metrics;
} AstraBlockDevice;

void astra_block_device_init(AstraBlockDevice *device,
                             const AstraBlockBackend *backend,
                             void *backend_context, AstraBlockClock clock,
                             void *clock_context);
AstraBlockStatus astra_block_query(AstraBlockDevice *device,
                                   AstraBlockGeometry *geometry);
AstraBlockStatus astra_block_read(AstraBlockDevice *device, uint64_t lba,
                                  uint32_t sector_count, void *buffer,
                                  uint64_t deadline);
AstraBlockStatus astra_block_write(AstraBlockDevice *device, uint64_t lba,
                                   uint32_t sector_count, const void *buffer,
                                   uint64_t deadline);
AstraBlockStatus astra_block_writev(AstraBlockDevice *device, uint64_t lba,
                                    const AstraBlockVector *vector,
                                    uint64_t deadline);
AstraBlockStatus astra_block_flush(AstraBlockDevice *device,
                                   uint64_t deadline);
const AstraBlockMetrics *astra_block_metrics(const AstraBlockDevice *device);
uint32_t astra_block_sampler(void *context, AstraMetricSample *out,
                             uint32_t capacity);

#endif
