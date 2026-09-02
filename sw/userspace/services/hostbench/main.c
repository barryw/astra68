#include <astra/host.h>
#include <astra/filesystem_library.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/service.h>
#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_host_direct.h>
#include <astra/vfs_host_transport.h>
#include <astra/vfs_path.h>

#include <stdint.h>
#include <string.h>

ASTRA_PROGRAM("hostbench", 1, 0, 0, "Barry Walker",
              "Copyright 2026 Barry Walker");

#ifndef ASTRA_HOSTBENCH_ITERATIONS
#define ASTRA_HOSTBENCH_ITERATIONS UINT32_C(65536)
#endif

#define HOSTBENCH_DATA_BYTES 64u
#define HOSTBENCH_LAYER_ITERATIONS UINT32_C(2048)
#define HOSTBENCH_PAIR_BATCH UINT32_C(4)
#define HOSTBENCH_ADAPTIVE_ITERATIONS UINT32_C(2048)
#define HOSTBENCH_MAX_POLLS UINT32_C(65536)
#define HOSTBENCH_MEMORY_BYTES UINT32_C(16384)
#define HOSTBENCH_MEMORY_ITERATIONS UINT32_C(64)
#define HOSTBENCH_WRITE_BYTES ASTRA_VFS_IO_MAX

static AstraVfsHostTransport shared_transport;
static AstraVfsHostBackend shared_backend;
static AstraVfsClient shared_direct_client;
static AstraAssignTable shared_assigns;
static AstraFilesystem shared_filesystem = ASTRA_FILESYSTEM_INIT;
static uint32_t filesystem_client_calls;

extern const AstraFilesystemLibraryV2 astra_library_exports;
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

static uint32_t append(char *out, uint32_t at, const char *text)
{
    while (*text != '\0')
        out[at++] = *text++;
    return at;
}

static uint32_t append_hex32(char *out, uint32_t at, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4)
        out[at++] = digits[(value >> shift) & 0xfu];
    return at;
}

static uint32_t append_hex64(char *out, uint32_t at, uint64_t value)
{
    at = append_hex32(out, at, (uint32_t)(value >> 32));
    return append_hex32(out, at, (uint32_t)value);
}

