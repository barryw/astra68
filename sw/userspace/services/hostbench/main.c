#include <astra/host.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/string.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("hostbench", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#ifndef ASTRA_HOSTBENCH_ITERATIONS
#define ASTRA_HOSTBENCH_ITERATIONS UINT32_C(65536)
#endif

#define HOSTBENCH_DATA_BYTES 64u
#define HOSTBENCH_ADAPTIVE_ITERATIONS UINT32_C(2048)
#define HOSTBENCH_MAX_POLLS UINT32_C(65536)
#define HOSTBENCH_MEMORY_BYTES UINT32_C(16384)
#define HOSTBENCH_MEMORY_ITERATIONS UINT32_C(64)

static uint8_t memory_source[HOSTBENCH_MEMORY_BYTES + 8u]
    __attribute__((aligned(4)));
static uint8_t memory_destination[HOSTBENCH_MEMORY_BYTES + 8u]
    __attribute__((aligned(4)));

uint32_t astra_hostbench_run(volatile AstraHostChannelHeader *header,
                             volatile AstraHostCommand *commands,
                             volatile uint32_t *doorbell,
                             uint32_t capacity, uint32_t iterations,
                             uint32_t expected_status);

static void clear_words(volatile void *address, uint32_t bytes)
{
    volatile uint32_t *words = address;

    for (uint32_t index = 0u; index < bytes / sizeof(*words); ++index)
        words[index] = 0u;
}

static void report(uint32_t depth, uint64_t elapsed)
{
    char line_text[112];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "RAW USER depth=");
    (void)astra_string_append_hex(&line, depth, 8u);
    (void)astra_string_append(&line, " iterations=");
    (void)astra_string_append_hex(&line, ASTRA_HOSTBENCH_ITERATIONS, 8u);
    (void)astra_string_append(&line, " elapsed-ns=");
    (void)astra_string_append_hex(&line, elapsed, 16u);
    (void)astra_log(line_text);
}

static void report_adaptive(uint32_t polls, uint32_t misses,
                            uint64_t elapsed)
{
    char line_text[128];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "ADAPT polls=");
    (void)astra_string_append_hex(&line, polls, 8u);
    (void)astra_string_append(&line, " iterations=");
    (void)astra_string_append_hex(&line, HOSTBENCH_ADAPTIVE_ITERATIONS, 8u);
    (void)astra_string_append(&line, " misses=");
    (void)astra_string_append_hex(&line, misses, 8u);
    (void)astra_string_append(&line, " elapsed-ns=");
    (void)astra_string_append_hex(&line, elapsed, 16u);
    (void)astra_log(line_text);
}

static void report_memory(const char *name, uint64_t bytes, uint64_t elapsed)
{
    char line_text[112];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));

    (void)astra_string_append(&line, "MEM name=");
    (void)astra_string_append(&line, name);
    (void)astra_string_append(&line, " bytes=");
    (void)astra_string_append_hex(&line, bytes, 16u);
    (void)astra_string_append(&line, " elapsed-ns=");
    (void)astra_string_append_hex(&line, elapsed, 16u);
    (void)astra_log(line_text);
}

static uint32_t memory_check(void)
{
    uint8_t overlap[264];

    for (uint32_t index = 0u; index < sizeof(memory_source); ++index)
        memory_source[index] = (uint8_t)(index * 131u + (index >> 2));
    for (uint32_t from = 0u; from < 4u; ++from)
        for (uint32_t to = 0u; to < 4u; ++to) {
            memset(memory_destination, 0xa5, sizeof(memory_destination));
            if (memcpy(memory_destination + to, memory_source + from, 257u) !=
                    memory_destination + to ||
                memcmp(memory_destination + to, memory_source + from, 257u) !=
                    0)
                return 0u;
        }
    memcpy(memory_destination, memory_source, 257u);
    for (uint32_t index = 0u; index < 257u; ++index) {
        uint8_t original = memory_destination[index];
        int forward;
        int reverse;

        memory_destination[index] ^= 0x80u;
        forward = memcmp(memory_destination, memory_source, 257u);
        reverse = memcmp(memory_source, memory_destination, 257u);
        if ((memory_destination[index] > original &&
             (forward <= 0 || reverse >= 0)) ||
            (memory_destination[index] < original &&
             (forward >= 0 || reverse <= 0)))
            return 0u;
        memory_destination[index] = original;
    }
    if (memcmp("astra", "astrb", 5u) >= 0 ||
        memcmp("astrb", "astra", 5u) <= 0 || memcmp("", "", 0u) != 0)
        return 0u;
    for (uint32_t offset = 0u; offset < 4u; ++offset) {
        if (memset(memory_destination + offset, 0x5a, 257u) !=
            memory_destination + offset)
            return 0u;
        for (uint32_t index = 0u; index < 257u; ++index)
            if (memory_destination[offset + index] != 0x5au)
                return 0u;
    }
    for (uint32_t index = 0u; index < sizeof(overlap); ++index)
        overlap[index] = (uint8_t)index;
    if (memmove(overlap + 1u, overlap, 255u) != overlap + 1u)
        return 0u;
    for (uint32_t index = 1u; index < 256u; ++index)
        if (overlap[index] != (uint8_t)(index - 1u))
            return 0u;
    for (uint32_t index = 0u; index < sizeof(overlap); ++index)
        overlap[index] = (uint8_t)index;
    if (memmove(overlap, overlap + 1u, 255u) != overlap)
        return 0u;
    for (uint32_t index = 0u; index < 255u; ++index)
        if (overlap[index] != (uint8_t)(index + 1u))
            return 0u;
    return 1u;
}

