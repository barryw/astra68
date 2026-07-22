#ifndef ASTRA_KERNEL_DMA_H
#define ASTRA_KERNEL_DMA_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_DMA_MAX_BUFFERS 32u
#define KERNEL_DMA_HANDLE_INVALID 0u

typedef uint32_t KernelDmaHandle;

typedef enum KernelDmaDirection {
    KERNEL_DMA_TO_DEVICE = 1,
    KERNEL_DMA_FROM_DEVICE,
    KERNEL_DMA_BIDIRECTIONAL
} KernelDmaDirection;

typedef enum KernelDmaState {
    KERNEL_DMA_FREE = 0,
    KERNEL_DMA_CPU_OWNED,
    KERNEL_DMA_DEVICE_OWNED,
    KERNEL_DMA_REVOKING
} KernelDmaState;

typedef enum KernelDmaStatus {
    KERNEL_DMA_OK = 0,
    KERNEL_DMA_INVALID_ARGUMENT,
    KERNEL_DMA_INVALID_HANDLE,
    KERNEL_DMA_NOT_OWNED,
    KERNEL_DMA_NO_RESOURCES,
    KERNEL_DMA_OUT_OF_MEMORY,
    KERNEL_DMA_BUSY,
    KERNEL_DMA_STALE,
    KERNEL_DMA_CORRUPT
} KernelDmaStatus;

typedef struct KernelDmaToken {
    KernelDmaHandle handle;
    uint32_t transfer_generation;
    uint32_t device_generation;
    uint32_t physical_address;
    uint32_t byte_count;
    uint8_t direction;
    uint8_t reserved[3];
} KernelDmaToken;

typedef struct KernelDmaBufferInfo {
    uint32_t owner;
    uint32_t physical_base;
    uint32_t byte_size;
    uint32_t frame_count;
    uint32_t transfer_generation;
    uint32_t device_generation;
    uint8_t state;
    uint8_t reserved[3];
} KernelDmaBufferInfo;

typedef struct KernelDmaStats {
    uint32_t buffers_created;
    uint32_t live_buffers;
    uint32_t in_flight_buffers;
    uint32_t deferred_reclaims;
    uint32_t stale_completions;
    uint32_t create_failures;
} KernelDmaStats;

void kernel_dma_init(void);
KernelDmaStatus kernel_dma_create(uint32_t owner, uint32_t byte_size,
                                  uint32_t alignment_frames,
                                  KernelDmaHandle *handle);
KernelDmaStatus kernel_dma_buffer_info(KernelDmaHandle handle,
                                       uint32_t owner,
                                       KernelDmaBufferInfo *info);
KernelDmaStatus kernel_dma_begin(KernelDmaHandle handle, uint32_t owner,
                                 uint32_t byte_offset, uint32_t byte_count,
                                 KernelDmaDirection direction,
                                 uint32_t device_generation,
                                 KernelDmaToken *token);
KernelDmaStatus kernel_dma_complete(const KernelDmaToken *token);
KernelDmaStatus kernel_dma_abort(const KernelDmaToken *token);
KernelDmaStatus kernel_dma_close(KernelDmaHandle handle, uint32_t owner);
KernelDmaStatus kernel_dma_revoke_owner(uint32_t owner,
                                        uint32_t *released_buffers,
                                        uint32_t *deferred_buffers);
bool kernel_dma_stats(KernelDmaStats *stats);

#endif
