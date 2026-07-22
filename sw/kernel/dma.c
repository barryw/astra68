#include "dma.h"

#include "memory.h"

#include <stddef.h>

typedef struct KernelDmaSlot {
    uint32_t owner;
    uint32_t physical_base;
    uint32_t byte_size;
    uint32_t frame_count;
    uint32_t transfer_generation;
    uint32_t device_generation;
    uint32_t transfer_physical;
    uint32_t transfer_bytes;
    uint16_t generation;
    uint8_t state;
    uint8_t direction;
} KernelDmaSlot;

static KernelDmaSlot slots[KERNEL_DMA_MAX_BUFFERS];
static KernelDmaStats dma_stats;
static bool initialized;

static void reset_stats(void)
{
    dma_stats.buffers_created = 0u;
    dma_stats.live_buffers = 0u;
    dma_stats.in_flight_buffers = 0u;
    dma_stats.deferred_reclaims = 0u;
    dma_stats.stale_completions = 0u;
    dma_stats.create_failures = 0u;
}

static void clear_slot(KernelDmaSlot *slot)
{
    uint16_t generation = slot->generation;

    slot->owner = 0u;
    slot->physical_base = 0u;
    slot->byte_size = 0u;
    slot->frame_count = 0u;
    slot->transfer_generation = 0u;
    slot->device_generation = 0u;
    slot->transfer_physical = 0u;
    slot->transfer_bytes = 0u;
    slot->generation = generation;
    slot->state = KERNEL_DMA_FREE;
    slot->direction = 0u;
}

