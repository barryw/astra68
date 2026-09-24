#include "../pcm_protocol.h"

#include <assert.h>

int main(void)
{
    AstraPcmRequest request = {0};

    astra_message_header_set(&request.header, sizeof(request),
                             ASTRA_PCM_PROTOCOL, ASTRA_PCM_PROTOCOL_VERSION,
                             ASTRA_PCM_OPEN, 1u);
    assert(astra_pcm_request_valid(&request, sizeof(request), 1));
    assert(!astra_pcm_request_valid(&request, sizeof(request) - 1u, 1));
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.operation = ASTRA_PCM_WRITE;
    assert(astra_pcm_request_valid(&request, sizeof(request), 0));
    assert(!astra_pcm_request_valid(&request, sizeof(request), 1));
    request.header.operation = ASTRA_PCM_PAUSE;
    assert(astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.operation = ASTRA_PCM_CLEAR;
    assert(astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.operation = ASTRA_PCM_REPLY;
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.operation = ASTRA_PCM_CLEAR + 1u;
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.operation = ASTRA_PCM_WRITE;
    request.header.protocol_version++;
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.protocol_version--;
    request.header.transaction_id = 0u;
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    request.header.transaction_id = 1u;
    request.header.flags = 1u;
    assert(!astra_pcm_request_valid(&request, sizeof(request), 0));
    return 0;
}
