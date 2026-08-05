#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <astra/block_device.h>
#include <astra/memory_block.h>

#define TEST_SECTORS 4096u
#define TEST_BYTES (TEST_SECTORS * 512u)
#define STRESS_OPERATIONS 100000u

static uint8_t storage[TEST_BYTES];
static uint8_t oracle[TEST_BYTES];
static uint8_t transfer[16u * 512u];

static uint64_t
nanoseconds(void *context)
{
    struct timespec now;

    (void)context;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static uint32_t
random_next(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void
test_contract(AstraBlockDevice *device, AstraMemoryBlock *memory)
{
    AstraBlockGeometry geometry;
    uint8_t sector[512] = {0};

    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);
    assert(geometry.sector_count == TEST_SECTORS);
    assert(geometry.sector_size == 512u);
    assert(astra_block_read(device, TEST_SECTORS, 1u, sector, 0u) ==
           ASTRA_BLOCK_OUT_OF_RANGE);
    assert(astra_block_read(device, TEST_SECTORS - 1u, 2u, sector, 0u) ==
           ASTRA_BLOCK_OUT_OF_RANGE);
    assert(astra_block_read(device, 0u, 17u, sector, 0u) ==
           ASTRA_BLOCK_TRANSFER_TOO_LARGE);

    memory->flags |= ASTRA_BLOCK_FLAG_READ_ONLY;
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_block_write(device, 0u, 1u, sector, 0u) ==
           ASTRA_BLOCK_READ_ONLY);
    memory->flags &= ~ASTRA_BLOCK_FLAG_READ_ONLY;
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);

    astra_memory_block_set_present(memory, 0);
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);
    assert(astra_block_read(device, 0u, 1u, sector, 0u) ==
           ASTRA_BLOCK_NO_MEDIA);
    astra_memory_block_set_present(memory, 1);
    assert(astra_block_query(device, &geometry) == ASTRA_BLOCK_OK);

    astra_memory_block_fail_at(memory, memory->operation_count + 1u);
    assert(astra_block_read(device, 0u, 1u, sector, 0u) ==
           ASTRA_BLOCK_IO_ERROR);
    astra_memory_block_fail_at(memory, 0u);
}

static void
stress(AstraBlockDevice *device)
{
    uint32_t random = 0x68a57a31u;
    uint32_t operation;

    memset(storage, 0, sizeof(storage));
    memset(oracle, 0, sizeof(oracle));
    for (operation = 0u; operation < STRESS_OPERATIONS; ++operation) {
        uint32_t sectors = random_next(&random) % 16u + 1u;
        uint32_t lba = random_next(&random) % (TEST_SECTORS - sectors + 1u);
        size_t bytes = (size_t)sectors * 512u;
        size_t offset = (size_t)lba * 512u;

        if ((random_next(&random) & 3u) != 0u) {
            size_t index;
            for (index = 0u; index < bytes; ++index) {
                transfer[index] = (uint8_t)random_next(&random);
            }
            assert(astra_block_write(device, lba, sectors, transfer, 0u) ==
                   ASTRA_BLOCK_OK);
            memcpy(oracle + offset, transfer, bytes);
        } else {
            memset(transfer, 0xa5, bytes);
            assert(astra_block_read(device, lba, sectors, transfer, 0u) ==
                   ASTRA_BLOCK_OK);
            assert(memcmp(transfer, oracle + offset, bytes) == 0);
        }
        if ((operation & 1023u) == 0u) {
            assert(astra_block_flush(device, 0u) == ASTRA_BLOCK_OK);
        }
    }
    assert(memcmp(storage, oracle, sizeof(storage)) == 0);
}

static void
report(const AstraBlockDevice *device)
{
    const AstraBlockMetrics *metrics = astra_block_metrics(device);
    static const char *const names[ASTRA_BLOCK_OPERATION_COUNT] = {
        "query", "read", "write", "flush"
    };
    uint32_t operation;

    for (operation = 0u; operation < ASTRA_BLOCK_OPERATION_COUNT;
         ++operation) {
        const AstraOpMetrics *item =
            &metrics->operation[operation];
        uint64_t average = item->calls != 0u ? item->ticks / item->calls : 0u;
        printf("block %-5s calls=%" PRIu64 " failures=%" PRIu64
               " sectors=%" PRIu64 " avg_ns=%" PRIu64
               " max_ns=%" PRIu64 "\n",
               names[operation], item->calls, item->failures, item->units,
               average, item->maximum_ticks);
    }
}

int
main(void)
{
    AstraMemoryBlock memory;
    AstraBlockDevice device;

    astra_memory_block_init(&memory, storage, sizeof(storage), 512u, 16u,
                            ASTRA_BLOCK_FLAG_PRESENT);
    astra_block_device_init(&device, &astra_memory_block_backend, &memory,
                            nanoseconds, NULL);
    test_contract(&device, &memory);
    stress(&device);
    report(&device);
    puts("astra block device: PASS");
    return 0;
}
