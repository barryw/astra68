#include <astra/runtime.h>

#include <string.h>

uint32_t
astra_host_client_open(uint32_t device, uint32_t required_capabilities,
                       uint32_t data_capacity, AstraHostChannelClient *client)
{
    AstraHostLeaseInfo lease = {0};
    AstraDmaBufferInfo dma = {0};
    AstraHostChannelOpen open = {0};
    uint32_t needed;
    uint32_t status;

    if (device == 0u || client == NULL || client->dma != 0u ||
        data_capacity > UINT32_MAX - ASTRA_HOST_CHANNEL_HEADER_SIZE -
                            ASTRA_HOST_COMMAND_SIZE)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    status = astra_host_lease_query(device, &lease);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    required_capabilities |= ASTRA_HOST_CAP_CHANNEL;
    if ((lease.capabilities & required_capabilities) !=
            required_capabilities ||
        (lease.state_flags & ASTRA_HOST_STATE_READY) == 0u ||
        lease.host_generation == 0u || lease.maximum_commands == 0u)
        return ASTRA_SYSCALL_UNSUPPORTED;
    needed = ASTRA_HOST_CHANNEL_HEADER_SIZE + ASTRA_HOST_COMMAND_SIZE +
             data_capacity;
    if (needed > lease.maximum_transfer)
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    status = astra_dma_create(needed, &dma);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    if (dma.handle == 0u || dma.virtual_base == 0u ||
        dma.byte_size < needed || dma.byte_size > lease.maximum_transfer) {
        if (dma.handle != 0u)
            (void)astra_close(dma.handle);
        return ASTRA_SYSCALL_RESOURCE_LIMIT;
    }
    (void)memset((void *)(uintptr_t)dma.virtual_base, 0, needed);
    open.size = sizeof(open);
    open.buffer = dma.handle;
    open.byte_size = needed;
    open.command_capacity = 1u;
    status = astra_host_channel_open(device, &open);
    if (status != ASTRA_SYSCALL_OK || open.channel_address == 0u ||
        open.channel_generation == 0u ||
        open.host_generation != lease.host_generation) {
        (void)astra_close(dma.handle);
        return status == ASTRA_SYSCALL_OK ? ASTRA_SYSCALL_IO_ERROR : status;
    }
    client->device = device;
    client->dma = dma.handle;
    client->channel_address = open.channel_address;
    client->generation = open.host_generation;
    client->byte_size = needed;
    client->data_capacity = data_capacity;
    client->header = (volatile AstraHostChannelHeader *)(uintptr_t)
        dma.virtual_base;
    client->command = (volatile AstraHostCommand *)(uintptr_t)
        (dma.virtual_base + ASTRA_HOST_CHANNEL_HEADER_SIZE);
    client->data = (volatile uint8_t *)(uintptr_t)
        (dma.virtual_base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
         ASTRA_HOST_COMMAND_SIZE);
    return ASTRA_SYSCALL_OK;
}

AstraHostCommand *
astra_host_client_prepare(AstraHostChannelClient *client, uint16_t service,
                          uint16_t operation)
{
    AstraHostCommand *command;

    if (client == NULL || client->command == NULL || client->generation == 0u)
        return NULL;
    command = (AstraHostCommand *)(uintptr_t)client->command;
    (void)memset(command, 0, sizeof(*command));
    command->size = sizeof(*command);
    command->version = ASTRA_HOST_COMMAND_VERSION;
    command->service = service;
    command->operation = operation;
    command->generation = client->generation;
    if (client->data_capacity != 0u)
        command->data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                               ASTRA_HOST_COMMAND_SIZE;
    return command;
}

uint32_t
astra_host_client_submit(AstraHostChannelClient *client)
{
    uint32_t producer;
    uint32_t status;

    if (client == NULL || client->header == NULL || client->command == NULL ||
        client->channel_address == 0u)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    producer = client->producer + 1u;
    astra_memory_release_fence();
    client->header->producer_position = producer;
    status = astra_host_channel_kick(client->channel_address, producer);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    status = astra_host_channel_wait(producer, ASTRA_DEADLINE_FOREVER);
    if (status != ASTRA_SYSCALL_OK)
        return status;
    astra_memory_acquire_fence();
    if (client->header->consumer_position != producer ||
        client->header->transport_status != ASTRA_SYSCALL_OK)
        return ASTRA_SYSCALL_IO_ERROR;
    client->producer = producer;
    return ASTRA_SYSCALL_OK;
}

uint32_t
astra_host_client_close(AstraHostChannelClient *client)
{
    uint32_t first = ASTRA_SYSCALL_OK;
    uint32_t status;

    if (client == NULL)
        return ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (client->channel_address != 0u) {
        status = astra_host_channel_close(client->device);
        if (status != ASTRA_SYSCALL_OK)
            first = status;
    }
    if (client->dma != 0u) {
        status = astra_close(client->dma);
        if (first == ASTRA_SYSCALL_OK && status != ASTRA_SYSCALL_OK)
            first = status;
    }
    (void)memset(client, 0, sizeof(*client));
    return first;
}