static void report(uint32_t depth, uint64_t elapsed)
{
    char line[112];
    uint32_t at = 0u;

    at = append(line, at, "RAW USER depth=");
    at = append_hex32(line, at, depth);
    at = append(line, at, " iterations=");
    at = append_hex32(line, at, ASTRA_HOSTBENCH_ITERATIONS);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_layer(const char *name, uint64_t elapsed)
{
    char line[112];
    uint32_t at = 0u;

    at = append(line, at, "LAYER name=");
    at = append(line, at, name);
    at = append(line, at, " iterations=");
    at = append_hex32(line, at, HOSTBENCH_LAYER_ITERATIONS);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_adaptive(uint32_t polls, uint32_t misses,
                            uint64_t elapsed)
{
    char line[128];
    uint32_t at = 0u;

    at = append(line, at, "ADAPT polls=");
    at = append_hex32(line, at, polls);
    at = append(line, at, " iterations=");
    at = append_hex32(line, at, HOSTBENCH_ADAPTIVE_ITERATIONS);
    at = append(line, at, " misses=");
    at = append_hex32(line, at, misses);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
}

static void report_memory(const char *name, uint64_t bytes, uint64_t elapsed)
{
    char line[112];
    uint32_t at = 0u;

    at = append(line, at, "MEM name=");
    at = append(line, at, name);
    at = append(line, at, " bytes=");
    at = append_hex64(line, at, bytes);
    at = append(line, at, " elapsed-ns=");
    at = append_hex64(line, at, elapsed);
    line[at] = '\0';
    (void)astra_log(line);
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
    char line[40];
    uint32_t at = append(line, 0u, "ASTRA RAW USER FAIL code=");

    at = append_hex32(line, at, code);
    line[at] = '\0';
    (void)astra_log(line);
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

static uint32_t transport_loop(uint16_t operation, uint32_t expected,
                               uint64_t *elapsed)
{
    AstraHostCommand command = {0};
    uint64_t started;

    command.size = sizeof(command);
    command.version = ASTRA_HOST_COMMAND_VERSION;
    command.service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command.operation = operation;
    command.generation = shared_transport.generation;
    if (operation == ASTRA_HOST_FS_STAT) {
        command.path[0] = '/';
        command.path[1] = '\0';
    }
    if (astra_vfs_host_transport_execute(
            &shared_transport, &command, NULL, 0u, NULL, 0u) != expected)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_vfs_host_transport_execute(
                &shared_transport, &command, NULL, 0u, NULL, 0u) != expected)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t transport_invalid_write_loop(uint64_t *elapsed)
{
    AstraHostCommand command = {0};
    uint64_t started;

    command.size = sizeof(command);
    command.version = ASTRA_HOST_COMMAND_VERSION;
    command.service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command.operation = ASTRA_HOST_FS_WRITE;
    command.generation = shared_transport.generation;
    command.handle = UINT32_MAX;
    if (astra_vfs_host_transport_execute(
            &shared_transport, &command, memory_source,
            HOSTBENCH_WRITE_BYTES, NULL, 0u) != ASTRA_VFS_ERR_BAD_HANDLE)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_vfs_host_transport_execute(
                &shared_transport, &command, memory_source,
                HOSTBENCH_WRITE_BYTES, NULL, 0u) !=
            ASTRA_VFS_ERR_BAD_HANDLE)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t backend_loop(uint64_t *elapsed)
{
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    AstraVfsNodeInfo info;
    uint64_t started;

    if (ops->stat(&shared_backend, "/", &info) != ASTRA_VFS_OK)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (ops->stat(&shared_backend, "/", &info) != ASTRA_VFS_OK)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t backend_write_loop(uint64_t *elapsed)
{
    const AstraVfsBackendOps *ops = astra_vfs_host_ops();
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE;
    AstraVfsNodeInfo info;
    uintptr_t node;
    uint64_t started;

    if (ops->open(&shared_backend, "/hostbench", flags,
                  ASTRA_VFS_MODE_DEFAULT, &node, &info) != ASTRA_VFS_OK)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index) {
        uint64_t position = 0u;
        uint32_t moved = 0u;

        if (ops->write(&shared_backend, node, 0u, 0u, memory_source,
                       HOSTBENCH_WRITE_BYTES, &moved, &position) !=
                ASTRA_VFS_OK ||
            moved != HOSTBENCH_WRITE_BYTES ||
            position != HOSTBENCH_WRITE_BYTES)
            return 0u;
    }
    *elapsed = astra_clock_monotonic() - started;
    return ops->close(&shared_backend, node) == ASTRA_VFS_OK &&
           *elapsed != 0u;
}

static uint32_t backend_sequence_batch(uint32_t iterations, uint32_t flags,
                                       uint32_t write_data,
                                       uint32_t sync_data,
                                       uint64_t *elapsed)
{
    const AstraVfsBackendOps *ops = shared_direct_client.direct_backend_ops;
    void *context = shared_direct_client.direct_backend_context;
    AstraVfsNodeInfo info;
    uintptr_t node;
    uint64_t started;

    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < iterations; ++index) {
        uint64_t position = 0u;
        uint32_t moved = 0u;

        if (ops->open(context, "/hostbench", flags,
                      ASTRA_VFS_MODE_DEFAULT, &node, &info) != ASTRA_VFS_OK)
            return 0u;
        if (write_data != 0u &&
            (ops->write(context, node, 0u, 0u, memory_source,
                        HOSTBENCH_WRITE_BYTES, &moved, &position) !=
                 ASTRA_VFS_OK ||
             moved != HOSTBENCH_WRITE_BYTES ||
             position != HOSTBENCH_WRITE_BYTES))
            return 0u;
        if (sync_data != 0u && ops->sync(context, node) != ASTRA_VFS_OK)
            return 0u;
        if (ops->close(context, node) != ASTRA_VFS_OK)
            return 0u;
    }
    *elapsed += astra_clock_monotonic() - started;
    return 1u;
}

static uint32_t backend_open_batch(uint32_t iterations, uint64_t *elapsed)
{
    return backend_sequence_batch(
        iterations, ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE, 0u, 0u,
        elapsed);
}

static uint32_t direct_vfs_loop(uint64_t *elapsed)
{
    uint64_t size;
    uint16_t kind;
    uint64_t started;

    if (astra_vfs_stat(&shared_direct_client, "/", &size, &kind) !=
        ASTRA_VFS_OK)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_vfs_stat(&shared_direct_client, "/", &size, &kind) !=
            ASTRA_VFS_OK)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t direct_vfs_open_batch(uint32_t iterations, uint64_t *elapsed)
{
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    AstraVfsFile file;
    uint64_t size;
    uint16_t kind;
    uint64_t started;

    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < iterations; ++index) {
        if (astra_vfs_open_mode(&shared_direct_client, "/hostbench", flags,
                                ASTRA_VFS_MODE_DEFAULT, &file, &size, &kind) !=
                ASTRA_VFS_OK ||
            astra_vfs_close(&shared_direct_client, file) != ASTRA_VFS_OK)
            return 0u;
    }
    *elapsed += astra_clock_monotonic() - started;
    return 1u;
}

static AstraVfsClient *filesystem_client(const AstraAssign *assign,
                                        void *context)
{
    (void)assign;
    ++filesystem_client_calls;
    return context;
}

static uint32_t filesystem_loop(uint64_t *elapsed)
{
    AstraFileInfo info = ASTRA_FILE_INFO_INIT;
    uint64_t started;

    if (astra_library_exports.stat(&shared_filesystem, "WORK:", &info) !=
            ASTRA_VFS_OK)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_library_exports.stat(&shared_filesystem, "WORK:", &info) !=
                ASTRA_VFS_OK)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t path_normalise_loop(uint64_t *elapsed)
{
    char wire[ASTRA_VFS_PATH_MAX];
    uint64_t started;

    if (astra_path_normalise("hostbench", wire, sizeof(wire)) != ASTRA_VFS_OK)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_path_normalise("hostbench", wire, sizeof(wire)) !=
                ASTRA_VFS_OK)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t assign_member_loop(uint32_t member, uint32_t expect_found,
                                   uint64_t *elapsed)
{
    const AstraAssign *assign;
    uint64_t started;

    assign = astra_assign_member(&shared_assigns, "WORK", member);
    if ((assign != NULL) != expect_found)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index) {
        assign = astra_assign_member(&shared_assigns, "WORK", member);
        if ((assign != NULL) != expect_found)
            return 0u;
    }
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t assign_resolve_loop(uint64_t *elapsed)
{
    const AstraAssign *assign;
    char wire[ASTRA_VFS_PATH_MAX];
    uint64_t started;

    if (astra_assign_resolve(&shared_assigns, "WORK:hostbench",
                             ASTRA_RIGHT_READ, 0u, wire, sizeof(wire),
                             &assign) != ASTRA_VFS_OK || assign == NULL)
        return 0u;
    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < HOSTBENCH_LAYER_ITERATIONS; ++index)
        if (astra_assign_resolve(&shared_assigns, "WORK:hostbench",
                                 ASTRA_RIGHT_READ, 0u, wire, sizeof(wire),
                                 &assign) != ASTRA_VFS_OK || assign == NULL)
            return 0u;
    *elapsed = astra_clock_monotonic() - started;
    return *elapsed != 0u;
}

