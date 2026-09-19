#include "entropy_service.h"

#include <astra/status.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static AstraEntropyRequest valid_request(void)
{
    AstraEntropyRequest request = {0};

    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_ENTROPY_PROTOCOL,
                             ASTRA_ENTROPY_PROTOCOL_VERSION,
                             ASTRA_ENTROPY_OPERATION_GET, 91u);
    request.length = 32u;
    return request;
}

int main(void)
{
    AstraEntropyRequest request = valid_request();
    AstraEntropyReply reply;
    uint8_t bytes[32];

    assert(astra_entropy_request_valid(&request, sizeof(request), 1u));
    request.length = 0u;
    assert(!astra_entropy_request_valid(&request, sizeof(request), 1u));
    request = valid_request();
    request.length = ASTRA_ENTROPY_MAX + 1u;
    assert(!astra_entropy_request_valid(&request, sizeof(request), 1u));
    request = valid_request();
    request.reserved[1] = 1u;
    assert(!astra_entropy_request_valid(&request, sizeof(request), 1u));
    request = valid_request();
    assert(!astra_entropy_request_valid(&request, sizeof(request) - 1u, 1u));
    assert(!astra_entropy_request_valid(&request, sizeof(request), 0u));
    request.header.protocol_version++;
    assert(!astra_entropy_request_valid(&request, sizeof(request), 1u));

    request = valid_request();
    for (uint32_t index = 0u; index < sizeof(bytes); ++index)
        bytes[index] = (uint8_t)(index + 1u);
    astra_entropy_reply_init(&reply, &request, ASTRA_STATUS_OK,
                             bytes, sizeof(bytes));
    assert(reply.header.total_size ==
           ASTRA_ENTROPY_REPLY_PREFIX_SIZE + sizeof(bytes));
    assert(reply.header.operation == ASTRA_ENTROPY_OPERATION_REPLY);
    assert(reply.header.transaction_id == request.header.transaction_id);
    assert(reply.status == ASTRA_STATUS_OK && reply.length == sizeof(bytes));
    assert(memcmp(reply.data, bytes, sizeof(bytes)) == 0);

    astra_entropy_reply_init(&reply, &request, ASTRA_STATUS_IO, NULL, 0u);
    assert(reply.header.total_size == ASTRA_ENTROPY_REPLY_PREFIX_SIZE);
    assert(reply.status == ASTRA_STATUS_IO && reply.length == 0u);

    puts("ASTRA ENTROPY SERVICE PASS");
    return 0;
}
