#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "astra_host_protocol.h"
#include "astra_partition.h"

typedef struct {
    bool valid;
    uint32_t id;
    uint8_t operation;
    uint8_t flags;
    uint16_t sectors;
    uint64_t lba;
    uint32_t buffer;
    uint32_t media_generation;
    uint32_t host_generation;
} astra_block_request_t;

typedef enum {
    ASTRA_BLOCK_POLICY_INVALID = 0,
    ASTRA_BLOCK_POLICY_FLUSH,
    ASTRA_BLOCK_POLICY_DATA,
} astra_block_policy_result_t;

astra_block_policy_result_t astra_block_policy_classify(
    const astra_block_request_t *request, uint32_t host_generation,
    uint32_t media_generation, uint32_t media_flags,
    const astra_partition_t *partition, uint32_t *absolute_lba);