static uint32_t filesystem_open_batch(uint32_t iterations, uint64_t *elapsed)
{
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    AstraFile file = ASTRA_FILE_INIT;
    uint64_t started;

    started = astra_clock_monotonic();
    for (uint32_t index = 0u; index < iterations; ++index) {
        if (astra_library_exports.open_mode(
                &shared_filesystem, "WORK:hostbench", flags,
                ASTRA_VFS_MODE_DEFAULT, &file) != ASTRA_VFS_OK ||
            astra_library_exports.close(&file) != ASTRA_VFS_OK)
            return 0u;
    }
    *elapsed += astra_clock_monotonic() - started;
    return 1u;
}

static uint32_t paired_open_loops(uint64_t *backend_elapsed,
                                  uint64_t *direct_elapsed,
                                  uint64_t *filesystem_elapsed)
{
    const AstraVfsBackendOps *ops = shared_direct_client.direct_backend_ops;
    void *context = shared_direct_client.direct_backend_context;
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE;
    AstraVfsNodeInfo info;
    uintptr_t node;
    AstraVfsFile local_file;
    AstraFile filesystem_file = ASTRA_FILE_INIT;
    uint64_t size;
    uint16_t kind;

    filesystem_client_calls = 0u;
    if (ops->open(context, "/hostbench", flags,
                  ASTRA_VFS_MODE_DEFAULT, &node, &info) != ASTRA_VFS_OK ||
        ops->close(context, node) != ASTRA_VFS_OK ||
        astra_vfs_open_mode(&shared_direct_client, "/hostbench", flags,
                            ASTRA_VFS_MODE_DEFAULT, &local_file, &size,
                            &kind) != ASTRA_VFS_OK ||
        astra_vfs_close(&shared_direct_client, local_file) != ASTRA_VFS_OK ||
        astra_library_exports.open_mode(
            &shared_filesystem, "WORK:hostbench", flags,
            ASTRA_VFS_MODE_DEFAULT, &filesystem_file) != ASTRA_VFS_OK ||
        astra_library_exports.close(&filesystem_file) != ASTRA_VFS_OK)
        return 0u;
    *backend_elapsed = 0u;
    *direct_elapsed = 0u;
    *filesystem_elapsed = 0u;
    for (uint32_t at = 0u; at < HOSTBENCH_LAYER_ITERATIONS;
         at += HOSTBENCH_PAIR_BATCH) {
        uint32_t order = (at / HOSTBENCH_PAIR_BATCH) % 3u;

        if (order == 0u) {
            if (!backend_open_batch(HOSTBENCH_PAIR_BATCH, backend_elapsed) ||
                !direct_vfs_open_batch(HOSTBENCH_PAIR_BATCH, direct_elapsed) ||
                !filesystem_open_batch(HOSTBENCH_PAIR_BATCH,
                                       filesystem_elapsed))
                return 0u;
        } else if (order == 1u) {
            if (!direct_vfs_open_batch(HOSTBENCH_PAIR_BATCH, direct_elapsed) ||
                !filesystem_open_batch(HOSTBENCH_PAIR_BATCH,
                                       filesystem_elapsed) ||
                !backend_open_batch(HOSTBENCH_PAIR_BATCH, backend_elapsed))
                return 0u;
        } else {
            if (!filesystem_open_batch(HOSTBENCH_PAIR_BATCH,
                                       filesystem_elapsed) ||
                !backend_open_batch(HOSTBENCH_PAIR_BATCH, backend_elapsed) ||
                !direct_vfs_open_batch(HOSTBENCH_PAIR_BATCH, direct_elapsed))
                return 0u;
        }
    }
    return *backend_elapsed != 0u && *direct_elapsed != 0u &&
           *filesystem_elapsed != 0u &&
           filesystem_client_calls == HOSTBENCH_LAYER_ITERATIONS + 1u;
}

