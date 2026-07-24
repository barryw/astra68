#include "block.h"

#include "generation.h"
#include "platform.h"
#include "vesta.h"

#include <stddef.h>

typedef struct KernelBlockSlot {
    uint32_t owner;
    KernelDmaHandle dma_handle;
    KernelDmaToken dma_token;
    KernelBlockResult result;
    uint32_t expected_media_generation;
    uint32_t expected_host_generation;
    uint16_t generation;
    uint16_t sectors;
    uint8_t operation;
    uint8_t state;
    uint8_t has_dma;
    uint8_t reserved;
} KernelBlockSlot;

static KernelBlockSlot slots[KERNEL_BLOCK_MAX_REQUESTS];
static KernelPlatformBlockState device_state;
static KernelBlockStats block_stats;
static uint32_t device_generation;
static bool device_state_valid;
static bool initialized;

static void clear_token(KernelDmaToken *token)
{
    token->handle = KERNEL_DMA_HANDLE_INVALID;
    token->transfer_generation = 0u;
    token->device_generation = 0u;
    token->physical_address = 0u;
    token->byte_count = 0u;
    token->direction = 0u;
    token->reserved[0] = 0u;
    token->reserved[1] = 0u;
    token->reserved[2] = 0u;
}

static void copy_token(KernelDmaToken *destination,
                       const KernelDmaToken *source)
{
    destination->handle = source->handle;
    destination->transfer_generation = source->transfer_generation;
    destination->device_generation = source->device_generation;
    destination->physical_address = source->physical_address;
    destination->byte_count = source->byte_count;
    destination->direction = source->direction;
    destination->reserved[0] = 0u;
    destination->reserved[1] = 0u;
    destination->reserved[2] = 0u;
}

static void clear_result(KernelBlockResult *result)
{
    result->status = 0u;
    result->sectors = 0u;
    result->detail = 0u;
    result->media_generation = 0u;
    result->host_generation = 0u;
}

static void clear_slot(KernelBlockSlot *slot)
{
    uint16_t generation = slot->generation;

    slot->owner = 0u;
    slot->dma_handle = KERNEL_DMA_HANDLE_INVALID;
    clear_token(&slot->dma_token);
    clear_result(&slot->result);
    slot->expected_media_generation = 0u;
    slot->expected_host_generation = 0u;
    slot->generation = generation;
    slot->sectors = 0u;
    slot->operation = 0u;
    slot->state = KERNEL_BLOCK_REQUEST_FREE;
    slot->has_dma = 0u;
    slot->reserved = 0u;
}

static void reset_stats(void)
{
    block_stats.submitted = 0u;
    block_stats.completed = 0u;
    block_stats.rejected = 0u;
    block_stats.unknown_completions = 0u;
    block_stats.generation_mismatches = 0u;
    block_stats.revoked_requests = 0u;
}

