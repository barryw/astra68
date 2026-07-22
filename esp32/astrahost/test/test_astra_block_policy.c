#include "astra_block_policy.h"

#include <assert.h>
#include <stdio.h>

#define HOST_GENERATION 0x11223344u
#define MEDIA_GENERATION 0x55667788u
#define MEDIA_FLAGS                                                        \
    (ASTRA_STATE_LINK_UP | ASTRA_STATE_MEDIA_PRESENT |                    \
     ASTRA_STATE_WRITE_ENABLE)

static const astra_partition_t partition = {
    .first_lba = 2048,
    .sector_count = 4096,
};

static astra_block_request_t make_request(uint8_t operation,
                                          uint16_t sectors,
                                          uint64_t lba)
{
    astra_block_request_t request = {
        .valid = true,
        .id = 1,
        .operation = operation,
        .flags = 0,
        .sectors = sectors,
        .lba = lba,
        .buffer = 0x02000000u,
        .media_generation = MEDIA_GENERATION,
        .host_generation = HOST_GENERATION,
    };
    return request;
}

static astra_block_policy_result_t classify(
    const astra_block_request_t *request, uint32_t media_flags,
    uint32_t *absolute_lba)
{
    return astra_block_policy_classify(
        request, HOST_GENERATION, MEDIA_GENERATION, media_flags,
        &partition, absolute_lba);
}

static void expect_invalid(astra_block_request_t request)
{
    uint32_t absolute_lba = UINT32_MAX;
    assert(classify(&request, MEDIA_FLAGS, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);
    assert(absolute_lba == UINT32_MAX);
}

int main(void)
{
    uint32_t absolute_lba = 0;
    astra_block_request_t request =
        make_request(ASTRA_BLOCK_READ, 1, 0);
    assert(classify(&request, MEDIA_FLAGS, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_DATA);
    assert(absolute_lba == 2048);

    request = make_request(ASTRA_BLOCK_READ, ASTRA_BLOCK_MAX_SECTORS,
                           partition.sector_count - ASTRA_BLOCK_MAX_SECTORS);
    assert(classify(&request, ASTRA_STATE_LINK_UP |
                    ASTRA_STATE_MEDIA_PRESENT, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_DATA);
    assert(absolute_lba == 6128);

    request = make_request(ASTRA_BLOCK_WRITE, 1,
                           partition.sector_count - 1);
    assert(classify(&request, MEDIA_FLAGS, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_DATA);
    assert(absolute_lba == 6143);

    request = make_request(ASTRA_BLOCK_FLUSH, 0, UINT64_MAX);
    assert(classify(&request, ASTRA_STATE_LINK_UP |
                    ASTRA_STATE_MEDIA_PRESENT, NULL) ==
           ASTRA_BLOCK_POLICY_FLUSH);

    assert(astra_block_policy_classify(
               NULL, HOST_GENERATION, MEDIA_GENERATION, MEDIA_FLAGS,
               &partition, &absolute_lba) == ASTRA_BLOCK_POLICY_INVALID);
    assert(astra_block_policy_classify(
               &request, HOST_GENERATION, MEDIA_GENERATION, MEDIA_FLAGS,
               NULL, &absolute_lba) == ASTRA_BLOCK_POLICY_INVALID);

    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    request.valid = false;
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    request.id = 0;
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    request.flags = 1;
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    request.host_generation++;
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    request.media_generation++;
    expect_invalid(request);

    request = make_request(ASTRA_BLOCK_READ, 1, 0);
    assert(classify(&request, ASTRA_STATE_MEDIA_PRESENT, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);
    assert(classify(&request, ASTRA_STATE_LINK_UP, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);
    assert(classify(&request, 0, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);

    request = make_request(0xff, 1, 0);
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 0, 0);
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ,
                           ASTRA_BLOCK_MAX_SECTORS + 1u, 0);
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, partition.sector_count);
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 2,
                           partition.sector_count - 1);
    expect_invalid(request);
    request = make_request(ASTRA_BLOCK_READ, 1, UINT64_MAX);
    expect_invalid(request);

    request = make_request(ASTRA_BLOCK_WRITE, 1, 0);
    assert(classify(&request, ASTRA_STATE_LINK_UP |
                    ASTRA_STATE_MEDIA_PRESENT, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);
    assert(classify(&request, MEDIA_FLAGS, NULL) ==
           ASTRA_BLOCK_POLICY_INVALID);

    request = make_request(ASTRA_BLOCK_FLUSH, 1, 0);
    expect_invalid(request);

    astra_partition_t unaddressable = {
        .first_lba = UINT32_MAX,
        .sector_count = 2,
    };
    request = make_request(ASTRA_BLOCK_READ, 2, 0);
    assert(astra_block_policy_classify(
               &request, HOST_GENERATION, MEDIA_GENERATION, MEDIA_FLAGS,
               &unaddressable, &absolute_lba) ==
           ASTRA_BLOCK_POLICY_INVALID);

    puts("Astra block request policy tests passed");
    return 0;
}
