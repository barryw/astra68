#include <astra/runtime.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t storage[4096] __attribute__((aligned(4096)));
static uint32_t closed_dma;
static uint32_t closed_channel;

uint32_t astra_host_lease_query(uint32_t device, AstraHostLeaseInfo *lease)
{
    assert(device == 7u);
    memset(lease, 0, sizeof(*lease));
    lease->size = sizeof(*lease);
    lease->capabilities = ASTRA_HOST_CAP_CHANNEL | ASTRA_HOST_CAP_ENTROPY;
    lease->state_flags = ASTRA_HOST_STATE_READY;
    lease->host_generation = 11u;
    lease->maximum_transfer = sizeof(storage);
    lease->maximum_commands = 1u;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_dma_create(uint32_t bytes, AstraDmaBufferInfo *dma)
{
    assert(bytes == ASTRA_HOST_CHANNEL_HEADER_SIZE +
                    ASTRA_HOST_COMMAND_SIZE + ASTRA_HOST_ENTROPY_MAX);
    dma->handle = 9u;
    dma->virtual_base = (uint32_t)(uintptr_t)storage;
    dma->byte_size = sizeof(storage);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_open(uint32_t device, AstraHostChannelOpen *open)
{
    AstraHostChannelHeader *header = (AstraHostChannelHeader *)(void *)storage;

    assert(device == 7u && open->buffer == 9u);
    open->channel_generation = 3u;
    open->channel_address = ASTRA_HOST_CHANNEL_PHYSICAL_BASE;
    open->host_generation = 11u;
    memset(header, 0, sizeof(*header));
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_kick(uint32_t address, uint32_t producer)
{
    AstraHostChannelHeader *header = (AstraHostChannelHeader *)(void *)storage;

    assert(address == ASTRA_HOST_CHANNEL_PHYSICAL_BASE);
    assert(header->producer_position == producer);
    header->consumer_position = producer;
    header->transport_status = ASTRA_SYSCALL_OK;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_wait(uint32_t producer, uint64_t deadline)
{
    assert(producer == 1u && deadline == ASTRA_DEADLINE_FOREVER);
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_host_channel_close(uint32_t device)
{
    assert(device == 7u);
    ++closed_channel;
    return ASTRA_SYSCALL_OK;
}

uint32_t astra_close(uint32_t handle)
{
    assert(handle == 9u);
    ++closed_dma;
    return ASTRA_SYSCALL_OK;
}

int main(void)
{
    AstraHostChannelClient client = {0};
    AstraHostCommand *command;

    assert(astra_host_client_open(7u, ASTRA_HOST_CAP_ENTROPY,
                                  ASTRA_HOST_ENTROPY_MAX, &client) ==
           ASTRA_SYSCALL_OK);
    assert(client.generation == 11u && client.data_capacity == 256u);
    command = astra_host_client_prepare(&client, ASTRA_HOST_SERVICE_ENTROPY,
                                        ASTRA_HOST_ENTROPY_FILL);
    assert(command != NULL);
    assert(command->size == sizeof(*command));
    assert(command->generation == 11u);
    assert(command->data_offset == ASTRA_HOST_CHANNEL_HEADER_SIZE +
                                   ASTRA_HOST_COMMAND_SIZE);
    command->data_capacity = 32u;
    assert(astra_host_client_submit(&client) == ASTRA_SYSCALL_OK);
    assert(client.producer == 1u);
    assert(astra_host_client_close(&client) == ASTRA_SYSCALL_OK);
    assert(closed_channel == 1u && closed_dma == 1u && client.dma == 0u);

    assert(astra_host_client_open(7u, ASTRA_HOST_CAP_REMOTE_DESKTOP,
                                  0u, &client) ==
           ASTRA_SYSCALL_UNSUPPORTED);
    assert(astra_host_client_prepare(NULL, 0u, 0u) == NULL);
    assert(astra_host_client_submit(NULL) == ASTRA_SYSCALL_INVALID_ARGUMENT);

    puts("ASTRA HOST CHANNEL CLIENT PASS");
    return 0;
}