static KernelDmaHandle make_handle(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static KernelDmaSlot *lookup_slot(KernelDmaHandle handle, uint32_t *index)
{
    uint32_t encoded_index = handle & 0xffffu;
    uint16_t generation = (uint16_t)(handle >> 16);
    KernelDmaSlot *slot;

    if (!initialized || encoded_index == 0u ||
        encoded_index > KERNEL_DMA_MAX_BUFFERS || generation == 0u)
        return NULL;
    slot = &slots[encoded_index - 1u];
    if (slot->state == KERNEL_DMA_FREE || slot->generation != generation)
        return NULL;
    if (index != NULL)
        *index = encoded_index - 1u;
    return slot;
}

static KernelDmaStatus release_slot(KernelDmaSlot *slot)
{
    KernelMemoryStatus status = kernel_memory_release(
        slot->physical_base, slot->frame_count, slot->owner);

    if (status != KERNEL_MEMORY_OK)
        return KERNEL_DMA_CORRUPT;
    clear_slot(slot);
    --dma_stats.live_buffers;
    return KERNEL_DMA_OK;
}

static bool token_matches(const KernelDmaSlot *slot,
                          const KernelDmaToken *token)
{
    return token->transfer_generation == slot->transfer_generation &&
           token->device_generation == slot->device_generation &&
           token->physical_address == slot->transfer_physical &&
           token->byte_count == slot->transfer_bytes &&
           token->direction == slot->direction;
}

static KernelDmaStatus finish_transfer(const KernelDmaToken *token)
{
    KernelDmaSlot *slot;
    uint32_t pin_base;
    uint32_t pin_end;
    uint32_t pin_frames;
    bool reclaim;

    if (token == NULL)
        return KERNEL_DMA_INVALID_ARGUMENT;
    slot = lookup_slot(token->handle, NULL);
    if (slot == NULL) {
        if (initialized)
            ++dma_stats.stale_completions;
        return KERNEL_DMA_STALE;
    }
    if ((slot->state != KERNEL_DMA_DEVICE_OWNED &&
         slot->state != KERNEL_DMA_REVOKING) ||
        !token_matches(slot, token)) {
        ++dma_stats.stale_completions;
        return KERNEL_DMA_STALE;
    }

    pin_base = slot->transfer_physical & ~(KERNEL_PAGE_SIZE - 1u);
    pin_end = (slot->transfer_physical + slot->transfer_bytes - 1u) &
              ~(KERNEL_PAGE_SIZE - 1u);
    pin_frames = (pin_end - pin_base) / KERNEL_PAGE_SIZE + 1u;
    if (kernel_memory_unpin(pin_base, pin_frames, slot->owner) !=
        KERNEL_MEMORY_OK)
        return KERNEL_DMA_CORRUPT;

    reclaim = slot->state == KERNEL_DMA_REVOKING;
    slot->state = KERNEL_DMA_CPU_OWNED;
    slot->device_generation = 0u;
    slot->transfer_physical = 0u;
    slot->transfer_bytes = 0u;
    slot->direction = 0u;
    --dma_stats.in_flight_buffers;
    if (!reclaim)
        return KERNEL_DMA_OK;

    --dma_stats.deferred_reclaims;
    return release_slot(slot);
}

void kernel_dma_init(void)
{
    initialized = false;
    reset_stats();
    for (uint32_t index = 0u; index < KERNEL_DMA_MAX_BUFFERS; ++index) {
        slots[index].generation = 0u;
        clear_slot(&slots[index]);
    }
    initialized = true;
}

KernelDmaStatus kernel_dma_create(uint32_t owner, uint32_t byte_size,
                                  uint32_t alignment_frames,
                                  KernelDmaHandle *handle)
{
    KernelDmaSlot *slot = NULL;
    uint64_t rounded_size;
    uint32_t frame_count;
    uint32_t physical_base;
    KernelMemoryStatus memory_status;

    if (!initialized || owner == KERNEL_OWNER_NONE || byte_size == 0u ||
        alignment_frames == 0u || handle == NULL)
        return KERNEL_DMA_INVALID_ARGUMENT;
    rounded_size = (uint64_t)byte_size + KERNEL_PAGE_SIZE - 1u;
    frame_count = (uint32_t)(rounded_size >> KERNEL_PAGE_SHIFT);
    if (frame_count == 0u || frame_count > KERNEL_MAX_FRAMES)
        return KERNEL_DMA_INVALID_ARGUMENT;

    for (uint32_t index = 0u; index < KERNEL_DMA_MAX_BUFFERS; ++index) {
        if (slots[index].state == KERNEL_DMA_FREE) {
            slot = &slots[index];
            break;
        }
    }
    if (slot == NULL) {
        ++dma_stats.create_failures;
        return KERNEL_DMA_NO_RESOURCES;
    }

    memory_status = kernel_memory_alloc(frame_count, alignment_frames,
                                        KERNEL_FRAME_DMA, owner,
                                        &physical_base);
    if (memory_status != KERNEL_MEMORY_OK) {
        ++dma_stats.create_failures;
        return memory_status == KERNEL_MEMORY_OUT_OF_MEMORY ?
            KERNEL_DMA_OUT_OF_MEMORY : KERNEL_DMA_INVALID_ARGUMENT;
    }

    ++slot->generation;
    if (slot->generation == 0u)
        ++slot->generation;
    slot->owner = owner;
    slot->physical_base = physical_base;
    slot->byte_size = byte_size;
    slot->frame_count = frame_count;
    slot->transfer_generation = 0u;
    slot->device_generation = 0u;
    slot->transfer_physical = 0u;
    slot->transfer_bytes = 0u;
    slot->state = KERNEL_DMA_CPU_OWNED;
    slot->direction = 0u;
    *handle = make_handle((uint32_t)(slot - slots), slot->generation);
    ++dma_stats.buffers_created;
    ++dma_stats.live_buffers;
    return KERNEL_DMA_OK;
}

KernelDmaStatus kernel_dma_buffer_info(KernelDmaHandle handle,
                                       uint32_t owner,
                                       KernelDmaBufferInfo *info)
{
    KernelDmaSlot *slot;

    if (owner == KERNEL_OWNER_NONE || info == NULL)
        return KERNEL_DMA_INVALID_ARGUMENT;
    slot = lookup_slot(handle, NULL);
    if (slot == NULL)
        return KERNEL_DMA_INVALID_HANDLE;
    if (slot->owner != owner)
        return KERNEL_DMA_NOT_OWNED;

    info->owner = slot->owner;
    info->physical_base = slot->physical_base;
    info->byte_size = slot->byte_size;
    info->frame_count = slot->frame_count;
    info->transfer_generation = slot->transfer_generation;
    info->device_generation = slot->device_generation;
    info->state = slot->state;
    info->reserved[0] = 0u;
    info->reserved[1] = 0u;
    info->reserved[2] = 0u;
    return KERNEL_DMA_OK;
}

KernelDmaStatus kernel_dma_begin(KernelDmaHandle handle, uint32_t owner,
                                 uint32_t byte_offset, uint32_t byte_count,
                                 KernelDmaDirection direction,
                                 uint32_t device_generation,
                                 KernelDmaToken *token)
{
    KernelDmaSlot *slot;
    uint64_t transfer_end;
    uint32_t physical;
    uint32_t pin_base;
    uint32_t pin_end;
    uint32_t pin_frames;

    if (owner == KERNEL_OWNER_NONE || byte_count == 0u ||
        direction < KERNEL_DMA_TO_DEVICE ||
        direction > KERNEL_DMA_BIDIRECTIONAL ||
        device_generation == 0u || token == NULL)
        return KERNEL_DMA_INVALID_ARGUMENT;
    slot = lookup_slot(handle, NULL);
    if (slot == NULL)
        return KERNEL_DMA_INVALID_HANDLE;
    if (slot->owner != owner)
        return KERNEL_DMA_NOT_OWNED;
    if (slot->state != KERNEL_DMA_CPU_OWNED)
        return KERNEL_DMA_BUSY;

    transfer_end = (uint64_t)byte_offset + byte_count;
    if (transfer_end > slot->byte_size)
        return KERNEL_DMA_INVALID_ARGUMENT;
    physical = slot->physical_base + byte_offset;
    pin_base = physical & ~(KERNEL_PAGE_SIZE - 1u);
    pin_end = (physical + byte_count - 1u) & ~(KERNEL_PAGE_SIZE - 1u);
    pin_frames = (pin_end - pin_base) / KERNEL_PAGE_SIZE + 1u;
    if (kernel_memory_pin(pin_base, pin_frames, owner) != KERNEL_MEMORY_OK)
        return KERNEL_DMA_CORRUPT;

    ++slot->transfer_generation;
    if (slot->transfer_generation == 0u)
        ++slot->transfer_generation;
    slot->device_generation = device_generation;
    slot->transfer_physical = physical;
    slot->transfer_bytes = byte_count;
    slot->direction = (uint8_t)direction;
    slot->state = KERNEL_DMA_DEVICE_OWNED;

    token->handle = handle;
    token->transfer_generation = slot->transfer_generation;
    token->device_generation = device_generation;
    token->physical_address = physical;
    token->byte_count = byte_count;
    token->direction = (uint8_t)direction;
    token->reserved[0] = 0u;
    token->reserved[1] = 0u;
    token->reserved[2] = 0u;
    ++dma_stats.in_flight_buffers;
    return KERNEL_DMA_OK;
}

KernelDmaStatus kernel_dma_complete(const KernelDmaToken *token)
{
    return finish_transfer(token);
}

KernelDmaStatus kernel_dma_abort(const KernelDmaToken *token)
{
    return finish_transfer(token);
}

KernelDmaStatus kernel_dma_close(KernelDmaHandle handle, uint32_t owner)
{
    KernelDmaSlot *slot;

    if (owner == KERNEL_OWNER_NONE)
        return KERNEL_DMA_INVALID_ARGUMENT;
    slot = lookup_slot(handle, NULL);
    if (slot == NULL)
        return KERNEL_DMA_INVALID_HANDLE;
    if (slot->owner != owner)
        return KERNEL_DMA_NOT_OWNED;
    if (slot->state != KERNEL_DMA_CPU_OWNED)
        return KERNEL_DMA_BUSY;
    return release_slot(slot);
}

KernelDmaStatus kernel_dma_revoke_owner(uint32_t owner,
                                        uint32_t *released_buffers,
                                        uint32_t *deferred_buffers)
{
    uint32_t released = 0u;
    uint32_t deferred = 0u;

    if (!initialized || owner == KERNEL_OWNER_NONE)
        return KERNEL_DMA_INVALID_ARGUMENT;

    for (uint32_t index = 0u; index < KERNEL_DMA_MAX_BUFFERS; ++index) {
        KernelDmaSlot *slot = &slots[index];
        if (slot->owner != owner)
            continue;
        if (slot->state == KERNEL_DMA_CPU_OWNED) {
            if (release_slot(slot) != KERNEL_DMA_OK)
                return KERNEL_DMA_CORRUPT;
            ++released;
        } else if (slot->state == KERNEL_DMA_DEVICE_OWNED) {
            slot->state = KERNEL_DMA_REVOKING;
            ++dma_stats.deferred_reclaims;
            ++deferred;
        } else if (slot->state == KERNEL_DMA_REVOKING) {
            ++deferred;
        } else {
            return KERNEL_DMA_CORRUPT;
        }
    }

    if (released_buffers != NULL)
        *released_buffers = released;
    if (deferred_buffers != NULL)
        *deferred_buffers = deferred;
    return KERNEL_DMA_OK;
}

bool kernel_dma_stats(KernelDmaStats *result)
{
    if (!initialized || result == NULL)
        return false;
    result->buffers_created = dma_stats.buffers_created;
    result->live_buffers = dma_stats.live_buffers;
    result->in_flight_buffers = dma_stats.in_flight_buffers;
    result->deferred_reclaims = dma_stats.deferred_reclaims;
    result->stale_completions = dma_stats.stale_completions;
    result->create_failures = dma_stats.create_failures;
    return true;
}
