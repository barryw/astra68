#include "astra_block_policy.h"

astra_block_policy_result_t astra_block_policy_classify(
    const astra_block_request_t *request, uint32_t host_generation,
    uint32_t media_generation, uint32_t media_flags,
    const astra_partition_t *partition, uint32_t *absolute_lba)
{
    const uint32_t required_media =
        ASTRA_STATE_LINK_UP | ASTRA_STATE_MEDIA_PRESENT;

    if (request == NULL || partition == NULL || !request->valid ||
        request->id == 0 ||
        request->flags != 0 ||
        request->host_generation != host_generation ||
        request->media_generation != media_generation ||
        (media_flags & required_media) != required_media)
        return ASTRA_BLOCK_POLICY_INVALID;

    if (request->operation == ASTRA_BLOCK_FLUSH)
        return request->sectors == 0 ? ASTRA_BLOCK_POLICY_FLUSH :
                                       ASTRA_BLOCK_POLICY_INVALID;

    if ((request->operation != ASTRA_BLOCK_READ &&
         request->operation != ASTRA_BLOCK_WRITE) ||
        request->sectors == 0 ||
        request->sectors > ASTRA_BLOCK_MAX_SECTORS ||
        (request->operation == ASTRA_BLOCK_WRITE &&
         (media_flags & ASTRA_STATE_WRITE_ENABLE) == 0) ||
        absolute_lba == NULL ||
        !astra_partition_translate_u32(
            partition, request->lba, request->sectors, absolute_lba))
        return ASTRA_BLOCK_POLICY_INVALID;

    return ASTRA_BLOCK_POLICY_DATA;
}