static uint32_t run_memory(void)
{
    uint64_t bytes = (uint64_t)HOSTBENCH_MEMORY_BYTES *
                     HOSTBENCH_MEMORY_ITERATIONS;
    uint64_t started;
    uint64_t elapsed;
    int compared = 0;

    if (!memory_check())
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t iteration = 0u;
         iteration < HOSTBENCH_MEMORY_ITERATIONS; ++iteration)
        (void)memcpy(memory_destination, memory_source,
                     HOSTBENCH_MEMORY_BYTES);
    elapsed = astra_clock_monotonic() - started;
    if (elapsed == 0u ||
        memcmp(memory_destination, memory_source,
               HOSTBENCH_MEMORY_BYTES) != 0)
        return 0u;
    report_memory("memcpy", bytes, elapsed);

    started = astra_clock_monotonic();
    for (uint32_t iteration = 0u;
         iteration < HOSTBENCH_MEMORY_ITERATIONS; ++iteration)
        compared |= memcmp(memory_destination, memory_source,
                           HOSTBENCH_MEMORY_BYTES);
    elapsed = astra_clock_monotonic() - started;
    if (elapsed == 0u || compared != 0)
        return 0u;
    report_memory("memcmp", bytes, elapsed);
    return 1u;
}

static uint32_t run_adaptive(volatile AstraHostChannelHeader *header,
                             volatile AstraHostCommand *command,
                             uint32_t channel_address, uint32_t polls,
                             uint32_t *misses,
                             uint64_t *elapsed)
{
    uint64_t started = astra_clock_monotonic();

    *misses = 0u;
    for (uint32_t iteration = 0u;
         iteration < HOSTBENCH_ADAPTIVE_ITERATIONS; ++iteration) {
        uint32_t producer = iteration + 1u;
        uint32_t completed = 0u;

        command->status = UINT32_MAX;
        header->producer_position = producer;
        if (astra_host_channel_kick(channel_address, producer) !=
            ASTRA_SYSCALL_OK)
            return 0u;
        for (uint32_t poll = 0u; poll < polls; ++poll) {
            astra_compiler_barrier();
            if ((int32_t)(header->consumer_position - producer) >= 0) {
                completed = 1u;
                break;
            }
        }
        if (completed == 0u) {
            ++*misses;
            if (astra_host_channel_wait(
                    producer, UINT64_C(0x7fffffffffffffff)) !=
                ASTRA_SYSCALL_OK)
                return 0u;
        }
        astra_memory_acquire_fence();
        if ((int32_t)(header->consumer_position - producer) < 0 ||
            header->transport_status != ASTRA_SYSCALL_OK ||
            command->status != ASTRA_STATUS_UNSUPPORTED)
            return 0u;
    }
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static int fail(const AstraStartupCapability *bootstrap, uint32_t code)
{
    char line_text[40];
    AstraString line;

    astra_string_init(&line, line_text, sizeof(line_text));
    (void)astra_string_append(&line, "ASTRA RAW USER FAIL code=");
    (void)astra_string_append_hex(&line, code, 8u);
    (void)astra_log(line_text);
    if (bootstrap != NULL)
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_IO, NULL,
                                  0u);
    return (int)(ASTRA_STATUS_PROGRAM_FIRST + code);
}

