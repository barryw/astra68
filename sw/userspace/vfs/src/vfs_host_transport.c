#include <astra/vfs_host_transport.h>

#include <astra/runtime.h>

#include <stddef.h>
#include <string.h>

typedef struct AstraVfsHostLaneCache {
    AstraVfsHostTransport *transport;
    AstraVfsHostLane *lane;
} AstraVfsHostLaneCache;

static _Thread_local AstraVfsHostLaneCache lane_cache;

static int channel_pending(const volatile AstraHostChannelHeader *header,
                           uint32_t producer)
{
    return (int32_t)(header->consumer_position - producer) < 0;
}

static uint32_t publish_path(char destination[ASTRA_HOST_FS_PATH_MAX],
                             const char source[ASTRA_HOST_FS_PATH_MAX])
{
    uint32_t length = 0u;

    while (length < ASTRA_HOST_FS_PATH_MAX && source[length] != '\0') {
        destination[length] = source[length];
        ++length;
    }
    if (length == ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_VFS_ERR_INVALID;
    destination[length] = '\0';
    return ASTRA_VFS_OK;
}

static uint32_t publish_command(AstraHostCommand *destination,
                                const AstraHostCommand *source)
{
    memcpy(destination, source, offsetof(AstraHostCommand, status));
    memcpy(&destination->handle, &source->handle,
           offsetof(AstraHostCommand, data_offset) -
               offsetof(AstraHostCommand, handle));
    switch (source->operation) {
    case ASTRA_HOST_FS_OPEN:
    case ASTRA_HOST_FS_STAT:
    case ASTRA_HOST_FS_READDIR:
    case ASTRA_HOST_FS_MKDIR:
    case ASTRA_HOST_FS_UNLINK:
    case ASTRA_HOST_FS_CHMOD:
    case ASTRA_HOST_FS_READLINK:
        return publish_path(destination->path, source->path);
    case ASTRA_HOST_FS_RENAME:
    case ASTRA_HOST_FS_SYMLINK:
        if (publish_path(destination->path, source->path) != ASTRA_VFS_OK)
            return ASTRA_VFS_ERR_INVALID;
        return publish_path(destination->path2, source->path2);
    default:
        return ASTRA_VFS_OK;
    }
}

static void collect_command(AstraHostCommand *destination,
                            const AstraHostCommand *source)
{
    destination->status = source->status;
    destination->handle = source->handle;
    destination->value_hi = source->value_hi;
    destination->value_lo = source->value_lo;
    destination->result_length = source->result_length;
    destination->result_value = source->result_value;
    destination->node_size_hi = source->node_size_hi;
    destination->node_size_lo = source->node_size_lo;
    destination->mtime_hi = source->mtime_hi;
    destination->mtime_lo = source->mtime_lo;
    destination->uid = source->uid;
    destination->gid = source->gid;
    destination->kind = source->kind;
    destination->mode = source->mode;
    destination->nlink = source->nlink;
}

uint32_t astra_vfs_host_status_from_syscall(uint32_t status)
{
    switch (status) {
    case ASTRA_SYSCALL_OK: return ASTRA_VFS_OK;
    case ASTRA_SYSCALL_INVALID_ARGUMENT:
    case ASTRA_SYSCALL_BAD_ADDRESS: return ASTRA_VFS_ERR_INVALID;
    case ASTRA_SYSCALL_INVALID_HANDLE: return ASTRA_VFS_ERR_BAD_HANDLE;
    case ASTRA_SYSCALL_ACCESS_DENIED: return ASTRA_VFS_ERR_ACCESS;
    case ASTRA_SYSCALL_RESOURCE_LIMIT:
    case ASTRA_SYSCALL_OUT_OF_MEMORY: return ASTRA_VFS_ERR_LIMIT;
    case ASTRA_SYSCALL_WOULD_BLOCK: return ASTRA_VFS_ERR_BUSY;
    case ASTRA_SYSCALL_PEER_DEAD:
    case ASTRA_SYSCALL_UNSUPPORTED: return ASTRA_VFS_ERR_PEER;
    default: return ASTRA_VFS_ERR_IO;
    }
}

int astra_vfs_host_transport_init(AstraVfsHostTransport *transport,
                                  uint32_t device,
                                  AstraVfsStateAcquire acquire,
                                  AstraVfsStateRelease release,
                                  void *lock_context)
{
    AstraHostLeaseInfo lease;

    if (transport == NULL || device == 0u ||
        ((acquire == NULL) != (release == NULL)))
        return 0;
    memset(&lease, 0, sizeof(lease));
    if (astra_host_lease_query(device, &lease) != ASTRA_SYSCALL_OK ||
        (lease.capabilities & (ASTRA_HOST_CAP_FILESYSTEM |
                               ASTRA_HOST_CAP_OWNER_SCOPED)) !=
            (ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED) ||
        (lease.state_flags & ASTRA_HOST_STATE_READY) == 0u ||
        lease.host_generation == 0u ||
        lease.maximum_transfer < ASTRA_HOST_COMMAND_SIZE ||
        lease.maximum_commands == 0u)
        return 0;
    memset(transport, 0, sizeof(*transport));
    transport->device = device;
    transport->maximum_transfer = lease.maximum_transfer;
    transport->maximum_commands = lease.maximum_commands;
    transport->generation = lease.host_generation;
    transport->channel_supported =
        (lease.capabilities & (ASTRA_HOST_CAP_CHANNEL |
                               ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ)) ==
        (ASTRA_HOST_CAP_CHANNEL | ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ);
    transport->acquire = acquire;
    transport->release = release;
    transport->lock_context = lock_context;
    return 1;
}

void astra_vfs_host_transport_destroy(AstraVfsHostTransport *transport)
{
    if (transport == NULL)
        return;
    if (lane_cache.transport == transport)
        memset(&lane_cache, 0, sizeof(lane_cache));
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index)
        if (transport->lanes[index].dma != 0u)
            (void)astra_close(transport->lanes[index].dma);
    memset(transport, 0, sizeof(*transport));
}

