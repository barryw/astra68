#ifndef ASTRA_KERNEL_BLOCK_H
#define ASTRA_KERNEL_BLOCK_H

#include "dma.h"

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_BLOCK_MAX_REQUESTS 4u
#define KERNEL_BLOCK_HANDLE_INVALID 0u
/*
 * Synthetic completion statuses the engine produces itself. They sit above the
 * transport's own status values so a device error can never be mistaken for
 * one, and they exist because a service must be able to tell "the device said
 * no" from "the device was taken away".
 */
#define KERNEL_BLOCK_COMPLETION_MEDIA_CHANGED 6u
#define KERNEL_BLOCK_COMPLETION_RESET 7u
#define KERNEL_BLOCK_COMPLETION_CANCELLED 8u

typedef uint32_t KernelBlockHandle;

typedef enum KernelBlockStatus {
    KERNEL_BLOCK_OK = 0,
    KERNEL_BLOCK_INVALID_ARGUMENT,
    KERNEL_BLOCK_INVALID_HANDLE,
    KERNEL_BLOCK_NOT_OWNED,
    KERNEL_BLOCK_NOT_PRESENT,
    KERNEL_BLOCK_NO_MEDIA,
    KERNEL_BLOCK_WRITE_PROTECTED,
    KERNEL_BLOCK_OUT_OF_RANGE,
    KERNEL_BLOCK_UNSUPPORTED,
    KERNEL_BLOCK_QUEUE_FULL,
    KERNEL_BLOCK_BUSY,
    KERNEL_BLOCK_PENDING,
    KERNEL_BLOCK_REJECTED,
    KERNEL_BLOCK_CORRUPT
} KernelBlockStatus;

typedef enum KernelBlockRequestState {
    KERNEL_BLOCK_REQUEST_FREE = 0,
    KERNEL_BLOCK_REQUEST_ACTIVE,
    KERNEL_BLOCK_REQUEST_COMPLETED,
    KERNEL_BLOCK_REQUEST_REVOKING
} KernelBlockRequestState;

typedef struct KernelBlockResult {
    uint16_t status;
    uint16_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} KernelBlockResult;

typedef struct KernelBlockRequestInfo {
    uint32_t owner;
    KernelDmaHandle dma_handle;
    uint32_t expected_media_generation;
    uint32_t expected_host_generation;
    uint16_t sectors;
    uint8_t operation;
    uint8_t state;
} KernelBlockRequestInfo;

typedef struct KernelBlockStats {
    uint32_t submitted;
    uint32_t completed;
    uint32_t rejected;
    uint32_t unknown_completions;
    uint32_t generation_mismatches;
    uint32_t revoked_requests;
} KernelBlockStats;

void kernel_block_init(void);
bool kernel_block_available(void);
KernelBlockStatus kernel_block_submit(uint32_t owner, uint8_t operation,
                                      uint64_t lba, uint16_t sectors,
                                      KernelDmaHandle dma_handle,
                                      uint32_t dma_offset,
                                      KernelBlockHandle *request);
KernelBlockStatus kernel_block_service(uint32_t *serviced_completions);
KernelBlockStatus kernel_block_collect(KernelBlockHandle request,
                                       uint32_t owner,
                                       KernelBlockResult *result);
KernelBlockStatus kernel_block_request_info(KernelBlockHandle request,
                                            uint32_t owner,
                                            KernelBlockRequestInfo *info);
KernelBlockStatus kernel_block_revoke_owner(uint32_t owner,
                                            uint32_t *released_buffers,
                                            uint32_t *deferred_buffers);
/*
 * Ends every in-flight request of a living owner with a synthetic status, so
 * a reset cannot leave a service waiting for completions the device will
 * never send. The records stay collectable: the owner is still there to be
 * told what happened to its requests.
 */
KernelBlockStatus kernel_block_terminate_owner(uint32_t owner,
                                               uint16_t status,
                                               uint32_t *terminated);
/* In-flight and collectable requests a single owner holds. */
uint32_t kernel_block_owner_requests(uint32_t owner);
bool kernel_block_stats(KernelBlockStats *stats);
bool kernel_block_valid(void);

#endif