static uint32_t largest_capacity(const AstraHostLeaseInfo *lease)
{
    uint32_t layout_max;
    uint32_t capacity = 1u;

    if (lease->maximum_transfer <=
        ASTRA_HOST_CHANNEL_HEADER_SIZE + HOSTBENCH_DATA_BYTES)
        return 0u;
    layout_max = (lease->maximum_transfer - ASTRA_HOST_CHANNEL_HEADER_SIZE -
                  HOSTBENCH_DATA_BYTES) / ASTRA_HOST_COMMAND_SIZE;
    if (layout_max > lease->maximum_commands)
        layout_max = lease->maximum_commands;
    while (capacity <= layout_max / 2u)
        capacity <<= 1;
    return capacity <= layout_max ? capacity : 0u;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *device;
    const AstraStartupCapability *bootstrap;
    AstraHostLeaseInfo lease = {0};
    AstraDmaBufferInfo dma = {0};
    volatile uint8_t *bytes;
    volatile AstraHostCommand *commands;
    uint32_t maximum;
    uint32_t total_bytes;

    if (!astra_startup_validate(startup))
        return ASTRA_STATUS_INVALID;
    device = astra_startup_capability(startup,
                                      ASTRA_CAPABILITY_HOST_DEVICE);
    bootstrap = astra_startup_capability(startup,
                                         ASTRA_CAPABILITY_SERVICE_READY);
    if (device == NULL)
        return fail(bootstrap, 1u);
    if (!run_memory())
        return fail(bootstrap, 9u);
    if (astra_host_lease_query(device->handle, &lease) != ASTRA_SYSCALL_OK ||
        (lease.capabilities & ASTRA_HOST_CAP_CHANNEL) == 0u ||
        lease.host_generation == 0u || ASTRA_HOSTBENCH_ITERATIONS == 0u)
        return fail(bootstrap, 2u);
    maximum = largest_capacity(&lease);
    if (maximum == 0u)
        return fail(bootstrap, 3u);
    total_bytes = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                  maximum * ASTRA_HOST_COMMAND_SIZE + HOSTBENCH_DATA_BYTES;
    if (astra_dma_create(total_bytes, &dma) != ASTRA_SYSCALL_OK ||
        dma.handle == 0u || dma.virtual_base == 0u ||
        dma.byte_size < total_bytes)
        return fail(bootstrap, 4u);
    bytes = (volatile uint8_t *)(uintptr_t)dma.virtual_base;
    clear_words(bytes, total_bytes);
    commands = (volatile AstraHostCommand *)(void *)(
        bytes + ASTRA_HOST_CHANNEL_HEADER_SIZE);
    for (uint32_t index = 0u; index < maximum; ++index) {
        commands[index].size = sizeof(commands[index]);
        commands[index].version = ASTRA_HOST_COMMAND_VERSION;
        commands[index].service = ASTRA_HOST_SERVICE_FILESYSTEM;
        commands[index].generation = lease.host_generation;
    }

    for (uint32_t depth = 1u; depth <= maximum; depth <<= 1) {
        volatile AstraHostChannelHeader *header =
            (volatile AstraHostChannelHeader *)(void *)bytes;
        AstraHostChannelOpen channel = {0};
        uint64_t started;
        uint64_t elapsed;
        uint32_t result;

        channel.size = sizeof(channel);
        channel.buffer = dma.handle;
        channel.byte_size = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                            depth * ASTRA_HOST_COMMAND_SIZE +
                            HOSTBENCH_DATA_BYTES;
        channel.command_capacity = depth;
        if (astra_host_channel_open(device->handle, &channel) !=
                ASTRA_SYSCALL_OK ||
            channel.channel_address == 0u ||
            channel.channel_generation == 0u ||
            channel.host_generation != lease.host_generation ||
            header->magic != ASTRA_HOST_CHANNEL_MAGIC ||
            header->command_capacity != depth)
            return fail(bootstrap, 5u);
        started = astra_clock_monotonic();
        result = astra_hostbench_run(
            header, commands,
            (volatile uint32_t *)(uintptr_t)channel.channel_address,
            depth, ASTRA_HOSTBENCH_ITERATIONS,
            ASTRA_STATUS_UNSUPPORTED);
        elapsed = astra_clock_monotonic() - started;
        if (result != 0u || elapsed == 0u ||
            header->producer_position != ASTRA_HOSTBENCH_ITERATIONS ||
            header->consumer_position != ASTRA_HOSTBENCH_ITERATIONS ||
            header->transport_status != ASTRA_SYSCALL_OK)
            return fail(bootstrap, 0x10u + result);
        if (astra_host_channel_close(device->handle) != ASTRA_SYSCALL_OK)
            return fail(bootstrap, 6u);
        report(depth, elapsed);
        if (depth == maximum)
            break;
    }
    for (uint32_t polls = 1u; polls <= HOSTBENCH_MAX_POLLS; polls <<= 1) {
        volatile AstraHostChannelHeader *header =
            (volatile AstraHostChannelHeader *)(void *)bytes;
        AstraHostChannelOpen channel = {0};
        uint64_t elapsed;
        uint32_t misses;

        channel.size = sizeof(channel);
        channel.buffer = dma.handle;
        channel.byte_size = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                            ASTRA_HOST_COMMAND_SIZE + HOSTBENCH_DATA_BYTES;
        channel.command_capacity = 1u;
        if (astra_host_channel_open(device->handle, &channel) !=
                ASTRA_SYSCALL_OK ||
            !run_adaptive(header, &commands[0], channel.channel_address,
                          polls, &misses, &elapsed) ||
            astra_host_channel_close(device->handle) != ASTRA_SYSCALL_OK)
            return fail(bootstrap, 8u);
        report_adaptive(polls, misses, elapsed);
        if (polls == HOSTBENCH_MAX_POLLS)
            break;
    }
    (void)astra_close(dma.handle);
    (void)astra_log("ASTRA RAW USER PASS");
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL,
                                  0u);
        (void)astra_close(bootstrap->handle);
    }
    return ASTRA_STATUS_OK;
}