static uint32_t lane_open(AstraVfsHostTransport *transport,
                          AstraVfsHostLane *lane, uint32_t thread)
{
    AstraHostChannelOpen channel = {0};

    channel.size = sizeof(channel);
    channel.buffer = lane->dma;
    channel.byte_size = lane->byte_size;
    channel.command_capacity = lane->command_capacity;
    if (channel.byte_size > transport->maximum_transfer ||
        astra_host_channel_open(transport->device, &channel) !=
            ASTRA_SYSCALL_OK ||
        channel.channel_generation == 0u || channel.channel_address == 0u ||
        channel.host_generation != transport->generation)
        return ASTRA_VFS_ERR_PEER;
    lane->thread = thread;
    lane->channel_address = channel.channel_address;
    lane->producer_position = 0u;
    lane->active = 1u;
    return ASTRA_VFS_OK;
}

static uint32_t lane_allocate(AstraVfsHostTransport *transport,
                              AstraVfsHostLane *lane, uint32_t thread,
                              uint32_t needed, uint32_t command_capacity)
{
    AstraDmaBufferInfo dma = {0};
    uint32_t status;

    if (astra_dma_create(needed, &dma) != ASTRA_SYSCALL_OK ||
        dma.handle == 0u || dma.virtual_base == 0u || dma.byte_size < needed ||
        dma.byte_size > transport->maximum_transfer) {
        if (dma.handle != 0u)
            (void)astra_close(dma.handle);
        return ASTRA_VFS_ERR_LIMIT;
    }
    memset(lane, 0, sizeof(*lane));
    lane->dma = dma.handle;
    lane->bytes = (uint8_t *)(uintptr_t)dma.virtual_base;
    lane->byte_size = dma.byte_size;
    lane->command_capacity = command_capacity;
    if (transport->channel_supported == 0u) {
        lane->thread = thread;
        lane->active = 1u;
        return ASTRA_VFS_OK;
    }
    status = lane_open(transport, lane, thread);
    if (status != ASTRA_VFS_OK) {
        (void)astra_close(lane->dma);
        memset(lane, 0, sizeof(*lane));
    }
    return status;
}