static uint32_t backend_write_batch(uint32_t iterations, uint32_t sync_data,
                                    uint64_t *elapsed)
{
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE;

    return backend_sequence_batch(iterations, flags, 1u, sync_data, elapsed);
}

static uint32_t direct_vfs_write_batch(uint32_t iterations,
                                       uint32_t sync_data,
                                       uint64_t *elapsed)
{
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE;
    uint64_t started = astra_clock_monotonic();

    for (uint32_t index = 0u; index < iterations; ++index) {
        AstraVfsFile file;
        uint64_t size;
        uint16_t kind;
        uint32_t moved = 0u;

        if (astra_vfs_open_mode(&shared_direct_client, "/hostbench", flags,
                                ASTRA_VFS_MODE_DEFAULT, &file, &size, &kind) !=
                ASTRA_VFS_OK ||
            astra_vfs_write(&shared_direct_client, file, 0u, memory_source,
                            HOSTBENCH_WRITE_BYTES, &moved) != ASTRA_VFS_OK ||
            moved != HOSTBENCH_WRITE_BYTES ||
            (sync_data != 0u &&
             astra_vfs_sync(&shared_direct_client, file) != ASTRA_VFS_OK) ||
            astra_vfs_close(&shared_direct_client, file) != ASTRA_VFS_OK)
            return 0u;
    }
    *elapsed += astra_clock_monotonic() - started;
    return 1u;
}

