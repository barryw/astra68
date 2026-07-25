#ifndef ASTRA_KERNEL_PORT_H
#define ASTRA_KERNEL_PORT_H

#include "handle.h"
#include "thread.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_PORT_MAX 16u
#define KERNEL_PORT_OWNER_MAX 4u
#define KERNEL_PORT_MESSAGE_MAX 32u
#define KERNEL_PORT_OWNER_MESSAGE_MAX 16u
#define KERNEL_PORT_MESSAGE_BYTES_MAX 8960u
#define KERNEL_PORT_OWNER_BYTES_MAX 4480u
#define KERNEL_PORT_MESSAGE_SIZE_MIN 24u
#define KERNEL_PORT_MESSAGE_SIZE_MAX 280u
#define KERNEL_PORT_INLINE_SIZE_MAX 256u
#define KERNEL_PORT_MESSAGE_HANDLE_MAX KERNEL_HANDLE_TRANSFER_MAX
#define KERNEL_PORT_QUEUE_MESSAGES_MAX 8u
#define KERNEL_PORT_QUEUE_BYTES_MAX 2240u
#define KERNEL_PORT_WAITER_MAX KERNEL_THREAD_MAX

#define KERNEL_PORT_SEND_RIGHTS \
    ((1u << 0) | (1u << 3) | (1u << 4) | (1u << 5))
#define KERNEL_PORT_RECEIVE_RIGHTS \
    ((1u << 0) | (1u << 4) | (1u << 6))

typedef enum KernelPortEndpoint {
    KERNEL_PORT_ENDPOINT_SEND = 1,
    KERNEL_PORT_ENDPOINT_RECEIVE = 2
} KernelPortEndpoint;

typedef enum KernelPortState {
    KERNEL_PORT_FREE = 0,
    KERNEL_PORT_OPEN,
    KERNEL_PORT_PEER_CLOSED,
    KERNEL_PORT_CLOSING
} KernelPortState;

typedef enum KernelPortStatus {
    KERNEL_PORT_OK = 0,
    KERNEL_PORT_WOULD_BLOCK,
    KERNEL_PORT_PEER_DEAD,
    KERNEL_PORT_CLOSED,
    KERNEL_PORT_BUFFER_TOO_SMALL,
    KERNEL_PORT_INVALID_ARGUMENT,
    KERNEL_PORT_INVALID_STATE,
    KERNEL_PORT_NO_SLOT,
    KERNEL_PORT_QUOTA_EXCEEDED,
    KERNEL_PORT_INVALID_HANDLE,
    KERNEL_PORT_ACCESS_DENIED,
    KERNEL_PORT_DUPLICATE_HANDLE,
    KERNEL_PORT_TRANSFER_POOL_FULL,
    KERNEL_PORT_HANDLE_TABLE_FULL,
    KERNEL_PORT_CORRUPT
} KernelPortStatus;

typedef struct KernelPort KernelPort;

typedef struct KernelPortReceipt {
    KernelHandleImportReservation import;
    KernelPort *port;
    KernelHandleTable *destination_table;
    const uint8_t *message;
    uint32_t message_size;
    uint32_t message_generation;
    uint16_t message_slot;
    uint8_t handle_count;
    uint8_t active;
} KernelPortReceipt;

typedef struct KernelPortSnapshot {
    uint32_t owner;
    uint32_t generation;
    uint32_t queued_bytes;
    uint32_t receive_terminal;
    uint32_t send_terminal;
    uint16_t references;
    uint16_t send_references;
    uint16_t receive_references;
    uint16_t queued_messages;
    uint16_t maximum_messages;
    uint16_t maximum_bytes;
    uint16_t readable_waiters;
    uint16_t writable_waiters;
    uint8_t state;
    uint8_t capacity_reserved;
    uint8_t reserved[2];
} KernelPortSnapshot;

typedef struct KernelPortPoolStats {
    uint32_t created_ports;
    uint32_t active_ports;
    uint32_t closing_ports;
    uint32_t max_active_ports;
    uint32_t allocation_failures;
    uint32_t quota_failures;
    uint32_t publication_rollbacks;
    uint32_t sends;
    uint32_t receives;
    uint32_t send_would_block;
    uint32_t receive_would_block;
    uint32_t receive_buffer_too_small;
    uint32_t owner_deaths;
    uint32_t peer_closures;
    uint32_t close_operations;
    uint32_t discarded_messages;
    uint32_t discarded_handles;
    uint32_t wait_wakeups;
    uint32_t queued_messages;
    uint32_t queued_bytes;
    uint32_t queued_handles;
    uint32_t max_queued_messages;
    uint32_t max_queued_bytes;
    uint32_t max_queued_handles;
    uint32_t reserved_message_capacity;
    uint32_t reserved_byte_capacity;
} KernelPortPoolStats;

void kernel_port_pool_init(void);
KernelPortStatus kernel_port_create(uint32_t owner,
                                    uint32_t maximum_messages,
                                    uint32_t maximum_bytes,
                                    KernelPort **port);
void kernel_port_abandon_unpublished(KernelPort *port);
void kernel_port_handle_release(void *object, void *context);
KernelPortStatus kernel_port_send(
    KernelPort *port, KernelHandleTable *source_table,
    const void *message, uint32_t message_size,
    const KernelHandle *handles, uint32_t handle_count,
    uint32_t *woken_threads);
KernelPortStatus kernel_port_receive_prepare(
    KernelPort *port, KernelHandleTable *destination_table,
    uint32_t message_capacity, uint32_t handle_capacity,
    KernelPortReceipt *receipt, uint32_t *required_message_size,
    uint32_t *required_handle_count);
KernelPortStatus kernel_port_receive_commit(KernelPortReceipt *receipt,
                                            uint32_t *woken_threads);
KernelPortStatus kernel_port_receive_cancel(KernelPortReceipt *receipt,
                                            uint32_t *woken_threads);
KernelPortStatus kernel_port_prepare_wait(KernelPort *port,
                                          KernelPortEndpoint endpoint,
                                          KernelThreadWaitSpec *spec);
KernelPortStatus kernel_port_prepare_wait_after(
    KernelPort *port, KernelPortEndpoint endpoint,
    uint32_t expected_sequence, KernelThreadWaitSpec *spec);
KernelPortStatus kernel_port_wait_sequence(const KernelPort *port,
                                           KernelPortEndpoint endpoint,
                                           uint32_t *sequence);
KernelPortStatus kernel_port_commit_wait(KernelPort *port,
                                         KernelPortEndpoint endpoint);
KernelPortStatus kernel_port_owner_died(uint32_t owner,
                                        uint32_t *closed_ports,
                                        uint32_t *woken_threads);
uint32_t kernel_port_terminal_result(const KernelPort *port,
                                     KernelPortEndpoint endpoint);
bool kernel_port_snapshot(uint32_t slot, KernelPortSnapshot *snapshot);
bool kernel_port_pool_stats(KernelPortPoolStats *stats);
bool kernel_port_pool_healthy(void);
bool kernel_port_pool_valid(void);

#endif