static int lane_layout(const AstraVfsHostTransport *transport,
                       uint32_t command_capacity, uint32_t data_bytes,
                       uint32_t *needed)
{
    uint32_t header = transport->channel_supported != 0u ?
        ASTRA_HOST_CHANNEL_HEADER_SIZE : 0u;

    if (command_capacity == 0u ||
        command_capacity > (UINT32_MAX - header) / ASTRA_HOST_COMMAND_SIZE)
        return 0;
    *needed = header + command_capacity * ASTRA_HOST_COMMAND_SIZE;
    if (data_bytes > UINT32_MAX - *needed)
        return 0;
    *needed += data_bytes;
    return *needed <= transport->maximum_transfer;
}

static uint32_t lane_resize(AstraVfsHostTransport *transport,
                            AstraVfsHostLane *lane, uint32_t thread,
                            uint32_t data_bytes, uint32_t command_capacity)
{
    uint32_t target_capacity = lane->command_capacity;
    uint32_t needed;

    if (target_capacity < command_capacity ||
        !lane_layout(transport, target_capacity, data_bytes, &needed)) {
        target_capacity = command_capacity;
        if (!lane_layout(transport, target_capacity, data_bytes, &needed))
            return ASTRA_VFS_ERR_LIMIT;
    }
    if (lane->byte_size >= needed)
        return ASTRA_VFS_OK;
    if (transport->channel_supported != 0u)
        (void)astra_host_channel_close(transport->device);
    if (lane->dma != 0u)
        (void)astra_close(lane->dma);
    memset(lane, 0, sizeof(*lane));
    return lane_allocate(transport, lane, thread, needed, target_capacity);
}

static uint32_t ensure_lane(AstraVfsHostTransport *transport,
                            uint32_t thread, uint32_t data_bytes,
                            uint32_t command_capacity,
                            AstraVfsHostLane **result)
{
    AstraVfsHostLane *free_lane = NULL;
    uint32_t needed;

    *result = NULL;
    if (!lane_layout(transport, command_capacity, data_bytes, &needed))
        return ASTRA_VFS_ERR_LIMIT;
    for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
         ++index) {
        AstraVfsHostLane *lane = &transport->lanes[index];

        if (lane->active != 0u && lane->thread == thread) {
            uint32_t status = lane_resize(transport, lane, thread, data_bytes,
                                          command_capacity);

            if (status == ASTRA_VFS_OK)
                *result = lane;
            return status;
        }
        if (lane->active == 0u && free_lane == NULL)
            free_lane = lane;
    }
    if (free_lane != NULL) {
        uint32_t status = lane_allocate(transport, free_lane, thread, needed,
                                        command_capacity);

        if (status == ASTRA_VFS_OK)
            *result = free_lane;
        return status;
    }
    /* A dead thread's kernel channel is already closed; reclaim its DMA lane. */
    if (transport->channel_supported != 0u) {
        for (uint32_t index = 0u; index < ASTRA_PROCESS_THREAD_COUNT_MAX;
             ++index) {
            AstraVfsHostLane *lane = &transport->lanes[index];

            if (lane_open(transport, lane, thread) != ASTRA_VFS_OK)
                continue;
            {
                uint32_t status = lane_resize(transport, lane, thread,
                                              data_bytes,
                                              command_capacity);

                if (status == ASTRA_VFS_OK)
                    *result = lane;
                return status;
            }
        }
    }
    return ASTRA_VFS_ERR_LIMIT;
}

static uint32_t command_capacity(uint32_t count)
{
    uint32_t capacity = 1u;

    while (capacity < count)
        capacity <<= 1;
    return capacity;
}