static uint32_t filesystem_write_batch(uint32_t iterations,
                                       uint32_t sync_data,
                                       uint64_t *elapsed)
{
    const uint32_t flags = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE |
                           ASTRA_VFS_OPEN_TRUNCATE;
    uint64_t started = astra_clock_monotonic();

    for (uint32_t index = 0u; index < iterations; ++index) {
        AstraFile file = ASTRA_FILE_INIT;
        uint32_t moved = 0u;

        if (astra_library_exports.open_mode(
                &shared_filesystem, "WORK:hostbench", flags,
                ASTRA_VFS_MODE_DEFAULT, &file) != ASTRA_VFS_OK ||
            astra_library_exports.write(&file, memory_source,
                                        HOSTBENCH_WRITE_BYTES, &moved) !=
                ASTRA_VFS_OK ||
            moved != HOSTBENCH_WRITE_BYTES ||
            (sync_data != 0u &&
             astra_library_exports.sync(&file) != ASTRA_VFS_OK) ||
            astra_library_exports.close(&file) != ASTRA_VFS_OK)
            return 0u;
    }
    *elapsed += astra_clock_monotonic() - started;
    return 1u;
}

static uint32_t paired_write_loops(uint32_t sync_data,
                                   uint64_t *backend_elapsed,
                                   uint64_t *direct_elapsed,
                                   uint64_t *filesystem_elapsed)
{
    *backend_elapsed = 0u;
    *direct_elapsed = 0u;
    *filesystem_elapsed = 0u;
    for (uint32_t at = 0u; at < HOSTBENCH_LAYER_ITERATIONS;
         at += HOSTBENCH_PAIR_BATCH) {
        uint32_t order = (at / HOSTBENCH_PAIR_BATCH) % 3u;

        if (order == 0u) {
            if (!backend_write_batch(HOSTBENCH_PAIR_BATCH, sync_data,
                                     backend_elapsed) ||
                !direct_vfs_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        direct_elapsed) ||
                !filesystem_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        filesystem_elapsed))
                return 0u;
        } else if (order == 1u) {
            if (!direct_vfs_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        direct_elapsed) ||
                !filesystem_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        filesystem_elapsed) ||
                !backend_write_batch(HOSTBENCH_PAIR_BATCH, sync_data,
                                     backend_elapsed))
                return 0u;
        } else {
            if (!filesystem_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        filesystem_elapsed) ||
                !backend_write_batch(HOSTBENCH_PAIR_BATCH, sync_data,
                                     backend_elapsed) ||
                !direct_vfs_write_batch(HOSTBENCH_PAIR_BATCH,
                                        sync_data,
                                        direct_elapsed))
                return 0u;
        }
    }
    return *backend_elapsed != 0u && *direct_elapsed != 0u &&
           *filesystem_elapsed != 0u;
}

