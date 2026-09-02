#ifndef ASTRA_VFS_HOST_TRANSPORT_H
#define ASTRA_VFS_HOST_TRANSPORT_H

#include <stdint.h>

#include <astra/host.h>
#include <astra/limits.h>
#include <astra/vfs_host_backend.h>
#include <astra/vfs_service_core.h>

typedef struct AstraVfsHostLane {
    uint32_t thread;
    uint32_t dma;
    uint8_t *bytes;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t channel_address;
    uint32_t producer_position;
    uint8_t active;
    uint8_t reserved[3];
} AstraVfsHostLane;

struct AstraVfsHostTransport {
    uint32_t device;
    uint32_t maximum_transfer;
    uint32_t maximum_commands;
    uint32_t generation;
    uint8_t channel_supported;
    AstraVfsStateAcquire acquire;
    AstraVfsStateRelease release;
    void *lock_context;
    AstraVfsHostLane lanes[ASTRA_PROCESS_THREAD_COUNT_MAX];
};

typedef struct AstraVfsHostTransfer {
    AstraHostCommand *command;
    const void *input;
    uint32_t input_size;
    void *output;
    uint32_t output_capacity;
} AstraVfsHostTransfer;

int astra_vfs_host_transport_init(AstraVfsHostTransport *transport,
                                  uint32_t device,
                                  AstraVfsStateAcquire acquire,
                                  AstraVfsStateRelease release,
                                  void *lock_context);
void astra_vfs_host_transport_destroy(AstraVfsHostTransport *transport);
uint32_t astra_vfs_host_transport_execute(
    void *context, AstraHostCommand *command, const void *input,
    uint32_t input_size, void *output, uint32_t output_capacity);
uint32_t astra_vfs_host_transport_begin(void *context,
                                        uint32_t data_capacity,
                                        AstraVfsHostRequest *request)
    __attribute__((noinline));
uint32_t astra_vfs_host_transport_submit(
    void *context, AstraVfsHostRequest *request, const void *input,
    uint32_t input_size, void *output, uint32_t output_capacity)
    __attribute__((noinline));
uint32_t astra_vfs_host_transport_execute_batch(
    AstraVfsHostTransport *transport, AstraVfsHostTransfer *transfers,
    uint32_t count);

#endif