static int lane_has_capacity(const AstraVfsHostLane *lane,
                             uint32_t data_capacity)
{
    uint32_t data_offset;

    if (lane == NULL || lane->active == 0u ||
        lane->command_capacity == 0u)
        return 0;
    data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                  lane->command_capacity * ASTRA_HOST_COMMAND_SIZE;
    return data_offset <= lane->byte_size &&
           data_capacity <= lane->byte_size - data_offset;
}

static uint32_t __attribute__((noinline, cold))
lane_get_slow(AstraVfsHostTransport *transport, uint32_t data_capacity,
              AstraVfsHostLane **result)
{
    AstraVfsHostLane *lane = NULL;
    uint32_t thread;
    uint32_t status;

    if (astra_current_thread_handle(&thread) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_PEER;
    if (transport->acquire != NULL &&
        !transport->acquire(transport->lock_context))
        return ASTRA_VFS_ERR_IO;
    status = ensure_lane(transport, thread, data_capacity, 1u, &lane);
    if (transport->release != NULL)
        transport->release(transport->lock_context);
    if (status != ASTRA_VFS_OK)
        return status;
    lane_cache.transport = transport;
    lane_cache.lane = lane;
    *result = lane;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_transport_begin(void *context,
                                        uint32_t data_capacity,
                                        AstraVfsHostRequest *request)
{
    AstraVfsHostTransport *transport = context;
    AstraVfsHostLane *lane = NULL;
    uint32_t status;

    if (transport == NULL || request == NULL ||
        transport->channel_supported == 0u)
        return ASTRA_VFS_ERR_PEER;
    request->command = NULL;
    request->private_lane = 0u;
    request->private_capacity = 0u;
    request->private_producer = 0u;
    if (lane_cache.transport == transport &&
        lane_has_capacity(lane_cache.lane, data_capacity)) {
        lane = lane_cache.lane;
    } else {
        status = lane_get_slow(transport, data_capacity, &lane);
        if (status != ASTRA_VFS_OK)
            return status;
    }
    request->command = (AstraHostCommand *)(void *)(
        lane->bytes + ASTRA_HOST_CHANNEL_HEADER_SIZE +
        (lane->producer_position & (lane->command_capacity - 1u)) *
            ASTRA_HOST_COMMAND_SIZE);
    request->private_lane = (uintptr_t)lane;
    request->private_capacity = lane->command_capacity;
    request->private_producer = lane->producer_position + 1u;
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_transport_submit(
    void *context, AstraVfsHostRequest *request, const void *input,
    uint32_t input_size, void *output, uint32_t output_capacity)
{
    AstraVfsHostTransport *transport = context;
    AstraVfsHostLane *lane;
    volatile AstraHostChannelHeader *header;
    AstraHostCommand *command;
    uint32_t data_capacity = input_size > output_capacity ?
        input_size : output_capacity;
    uint32_t data_offset;
    uint32_t status;

    if (transport == NULL || request == NULL || request->command == NULL ||
        request->private_lane < (uintptr_t)&transport->lanes[0] ||
        request->private_lane >
            (uintptr_t)&transport->lanes[ASTRA_PROCESS_THREAD_COUNT_MAX - 1u] ||
        (input_size != 0u && input == NULL) ||
        (output_capacity != 0u && output == NULL))
        return ASTRA_VFS_ERR_INVALID;
    lane = (AstraVfsHostLane *)request->private_lane;
    if (lane->active == 0u || lane->command_capacity !=
                                  request->private_capacity ||
        request->private_producer != lane->producer_position + 1u)
        return ASTRA_VFS_ERR_INVALID;
    data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                  lane->command_capacity * ASTRA_HOST_COMMAND_SIZE;
    if (data_offset > lane->byte_size ||
        data_capacity > lane->byte_size - data_offset)
        return ASTRA_VFS_ERR_LIMIT;
    command = request->command;
    command->data_offset = data_capacity != 0u ? data_offset : 0u;
    command->data_length = input_size;
    command->data_capacity = output_capacity;
    if (input_size != 0u)
        memcpy(lane->bytes + data_offset, input, input_size);
    header = (volatile AstraHostChannelHeader *)(void *)lane->bytes;
    astra_memory_release_fence();
    header->producer_position = request->private_producer;
    status = astra_host_channel_kick(lane->channel_address,
                                     request->private_producer);
    if (status == ASTRA_SYSCALL_OK) {
        astra_compiler_barrier();
        if (channel_pending(header, request->private_producer))
            status = astra_host_channel_wait(
                request->private_producer,
                UINT64_C(0x7fffffffffffffff));
    }
    astra_memory_acquire_fence();
    if (status == ASTRA_SYSCALL_OK &&
        channel_pending(header, request->private_producer))
        status = ASTRA_SYSCALL_IO_ERROR;
    if (status == ASTRA_SYSCALL_OK)
        status = header->transport_status;
    if (status != ASTRA_SYSCALL_OK)
        return astra_vfs_host_status_from_syscall(status);
    lane->producer_position = request->private_producer;
    if (command->status != ASTRA_VFS_OK)
        return command->status;
    if (output_capacity != 0u &&
        command->result_length > output_capacity)
        return ASTRA_VFS_ERR_IO;
    if (output_capacity != 0u && command->result_length != 0u)
        memcpy(output, lane->bytes + data_offset, command->result_length);
    return ASTRA_VFS_OK;
}

uint32_t astra_vfs_host_transport_execute_batch(
    AstraVfsHostTransport *transport, AstraVfsHostTransfer *transfers,
    uint32_t count)
{
    AstraVfsHostLane *lane = NULL;
    AstraHostTransportRequest request = {0};
    uint32_t capacity;
    uint32_t command_bytes;
    uint32_t data_bytes = 0u;
    uint32_t data_offset;
    uint32_t total;
    uint32_t executed = 0u;
    uint32_t status = ASTRA_VFS_OK;
    uint32_t thread = 0u;
    int locked = 0;

    if (transport == NULL || transfers == NULL || count == 0u ||
        count > transport->maximum_commands)
        return ASTRA_VFS_ERR_INVALID;
    capacity = command_capacity(count);
    if (capacity < count || capacity > transport->maximum_commands)
        return ASTRA_VFS_ERR_LIMIT;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t data_size;

        if (transfers[index].command == NULL ||
            (transfers[index].input_size != 0u &&
             transfers[index].input == NULL) ||
            (transfers[index].output_capacity != 0u &&
             transfers[index].output == NULL))
            return ASTRA_VFS_ERR_INVALID;
        data_size = transfers[index].input_size >
                            transfers[index].output_capacity ?
                        transfers[index].input_size :
                        transfers[index].output_capacity;
        if (data_size > UINT32_MAX - data_bytes)
            return ASTRA_VFS_ERR_LIMIT;
        data_bytes += data_size;
    }
    if (!lane_layout(transport, capacity, data_bytes, &total))
        return ASTRA_VFS_ERR_LIMIT;
    if (transport->channel_supported != 0u &&
        astra_current_thread_handle(&thread) != ASTRA_SYSCALL_OK)
        return ASTRA_VFS_ERR_PEER;
    if (transport->acquire != NULL) {
        if (!transport->acquire(transport->lock_context))
            return ASTRA_VFS_ERR_IO;
        locked = 1;
    }
    status = ensure_lane(transport, thread, data_bytes, capacity, &lane);
    if (status != ASTRA_VFS_OK)
        goto done;
    if (transport->channel_supported != 0u && locked) {
        transport->release(transport->lock_context);
        locked = 0;
    }
    capacity = lane->command_capacity;
    if (!lane_layout(transport, capacity, data_bytes, &total)) {
        status = ASTRA_VFS_ERR_LIMIT;
        goto done;
    }
    command_bytes = total - data_bytes;
    data_offset = command_bytes;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t position = transport->channel_supported != 0u ?
            lane->producer_position + index : index;
        AstraHostCommand *command = (AstraHostCommand *)(void *)(
            lane->bytes +
            (transport->channel_supported != 0u ?
                 ASTRA_HOST_CHANNEL_HEADER_SIZE : 0u) +
            (position & (capacity - 1u)) * ASTRA_HOST_COMMAND_SIZE);
        uint32_t data_size = transfers[index].input_size >
                                     transfers[index].output_capacity ?
                                 transfers[index].input_size :
                                 transfers[index].output_capacity;

        status = publish_command(command, transfers[index].command);
        if (status != ASTRA_VFS_OK)
            goto done;
        command->data_offset = data_size != 0u ? data_offset : 0u;
        command->data_length = transfers[index].input_size;
        command->data_capacity = transfers[index].output_capacity;
        if (transfers[index].input_size != 0u)
            memcpy(lane->bytes + data_offset, transfers[index].input,
                   transfers[index].input_size);
        data_offset += data_size;
    }
    if (transport->channel_supported != 0u) {
        volatile AstraHostChannelHeader *header =
            (volatile AstraHostChannelHeader *)(void *)lane->bytes;
        uint32_t producer = lane->producer_position + count;

        astra_memory_release_fence();
        header->producer_position = producer;
        status = astra_host_channel_kick(lane->channel_address, producer);
        if (status == ASTRA_SYSCALL_OK) {
            astra_compiler_barrier();
            if (channel_pending(header, producer))
                status = astra_host_channel_wait(
                    producer,
                    UINT64_C(0x7fffffffffffffff));
        }
        astra_memory_acquire_fence();
        if (status == ASTRA_SYSCALL_OK &&
            channel_pending(header, producer))
            status = ASTRA_SYSCALL_IO_ERROR;
        if (status == ASTRA_SYSCALL_OK)
            status = header->transport_status;
        if (status == ASTRA_SYSCALL_OK)
            lane->producer_position = producer;
    } else {
        request.size = sizeof(request);
        request.buffer = lane->dma;
        request.byte_size = total;
        request.command_count = count;
        status = astra_host_lease_execute(transport->device, &request,
                                          &executed);
        if (status == ASTRA_SYSCALL_OK && executed != count)
            status = ASTRA_SYSCALL_IO_ERROR;
    }
    if (status != ASTRA_SYSCALL_OK) {
        status = astra_vfs_host_status_from_syscall(status);
        goto done;
    }
    status = ASTRA_VFS_OK;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t position = transport->channel_supported != 0u ?
            lane->producer_position - count + index : index;
        AstraHostCommand *command = (AstraHostCommand *)(void *)(
            lane->bytes +
            (transport->channel_supported != 0u ?
                 ASTRA_HOST_CHANNEL_HEADER_SIZE : 0u) +
            (position & (capacity - 1u)) * ASTRA_HOST_COMMAND_SIZE);

        collect_command(transfers[index].command, command);
        if (command->status == ASTRA_VFS_OK &&
            transfers[index].output_capacity != 0u) {
            if (command->result_length >
                transfers[index].output_capacity) {
                status = ASTRA_VFS_ERR_IO;
                continue;
            }
            if (command->result_length != 0u)
                memcpy(transfers[index].output,
                       lane->bytes + command->data_offset,
                       command->result_length);
        }
        if (status == ASTRA_VFS_OK && command->status != ASTRA_VFS_OK)
            status = command->status;
    }

done:
    if (locked)
        transport->release(transport->lock_context);
    return status;
}

uint32_t astra_vfs_host_transport_execute(
    void *context, AstraHostCommand *command, const void *input,
    uint32_t input_size, void *output, uint32_t output_capacity)
{
    AstraVfsHostTransfer transfer = {
        command, input, input_size, output, output_capacity
    };

    return astra_vfs_host_transport_execute_batch(context, &transfer, 1u);
}
