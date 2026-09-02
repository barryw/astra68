#include <astra/vfs_host_transport.h>

#include <astra/runtime.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define DMA_SLOT_COUNT (ASTRA_PROCESS_THREAD_COUNT_MAX + 4u)
static uint8_t dma_storage[DMA_SLOT_COUNT][16384];
static uint32_t dma_slot_count;
static uint32_t channel_thread[DMA_SLOT_COUNT];
static uint8_t channel_opened[DMA_SLOT_COUNT];
static _Thread_local uint32_t current_thread = 1u;
static uint32_t capabilities;
static uint32_t dma_creates;
static uint32_t closes;
static uint32_t executes;
static uint32_t channel_opens;
static uint32_t channel_closes;
static uint32_t channel_kicks;
static uint32_t channel_waits;
static uint32_t maximum_kick_batch;
static uint32_t lock_depth;
static uint32_t lock_acquires;
static pthread_mutex_t transport_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kick_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t kick_condition = PTHREAD_COND_INITIALIZER;
static int require_two_in_flight;
static int complete_on_wait_only;
static uint32_t concurrent_kick_base;

static uint32_t dma_slot(uint32_t handle)
{
    assert(handle >= 71u && handle - 71u < dma_slot_count);
    return handle - 71u;
}

uint32_t astra_current_thread_handle(uint32_t *thread)
{
    assert(thread != NULL);
    *thread = current_thread;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_lease_query(uint32_t device, AstraHostLeaseInfo *lease)
{
    assert(device == 41u);
    memset(lease, 0, sizeof(*lease));
    lease->size = sizeof(*lease);
    lease->capabilities = capabilities;
    lease->state_flags = ASTRA_HOST_STATE_READY;
    lease->host_generation = 9u;
    lease->maximum_transfer = sizeof(dma_storage[0]);
    lease->maximum_commands = 8u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_dma_create(uint32_t byte_size, AstraDmaBufferInfo *info)
{
    uint32_t slot = dma_slot_count++;

    assert(byte_size <= sizeof(dma_storage[0]));
    assert(slot < DMA_SLOT_COUNT);
    assert((uintptr_t)dma_storage[slot] <= UINT32_MAX);
    ++dma_creates;
    memset(info, 0, sizeof(*info));
    info->size = sizeof(*info);
    info->handle = 71u + slot;
    info->virtual_base = (uint32_t)(uintptr_t)dma_storage[slot];
    info->byte_size = sizeof(dma_storage[slot]);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_lease_execute(uint32_t device,
                                  const AstraHostTransportRequest *request,
                                  uint32_t *executed)
{
    uint32_t slot = dma_slot(request->buffer);
    AstraHostCommand *command =
        (AstraHostCommand *)(void *)dma_storage[slot];

    assert(device == 41u);
    assert(request->byte_size >= sizeof(*command));
    assert(request->command_count == 1u);
    assert(command->generation == 9u);
    ++executes;
    command->status = ASTRA_VFS_OK;
    command->handle = 0x1234u;
    if (command->data_capacity >= 4u) {
        memcpy(dma_storage[slot] + command->data_offset, "data", 4u);
        command->result_length = 4u;
    }
    *executed = 1u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_open(uint32_t device,
                                 AstraHostChannelOpen *channel)
{
    uint32_t slot = dma_slot(channel->buffer);
    AstraHostChannelHeader *header =
        (AstraHostChannelHeader *)(void *)dma_storage[slot];

    assert(device == 41u);
    assert(channel->command_capacity != 0u &&
           (channel->command_capacity &
            (channel->command_capacity - 1u)) == 0u);
    assert(channel_opened[slot] == 0u);
    memset(header, 0, sizeof(*header));
    header->magic = ASTRA_HOST_CHANNEL_MAGIC;
    header->version = ASTRA_HOST_CHANNEL_VERSION;
    header->header_size = sizeof(*header);
    header->command_size = ASTRA_HOST_COMMAND_SIZE;
    header->command_capacity = channel->command_capacity;
    header->command_offset = sizeof(*header);
    header->data_offset = sizeof(*header) +
                          channel->command_capacity * ASTRA_HOST_COMMAND_SIZE;
    header->total_size = channel->byte_size;
    header->channel_generation = 3u;
    channel->channel_generation = 3u;
    channel->channel_address = 0x4ff00000u + slot * 0x1000u;
    channel->host_generation = 9u;
    channel_opened[slot] = 1u;
    channel_thread[slot] = current_thread;
    ++channel_opens;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_close(uint32_t device)
{
    assert(device == 41u);
    for (uint32_t slot = 0u; slot < dma_slot_count; ++slot) {
        if (channel_opened[slot] == 0u ||
            channel_thread[slot] != current_thread)
            continue;
        channel_opened[slot] = 0u;
        channel_thread[slot] = 0u;
        ++channel_closes;
        return ASTRA_SYSCALL_OK;
    }
    assert(0);
    return ASTRA_SYSCALL_INVALID_ARGUMENT;
}

static void complete_channel(uint32_t slot, uint32_t producer)
{
    AstraHostChannelHeader *header =
        (AstraHostChannelHeader *)(void *)dma_storage[slot];

    assert(header->producer_position == producer);
    for (uint32_t position = header->consumer_position;
         position != producer; ++position) {
        AstraHostCommand *command = (AstraHostCommand *)(void *)(
            dma_storage[slot] + ASTRA_HOST_CHANNEL_HEADER_SIZE +
            (position & (header->command_capacity - 1u)) *
                ASTRA_HOST_COMMAND_SIZE);

        command->status = ASTRA_VFS_OK;
        command->handle = 0x5678u + position;
        if (command->operation == ASTRA_HOST_FS_WRITE) {
            assert(command->data_length == 4u);
            assert(memcmp(dma_storage[slot] + command->data_offset,
                          "send", 4u) == 0);
            command->result_length = command->data_length;
        } else if (command->data_capacity >= 4u) {
            memcpy(dma_storage[slot] + command->data_offset, "ring", 4u);
            command->result_length = 4u;
        }
    }
    header->consumer_position = producer;
    header->transport_status = ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_kick(uint32_t address, uint32_t producer)
{
    uint32_t slot;

    assert(address >= 0x4ff00000u);
    slot = (address - 0x4ff00000u) / 0x1000u;
    assert(slot < dma_slot_count && channel_opened[slot] != 0u);
    assert(pthread_mutex_lock(&kick_mutex) == 0);
    {
        AstraHostChannelHeader *header =
            (AstraHostChannelHeader *)(void *)dma_storage[slot];
        uint32_t pending = producer - header->consumer_position;

        if (pending > maximum_kick_batch)
            maximum_kick_batch = pending;
    }
    ++channel_kicks;
    if (!require_two_in_flight && !complete_on_wait_only) {
        complete_channel(slot, producer);
    } else if (require_two_in_flight &&
               channel_kicks - concurrent_kick_base >= 2u) {
        for (uint32_t index = 0u; index < dma_slot_count; ++index) {
            AstraHostChannelHeader *header;

            if (channel_opened[index] == 0u)
                continue;
            header = (AstraHostChannelHeader *)(void *)dma_storage[index];
            if (header->consumer_position != header->producer_position)
                complete_channel(index, header->producer_position);
        }
    }
    assert(pthread_cond_broadcast(&kick_condition) == 0);
    assert(pthread_mutex_unlock(&kick_mutex) == 0);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_wait(uint32_t producer, uint64_t deadline_ns)
{
    struct timespec deadline;
    uint32_t slot = DMA_SLOT_COUNT;

    assert(deadline_ns == UINT64_C(0x7fffffffffffffff));
    for (uint32_t index = 0u; index < dma_slot_count; ++index)
        if (channel_opened[index] != 0u &&
            channel_thread[index] == current_thread)
            slot = index;
    assert(slot < dma_slot_count);
    assert(pthread_mutex_lock(&kick_mutex) == 0);
    ++channel_waits;
    if (!require_two_in_flight) {
        complete_channel(slot, producer);
        assert(pthread_mutex_unlock(&kick_mutex) == 0);
        return ASTRA_SYSCALL_OK;
    }
    assert(pthread_cond_broadcast(&kick_condition) == 0);
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_nsec += 100000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        ++deadline.tv_sec;
    }
    while (channel_kicks - concurrent_kick_base < 2u) {
        if (pthread_cond_timedwait(&kick_condition, &kick_mutex,
                                   &deadline) != 0) {
            assert(pthread_mutex_unlock(&kick_mutex) == 0);
            return ASTRA_SYSCALL_TIMED_OUT;
        }
    }
    complete_channel(slot, producer);
    assert(pthread_mutex_unlock(&kick_mutex) == 0);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    uint32_t slot = dma_slot(handle);

    if (channel_opened[slot] != 0u) {
        channel_opened[slot] = 0u;
        channel_thread[slot] = 0u;
        ++channel_closes;
    }
    ++closes;
    return ASTRA_SYSCALL_OK;
}

static int acquire(void *context)
{
    assert(context == &lock_depth);
    assert(pthread_mutex_lock(&transport_mutex) == 0);
    assert(lock_depth == 0u);
    lock_depth = 1u;
    ++lock_acquires;
    return 1;
}

static void release(void *context)
{
    assert(context == &lock_depth);
    assert(lock_depth == 1u);
    lock_depth = 0u;
    assert(pthread_mutex_unlock(&transport_mutex) == 0);
}

typedef struct ExecuteThread {
    AstraVfsHostTransport *transport;
    uint32_t thread;
    uint32_t status;
} ExecuteThread;

static void *execute_thread(void *opaque)
{
    ExecuteThread *thread = opaque;
    AstraHostCommand command = {0};

    current_thread = thread->thread;
    command.generation = thread->transport->generation;
    thread->status = astra_vfs_host_transport_execute(
        thread->transport, &command, NULL, 0u, NULL, 0u);
    return NULL;
}

int main(void)
{
    AstraVfsHostTransport transport;
    AstraHostCommand command;
    AstraHostCommand batch_commands[8];
    AstraVfsHostTransfer batch[8];
    AstraVfsHostRequest request;
    ExecuteThread calls[2];
    pthread_t threads[2];
    uint32_t wait_base;
    uint8_t output[4];

    capabilities = ASTRA_HOST_CAP_FILESYSTEM;
    assert(!astra_vfs_host_transport_init(&transport, 41u, acquire, release,
                                          &lock_depth));
    capabilities = ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED;
    assert(astra_vfs_host_transport_init(&transport, 41u, acquire, release,
                                         &lock_depth));
    assert(transport.generation == 9u);
    memset(&command, 0, sizeof(command));
    command.size = sizeof(command);
    command.version = ASTRA_HOST_COMMAND_VERSION;
    command.service = ASTRA_HOST_SERVICE_FILESYSTEM;
    command.operation = ASTRA_HOST_FS_OPEN;
    command.generation = transport.generation;
    assert(astra_vfs_host_transport_execute(
               &transport, &command, NULL, 0u, output, sizeof(output)) ==
           ASTRA_VFS_OK);
    assert(command.handle == 0x1234u);
    assert(memcmp(output, "data", sizeof(output)) == 0);
    assert(dma_creates == 1u && executes == 1u && lock_depth == 0u);
    memset(&command, 0, sizeof(command));
    command.generation = transport.generation;
    assert(astra_vfs_host_transport_execute(
               &transport, &command, NULL, 0u, NULL, 0u) == ASTRA_VFS_OK);
    assert(dma_creates == 1u && executes == 2u);
    assert(astra_vfs_host_transport_execute(
               &transport, &command, dma_storage[0], sizeof(dma_storage[0]),
               NULL, 0u) == ASTRA_VFS_ERR_LIMIT);
    assert(executes == 2u);
    astra_vfs_host_transport_destroy(&transport);
    assert(closes == 1u);

    memset(&transport, 0, sizeof(transport));
    capabilities = ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED |
                   ASTRA_HOST_CAP_CHANNEL |
                   ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ;
    assert(astra_vfs_host_transport_init(&transport, 41u, acquire, release,
                                         &lock_depth));
    memset(batch_commands, 0, sizeof(batch_commands));
    memset(batch, 0, sizeof(batch));
    for (uint32_t index = 0u; index < 8u; ++index) {
        batch_commands[index].generation = transport.generation;
        batch[index].command = &batch_commands[index];
    }
    assert(astra_vfs_host_transport_execute_batch(&transport, batch, 8u) ==
           ASTRA_VFS_OK);
    for (uint32_t index = 0u; index < 8u; ++index)
        assert(batch_commands[index].handle == 0x5678u + index);
    assert(channel_opens == 1u && channel_kicks == 1u &&
           channel_waits == 0u && maximum_kick_batch == 8u);
    memset(&command, 0, sizeof(command));
    command.generation = transport.generation;
    memset(output, 0, sizeof(output));
    assert(astra_vfs_host_transport_execute(
               &transport, &command, NULL, 0u, output, sizeof(output)) ==
           ASTRA_VFS_OK);
    assert(command.handle == 0x5680u);
    assert(memcmp(output, "ring", sizeof(output)) == 0);
    assert(channel_opens == 1u && channel_kicks == 2u && executes == 2u);
    assert(astra_vfs_host_transport_begin(&transport, sizeof(output),
                                          &request) == ASTRA_VFS_OK);
    assert((uint8_t *)(void *)request.command >= dma_storage[1] &&
           (uint8_t *)(void *)request.command <
               dma_storage[1] + sizeof(dma_storage[1]));
    request.command->generation = transport.generation;
    memset(output, 0, sizeof(output));
    assert(astra_vfs_host_transport_submit(
               &transport, &request, NULL, 0u, output, sizeof(output)) ==
           ASTRA_VFS_OK);
    assert(request.command->handle == 0x5681u);
    assert(memcmp(output, "ring", sizeof(output)) == 0);
    assert(channel_opens == 1u && channel_kicks == 3u && executes == 2u);
    {
        uint32_t acquired = lock_acquires;

        assert(astra_vfs_host_transport_begin(&transport, sizeof(output),
                                              &request) == ASTRA_VFS_OK);
        assert(lock_acquires == acquired);
        request.command->generation = transport.generation;
        request.command->operation = ASTRA_HOST_FS_WRITE;
        assert(astra_vfs_host_transport_submit(
                   &transport, &request, "send", 4u, NULL, 0u) ==
               ASTRA_VFS_OK);
        assert(request.command->result_length == 4u);
    }
    assert(channel_kicks == 4u);
    complete_on_wait_only = 1;
    {
        uint32_t waits = channel_waits;

        assert(astra_vfs_host_transport_begin(&transport, 0u, &request) ==
               ASTRA_VFS_OK);
        request.command->generation = transport.generation;
        assert(astra_vfs_host_transport_submit(
                   &transport, &request, NULL, 0u, NULL, 0u) ==
               ASTRA_VFS_OK);
        assert(channel_waits == waits + 1u);
    }
    complete_on_wait_only = 0;
    assert(channel_kicks == 5u);
    concurrent_kick_base = channel_kicks;
    wait_base = channel_waits;
    require_two_in_flight = 1;
    calls[0].transport = &transport;
    calls[1].transport = &transport;
    calls[0].thread = 2u;
    calls[1].thread = 3u;
    assert(pthread_create(&threads[0], NULL, execute_thread, &calls[0]) == 0);
    assert(pthread_create(&threads[1], NULL, execute_thread, &calls[1]) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(calls[0].status == ASTRA_VFS_OK);
    assert(calls[1].status == ASTRA_VFS_OK);
    assert(channel_kicks == 7u);
    assert(channel_waits - wait_base <= 1u);
    require_two_in_flight = 0;
    astra_vfs_host_transport_destroy(&transport);
    assert(channel_opens == 3u && channel_closes == 3u && closes == 4u);
    return 0;
}
