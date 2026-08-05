#include <astra/block_device.h>

#include <astra/bytes.h>

static uint64_t
clock_read(const AstraBlockDevice *device)
{
    return astra_clock_read(device->clock, device->clock_context);
}

static void
record(AstraBlockDevice *device, AstraBlockOperation operation,
       AstraBlockStatus status, uint32_t sectors, uint64_t start)
{
    astra_op_record(&device->metrics.operation[operation],
                    status != ASTRA_BLOCK_OK, sectors,
                    clock_read(device) - start);
}

void
astra_block_device_init(AstraBlockDevice *device,
                        const AstraBlockBackend *backend,
                        void *backend_context, AstraBlockClock clock,
                        void *clock_context)
{
    if (device == NULL) {
        return;
    }
    memset(device, 0, sizeof(*device));
    device->backend = backend;
    device->backend_context = backend_context;
    device->clock = clock;
    device->clock_context = clock_context;
}

static int
geometry_valid(const AstraBlockGeometry *geometry)
{
    return geometry->sector_count != 0u &&
           geometry->sector_size >= ASTRA_BLOCK_SECTOR_SIZE_MIN &&
           geometry->sector_size <= ASTRA_BLOCK_SECTOR_SIZE_MAX &&
           (geometry->sector_size & (geometry->sector_size - 1u)) == 0u &&
           geometry->max_transfer_sectors != 0u &&
           geometry->max_transfer_sectors <=
               ASTRA_BLOCK_TRANSFER_SECTORS_MAX &&
           geometry->media_generation != 0u;
}

AstraBlockStatus
astra_block_query(AstraBlockDevice *device, AstraBlockGeometry *geometry)
{
    AstraBlockStatus status;
    uint64_t start;

    if (device == NULL || geometry == NULL || device->backend == NULL ||
        device->backend->query == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    start = clock_read(device);
    status = device->backend->query(device->backend_context, geometry);
    if (status == ASTRA_BLOCK_OK && !geometry_valid(geometry)) {
        status = ASTRA_BLOCK_CORRUPT;
    }
    if (status == ASTRA_BLOCK_OK) {
        device->geometry = *geometry;
    }
    record(device, ASTRA_BLOCK_OPERATION_QUERY, status, 0u, start);
    return status;
}

static AstraBlockStatus
validate_transfer(AstraBlockDevice *device, uint64_t lba,
                  uint32_t sector_count, const void *buffer, int writing)
{
    uint64_t remaining;

    if (device == NULL || buffer == NULL || sector_count == 0u ||
        device->backend == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    if ((device->geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u) {
        return ASTRA_BLOCK_NO_MEDIA;
    }
    if (writing != 0 &&
        (device->geometry.flags & ASTRA_BLOCK_FLAG_READ_ONLY) != 0u) {
        return ASTRA_BLOCK_READ_ONLY;
    }
    if (sector_count > device->geometry.max_transfer_sectors) {
        return ASTRA_BLOCK_TRANSFER_TOO_LARGE;
    }
    if (lba >= device->geometry.sector_count) {
        return ASTRA_BLOCK_OUT_OF_RANGE;
    }
    remaining = device->geometry.sector_count - lba;
    if ((uint64_t)sector_count > remaining) {
        return ASTRA_BLOCK_OUT_OF_RANGE;
    }
    return ASTRA_BLOCK_OK;
}

AstraBlockStatus
astra_block_read(AstraBlockDevice *device, uint64_t lba,
                 uint32_t sector_count, void *buffer, uint64_t deadline)
{
    AstraBlockStatus status = validate_transfer(
        device, lba, sector_count, buffer, 0);
    uint64_t start = device != NULL ? clock_read(device) : 0u;

    if (status == ASTRA_BLOCK_OK) {
        if (device->backend->read == NULL) {
            status = ASTRA_BLOCK_INVALID_ARGUMENT;
        } else {
            status = device->backend->read(
                device->backend_context, lba, sector_count, buffer, deadline);
        }
    }
    if (device != NULL) {
        record(device, ASTRA_BLOCK_OPERATION_READ, status,
               status == ASTRA_BLOCK_OK ? sector_count : 0u, start);
    }
    return status;
}

AstraBlockStatus
astra_block_write(AstraBlockDevice *device, uint64_t lba,
                  uint32_t sector_count, const void *buffer,
                  uint64_t deadline)
{
    AstraBlockStatus status = validate_transfer(
        device, lba, sector_count, buffer, 1);
    uint64_t start = device != NULL ? clock_read(device) : 0u;

    if (status == ASTRA_BLOCK_OK) {
        if (device->backend->write == NULL) {
            status = ASTRA_BLOCK_INVALID_ARGUMENT;
        } else {
            status = device->backend->write(
                device->backend_context, lba, sector_count, buffer, deadline);
        }
    }
    if (device != NULL) {
        record(device, ASTRA_BLOCK_OPERATION_WRITE, status,
               status == ASTRA_BLOCK_OK ? sector_count : 0u, start);
    }
    return status;
}

AstraBlockStatus
astra_block_flush(AstraBlockDevice *device, uint64_t deadline)
{
    AstraBlockStatus status;
    uint64_t start;

    if (device == NULL || device->backend == NULL ||
        device->backend->flush == NULL) {
        return ASTRA_BLOCK_INVALID_ARGUMENT;
    }
    start = clock_read(device);
    if ((device->geometry.flags & ASTRA_BLOCK_FLAG_PRESENT) == 0u) {
        status = ASTRA_BLOCK_NO_MEDIA;
    } else {
        status = device->backend->flush(device->backend_context, deadline);
    }
    record(device, ASTRA_BLOCK_OPERATION_FLUSH, status, 0u, start);
    return status;
}

const AstraBlockMetrics *
astra_block_metrics(const AstraBlockDevice *device)
{
    return device != NULL ? &device->metrics : NULL;
}

static const char *const block_sample_names[ASTRA_BLOCK_OPERATION_COUNT][5] = {
    {"query.calls", "query.failures", "query.sectors", "query.ticks",
     "query.max_ticks"},
    {"read.calls", "read.failures", "read.sectors", "read.ticks",
     "read.max_ticks"},
    {"write.calls", "write.failures", "write.sectors", "write.ticks",
     "write.max_ticks"},
    {"flush.calls", "flush.failures", "flush.sectors", "flush.ticks",
     "flush.max_ticks"},
};

uint32_t
astra_block_sampler(void *context, AstraMetricSample *out, uint32_t capacity)
{
    const AstraBlockDevice *device = context;
    uint32_t written = 0u;
    uint32_t operation;

    if (device == NULL || out == NULL) {
        return 0u;
    }
    for (operation = 0u; operation < ASTRA_BLOCK_OPERATION_COUNT;
         ++operation) {
        uint32_t emitted;

        if (capacity - written < 5u) {
            return written;
        }
        emitted = astra_op_samples(&device->metrics.operation[operation],
                                   block_sample_names[operation],
                                   out + written, capacity - written);
        if (emitted == 0u) {
            return written;
        }
        written += emitted;
    }

    if (capacity - written >= 3u) {
        out[written].name = "media.sector_count";
        out[written++].value = device->geometry.sector_count;
        out[written].name = "media.sector_size";
        out[written++].value = device->geometry.sector_size;
        out[written].name = "media.generation";
        out[written++].value = device->geometry.media_generation;
    }
    return written;
}