static KernelBlockHandle make_handle(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static KernelBlockSlot *lookup_slot(KernelBlockHandle handle)
{
    uint32_t encoded_index = handle & 0xffffu;
    uint16_t generation = (uint16_t)(handle >> 16);
    KernelBlockSlot *slot;

    if (!initialized || encoded_index == 0u ||
        encoded_index > KERNEL_BLOCK_MAX_REQUESTS || generation == 0u)
        return NULL;
    slot = &slots[encoded_index - 1u];
    if (slot->state == KERNEL_BLOCK_REQUEST_FREE ||
        slot->generation != generation)
        return NULL;
    return slot;
}

static bool refresh_device_state(void)
{
    KernelPlatformBlockState current;
    bool changed;

    if (!kernel_platform_block_state(&current)) {
        device_state_valid = false;
        return false;
    }
    changed = !device_state_valid ||
        current.host_generation != device_state.host_generation ||
        current.media_generation != device_state.media_generation;
    device_state.capabilities = current.capabilities;
    device_state.state_flags = current.state_flags;
    device_state.media_generation = current.media_generation;
    device_state.host_generation = current.host_generation;
    device_state.media_sectors = current.media_sectors;
    device_state.max_sectors = current.max_sectors;
    device_state.reserved = 0u;
    device_state_valid = true;
    if (changed) {
        device_generation = kernel_generation_next(device_generation);
    }
    return true;
}

static KernelBlockStatus submit_error_status(uint32_t error)
{
    if ((error & BLOCK_ERROR_NO_MEDIA) != 0u)
        return KERNEL_BLOCK_NO_MEDIA;
    if ((error & BLOCK_ERROR_WRITE_PROTECT) != 0u)
        return KERNEL_BLOCK_WRITE_PROTECTED;
    if ((error & BLOCK_ERROR_QUEUE_FULL) != 0u)
        return KERNEL_BLOCK_QUEUE_FULL;
    return KERNEL_BLOCK_REJECTED;
}

void kernel_block_init(void)
{
    initialized = false;
    device_state_valid = false;
    device_generation = 0u;
    reset_stats();
    for (uint32_t index = 0u; index < KERNEL_BLOCK_MAX_REQUESTS; ++index) {
        slots[index].generation = 0u;
        clear_slot(&slots[index]);
    }
    initialized = true;
    (void)refresh_device_state();
}

bool kernel_block_available(void)
{
    return initialized && refresh_device_state() &&
        (device_state.state_flags & BLOCK_STATE_LINK_UP) != 0u;
}

KernelBlockStatus kernel_block_submit(uint32_t owner, uint8_t operation,
                                      uint64_t lba, uint16_t sectors,
                                      KernelDmaHandle dma_handle,
                                      uint32_t dma_offset,
                                      KernelBlockHandle *request)
{
    KernelBlockSlot *slot = NULL;
    KernelDmaToken token;
    KernelDmaDirection direction;
    KernelBlockHandle handle;
    uint32_t transfer_bytes = 0u;
    uint32_t physical_buffer = 0u;
    uint32_t submit_error;
    bool has_dma;
    uint64_t lba_end;

    if (!initialized || owner == 0u || request == NULL)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    if (!refresh_device_state() ||
        (device_state.state_flags & BLOCK_STATE_LINK_UP) == 0u)
        return KERNEL_BLOCK_NOT_PRESENT;
    if ((device_state.state_flags & BLOCK_STATE_MEDIA_PRESENT) == 0u)
        return KERNEL_BLOCK_NO_MEDIA;

    has_dma = operation == BLOCK_OP_READ || operation == BLOCK_OP_WRITE;
    if (!has_dma && operation != BLOCK_OP_FLUSH)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    if (has_dma) {
        uint32_t required_capability = operation == BLOCK_OP_READ ?
            BLOCK_CAP_READ : BLOCK_CAP_WRITE;
        if (sectors == 0u || sectors > device_state.max_sectors ||
            dma_handle == KERNEL_DMA_HANDLE_INVALID ||
            (dma_offset & 3u) != 0u)
            return KERNEL_BLOCK_INVALID_ARGUMENT;
        if ((device_state.capabilities & required_capability) == 0u)
            return KERNEL_BLOCK_UNSUPPORTED;
        lba_end = lba + sectors;
        if (lba_end < lba || lba_end > device_state.media_sectors)
            return KERNEL_BLOCK_OUT_OF_RANGE;
        if (operation == BLOCK_OP_WRITE &&
            (device_state.state_flags & BLOCK_STATE_WRITE_ENABLE) == 0u)
            return KERNEL_BLOCK_WRITE_PROTECTED;
        transfer_bytes = (uint32_t)sectors << 9;
        direction = operation == BLOCK_OP_READ ?
            KERNEL_DMA_FROM_DEVICE : KERNEL_DMA_TO_DEVICE;
    } else {
        if (sectors != 0u || dma_handle != KERNEL_DMA_HANDLE_INVALID ||
            dma_offset != 0u)
            return KERNEL_BLOCK_INVALID_ARGUMENT;
        if ((device_state.capabilities & BLOCK_CAP_FLUSH) == 0u)
            return KERNEL_BLOCK_UNSUPPORTED;
        direction = KERNEL_DMA_TO_DEVICE;
    }

    for (uint32_t index = 0u; index < KERNEL_BLOCK_MAX_REQUESTS; ++index) {
        if (slots[index].state == KERNEL_BLOCK_REQUEST_FREE) {
            slot = &slots[index];
            break;
        }
    }
    if (slot == NULL)
        return KERNEL_BLOCK_QUEUE_FULL;

    slot->generation = (uint16_t)kernel_generation_next_masked(
        slot->generation, UINT16_MAX);
    handle = make_handle((uint32_t)(slot - slots), slot->generation);
    clear_token(&token);
    if (has_dma) {
        KernelDmaStatus dma_status = kernel_dma_begin(
            dma_handle, owner, dma_offset, transfer_bytes, direction,
            device_generation, &token);
        if (dma_status == KERNEL_DMA_INVALID_HANDLE)
            return KERNEL_BLOCK_INVALID_HANDLE;
        if (dma_status == KERNEL_DMA_NOT_OWNED)
            return KERNEL_BLOCK_NOT_OWNED;
        if (dma_status == KERNEL_DMA_BUSY)
            return KERNEL_BLOCK_BUSY;
        if (dma_status != KERNEL_DMA_OK)
            return dma_status == KERNEL_DMA_INVALID_ARGUMENT ?
                KERNEL_BLOCK_INVALID_ARGUMENT : KERNEL_BLOCK_CORRUPT;
        physical_buffer = token.physical_address;
    }

    submit_error = kernel_platform_block_submit(
        handle, operation, 0u, lba, sectors, physical_buffer);
    if (submit_error != 0u) {
        if (has_dma && kernel_dma_abort(&token) != KERNEL_DMA_OK)
            return KERNEL_BLOCK_CORRUPT;
        ++block_stats.rejected;
        return submit_error_status(submit_error);
    }

    slot->owner = owner;
    slot->dma_handle = dma_handle;
    copy_token(&slot->dma_token, &token);
    clear_result(&slot->result);
    slot->expected_media_generation = device_state.media_generation;
    slot->expected_host_generation = device_state.host_generation;
    slot->sectors = sectors;
    slot->operation = operation;
    slot->state = KERNEL_BLOCK_REQUEST_ACTIVE;
    slot->has_dma = has_dma ? 1u : 0u;
    slot->reserved = 0u;
    *request = handle;
    ++block_stats.submitted;
    return KERNEL_BLOCK_OK;
}

KernelBlockStatus kernel_block_service(uint32_t *serviced_completions)
{
    KernelPlatformBlockCompletion completion;
    uint32_t serviced = 0u;

    if (!initialized)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    (void)refresh_device_state();
    while (kernel_platform_block_pop_completion(&completion)) {
        KernelBlockSlot *slot = lookup_slot(completion.id);
        bool generation_match;
        bool revoking;

        ++serviced;
        if (slot == NULL ||
            (slot->state != KERNEL_BLOCK_REQUEST_ACTIVE &&
             slot->state != KERNEL_BLOCK_REQUEST_REVOKING)) {
            ++block_stats.unknown_completions;
            continue;
        }
        revoking = slot->state == KERNEL_BLOCK_REQUEST_REVOKING;
        generation_match =
            completion.media_generation ==
                slot->expected_media_generation &&
            completion.host_generation == slot->expected_host_generation;
        if (!generation_match)
            ++block_stats.generation_mismatches;
        if (slot->has_dma != 0u &&
            kernel_dma_complete(&slot->dma_token) != KERNEL_DMA_OK) {
            ++block_stats.unknown_completions;
            if (serviced_completions != NULL)
                *serviced_completions = serviced;
            return KERNEL_BLOCK_CORRUPT;
        }

        ++block_stats.completed;
        if (revoking) {
            clear_slot(slot);
            continue;
        }
        slot->result.status = generation_match ? completion.status :
            KERNEL_BLOCK_COMPLETION_MEDIA_CHANGED;
        slot->result.sectors = completion.sectors;
        slot->result.detail = completion.detail;
        slot->result.media_generation = completion.media_generation;
        slot->result.host_generation = completion.host_generation;
        slot->state = KERNEL_BLOCK_REQUEST_COMPLETED;
    }
    if (serviced_completions != NULL)
        *serviced_completions = serviced;
    return KERNEL_BLOCK_OK;
}

KernelBlockStatus kernel_block_collect(KernelBlockHandle request,
                                       uint32_t owner,
                                       KernelBlockResult *result)
{
    KernelBlockSlot *slot;

    if (owner == 0u || result == NULL)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    slot = lookup_slot(request);
    if (slot == NULL)
        return KERNEL_BLOCK_INVALID_HANDLE;
    if (slot->owner != owner)
        return KERNEL_BLOCK_NOT_OWNED;
    if (slot->state != KERNEL_BLOCK_REQUEST_COMPLETED)
        return KERNEL_BLOCK_PENDING;

    result->status = slot->result.status;
    result->sectors = slot->result.sectors;
    result->detail = slot->result.detail;
    result->media_generation = slot->result.media_generation;
    result->host_generation = slot->result.host_generation;
    clear_slot(slot);
    return KERNEL_BLOCK_OK;
}

KernelBlockStatus kernel_block_request_info(KernelBlockHandle request,
                                            uint32_t owner,
                                            KernelBlockRequestInfo *info)
{
    KernelBlockSlot *slot;

    if (owner == 0u || info == NULL)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    slot = lookup_slot(request);
    if (slot == NULL)
        return KERNEL_BLOCK_INVALID_HANDLE;
    if (slot->owner != owner)
        return KERNEL_BLOCK_NOT_OWNED;
    info->owner = slot->owner;
    info->dma_handle = slot->dma_handle;
    info->expected_media_generation = slot->expected_media_generation;
    info->expected_host_generation = slot->expected_host_generation;
    info->sectors = slot->sectors;
    info->operation = slot->operation;
    info->state = slot->state;
    return KERNEL_BLOCK_OK;
}

KernelBlockStatus kernel_block_revoke_owner(uint32_t owner,
                                            uint32_t *released_buffers,
                                            uint32_t *deferred_buffers)
{
    uint32_t revoked = 0u;
    KernelDmaStatus dma_status;

    if (!initialized || owner == 0u)
        return KERNEL_BLOCK_INVALID_ARGUMENT;
    for (uint32_t index = 0u; index < KERNEL_BLOCK_MAX_REQUESTS; ++index) {
        KernelBlockSlot *slot = &slots[index];
        if (slot->owner != owner)
            continue;
        if (slot->state == KERNEL_BLOCK_REQUEST_COMPLETED) {
            clear_slot(slot);
            ++revoked;
        } else if (slot->state == KERNEL_BLOCK_REQUEST_ACTIVE) {
            slot->state = KERNEL_BLOCK_REQUEST_REVOKING;
            ++revoked;
        } else if (slot->state != KERNEL_BLOCK_REQUEST_REVOKING) {
            return KERNEL_BLOCK_CORRUPT;
        }
    }
    dma_status = kernel_dma_revoke_owner(owner, released_buffers,
                                         deferred_buffers);
    if (dma_status != KERNEL_DMA_OK)
        return KERNEL_BLOCK_CORRUPT;
    block_stats.revoked_requests += revoked;
    return KERNEL_BLOCK_OK;
}

bool kernel_block_stats(KernelBlockStats *result)
{
    if (!initialized || result == NULL)
        return false;
    result->submitted = block_stats.submitted;
    result->completed = block_stats.completed;
    result->rejected = block_stats.rejected;
    result->unknown_completions = block_stats.unknown_completions;
    result->generation_mismatches = block_stats.generation_mismatches;
    result->revoked_requests = block_stats.revoked_requests;
    return true;
}