static uint32_t run_layers(uint32_t device)
{
    const AstraVfsBackendOps *ops;
    AstraVfsNodeInfo info;
    uintptr_t node;
    uint64_t elapsed;
    uint64_t paired_elapsed;
    uint64_t open_elapsed;

    if (!astra_vfs_host_transport_init(&shared_transport, device, NULL, NULL,
                                       NULL) ||
        !astra_vfs_host_init(&shared_backend,
                             &shared_transport,
                             shared_transport.generation))
        return 0u;
    ops = astra_vfs_host_ops();
    if (ops->open(&shared_backend, "/hostbench",
                  ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                      ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE,
                  ASTRA_VFS_MODE_DEFAULT, &node, &info) != ASTRA_VFS_OK ||
        ops->close(&shared_backend, node) != ASTRA_VFS_OK)
        return 0u;
    if (!transport_loop(0u, ASTRA_VFS_ERR_UNSUPPORTED, &elapsed))
        return 0u;
    report_layer("transport-unsupported", elapsed);
    if (!transport_loop(ASTRA_HOST_FS_STAT, ASTRA_VFS_OK, &elapsed))
        return 0u;
    report_layer("transport-stat", elapsed);
    if (!backend_loop(&elapsed))
        return 0u;
    report_layer("backend-stat", elapsed);
    if (!transport_invalid_write_loop(&elapsed))
        return 0u;
    report_layer("transport-write192-invalid-handle", elapsed);
    if (!backend_write_loop(&elapsed))
        return 0u;
    report_layer("backend-write192-open-handle", elapsed);
    astra_vfs_host_transport_destroy(&shared_transport);
    (void)memset(&shared_direct_client, 0, sizeof(shared_direct_client));
    shared_direct_client.session = 1u;
    shared_direct_client.version = ASTRA_VFS_VERSION;
    if (astra_vfs_host_direct_connect(&shared_direct_client, device) !=
            ASTRA_VFS_OK ||
        !direct_vfs_loop(&elapsed))
        return 0u;
    report_layer("direct-vfs-stat", elapsed);
    astra_assign_table_init(&shared_assigns);
    if (astra_assign_bind(&shared_assigns, "WORK", 1u,
                          ASTRA_RIGHT_READ | ASTRA_RIGHT_WRITE, "") !=
            ASTRA_VFS_OK ||
        astra_library_exports.attach(&shared_filesystem, &shared_assigns,
                                     filesystem_client, NULL,
                                     &shared_direct_client) !=
            ASTRA_VFS_OK)
        return 0u;
    if (!path_normalise_loop(&elapsed))
        return 0u;
    report_layer("path-normalise", elapsed);
    if (!assign_member_loop(0u, 1u, &elapsed))
        return 0u;
    report_layer("assign-member-hit", elapsed);
    if (!assign_member_loop(1u, 0u, &elapsed))
        return 0u;
    report_layer("assign-member-miss", elapsed);
    if (!assign_resolve_loop(&elapsed))
        return 0u;
    report_layer("assign-resolve", elapsed);
    if (!filesystem_loop(&elapsed))
        return 0u;
    report_layer("filesystem-stat", elapsed);
    if (!paired_open_loops(&elapsed, &paired_elapsed, &open_elapsed))
        return 0u;
    report_layer("direct-backend-open-close-paired", elapsed);
    report_layer("direct-vfs-open-close-paired", paired_elapsed);
    report_layer("filesystem-direct-open-close-paired", open_elapsed);
    elapsed = 0u;
    if (!backend_sequence_batch(
            HOSTBENCH_LAYER_ITERATIONS,
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                ASTRA_VFS_OPEN_CREATE,
            1u, 0u, &elapsed))
        return 0u;
    report_layer("direct-backend-open-write192-close-no-truncate", elapsed);
    elapsed = 0u;
    if (!backend_sequence_batch(
            HOSTBENCH_LAYER_ITERATIONS,
            ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE,
            0u, 0u, &elapsed))
        return 0u;
    report_layer("direct-backend-open-truncate-close", elapsed);
    if (!paired_write_loops(0u, &elapsed, &paired_elapsed, &open_elapsed))
        return 0u;
    report_layer("direct-backend-open-write192-close-paired", elapsed);
    report_layer("direct-vfs-open-write192-close-paired", paired_elapsed);
    report_layer("filesystem-direct-open-write192-close-paired", open_elapsed);
    if (!paired_write_loops(1u, &elapsed, &paired_elapsed, &open_elapsed))
        return 0u;
    report_layer("direct-backend-open-write192-sync-close", elapsed);
    report_layer("direct-vfs-open-write192-sync-close", paired_elapsed);
    report_layer("filesystem-direct-open-write192-sync-close", open_elapsed);
    astra_library_exports.detach(&shared_filesystem);
    astra_vfs_host_direct_disconnect(&shared_direct_client);
    return 1u;
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
    if (!run_layers(device->handle))
        return fail(bootstrap, 7u);
    (void)astra_log("ASTRA RAW USER PASS");
    if (bootstrap != NULL) {
        (void)astra_service_ready(bootstrap->handle, ASTRA_STATUS_OK, NULL,
                                  0u);
        (void)astra_close(bootstrap->handle);
    }
    return ASTRA_STATUS_OK;
}
